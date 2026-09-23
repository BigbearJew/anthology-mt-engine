// Runs the production PiP flow pixel shader offscreen on D3D11 WARP.
// Synthetic images validate motion semantics; these are not gameplay/FPS tests.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Pixel = std::array<float, 4>;
namespace fs = std::filesystem;

static void check(HRESULT result, const char* operation)
{
    if (FAILED(result)) {
        char message[512];
        std::snprintf(message, sizeof(message), "%s failed: 0x%08lx", operation, result);
        throw std::runtime_error(message);
    }
}

class Includes final : public ID3DInclude
{
    std::vector<fs::path> roots;
public:
    explicit Includes(std::vector<fs::path> paths) : roots(std::move(paths)) {}
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* output, UINT* count) override
    {
        for (const auto& root : roots) {
            std::ifstream stream(root / name, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const auto size = stream.tellg();
            if (size < 0) return E_FAIL;
            auto* bytes = new char[static_cast<size_t>(size)];
            stream.seekg(0);
            stream.read(bytes, size);
            *output = bytes;
            *count = static_cast<UINT>(size);
            return S_OK;
        }
        std::fprintf(stderr, "Missing shader include: %s\n", name);
        return E_FAIL;
    }
    HRESULT __stdcall Close(LPCVOID bytes) override
    {
        delete[] static_cast<const char*>(bytes);
        return S_OK;
    }
};

struct Texture
{
    UINT width = 0, height = 0;
    ComPtr<ID3D11Texture2D> data;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
};

class Shader
{
    struct ConstantBuffer
    {
        UINT slot = 0;
        std::vector<unsigned char> bytes;
        ComPtr<ID3D11Buffer> gpu;
    };
    struct Variable { size_t buffer; UINT offset, size; };
    std::vector<ConstantBuffer> buffers;
    std::map<std::string, Variable> variables;
    std::map<std::string, UINT> textures;
    std::map<std::string, UINT> samplers;
public:
    ComPtr<ID3D11PixelShader> shader;

    bool hasTexture(const char* name) const { return textures.find(name) != textures.end(); }

    Shader(ID3D11Device* device, const fs::path& source, Includes& includes, const char* entry = "main")
    {
        ComPtr<ID3DBlob> code, errors;
        const D3D_SHADER_MACRO defines[] = {{"USE_DX11", "1"}, {"GBUFFER_OPTIMIZATION", "1"}, {nullptr, nullptr}};
        const HRESULT result = D3DCompileFromFile(source.c_str(), defines, &includes, entry, "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) std::fprintf(stderr, "%s", static_cast<const char*>(errors->GetBufferPointer()));
        check(result, "D3DCompileFromFile");
        check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "CreatePixelShader");
        ComPtr<ID3D11ShaderReflection> reflection;
        check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
            reinterpret_cast<void**>(reflection.GetAddressOf())), "D3DReflect");
        D3D11_SHADER_DESC description{};
        check(reflection->GetDesc(&description), "shader GetDesc");
        for (UINT i = 0; i < description.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            check(reflection->GetResourceBindingDesc(i, &binding), "GetResourceBindingDesc");
            if (binding.Type == D3D_SIT_TEXTURE) textures[binding.Name] = binding.BindPoint;
            else if (binding.Type == D3D_SIT_SAMPLER) samplers[binding.Name] = binding.BindPoint;
            else if (binding.Type == D3D_SIT_CBUFFER) {
                auto* reflected = reflection->GetConstantBufferByName(binding.Name);
                D3D11_SHADER_BUFFER_DESC info{};
                check(reflected->GetDesc(&info), "buffer GetDesc");
                ConstantBuffer buffer;
                buffer.slot = binding.BindPoint;
                buffer.bytes.resize(info.Size);
                D3D11_BUFFER_DESC gpu{};
                gpu.ByteWidth = info.Size;
                gpu.Usage = D3D11_USAGE_DEFAULT;
                gpu.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                check(device->CreateBuffer(&gpu, nullptr, &buffer.gpu), "CreateBuffer");
                for (UINT j = 0; j < info.Variables; ++j) {
                    D3D11_SHADER_VARIABLE_DESC variable{};
                    check(reflected->GetVariableByIndex(j)->GetDesc(&variable), "variable GetDesc");
                    variables[variable.Name] = {buffers.size(), variable.StartOffset, variable.Size};
                }
                buffers.push_back(std::move(buffer));
            }
        }
    }

    template<size_t N> void constant(const char* name, const std::array<float, N>& values)
    {
        const auto found = variables.find(name);
        if (found == variables.end()) throw std::runtime_error(std::string("Missing production constant: ") + name);
        const auto& target = found->second;
        if (sizeof(values) > target.size) throw std::runtime_error("constant size mismatch");
        std::memcpy(buffers[target.buffer].bytes.data() + target.offset, values.data(), sizeof(values));
    }
    void copyConstants(const Shader& source)
    {
        for (const auto& entry : variables) {
            const auto found = source.variables.find(entry.first);
            if (found == source.variables.end()) continue;
            const auto& target = entry.second;
            const auto& origin = found->second;
            if (target.size != origin.size) throw std::runtime_error("Reflected shared constant size mismatch");
            std::memcpy(buffers[target.buffer].bytes.data() + target.offset,
                source.buffers[origin.buffer].bytes.data() + origin.offset, target.size);
        }
    }
    void texture(ID3D11DeviceContext* context, const char* name, Texture& input)
    {
        const auto found = textures.find(name);
        if (found == textures.end()) throw std::runtime_error(std::string("Missing production texture: ") + name);
        ID3D11ShaderResourceView* view = input.srv.Get();
        context->PSSetShaderResources(found->second, 1, &view);
    }
    void bind(ID3D11DeviceContext* context, ID3D11SamplerState* linear, ID3D11SamplerState* point)
    {
        context->PSSetShader(shader.Get(), nullptr, 0);
        for (auto& buffer : buffers) {
            context->UpdateSubresource(buffer.gpu.Get(), 0, nullptr, buffer.bytes.data(), 0, 0);
            ID3D11Buffer* gpu = buffer.gpu.Get();
            context->PSSetConstantBuffers(buffer.slot, 1, &gpu);
        }
        for (const auto& sampler : samplers) {
            ID3D11SamplerState* state = sampler.first.find("nofilter") != std::string::npos ||
                sampler.first.find("point") != std::string::npos ? point : linear;
            context->PSSetSamplers(sampler.second, 1, &state);
        }
    }
};

class Gpu
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11SamplerState> linear, point;
    ComPtr<ID3D11RasterizerState> rasterizer;
public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    Gpu()
    {
        D3D_FEATURE_LEVEL feature;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, &feature, &context), "D3D11CreateDevice WARP");
        const char* source =
            "struct V { float4 hpos : SV_Position; float2 tc0 : TEXCOORD0; };"
            "V main(uint id : SV_VertexID) { V o; o.tc0=float2((id<<1)&2,id&2);"
            "o.hpos=float4(o.tc0*float2(2,-2)+float2(-1,1),0,1); return o; }";
        ComPtr<ID3DBlob> code;
        check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
            &code, nullptr), "compile fullscreen vertex");
        check(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex), "CreateVertexShader");
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.MaxAnisotropy = 1;
        check(device->CreateSamplerState(&sampler, &linear), "CreateSamplerState linear");
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        check(device->CreateSamplerState(&sampler, &point), "CreateSamplerState point");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, &rasterizer), "CreateRasterizerState");
    }

    Texture texture(UINT width, UINT height, const std::vector<Pixel>& pixels = {})
    {
        Texture texture;
        texture.width = width; texture.height = height;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels.data();
        data.SysMemPitch = width * sizeof(Pixel);
        check(device->CreateTexture2D(&description, pixels.empty() ? nullptr : &data, &texture.data), "CreateTexture2D");
        check(device->CreateShaderResourceView(texture.data.Get(), nullptr, &texture.srv), "CreateShaderResourceView");
        check(device->CreateRenderTargetView(texture.data.Get(), nullptr, &texture.rtv), "CreateRenderTargetView");
        return texture;
    }

    void draw(Shader& shader, Texture& destination)
    {
        ID3D11RenderTargetView* target = destination.rtv.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(destination.width), float(destination.height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer.Get());
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        shader.bind(context.Get(), linear.Get(), point.Get());
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
        context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
    }

    std::vector<Pixel> read(Texture& texture)
    {
        D3D11_TEXTURE2D_DESC description{};
        texture.data->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(device->CreateTexture2D(&description, nullptr, &staging), "Create staging");
        context->CopyResource(staging.Get(), texture.data.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // Intentional harness-only wait to inspect the actual GPU result.
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map staging");
        std::vector<Pixel> output(size_t(texture.width) * texture.height);
        for (UINT y = 0; y < texture.height; ++y)
            std::memcpy(output.data() + size_t(y) * texture.width,
                static_cast<unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                size_t(texture.width) * sizeof(Pixel));
        context->Unmap(staging.Get(), 0);
        return output;
    }
};

// The scenario runner below binds the real shader's named interface, rather
// than copying an optical-flow implementation into the test.
static float pattern(float x, float y, bool background)
{
    const float phase = background ? 1.37f : 0.0f;
    return 0.5f + 0.18f * std::sin(x * 0.131f + y * 0.027f + phase) +
        0.14f * std::cos(y * 0.197f - x * 0.043f + phase) +
        0.13f * std::sin(x * 0.257f + y * 0.163f + phase);
}

static std::vector<Pixel> scene(UINT width, UINT height, float dx, float dy,
    bool movingObject, bool flat = false)
{
    std::vector<Pixel> pixels(size_t(width) * height);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const float sourceX = float(x) - dx, sourceY = float(y) - dy;
        const bool object = sourceX >= 64.f && sourceX < 192.f && sourceY >= 64.f && sourceY < 192.f;
        const float value = flat ? 0.4f : movingObject && !object ?
            pattern(float(x), float(y), true) : pattern(sourceX, sourceY, false);
        pixels[size_t(y) * width + x] = {value, value, value, 1.f};
    }
    return pixels;
}

struct RegionStats
{
    size_t count = 0, accepted = 0, correct = 0;
    double error = 0.0, confidence = 0.0, vectorX = 0.0, vectorY = 0.0;
};

static RegionStats measure(const std::vector<Pixel>& flow, UINT width, UINT height,
    float expectedX, float expectedY, UINT first, UINT last)
{
    RegionStats stats;
    for (UINT y = first; y < last; ++y) for (UINT x = first; x < last; ++x) {
        const Pixel& sample = flow[size_t(y) * width + x];
        for (float value : sample) if (!std::isfinite(value)) throw std::runtime_error("Non-finite GPU optical flow");
        const float errorX = sample[0] * float(width * 4) - expectedX;
        const float errorY = sample[1] * float(height * 4) - expectedY;
        const float error = std::sqrt(errorX * errorX + errorY * errorY);
        ++stats.count;
        stats.confidence += sample[3];
        if (sample[3] >= 0.25f) {
            ++stats.accepted;
            if (error <= 1.25f) ++stats.correct;
            stats.error += error;
            stats.vectorX += sample[0] * float(width * 4);
            stats.vectorY += sample[1] * float(height * 4);
        }
    }
    return stats;
}

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (argc < 2 || argc > 3) throw std::runtime_error("Usage: pip_flow_gpu_test.exe <engine-source-root> [production-PiP-shader-directory]");
        const fs::path root = fs::absolute(argv[1]);
        const fs::path lensRoot = argc == 3 ? fs::absolute(argv[2]) : root / "gamedata/shaders/r3";
        Includes includes({lensRoot, root / "gamedata/shaders/r3", root / "_build/shader_validation/v140/r3"});
        Gpu gpu;
        {
            Shader depthShader(gpu.device.Get(), root / "gamedata/shaders/r3/svp_temporal_depth.ps", includes);
            constexpr UINT sourceWidth = 37, sourceHeight = 23, targetWidth = 64, targetHeight = 48;
            const std::array<float, 8> values = {0.f, -1.f, 0.005f, 0.02f, 3.5f, 9999.f,
                10000.f, std::numeric_limits<float>::quiet_NaN()};
            std::vector<Pixel> positions(size_t(sourceWidth) * sourceHeight);
            for (UINT y = 0; y < sourceHeight; ++y) for (UINT x = 0; x < sourceWidth; ++x)
                positions[size_t(y) * sourceWidth + x] = {0.f, 0.f, values[(x + y) % values.size()], 0.f};
            auto source = gpu.texture(sourceWidth, sourceHeight, positions);
            auto target = gpu.texture(targetWidth, targetHeight);
            depthShader.texture(gpu.context.Get(), "s_svp_position", source);
            gpu.draw(depthShader, target);
            auto output = gpu.read(target);
            for (UINT y = 0; y < targetHeight; ++y) for (UINT x = 0; x < targetWidth; ++x) {
                const UINT sourceX = UINT((float(x) + 0.5f) / targetWidth * sourceWidth);
                const UINT sourceY = UINT((float(y) + 0.5f) / targetHeight * sourceHeight);
                const float z = positions[size_t(sourceY) * sourceWidth + sourceX][2];
                const float expected = z > 0.01f && z < 10000.f ? z : 10000.f;
                require(std::abs(output[size_t(y) * targetWidth + x][0] - expected) < 0.001f,
                    "Production depth capture used wrong source texel or sky/depth semantics");
            }
            std::puts("PASS: production depth shader,3072 samples, unequal/non-square source dimensions, linear Z and invalid/sky fallback.");
        }
        Shader shader(gpu.device.Get(), root / "gamedata/shaders/r3/svp_temporal_flow.ps", includes);
        Shader pyramidShader(gpu.device.Get(), root / "gamedata/shaders/r3/svp_temporal_pyramid.ps", includes);
        constexpr UINT width = 256, height = 256;
        size_t failures = 0;
        auto expectation = [&](bool condition, const char* message) {
            if (!condition) { ++failures; std::fprintf(stderr, "CHECK FAILED: %s\n", message); }
        };
        std::unique_ptr<Shader> lensShader;
        require(fs::exists(lensRoot / "anthology_pip_temporal.h"), "Production PiP temporal helper is required for full validation");
        {
            fs::create_directories(root / "_build/v142-validation");
            const fs::path wrapper = root / "_build/v142-validation/lens_sample_wrapper.ps";
            std::ofstream source(wrapper);
            source << "Texture2D<float4> s_second_vp, s_prev_frame, s_position;\n"
                "SamplerState smp_base;\n"
                "float4 screen_res, scope_lense_imaging, s3ds_param_3, markswitch_current;\n"
                "float4x4 scope_lense_reproject;\n"
                "#include \"anthology_pip_temporal.h\"\n"
                "float4 main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target { return sample_second_vp(uv); }\n"
                "float4 detail_main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target { return sample_second_vp_detail(uv); }\n";
            source.close();
            lensShader = std::make_unique<Shader>(gpu.device.Get(), wrapper, includes);
            Shader& lens = *lensShader;
            const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            lens.constant("scope_lense_reproject", identity);
            lens.constant("screen_res", std::array<float, 4>{float(width), float(height), 1.f / width, 1.f / height});
            lens.constant("scope_lense_camera", std::array<float, 4>{0, 0, 0, 0});
            lens.constant("scope_lense_main_view", std::array<float, 4>{1, 1, 0.5f, 0.5f});
            lens.constant("s3ds_param_3", std::array<float, 4>{0, 0, 0, 0});
            lens.constant("markswitch_current", std::array<float, 4>{1, 0, 0, 0});
            auto rectangle = [](UINT x, UINT y, float shift) {
                return float(x) >= 80.f + shift && float(x) < 144.f + shift && y >= 88 && y < 152;
            };
            auto backgroundColor = [](UINT x, UINT y) -> Pixel {
                return {0.10f + float(x) * 0.0001f, 0.12f + float(y) * 0.0001f, 0.3f, 1.f};
            };
            constexpr Pixel targetColor = {0.85f, 0.1f, 0.12f, 1.f};
            auto lensCase = [&](const char* name, float ratio, bool nearHud, bool imaging, bool changedSentinel, bool staticDetail) {
                std::vector<Pixel> captured(size_t(width) * height), mainImage(captured.size()),
                    captureDepth(captured.size()), mainPosition(captured.size());
                std::vector<Pixel> flow(size_t(width / 4) * (height / 4));
                for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
                    const size_t pixel = size_t(y) * width + x;
                    const bool before = rectangle(x, y, 0);
                    const bool after = rectangle(x, y, staticDetail ? 0.f : 8.f * ratio);
                    captured[pixel] = before ? targetColor : backgroundColor(x, y);
                    mainImage[pixel] = staticDetail ? Pixel{0, 0.9f, 0, 1} : after ? targetColor : backgroundColor(x, y);
                    captureDepth[pixel] = {before ? 5.f : 20.f, 0, 0, 0};
                    mainPosition[pixel] = {0, 0, nearHud ? 0.1f : after ? 5.f : 20.f, 0};
                }
                for (UINT y = 0; y < height / 4; ++y) for (UINT x = 0; x < width / 4; ++x) {
                    const bool before = rectangle(x * 4 + 2, y * 4 + 2, 0);
                    flow[size_t(y) * (width / 4) + x] = {before && !staticDetail ? 8.f / width : 0.f,
                        0, before ? 5.f : 20.f, changedSentinel ? -1.f : 1.f};
                }
                auto cachedTexture = gpu.texture(width, height, captured);
                auto mainTexture = gpu.texture(width, height, mainImage);
                auto cachedDepth = gpu.texture(width, height, captureDepth);
                auto mainDepth = gpu.texture(width, height, mainPosition);
                auto motion = gpu.texture(width / 4, height / 4, flow);
                auto output = gpu.texture(width, height);
                lens.constant("scope_lense_motion", std::array<float, 4>{ratio, 1, ratio * 0.033f, 0});
                lens.constant("scope_lense_imaging", std::array<float, 4>{imaging ? 1.f : 0.f, 0, 0, 0});
                lens.texture(gpu.context.Get(), "s_second_vp", cachedTexture);
                lens.texture(gpu.context.Get(), "s_prev_frame", mainTexture);
                lens.texture(gpu.context.Get(), "s_second_vp_depth", cachedDepth);
                lens.texture(gpu.context.Get(), "s_position", mainDepth);
                lens.texture(gpu.context.Get(), "s_second_vp_flow", motion);
                gpu.draw(lens, output);
                const auto result = gpu.read(output);
                size_t checks = 0, mismatches = 0;
                for (UINT y = 80; y < 160; ++y) for (UINT x = 64; x < 168; ++x) {
                    const size_t pixel = size_t(y) * width + x;
                    // Rejected same-depth motion must retain coherent capture;
                    // only confirmed changed visibility can use current color.
                    const bool sameSurface = rectangle(x, y, 0) == rectangle(x, y, 8.f * ratio);
                    const Pixel& expected = nearHud || staticDetail || imaging || (changedSentinel && sameSurface) ?
                        captured[pixel] : mainImage[pixel];
                    if (imaging && !changedSentinel) continue;
                    bool match = true;
                    for (UINT channel = 0; channel < 3; ++channel)
                        match = match && std::abs(result[pixel][channel] - expected[channel]) < 0.002f;
                    ++checks;
                    if (!match) ++mismatches;
                }
                std::printf("%s: compared=%zu mismatches=%zu\n", name, checks, mismatches);
                expectation(mismatches == 0, "Production lens helper failed motion silhouette/fallback/palette guard");
            };
            lensCase("lens ratio0 original frame", 0.f, false, false, false, false);
            lensCase("lens ratio.5 moving silhouette", 0.5f, false, false, false, false);
            lensCase("lens ratio1 moving silhouette", 1.f, false, false, false, false);
            lensCase("lens near-HUD exclusion", 1.f, true, false, false, false);
            lensCase("lens unmatched retains capture except confirmed disocclusion", 1.f, false, false, true, false);
            lensCase("lens NVG excludes live fallback", 1.f, false, true, true, false);
            lensCase("lens static fine detail stays captured", 1.f, false, false, false, true);
            Shader detail(gpu.device.Get(), wrapper, includes, "detail_main");
            detail.copyConstants(lens);
            detail.constant("scope_lense_motion", std::array<float, 4>{0, 0, 0, 0});
            auto detailInput = scene(width, height, 0, 0, false);
            for (auto& pixel : detailInput) pixel[3] = 0; // Actual swap-chain alpha may be zero.
            auto detailColor = gpu.texture(width, height, detailInput);
            auto detailDepth = gpu.texture(width, height, std::vector<Pixel>(size_t(width) * height, {10, 0, 0, 0}));
            auto detailPosition = gpu.texture(width, height, std::vector<Pixel>(size_t(width) * height, {0, 0, 10, 0}));
            auto detailFlow = gpu.texture(1, 1, {{0, 0, 10, 0}});
            std::array<double, 3> energies{};
            for (UINT setting = 0; setting <= 2; ++setting) {
                detail.constant("scope_lense_detail", std::array<float, 4>{float(setting), 0, 0, 0});
                detail.texture(gpu.context.Get(), "s_second_vp", detailColor);
                detail.texture(gpu.context.Get(), "s_prev_frame", detailColor);
                detail.texture(gpu.context.Get(), "s_second_vp_depth", detailDepth);
                detail.texture(gpu.context.Get(), "s_position", detailPosition);
                detail.texture(gpu.context.Get(), "s_second_vp_flow", detailFlow);
                auto output = gpu.texture(width, height);
                gpu.draw(detail, output);
                const auto pixels = gpu.read(output);
                for (UINT y = 4; y < height - 4; ++y) for (UINT x = 4; x < width - 4; ++x) {
                    const size_t i = size_t(y) * width + x;
                    float lo = detailInput[i][0], hi = lo;
                    for (const size_t neighbor : {i - 1, i + 1, i - width, i + width}) {
                        lo = std::min(lo, detailInput[neighbor][0]);
                        hi = std::max(hi, detailInput[neighbor][0]);
                    }
                    require(pixels[i][0] >= lo - 0.00001f && pixels[i][0] <= hi + 0.00001f,
                        "Production lens detail generated an overshoot/undershoot halo");
                    require(std::abs(pixels[i][3] - 1.f) < 0.00001f, "Lens opacity incorrectly used swap-chain alpha");
                    if (setting == 1) require(std::abs(pixels[i][0] - detailInput[i][0]) < 0.00001f,
                        "Sharpness1 changed neutral captured color");
                    const double gx = double(pixels[i][0]) - pixels[i - 1][0];
                    const double gy = double(pixels[i][0]) - pixels[i - width][0];
                    energies[setting] += gx * gx + gy * gy;
                }
            }
            require(energies[0] < energies[1] * 0.999 && energies[2] > energies[1] * 1.0001,
                "Sharpness0/2 did not soften/enhance image detail around neutral1");
            std::printf("PASS: actual sharpness0/1/2 soften/neutral/sharpen, bounded local range; gradient_energy=(%.6f,%.6f,%.6f).\n",
                energies[0], energies[1], energies[2]);
        }
        auto evaluate = [&](const char* name, float dx, float dy, bool object, bool camera,
            bool flat = false, bool invalidDepth = false, bool physicalCamera = false) {
            auto currentImage = scene(width, height, dx, dy, object, flat);
            std::vector<Pixel> currentDepth(size_t(width) * height, {10.f, 0, 0, 0});
            const float yaw = 0.015f, cosine = std::cos(yaw), sine = std::sin(yaw);
            const float translateX = 0.10f, translateZ = 0.15f;
            if (physicalCamera) {
                for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
                    const float rayX = (float(x) + 0.5f) * (2.f / width) - 1.f;
                    const float rayY = 1.f - (float(y) + 0.5f) * (2.f / height);
                    const float rayZWorld = cosine - sine * rayX;
                    const float z = (10.f - translateZ) / rayZWorld;
                    const float worldX = translateX + (cosine * rayX + sine) * z;
                    const float worldY = rayY * z;
                    const float sourceX = (worldX / 10.f * 0.5f + 0.5f) * width - 0.5f;
                    const float sourceY = (worldY / 10.f * -0.5f + 0.5f) * height - 0.5f;
                    const float value = pattern(sourceX, sourceY, false);
                    currentImage[size_t(y) * width + x] = {value, value, value, 1};
                    currentDepth[size_t(y) * width + x][0] = z;
                }
            }
            auto previous = gpu.texture(width, height, scene(width, height, 0, 0, object, flat));
            auto current = gpu.texture(width, height, currentImage);
            auto depth = gpu.texture(width, height, currentDepth);
            auto previousDepth = gpu.texture(width, height,
                std::vector<Pixel>(size_t(width) * height, {invalidDepth ? 20.f : 10.f, 0, 0, 0}));
            auto emptySeed = gpu.texture(1, 1, {{0, 0, 0, 0}});
            std::array<Texture, 3> levels;
            std::array<std::array<Texture, 3>, 2> pyramid;
            for (UINT level = 0; level < 3; ++level) for (UINT history = 0; history < 2; ++history) {
                const UINT divisor = 4u << level;
                pyramid[history][level] = gpu.texture(width / divisor, height / divisor);
                Texture& input = level > 0 ? pyramid[history][level - 1] : history ? previous : current;
                const float kernel = level == 0 ? 1.f : 0.5f;
                pyramidShader.constant("svp_pyramid_step", std::array<float, 4>{kernel / float(input.width),
                    kernel / float(input.height), 0, 0});
                pyramidShader.texture(gpu.context.Get(), "s_svp_pyramid_source", input);
                gpu.draw(pyramidShader, pyramid[history][level]);
            }
            std::array<float, 16> projection = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                camera ? -2.f * dx / width : 0.f, camera ? 2.f * dy / height : 0.f, 1.01f, 1,
                0, 0, -0.1f, 0};
            if (physicalCamera) projection = {
                cosine, 0, -1.01f * sine, -sine,
                0, 1, 0, 0,
                sine, 0, 1.01f * cosine, cosine,
                translateX, 0, 1.01f * translateZ - 0.1f, translateZ};
            shader.constant("svp_capture_projection", std::array<float, 4>{1, 1, 0, 0});
            shader.constant("svp_capture_to_previous", projection);
            for (UINT level = 0; level < 3; ++level) {
                const UINT divisor = 16u >> level;
                levels[level] = gpu.texture(width / divisor, height / divisor);
                shader.constant("svp_flow_res", std::array<float, 4>{float(divisor) / width,
                    float(divisor) / height, float(level), 1.f});
                shader.texture(gpu.context.Get(), "s_svp_current", pyramid[0][2 - level]);
                shader.texture(gpu.context.Get(), "s_svp_previous", pyramid[1][2 - level]);
                shader.texture(gpu.context.Get(), "s_svp_depth", depth);
                shader.texture(gpu.context.Get(), "s_svp_previous_depth", previousDepth);
                shader.texture(gpu.context.Get(), "s_svp_flow_seed", level ? levels[level - 1] : emptySeed);
                gpu.draw(shader, levels[level]);
            }
            auto flow = gpu.read(levels.back());
            const RegionStats stats = measure(flow, width / 4, height / 4, camera ? 0 : dx, camera ? 0 : dy, 22, 42);
            const double acceptance = double(stats.accepted) / double(stats.count);
            const double correctness = stats.accepted ? double(stats.correct) / double(stats.accepted) : 0.0;
            std::printf("%s: accepted=%.1f%% correct=%.1f%% endpoint=%.3fpx mean_confidence=%.3f vector=(%.3f,%.3f)\n", name,
                acceptance * 100, correctness * 100, stats.accepted ? stats.error / double(stats.accepted) : 0.0,
                stats.confidence / double(stats.count), stats.accepted ? stats.vectorX / double(stats.accepted) : 0.0,
                stats.accepted ? stats.vectorY / double(stats.accepted) : 0.0);
            if (flat || invalidDepth) expectation(stats.accepted == 0, "Unsafe flow accepted for ambiguous/depth-invalid input");
            else {
                expectation(acceptance >= 0.30, "Too little valid optical flow in a fully textured unoccluded target");
                expectation(correctness >= 0.80, "Optical-flow endpoint error exceeds target tolerance");
                if (!camera && stats.accepted) {
                    const double meanX = stats.vectorX / double(stats.accepted);
                    const double meanY = stats.vectorY / double(stats.accepted);
                    expectation(std::abs(meanX - dx) <= std::max(0.35, std::abs(double(dx)) * 0.25) &&
                        std::abs(meanY - dy) <= std::max(0.35, std::abs(double(dy)) * 0.25),
                        "Mean accepted flow fails subpixel direction/speed tolerance");
                }
            }
            if (object) {
                const RegionStats background = measure(flow, width / 4, height / 4, 0, 0, 2, 10);
                expectation(background.accepted > background.count / 2, "Lost static-background flow confidence");
                expectation(background.correct >= background.accepted * 9 / 10, "Invented motion in static background");
            }
            if (lensShader && object && !camera && !flat && !invalidDepth) {
                Shader& lens = *lensShader;
                const auto future = scene(width, height, dx * 1.5f, dy * 1.5f, true);
                auto mainImage = gpu.texture(width, height, future);
                auto mainPosition = gpu.texture(width, height,
                    std::vector<Pixel>(size_t(width) * height, {0, 0, 10, 0}));
                auto resultTexture = gpu.texture(width, height);
                lens.constant("scope_lense_motion", std::array<float, 4>{0.5f, 1, 0.0165f, 0});
                lens.constant("scope_lense_imaging", std::array<float, 4>{0, 0, 0, 0});
                lens.texture(gpu.context.Get(), "s_second_vp", current);
                lens.texture(gpu.context.Get(), "s_prev_frame", mainImage);
                lens.texture(gpu.context.Get(), "s_second_vp_depth", depth);
                lens.texture(gpu.context.Get(), "s_position", mainPosition);
                lens.texture(gpu.context.Get(), "s_second_vp_flow", levels.back());
                gpu.draw(lens, resultTexture);
                const auto result = gpu.read(resultTexture);
                double heldError = 0.0, warpedError = 0.0;
                double validHeldError = 0.0, validWarpedError = 0.0;
                size_t validSamples = 0, rejectedSamples = 0, rejectedChanged = 0;
                size_t samples = 0;
                for (UINT y = 88; y < 168; ++y) for (UINT x = 88; x < 168; ++x) {
                    const size_t pixel = size_t(y) * width + x;
                    const double held = double(currentImage[pixel][0]) - future[pixel][0];
                    const double warped = double(result[pixel][0]) - future[pixel][0];
                    heldError += held * held;
                    warpedError += warped * warped;
                    // Ground-truth motion identifies valid source/destination
                    // coverage independently from the lens's lookup algorithm.
                    const UINT sourceX = UINT(std::clamp(int(float(x) + 0.5f - dx * 0.5f), 0, int(width) - 1));
                    const UINT sourceY = UINT(std::clamp(int(float(y) + 0.5f - dy * 0.5f), 0, int(height) - 1));
                    const Pixel& atSource = flow[size_t(sourceY / 4) * (width / 4) + sourceX / 4];
                    const Pixel& atDestination = flow[size_t(y / 4) * (width / 4) + x / 4];
                    const auto agreesWithTruth = [&](const Pixel& value) {
                        return value[3] >= 0.25f && std::hypot(value[0] * width - dx, value[1] * height - dy) <= 1.25f;
                    };
                    if (agreesWithTruth(atSource) && agreesWithTruth(atDestination) &&
                        std::hypot((atSource[0] - atDestination[0]) * width, (atSource[1] - atDestination[1]) * height) < 1.25f) {
                        ++validSamples;
                        validHeldError += held * held;
                        validWarpedError += warped * warped;
                    }
                    const int flowX = int(std::floor((float(x) + 0.5f) / 4.f - 0.5f));
                    const int flowY = int(std::floor((float(y) + 0.5f) / 4.f - 0.5f));
                    bool footprintRejected = true;
                    for (int row = 0; row <= 1; ++row) for (int column = 0; column <= 1; ++column) {
                        const UINT tx = UINT(std::clamp(flowX + column, 0, int(width / 4) - 1));
                        const UINT ty = UINT(std::clamp(flowY + row, 0, int(height / 4) - 1));
                        if (flow[size_t(ty) * (width / 4) + tx][3] > 0.2f) footprintRejected = false;
                    }
                    if (footprintRejected) {
                        ++rejectedSamples;
                        bool changed = false;
                        for (size_t channel = 0; channel < 3; ++channel) {
                            require(std::isfinite(result[pixel][channel]), "Nonfinite production lens image");
                            if (std::abs(result[pixel][channel] - currentImage[pixel][channel]) > 0.00003f) changed = true;
                        }
                        if (changed) ++rejectedChanged;
                    }
                    ++samples;
                }
                const double heldRms = std::sqrt(heldError / double(samples));
                const double warpedRms = std::sqrt(warpedError / double(samples));
                std::printf("  actual flow+actual lens ratio.5: held_RMSE=%.6f rendered_RMSE=%.6f\n", heldRms, warpedRms);
                if (dx == 8.f && dy == -8.f) {
                    // Ambiguous same-surface patches now retain their capture.
                    // The old global30% claim relied on sentinel-driven live RGB
                    // patches, which the block-artifact regression proves unsafe.
                    const double validHeldRms = validSamples ? std::sqrt(validHeldError / double(validSamples)) : 0;
                    const double validWarpedRms = validSamples ? std::sqrt(validWarpedError / double(validSamples)) : 0;
                    std::printf("  diagonal conservative contract: valid_samples=%zu held_RMSE=%.6f rendered_RMSE=%.6f rejected_footprint=%zu changed_rejected=%zu\n",
                        validSamples, validHeldRms, validWarpedRms, rejectedSamples, rejectedChanged);
                    expectation(validSamples >= samples / 50, "Diagonal valid-flow regression lacks usable coverage");
                    expectation(validWarpedRms < validHeldRms * 0.7, "Reliable diagonal flow did not improve image error by30percent");
                    expectation(std::isfinite(warpedRms) && warpedRms <= heldRms, "Conservative diagonal reprojection made global image error worse");
                    expectation(rejectedSamples >= samples / 20 && rejectedChanged == 0,
                        "Rejected same-surface diagonal flow failed to retain coherent captured pixels");
                }
                else expectation(warpedRms < heldRms * 0.7, "Production flow+sampling pipeline failed to improve temporal image error by30percent");
            }
        };
        evaluate("static textured plane", 0, 0, false, false);
        evaluate("whole plane right 4 pixels", 4, 0, false, false);
        evaluate("target right 1 pixel", 1, 0, true, false);
        evaluate("target right 2.5 pixels", 2.5f, 0, true, false);
        evaluate("target right 4 pixels", 4, 0, true, false);
        evaluate("target left 4 pixels", -4, 0, true, false);
        evaluate("target down 4 pixels", 0, 4, true, false);
        evaluate("target up 4 pixels", 0, -4, true, false);
        evaluate("target diagonal 8 pixels", 8, -8, true, false);
        evaluate("static compensated camera", 8, -4, false, true);
        evaluate("static plane yaw+strafe+forward", 0, 0, false, true, false, false, true);
        evaluate("flat ambiguous frame", 0, 0, false, false, true);
        evaluate("depth disocclusion", 4, 0, false, false, false, true);
        require(failures == 0, "One or more production optical-flow scenarios failed");
        std::puts("PASS: production PiP flow shader on D3D11 WARP; moving target, static scene/camera compensation, ambiguity and depth rejection.");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

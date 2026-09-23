// Executes production PiP motion-address shaders against analytical world scenes.
// Standalone D3D11 utilities adapted from pip_flow_gpu_test; no flow implementation.
// Synthetic GPU fixtures validate coordinates/provenance, not game FPS.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxgi.h>
#include <chrono>
#include <functional>
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

static bool validMap(const Pixel& value)
{
    const bool owner=value[3]==1.f || (value[3]>-.5f && value[3]<=-1.f/16384.f);
    return owner && std::isfinite(value[0]) && std::isfinite(value[1]) &&
        value[0]>=0 && value[0]<=1 && value[1]>=0 && value[1]<=1;
}

static std::uint16_t halfBits(float value)
{
    std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));
    const std::uint32_t sign=(bits>>16)&0x8000,mantissa=bits&0x7fffff;
    const int exponent=int((bits>>23)&255)-127+15;
    if (exponent>=31)
        return std::uint16_t(sign|0x7c00|(((bits>>23)&255)==255 && mantissa?0x200:0));
    if (exponent<=0) {
        if (exponent<-10) return std::uint16_t(sign);
        const std::uint32_t significand=mantissa|0x800000;
        const unsigned shift=unsigned(14-exponent);
        std::uint32_t rounded=significand>>shift;
        const std::uint32_t remainder=significand&((1u<<shift)-1),half=1u<<(shift-1);
        rounded+=remainder>half || (remainder==half && (rounded&1));
        return std::uint16_t(sign|rounded);
    }
    const std::uint32_t rounded=(mantissa+0xfff+((mantissa>>13)&1))>>13;
    return std::uint16_t(sign|(std::uint32_t(exponent)<<10)+rounded);
}

static float halfValue(std::uint16_t bits)
{
    const unsigned exponent=(bits>>10)&31,mantissa=bits&1023;
    const float sign=bits&0x8000?-1.f:1.f;
    if (exponent==31) return mantissa?std::numeric_limits<float>::quiet_NaN():sign*std::numeric_limits<float>::infinity();
    return sign*std::ldexp(float(exponent?1024+mantissa:mantissa),exponent?int(exponent)-25:-24);
}

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
    bool hasConstant(const char* name) const { return variables.find(name) != variables.end(); }

    Shader(ID3D11Device* device, const fs::path& source, Includes& includes, const char* entry = "main")
    {
        ComPtr<ID3DBlob> code, errors;
        const D3D_SHADER_MACRO defines[] = {{"USE_DX11", "1"}, {"GBUFFER_OPTIMIZATION", "1"}, {nullptr, nullptr}};
        const HRESULT result = D3DCompileFromFile(source.c_str(), defines, &includes, entry, "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
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

    bool software = false;
    explicit Gpu(bool forceWarp)
    {
        D3D_FEATURE_LEVEL feature=D3D_FEATURE_LEVEL_9_1;
        HRESULT result = E_FAIL;
        if (!forceWarp)
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, &feature, &context);
        if (FAILED(result)) {
            software = true;
            check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, &feature, &context), "D3D11CreateDevice WARP fallback");
        }
        ComPtr<IDXGIDevice> dxgi;
        check(device.As(&dxgi), "Query IDXGIDevice");
        ComPtr<IDXGIAdapter> adapter;
        check(dxgi->GetAdapter(&adapter), "Get adapter");
        DXGI_ADAPTER_DESC description{};
        check(adapter->GetDesc(&description), "Get adapter description");
        char name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
        std::printf("ADAPTER %s mode=%s feature_level=0x%x dedicated_vram_mib=%zu\n",
            name, software ? "WARP SOFTWARE FALLBACK" : "HARDWARE", feature, description.DedicatedVideoMemory >> 20);
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

    Texture texture(UINT width, UINT height, const std::vector<Pixel>& pixels = {}, DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        Texture texture;
        texture.width = width; texture.height = height;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        std::vector<float> scalar;
        std::vector<std::array<std::uint8_t,4>> unorm;
        std::vector<std::uint16_t> halfScalar;
        std::vector<std::array<std::uint16_t,4>> halves;
        if (format == DXGI_FORMAT_R32_FLOAT && !pixels.empty()) {
            scalar.reserve(pixels.size());
            for (const auto& pixel : pixels) scalar.push_back(pixel[0]);
        }
        if (format==DXGI_FORMAT_R16G16B16A16_FLOAT && !pixels.empty()) {
            halves.resize(pixels.size());
            for (size_t i=0;i<pixels.size();++i) for (size_t c=0;c<4;++c) halves[i][c]=halfBits(pixels[i][c]);
        }
        if (format==DXGI_FORMAT_R16_FLOAT && !pixels.empty()) {
            halfScalar.reserve(pixels.size());
            for (const auto& pixel:pixels) halfScalar.push_back(halfBits(pixel[0]));
        }
        if (format==DXGI_FORMAT_R8G8B8A8_UNORM && !pixels.empty()) {
            unorm.resize(pixels.size());
            for (size_t i=0;i<pixels.size();++i) for (size_t c=0;c<4;++c)
                unorm[i][c]=std::uint8_t(std::floor(std::clamp(pixels[i][c],0.f,1.f)*255.f+.5f));
        }
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = scalar.empty() ? static_cast<const void*>(pixels.data()) : scalar.data();
        if (!halves.empty()) data.pSysMem=halves.data();
        if (!halfScalar.empty()) data.pSysMem=halfScalar.data();
        if (!unorm.empty()) data.pSysMem=unorm.data();
        data.SysMemPitch = width * (format == DXGI_FORMAT_R32_FLOAT ? sizeof(float) :
            format==DXGI_FORMAT_R8G8B8A8_UNORM?sizeof(std::array<std::uint8_t,4>):
            format==DXGI_FORMAT_R16_FLOAT?sizeof(std::uint16_t):
            format==DXGI_FORMAT_R16G16B16A16_FLOAT?sizeof(std::array<std::uint16_t,4>):sizeof(Pixel));
        check(device->CreateTexture2D(&description, pixels.empty() ? nullptr : &data, &texture.data), "CreateTexture2D");
        check(device->CreateShaderResourceView(texture.data.Get(), nullptr, &texture.srv), "CreateShaderResourceView");
        check(device->CreateRenderTargetView(texture.data.Get(), nullptr, &texture.rtv), "CreateRenderTargetView");
        return texture;
    }

    void draw(Shader& shader, Texture& destination,Texture* second=nullptr)
    {
        ID3D11RenderTargetView* targets[]={destination.rtv.Get(),second?second->rtv.Get():nullptr};
        context->OMSetRenderTargets(second?2:1,targets,nullptr);
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
        for (UINT y = 0; y < texture.height; ++y) {
            const auto* row = static_cast<unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch;
            if (description.Format == DXGI_FORMAT_R32_FLOAT) {
                const auto* values = reinterpret_cast<const float*>(row);
                for (UINT x = 0; x < texture.width; ++x) output[size_t(y) * texture.width + x] = {values[x],0,0,0};
            } else if (description.Format==DXGI_FORMAT_R16_FLOAT) {
                const auto* values=reinterpret_cast<const std::uint16_t*>(row);
                for (UINT x=0;x<texture.width;++x) output[size_t(y)*texture.width+x]={halfValue(values[x]),0,0,0};
            } else if (description.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const auto* values=reinterpret_cast<const std::uint16_t*>(row);
                for (UINT x=0;x<texture.width;++x) for (UINT channel=0;channel<4;++channel)
                    output[size_t(y)*texture.width+x][channel]=halfValue(values[size_t(x)*4+channel]);
            } else std::memcpy(output.data() + size_t(y) * texture.width, row, size_t(texture.width) * sizeof(Pixel));
        }
        context->Unmap(staging.Get(), 0);
        return output;
    }
};

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Analytical fixtures and production resource bindings follow below.
struct Vec3
{
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(double scale) const { return {x*scale,y*scale,z*scale}; }
};
static double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
using UV = std::array<double,2>;
using Mat4 = std::array<float,16>;

struct Camera
{
    Vec3 position;
    double yaw = 0, tangent = .65, aspect = 16.0/9.0;
    UV center = {.5,.5}, jitter = {0,0};
    Vec3 right() const { return {std::cos(yaw),0,-std::sin(yaw)}; }
    Vec3 forward() const { return {std::sin(yaw),0,std::cos(yaw)}; }
    Vec3 ray(UV uv) const
    {
        return right()*((uv[0]-center[0])*2*tangent*aspect) +
            Vec3{0,(center[1]-uv[1])*2*tangent,0} + forward();
    }
    UV project(Vec3 point) const
    {
        const auto offset=point-position;
        const double z=dot(offset,forward());
        return {center[0]+dot(offset,right())/(2*z*tangent*aspect),
            center[1]-offset.y/(2*z*tangent)};
    }
    double depth(Vec3 point) const { return dot(point-position,forward()); }
    Pixel rayConstant() const
    {
        return {float(tangent*aspect),float(tangent),
            float((1-2*center[0])*tangent*aspect),float((2*center[1]-1)*tangent)};
    }
};

static Mat4 relativeTransform(const Camera& source, const Camera& destination)
{
    const double delta=source.yaw-destination.yaw;
    const float c=float(std::cos(delta)),s=float(std::sin(delta));
    const Vec3 translation=source.position-destination.position;
    return {c,0,-s,0, 0,1,0,0, s,0,c,0,
        float(dot(translation,destination.right())),float(translation.y),
        float(dot(translation,destination.forward())),1};
}

struct Scene
{
    Vec3 velocity={7,1.2,-2.5};
    bool npc=true;
    double npcZ=10,backgroundZ=24;
    Vec3 npcCenter(double time) const { return Vec3{-.4,0,npcZ}+velocity*time; }
    struct Hit { Vec3 point; int surface; double edge; };
    Hit intersect(const Camera& camera, UV uv, double time) const
    {
        const Vec3 ray=camera.ray(uv);
        const Vec3 center=npcCenter(time);
        const Vec3 foreground=camera.position+ray*((center.z-camera.position.z)/ray.z);
        const Vec3 local=foreground-center;
        const double edge=std::min(1.25-std::abs(local.x),1.35-std::abs(local.y));
        if (npc && edge>0)
            return {foreground,1,edge};
        return {camera.position+ray*((backgroundZ-camera.position.z)/ray.z),0,-edge};
    }
    Vec3 corresponding(const Hit& hit,double from,double to) const
    {
        return hit.surface ? hit.point+velocity*(to-from) : hit.point;
    }
};

static Camera mainCamera(double time,bool move=true)
{
    Camera camera;
    if (move) {
        camera.position={.35*time,.12*time,.18*time};
        camera.yaw=.17*time;
    }
    return camera;
}

static Camera lensCamera(Camera camera)
{
    camera.tangent=.19;
    return camera;
}

static UV mainUv(const Camera& main,const Camera& lens,UV uv)
{
    const double scale=lens.tangent/main.tangent;
    return {main.center[0]+(uv[0]-lens.center[0])*scale,
        main.center[1]+(uv[1]-lens.center[1])*scale};
}

static Pixel mainCrop(const Camera& main,const Camera& lens)
{
    const double scale=lens.tangent/main.tangent;
    return {float(scale),float(scale),float(main.center[0]+(.5-lens.center[0])*scale),
        float(main.center[1]+(.5-lens.center[1])*scale)};
}

static Pixel materialColor(const Scene& scene,const Scene::Hit& hit,double time)
{
    const Vec3 point=scene.corresponding(hit,time,0);
    const double fine=.5+.5*std::sin(33.7*point.x+2.1*point.y)*std::cos(26.3*point.y-.3*point.x);
    return {float(.1+.75*fine),float(.15+.7*(.5+.5*std::sin(9.7*point.x))),
        hit.surface ? .8f : .15f,1};
}

struct Fixture
{
    UINT mainWidth=768,mainHeight=432,mapWidth=256,mapHeight=144;
    Scene scene;
    Camera previous,current,capture,previousLens,currentLens;
    double t0=0,t1=.1,tc=.05;
    std::vector<Pixel> position,motion,captureDepth,captureOwner,captureRgb,previousMap;

    void generate()
    {
        position.resize(size_t(mainWidth)*mainHeight);
        motion.resize(position.size());
        for (UINT y=0;y<mainHeight;++y) for (UINT x=0;x<mainWidth;++x) {
            const UV uv={(x+.5)/mainWidth-current.jitter[0],(y+.5)/mainHeight-current.jitter[1]};
            const auto hit=scene.intersect(current,uv,t1);
            const auto old=previous.project(scene.corresponding(hit,t1,t0));
            const size_t index=size_t(y)*mainWidth+x;
            position[index]={0,0,float(current.depth(hit.point)),0};
            motion[index]={float(uv[0]-old[0]),float(uv[1]-old[1]),0,hit.surface?-.25f:0.f};
        }
        captureDepth.resize(size_t(mapWidth)*mapHeight);
        captureRgb.resize(captureDepth.size());
        captureOwner.resize(captureDepth.size());
        previousMap.resize(captureDepth.size());
        for (UINT y=0;y<mapHeight;++y) for (UINT x=0;x<mapWidth;++x) {
            const UV uv={(x+.5)/mapWidth,(y+.5)/mapHeight};
            const UV captureUv={uv[0]-capture.jitter[0],uv[1]-capture.jitter[1]};
            const auto hit=scene.intersect(capture,captureUv,tc);
            const size_t index=size_t(y)*mapWidth+x;
            captureDepth[index]={halfValue(halfBits(float(capture.depth(hit.point)))),0,0,0};
            captureRgb[index]=materialColor(scene,hit,tc);
            captureOwner[index]={hit.surface?-.25f:1.f,0,0,0};
            UV prev=mainUv(previous,previousLens,uv);
            // The real previous map stores main G-buffer depth, not ideal lens depth.
            prev[0]=(std::floor((prev[0]+previous.jitter[0])*mainWidth)+.5)/mainWidth-previous.jitter[0];
            prev[1]=(std::floor((prev[1]+previous.jitter[1])*mainHeight)+.5)/mainHeight-previous.jitter[1];
            const auto old=scene.intersect(previous,prev,t0);
            previousMap[index]={-1,-1,halfValue(halfBits(float(previous.depth(old.point)))),old.surface?-.25f:1.f};
        }
    }

    UV oracle(UINT x,UINT y) const
    {
        const UV uv={(x+.5)/mapWidth,(y+.5)/mapHeight};
        const auto hit=scene.intersect(current,mainUv(current,currentLens,uv),t1);
        auto result=capture.project(scene.corresponding(hit,t1,tc));
        result[0]+=capture.jitter[0];result[1]+=capture.jitter[1];
        return result;
    }

    bool interior(UINT x,UINT y) const
    {
        const UV uv={(x+.5)/mapWidth,(y+.5)/mapHeight};
        const auto hit=scene.intersect(current,mainUv(current,currentLens,uv),t1);
        if (hit.edge<.12) return false;
        const auto cp=scene.corresponding(hit,t1,tc);
        const auto cu=capture.project(cp);
        if (cu[0]<.05 || cu[0]>.95 || cu[1]<.05 || cu[1]>.95) return false;
        const auto oldPoint=scene.corresponding(hit,t1,t0);
        const auto pu=previous.project(oldPoint);
        const auto oldHit=scene.intersect(previous,pu,t0);
        const auto capturedHit=scene.intersect(capture,cu,tc);
        return oldHit.surface==hit.surface && oldHit.edge>.12 &&
            capturedHit.surface==hit.surface && capturedHit.edge>.12;
    }
};

static Fixture scenario(double fraction,bool cameraMoves=true,bool npc=true);

static void constants(Shader& shader,const Fixture& fixture,float mode,bool previousMetadata=true)
{
    shader.constant("pip_current_main",mainCrop(fixture.current,fixture.currentLens));
    shader.constant("pip_previous_main",mainCrop(fixture.previous,fixture.previousLens));
    shader.constant("pip_main_jitter",Pixel{float(fixture.current.jitter[0]),float(fixture.current.jitter[1]),
        float(fixture.previous.jitter[0]),float(fixture.previous.jitter[1])});
    shader.constant("pip_current_ray",fixture.current.rayConstant());
    shader.constant("pip_previous_ray",fixture.previous.rayConstant());
    shader.constant("pip_capture_projection",Pixel{float(.5/(fixture.capture.tangent*fixture.capture.aspect)),
        float(-.5/fixture.capture.tangent),float(fixture.capture.center[0]+fixture.capture.jitter[0]),
        float(fixture.capture.center[1]+fixture.capture.jitter[1])});
    shader.constant("pip_current_to_capture",relativeTransform(fixture.current,fixture.capture));
    shader.constant("pip_previous_to_capture",relativeTransform(fixture.previous,fixture.capture));
    shader.constant("pip_motion_control",Pixel{mode,previousMetadata?1.f:0.f,
        float((fixture.tc-fixture.t0)/(fixture.t1-fixture.t0)),float(fixture.t1-fixture.t0)});
    shader.constant("pip_motion_limits",Pixel{.02f,.005f,64,4});
}

static std::vector<Pixel> execute(Gpu& gpu,Shader& shader,const Fixture& fixture,float mode,
    const std::vector<Pixel>* previous=nullptr,bool previousMetadata=true)
{
    constants(shader,fixture,mode,previousMetadata);
    auto position=gpu.texture(fixture.mainWidth,fixture.mainHeight,fixture.position,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto motion=gpu.texture(fixture.mainWidth,fixture.mainHeight,fixture.motion,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto old=gpu.texture(fixture.mapWidth,fixture.mapHeight,previous?*previous:fixture.previousMap);
    auto depth=gpu.texture(fixture.mapWidth,fixture.mapHeight,fixture.captureDepth,DXGI_FORMAT_R32_FLOAT);
    auto owner=gpu.texture(fixture.mapWidth,fixture.mapHeight,fixture.captureOwner,DXGI_FORMAT_R16_FLOAT);
    auto output=gpu.texture(fixture.mapWidth,fixture.mapHeight);
    shader.texture(gpu.context.Get(),"s_pip_main_position",position);
    shader.texture(gpu.context.Get(),"s_pip_main_motion",motion);
    shader.texture(gpu.context.Get(),"s_pip_previous_map",old);
    shader.texture(gpu.context.Get(),"s_pip_capture_depth",depth);
    shader.texture(gpu.context.Get(),"s_pip_capture_owner",owner);
    gpu.draw(shader,output);
    return gpu.read(output);
}

static void coordinates(const char* label,const Fixture& fixture,const std::vector<Pixel>& actual,double tolerance=.35,
    bool productionInterval=true)
{
    size_t tested=0,valid=0;
    double maximum=0,sum=0;
    for (UINT y=4;y<fixture.mapHeight-4;++y) for (UINT x=4;x<fixture.mapWidth-4;++x) {
        const Pixel pixel=actual[size_t(y)*fixture.mapWidth+x];
        require(std::all_of(pixel.begin(),pixel.end(),[](float value){return std::isfinite(value);}),
            "Nonfinite motion-map output");
        if (!fixture.interior(x,y)) continue;
        ++tested;
        if (!validMap(pixel)) continue;
        ++valid;
        const auto expected=fixture.oracle(x,y);
        const double error=std::hypot((pixel[0]-expected[0])*fixture.mapWidth,
            (pixel[1]-expected[1])*fixture.mapHeight);
        sum+=error;maximum=std::max(maximum,error);
    }
    std::printf("COORDINATES %s interior=%zu valid=%zu mean_px=%.6f max_px=%.6f tolerance_px=%.3f\n",
        label,tested,valid,sum/double(std::max(size_t(1),valid)),maximum,tolerance);
    require(tested>100,"Analytical fixture contains no useful interior");
    if (productionInterval)
        require(double(valid)/tested>.95,"Too many stable visible surfaces rejected within supported capture interval");
    require(maximum<tolerance,"Map disagrees with independently projected world trajectory");
}

static void captureDepthPass(Gpu& gpu,Shader& shader)
{
    constexpr UINT width=64,height=32;
    std::vector<Pixel> data(size_t(width)*height);
    for (size_t i=0;i<data.size();++i) data[i]={0,0,float(2+.125*(i%128)),0};
    data[0][2]=0;data[1][2]=-1;data[2][2]=std::numeric_limits<float>::quiet_NaN();
    auto input=gpu.texture(width,height,data,DXGI_FORMAT_R16G16B16A16_FLOAT);
    std::vector<Pixel> motion(data.size(),Pixel{0,0,0,0});
    for (size_t i=0;i<motion.size();++i) motion[i][3]=i%2?-.25f:0.f;
    auto velocity=gpu.texture(width,height,motion,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto output=gpu.texture(width,height,{},DXGI_FORMAT_R32_FLOAT);
    auto owner=gpu.texture(width,height,{},DXGI_FORMAT_R16_FLOAT);
    shader.texture(gpu.context.Get(),"s_pip_capture_position",input);
    shader.texture(gpu.context.Get(),"s_pip_capture_motion",velocity);
    gpu.draw(shader,output,&owner);
    const auto result=gpu.read(output);
    const auto owners=gpu.read(owner);
    for (size_t i=0;i<data.size();++i)
        require(result[i][0]==(i<3?0.f:data[i][2]),"Production capture-depth PS packing/invalid-depth mismatch");
    for (size_t i=3;i<data.size();++i)
        require(owners[i][0]==(i%2?-.25f:1.f),"Production capture-owner MRT disagrees with source surface");
    std::printf("DEPTH_CAPTURE production_R32F pixels=%zu invalid_rejected=3 max_error=0\n",data.size());
}

static void motionProducer(const fs::path& root,Gpu& gpu)
{
    const fs::path wrapper=root/"_build/v144-validation/production_motion_producer.ps";
    {
        std::ofstream source(wrapper);
        source<<"#include \"screenspace_mvectors.h\"\n"
            "float4 producer_current,producer_previous,producer_masks;\n"
            "float4 main(float4 p:SV_Position):SV_Target {\n"
            "return ssfx_mv_calc(producer_current,producer_previous,producer_masks.x,producer_masks.y); }\n";
    }
    const fs::path authored=root/"gamedata/shaders";
    const std::array<float,7> owners={0.f,1.f,-.25f,-.250244140625f,
        -1.f/16384.f,-.499755859375f,-.000061094760894775390625f};
    size_t cases=0;
    for (const char* renderer:{"r3","r4"}) {
        Includes includes({authored/renderer});
        Shader producer(gpu.device.Get(),wrapper,includes);
        producer.constant("producer_current",Pixel{.75f,.25f,0.f,2.f});
        producer.constant("producer_previous",Pixel{-.25f,.5f,0.f,1.f});
        auto output=gpu.texture(1,1,{},DXGI_FORMAT_R16G16B16A16_FLOAT);
        for (float owner:owners) for (float unknown:{0.f,2.f})
            for (float hud:{0.f,.5f,1.f}) for (float taa:{-1.f,-.75f,0.f,.25f,1.f,2.f}) {
                producer.constant("pip_motion_history",Pixel{unknown,owner,0.f,0.f});
                producer.constant("producer_masks",Pixel{hud,taa,0.f,0.f});
                gpu.draw(producer,output);
                const auto result=gpu.read(output).front();
                float mask=taa; if(hud>0) mask=std::max(mask,hud); if(unknown>0) mask=std::max(mask,unknown);
                const Pixel expected={.3125f*(1.f-hud),.1875f*(1.f-hud),hud,mask==0.f?owner:mask};
                for (size_t channel=0;channel<4;++channel)
                    require(result[channel]==halfValue(halfBits(expected[channel])),
                        "Actual ssfx_mv_calc producer altered velocity, HUD/reactive mask or FP16 owner identity");
                ++cases;
            }
    }
    std::printf("MOTION_PRODUCER actual_screenspace_mvectors_r3_r4 cases=%zu FP16_velocity_HUD_reactive_unknown_and_adjacent_owner_exact=PASS\n",cases);
}

static fs::path writeConsumer(const fs::path& root)
{
    const fs::path output=root/"_build/v144-validation/motion_map_consumer.ps";
    fs::create_directories(output.parent_path());
    std::ofstream source(output);
    source<<"#include \"anthology_pip_motion_sample.h\"\n"
        "Texture2D<float4> s_capture_rgb; SamplerState smp_linear;\n"
        "float4 address(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target { return pip_sample_motion_map(uv); }\n"
        "float4 color(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target {\n"
        "float4 a=pip_sample_motion_map(uv);if(a.w<.5)return float4(1,0,1,0);"
        "return float4(s_capture_rgb.SampleLevel(smp_linear,a.xy,0).rgb,1); }\n";
    return output;
}

static void mapSamplingSeams(Gpu& gpu,Shader& consumer)
{
    constexpr UINT width=32,height=16;
    consumer.constant("scope_lense_motion",Pixel{1,.02f,.005f,4});
    for (unsigned scenario=0;scenario<4;++scenario) {
        std::vector<Pixel> map(size_t(width)*height);
        for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
            const bool right=x>=width/2;
            map[size_t(y)*width+x]={right?(scenario==3?.21f:.8f):.2f,.5f,scenario==1 && right?20.f:5.f,
                scenario==2 && right?0.f:scenario==3 && right?-.25f:1.f};
        }
        auto input=gpu.texture(width,height,map),output=gpu.texture(width*4,height*4);
        consumer.texture(gpu.context.Get(),"s_pip_motion_map",input);
        gpu.draw(consumer,output);
        const auto result=gpu.read(output);
        size_t valid=0,rejected=0;
        for (const auto& pixel:result) {
            if (pixel[3]<.5f) { ++rejected;continue; }
            ++valid;
            require(std::abs(pixel[0]-.2f)<.00001f || std::abs(pixel[0]-(scenario==3?.21f:.8f))<.00001f,
                "Production map consumer averaged addresses across unrelated surfaces");
            if (scenario==1)
                require(std::abs(pixel[2]-5)<.0001 || std::abs(pixel[2]-20)<.0001,
                    "Production map consumer averaged depths across surface seam");
            if (scenario==2) require(std::abs(pixel[0]-.2f)<.00001f,"Invalid address provenance became valid");
        }
        std::printf("CONSUMER_SEAM kind=%s valid=%zu rejected=%zu invalid_address_blends=0\n",
            scenario==0?"coplanar_address_jump":scenario==1?"depth_jump":scenario==2?"invalid_provenance":"owner_identity",valid,rejected);
        require(valid>100 && rejected>0,"Seam fixture failed to exercise acceptance and rejection");
    }
}

static Pixel linearColor(const std::vector<Pixel>& pixels,UINT width,UINT height,UV uv)
{
    const double gx=uv[0]*width-.5,gy=uv[1]*height-.5;
    const int x=int(std::floor(gx)),y=int(std::floor(gy));
    const double fx=gx-x,fy=gy-y;
    Pixel output={0,0,0,0};
    for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx) {
        const int xx=std::clamp(x+dx,0,int(width)-1),yy=std::clamp(y+dy,0,int(height)-1);
        const auto& pixel=pixels[size_t(yy)*width+xx];
        const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
        for (size_t channel=0;channel<4;++channel) output[channel]+=float(pixel[channel]*weight);
    }
    return output;
}

static void immutableRgb(Gpu& gpu,Shader& mapShader,Shader& consumer)
{
    Fixture fixture;
    fixture.scene.npc=false;
    fixture.t0=-.016;fixture.t1=fixture.tc=0;
    fixture.previous=mainCamera(0,false);fixture.current=fixture.previous;
    fixture.previousLens=lensCamera(fixture.previous);fixture.currentLens=lensCamera(fixture.current);
    fixture.capture=fixture.currentLens;
    fixture.generate();
    auto history=execute(gpu,mapShader,fixture,1);
    const auto immutable=fixture.captureRgb;
    auto capture=gpu.texture(fixture.mapWidth,fixture.mapHeight,immutable);
    const double worldStep=.4*2*24*fixture.capture.tangent*fixture.capture.aspect/fixture.mapWidth;
    for (unsigned step=1;step<=32;++step) {
        fixture.t0=(step-1)*.016;fixture.t1=step*.016;
        fixture.previous.position.x=(step-1)*worldStep;
        fixture.current.position.x=step*worldStep;
        fixture.previousLens=lensCamera(fixture.previous);fixture.currentLens=lensCamera(fixture.current);
        fixture.generate();
        auto next=execute(gpu,mapShader,fixture,2,&history);
        history=std::move(next);
    }
    auto map=gpu.texture(fixture.mapWidth,fixture.mapHeight,history);
    auto output=gpu.texture(fixture.mapWidth,fixture.mapHeight);
    consumer.constant("scope_lense_motion",Pixel{1,.02f,.005f,4});
    consumer.texture(gpu.context.Get(),"s_pip_motion_map",map);
    consumer.texture(gpu.context.Get(),"s_capture_rgb",capture);
    gpu.draw(consumer,output);
    const auto result=gpu.read(output);
    // Compare filtering with the same GPU sampler: D3D11 bilinear weights have
    // limited subtexel precision, unlike the ideal CPU linearColor calculation.
    std::vector<Pixel> oracleMap(history.size());
    for(UINT y=0;y<fixture.mapHeight;++y)for(UINT x=0;x<fixture.mapWidth;++x){
        const auto uv=fixture.oracle(x,y);
        oracleMap[size_t(y)*fixture.mapWidth+x]={float(uv[0]),float(uv[1]),24.f,1.f};
    }
    auto oracleTexture=gpu.texture(fixture.mapWidth,fixture.mapHeight,oracleMap);
    consumer.texture(gpu.context.Get(),"s_pip_motion_map",oracleTexture);
    consumer.texture(gpu.context.Get(),"s_capture_rgb",capture);
    gpu.draw(consumer,output);
    const auto gpuOracle=gpu.read(output);
    double maximum=0,actualEnergy=0,oracleEnergy=0;size_t tested=0;
    for (UINT y=8;y<fixture.mapHeight-8;++y) for (UINT x=8;x<fixture.mapWidth-32;++x) {
        const size_t index=size_t(y)*fixture.mapWidth+x;
        require(result[index][3]>.5f && result[index-1][3]>.5f,"Static plane lost immutable RGB provenance");
        const auto expected=linearColor(immutable,fixture.mapWidth,fixture.mapHeight,fixture.oracle(x,y));
        for (size_t channel=0;channel<3;++channel)
            maximum=std::max(maximum,std::abs(double(result[index][channel])-expected[channel]));
        actualEnergy+=std::pow(double(result[index][0])-result[index-1][0],2);
        oracleEnergy+=std::pow(double(gpuOracle[index][0])-gpuOracle[index-1][0],2);
        ++tested;
    }
    const double retained=actualEnergy/oracleEnergy;
    std::printf("IMMUTABLE_RGB steps=32 motion_per_step_capture_px=.4 tested=%zu max_rgb_error=%.6f gradient_energy_ratio=%.6f\n",
        tested,maximum,retained);
    require(maximum<.003,"Immutable RGB differs from a single direct capture lookup after 32 map updates");
    require(retained>.99 && retained<1.01,"Map updates caused cumulative RGB blur");
}

static void disocclusion(Gpu& gpu,Shader& shader)
{
    Fixture fixture=scenario(.5);
    const auto output=execute(gpu,shader,fixture,1);
    size_t newlyVisible=0,borrowed=0;
    for (UINT y=4;y<fixture.mapHeight-4;++y) for (UINT x=4;x<fixture.mapWidth-4;++x) {
        const UV uv={(x+.5)/fixture.mapWidth,(y+.5)/fixture.mapHeight};
        const auto now=fixture.scene.intersect(fixture.current,mainUv(fixture.current,fixture.currentLens,uv),fixture.t1);
        if (now.surface || now.edge<.1) continue;
        const auto captureUv=fixture.capture.project(now.point);
        const auto old=fixture.scene.intersect(fixture.capture,captureUv,fixture.tc);
        if (!old.surface || old.edge<.1) continue;
        ++newlyVisible;
        borrowed+=validMap(output[size_t(y)*fixture.mapWidth+x]);
    }
    std::printf("DISOCCLUSION new_background_pixels=%zu borrowed_foreground_addresses=%zu\n",newlyVisible,borrowed);
    require(newlyVisible>30 && borrowed==0,"Newly exposed background borrowed the captured NPC silhouette");
}

static Fixture scenario(double fraction,bool cameraMoves,bool npc)
{
    Fixture fixture;
    fixture.tc=fixture.t0+(fixture.t1-fixture.t0)*fraction;
    fixture.scene.npc=npc;
    fixture.previous=mainCamera(fixture.t0,cameraMoves);
    fixture.current=mainCamera(fixture.t1,cameraMoves);
    fixture.previousLens=lensCamera(fixture.previous);
    fixture.currentLens=lensCamera(fixture.current);
    fixture.capture=lensCamera(mainCamera(fixture.tc,cameraMoves));
    fixture.generate();
    return fixture;
}

static bool coplanarDisocclusion(Gpu& gpu,Shader& shader,double gap)
{
    Fixture fixture=scenario(0,false);
    fixture.scene.backgroundZ=fixture.scene.npcZ+gap;
    fixture.scene.velocity={7,0,0};
    fixture.t0=-.1;fixture.t1=fixture.tc=0;
    fixture.generate();
    const auto old=execute(gpu,shader,fixture,1);
    fixture.t0=0;fixture.t1=.1;
    fixture.generate();
    const auto output=execute(gpu,shader,fixture,2,&old);
    size_t exposed=0,accepted=0,wrong=0;
    for (UINT y=4;y<fixture.mapHeight-4;++y) for (UINT x=4;x<fixture.mapWidth-4;++x) {
        const UV uv={(x+.5)/fixture.mapWidth,(y+.5)/fixture.mapHeight};
        const auto now=fixture.scene.intersect(fixture.current,mainUv(fixture.current,fixture.currentLens,uv),fixture.t1);
        if (now.surface || now.edge<.08) continue;
        const auto captured=fixture.scene.intersect(fixture.capture,fixture.capture.project(now.point),fixture.tc);
        if (!captured.surface || captured.edge<.08) continue;
        ++exposed;
        const Pixel value=output[size_t(y)*fixture.mapWidth+x];
        if (!validMap(value)) continue;
        ++accepted;
        const auto addressed=fixture.scene.intersect(fixture.capture,
            UV{value[0]-fixture.capture.jitter[0],value[1]-fixture.capture.jitter[1]},fixture.tc);
        wrong+=addressed.surface!=now.surface;
    }
    std::printf("COPLANAR_DISOCCLUSION gap_m=%.3f exposed=%zu accepted=%zu wrong_surface_addresses=%zu status=%s\n",
        gap,exposed,accepted,wrong,wrong?"FAIL_WRONG_PROVENANCE":"PASS");
    require(exposed>100,"Coplanar fixture did not expose enough wall pixels");
    return wrong==0;
}

static void missingCoverage(Gpu& gpu,Shader& shader)
{
    Fixture fixture=scenario(.5);
    fixture.previousLens.tangent=.01;
    fixture.generate();
    const auto output=execute(gpu,shader,fixture,1);
    size_t outside=0,accepted=0;
    for (UINT y=4;y<fixture.mapHeight-4;++y) for (UINT x=4;x<fixture.mapWidth-4;++x) {
        const UV uv={(x+.5)/fixture.mapWidth,(y+.5)/fixture.mapHeight};
        const auto hit=fixture.scene.intersect(fixture.current,mainUv(fixture.current,fixture.currentLens,uv),fixture.t1);
        const auto previous=fixture.previousLens.project(fixture.scene.corresponding(hit,fixture.t1,fixture.t0));
        if (previous[0]>-.01 && previous[0]<1.01 && previous[1]>-.01 && previous[1]<1.01) continue;
        ++outside;accepted+=validMap(output[size_t(y)*fixture.mapWidth+x]);
    }
    std::printf("PREVIOUS_COVERAGE outside=%zu accepted=%zu\n",outside,accepted);
    require(outside>10000 && accepted==0,"Missing previous lens coverage invented a capture correspondence");
}

static void adjacentOwnerIds(Gpu& gpu,Shader& shader)
{
    for (float first:{-.25f,-1.f/16384.f}) {
        const float adjacent=halfValue(std::uint16_t(halfBits(first)+1));
        Fixture fixture=scenario(.5,false,false);
        for (auto& pixel:fixture.motion) pixel[3]=first;
        for (auto& pixel:fixture.previousMap) pixel[3]=first;
        for (auto& pixel:fixture.captureOwner) pixel[0]=first;
        const auto seeded=execute(gpu,shader,fixture,1);
        coordinates("exact_FP16_owner_roundtrip",fixture,seeded,.01);
        for (auto& pixel:fixture.captureOwner) pixel[0]=adjacent;
        const auto wrongCapture=execute(gpu,shader,fixture,1);
        size_t captured=0;for (const auto& pixel:wrongCapture) captured+=validMap(pixel);
        for (auto& pixel:fixture.captureOwner) pixel[0]=first;
        auto wrongPrevious=seeded;
        for (auto& pixel:wrongPrevious) pixel[3]=adjacent;
        const auto wrongHistory=execute(gpu,shader,fixture,2,&wrongPrevious);
        size_t propagated=0;for (const auto& pixel:wrongHistory) propagated+=validMap(pixel);
        std::printf("ADJACENT_OWNER_IDS first=%.10f adjacent=%.10f wrong_capture_accepted=%zu wrong_previous_accepted=%zu\n",
            first,adjacent,captured,propagated);
        require(captured==0 && propagated==0,"Adjacent representable FP16 owner IDs were treated as one surface");
        const auto warmup=execute(gpu,shader,fixture,0);
        for (const auto& pixel:warmup)
            require(pixel[0]<0 && pixel[1]<0 && pixel[2]>0 && pixel[3]==first,
                "Depth-only warmup lost owner or invented an RGB address");
    }
}

class GpuTiming
{
    Gpu& gpu;
    ComPtr<ID3D11Query> disjoint,begin,end;
    template<class T> T read(ID3D11Query* query)
    {
        T data{};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        HRESULT result;
        while ((result=gpu.context->GetData(query,&data,sizeof(data),0))==S_FALSE) {
            require(std::chrono::steady_clock::now()<deadline,"GPU timing query timed out");
            Sleep(0);
        }
        check(result,"GPU timing GetData");
        return data;
    }
public:
    explicit GpuTiming(Gpu& context):gpu(context)
    {
        D3D11_QUERY_DESC desc={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        check(gpu.device->CreateQuery(&desc,&disjoint),"Create disjoint query");
        desc.Query=D3D11_QUERY_TIMESTAMP;
        check(gpu.device->CreateQuery(&desc,&begin),"Create begin query");
        check(gpu.device->CreateQuery(&desc,&end),"Create end query");
    }
    double sample(const std::function<void()>& work)
    {
        gpu.context->Begin(disjoint.Get());gpu.context->End(begin.Get());
        work();
        gpu.context->End(end.Get());gpu.context->End(disjoint.Get());
        gpu.context->Flush();
        const auto interval=read<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(disjoint.Get());
        const auto first=read<UINT64>(begin.Get()),last=read<UINT64>(end.Get());
        require(!interval.Disjoint && interval.Frequency>0,"Disjoint GPU timing sample");
        return double(last-first)*1000.0/double(interval.Frequency);
    }
};

static void benchmark(Gpu& gpu,Shader& shader)
{
    if (gpu.software) {
        std::printf("GPU_COST skipped=WARP_is_not_hardware_performance\n");
        return;
    }
    GpuTiming timer(gpu);
    auto depth=gpu.texture(1920,1080,std::vector<Pixel>(size_t(1920)*1080,Pixel{24,0,0,0}),DXGI_FORMAT_R32_FLOAT);
    auto owner=gpu.texture(1920,1080,std::vector<Pixel>(size_t(1920)*1080,Pixel{1,0,0,0}),DXGI_FORMAT_R16_FLOAT);
    for (UINT width:{512u,1024u,1920u}) {
        Fixture fixture=scenario(.5,false,false);
        fixture.mainWidth=1920;fixture.mainHeight=1080;
        fixture.mapWidth=width;fixture.mapHeight=width*9/16;
        fixture.generate();
        const auto seeded=execute(gpu,shader,fixture,1);
        auto position=gpu.texture(fixture.mainWidth,fixture.mainHeight,fixture.position,DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto motion=gpu.texture(fixture.mainWidth,fixture.mainHeight,fixture.motion,DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto previous=gpu.texture(fixture.mapWidth,fixture.mapHeight,seeded);
        auto output=gpu.texture(fixture.mapWidth,fixture.mapHeight);
        for (float mode:{0.f,1.f,2.f}) {
            constants(shader,fixture,mode);
            const auto draw=[&] {
                shader.texture(gpu.context.Get(),"s_pip_main_position",position);
                shader.texture(gpu.context.Get(),"s_pip_main_motion",motion);
                shader.texture(gpu.context.Get(),"s_pip_previous_map",previous);
                shader.texture(gpu.context.Get(),"s_pip_capture_depth",depth);
                shader.texture(gpu.context.Get(),"s_pip_capture_owner",owner);
                gpu.draw(shader,output);
            };
            for (unsigned i=0;i<3;++i) timer.sample(draw);
            std::vector<double> samples;
            for (unsigned i=0;i<15;++i) samples.push_back(timer.sample(draw));
            std::sort(samples.begin(),samples.end());
            std::printf("GPU_COST production_map width=%u height=%u main_and_capture=1920x1080 mode=%.0f samples=15 median_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",
                width,fixture.mapHeight,mode,samples[7],samples[14],samples.back());
        }
    }
}

static void expectInvalid(const char* label,const std::vector<Pixel>& pixels,bool expectDepth=true)
{
    size_t valid=0,depth=0;
    for (const Pixel& pixel:pixels) {
        valid+=validMap(pixel);
        depth+=pixel[2]>0;
    }
    std::printf("REJECTION %s total=%zu valid=%zu positive_depth=%zu\n",label,pixels.size(),valid,depth);
    require(valid==0,"Unavailable correspondence incorrectly retained a captured RGB address");
    if (expectDepth) require(depth==pixels.size(),"Invalid RGB-address provenance destroyed useful depth history");
}

static void lensIntegration(const fs::path& root,Gpu& gpu)
{
    const fs::path directory=root/"_build/v144-validation";
    fs::create_directories(directory);
    const fs::path wrapper=directory/"production_lens_wrapper.ps";
    {
        std::ofstream source(wrapper);
        source<<"Texture2D s_second_vp,s_prev_frame; SamplerState smp_base;\n"
            "float4 screen_res; float4x4 scope_lense_reproject;\n"
            "#include \"anthology_pip_temporal.h\"\n"
            "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target { return sample_second_vp_detail(uv); }\n";
    }
    const fs::path oracleFile=directory/"lens_direct_sample_oracle.ps";
    {
        std::ofstream source(oracleFile);
        source<<"Texture2D s_reference; SamplerState smp_base; float4 test_crop;\n"
            "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target {\n"
            "return float4(s_reference.SampleLevel(smp_base,(uv-.5)*test_crop.xy+test_crop.zw,0).rgb,1); }\n";
    }
    const fs::path baselineRoot="D:/ANTHOLOGY_DEV/releases/Anthology v143 - Stable PiP Reuse/gamedata/shaders/r3";
    require(fs::exists(baselineRoot/"anthology_pip_temporal.h"),"Preserved v143 lens helper is missing");
    Includes currentIncludes({root/"gamedata/shaders/r3"}),oldIncludes({baselineRoot});
    Shader candidate(gpu.device.Get(),wrapper,currentIncludes);
    Shader baseline(gpu.device.Get(),wrapper,oldIncludes);
    Shader oracle(gpu.device.Get(),oracleFile,currentIncludes);
    require(candidate.hasTexture("s_pip_motion_map") && candidate.hasTexture("s_prev_frame"),
        "Production lens helper did not compile the active motion-map/fallback path");
    require(!baseline.hasTexture("s_pip_motion_map"),"Preserved v143 helper unexpectedly uses the new map");
    constexpr UINT sourceWidth=1920,sourceHeight=1080,lensWidth=512,lensHeight=512;
    std::vector<Pixel> captured(size_t(sourceWidth)*sourceHeight),main(captured.size()),map(captured.size());
    for (UINT y=0;y<sourceHeight;++y) for (UINT x=0;x<sourceWidth;++x) {
        const size_t index=size_t(y)*sourceWidth+x;
        captured[index]={float(.45+.22*std::sin(x*.19)*std::cos(y*.13)),
            float(.3+.2*std::sin(x*.017+y*.023)),float(.25+.15*std::cos(x*.053-y*.077)),1};
        main[index]={float(.3+.15*std::cos(x*.041+y*.017)),float(.55+.1*std::sin(x*.029-y*.033)),.8f,1};
        const float u=float((x+.5)/sourceWidth),v=float((y+.5)/sourceHeight);
        map[index]={.1f+.7f*u,.15f+.6f*v,10,x<sourceWidth/3?1.f:-.25f};
        if (x>=sourceWidth*2/3) map[index]={-1,-1,10,1};
    }
    auto capture=gpu.texture(sourceWidth,sourceHeight,captured,DXGI_FORMAT_R8G8B8A8_UNORM);
    auto currentMain=gpu.texture(sourceWidth,sourceHeight,main,DXGI_FORMAT_R8G8B8A8_UNORM);
    auto address=gpu.texture(sourceWidth,sourceHeight,map);
    Texture* activeAddress=&address;
    auto destination=gpu.texture(lensWidth,lensHeight);
    Texture* activeDestination=&destination;
    const Mat4 affine={.7f,0,0,0, 0,.6f,0,0, 0,0,1,0, -.1f,.1f,0,1};
    const Mat4 wrongCamera={.91f,0,0,0, 0,.87f,0,0, 0,0,1,0, .08f,-.11f,0,1};
    const Pixel mainView={.31f,.27f,.51f+.37f/sourceWidth,.48f-.29f/sourceHeight};
    const auto common=[&](Shader& shader,float sharpness,float quality) {
        shader.constant("screen_res",Pixel{float(sourceWidth),float(sourceHeight),1.f/sourceWidth,1.f/sourceHeight});
        shader.constant("scope_lense_detail",Pixel{sharpness,0,0,0});
        shader.constant("scope_lense_quality",Pixel{quality,sourceWidth*quality,sourceHeight*quality,0});
        shader.constant("scope_lense_reproject",affine);
    };
    const auto draw=[&](Shader& shader) {
        shader.texture(gpu.context.Get(),"s_second_vp",capture);
        if (shader.hasTexture("s_prev_frame")) shader.texture(gpu.context.Get(),"s_prev_frame",currentMain);
        if (shader.hasTexture("s_pip_motion_map")) shader.texture(gpu.context.Get(),"s_pip_motion_map",*activeAddress);
        gpu.draw(shader,*activeDestination);
    };
    const auto reference=[&](Texture& texture,Pixel crop) {
        oracle.constant("test_crop",crop);
        oracle.texture(gpu.context.Get(),"s_reference",texture);
        gpu.draw(oracle,destination);
        return gpu.read(destination);
    };
    const auto neutralCapture=reference(capture,Pixel{.7f,.6f,.45f,.45f});
    const auto fallback=reference(currentMain,mainView);
    candidate.constant("scope_lense_main_view",mainView);
    std::printf("LENS_INTEGRATION actualhelper=anthology_pip_temporal.h baseline=preserved_v143 source_and_map=1920x1080 RGB=RGBA8 map=RGBA32F draw=512x512\n");
    for (float quality:{1.f,.5f,.25f}) {
        std::array<double,3> energy{};
        for (unsigned sharpness=0;sharpness<3;++sharpness) {
            common(candidate,float(sharpness),quality);common(baseline,float(sharpness),quality);
            draw(baseline);const auto expected=gpu.read(destination);
            candidate.constant("scope_lense_motion",Pixel{0,.02f,.005f,4});
            draw(candidate);const auto disabled=gpu.read(destination);
            double disabledError=0;
            for (size_t i=0;i<disabled.size();++i) for (size_t channel=0;channel<4;++channel)
                disabledError=std::max(disabledError,std::abs(double(disabled[i][channel])-expected[i][channel]));
            require(disabledError<.00001,"Disabled motion mode changed the preserved v143 lens behavior");
            candidate.constant("scope_lense_motion",Pixel{1,.02f,.005f,4});
            candidate.constant("scope_lense_reproject",wrongCamera); // Must be ignored after actual engine motion.
            draw(candidate);const auto active=gpu.read(destination);
            double mapError=0,fallbackError=0,neutralError=0;size_t world=0,dynamic=0,rejected=0;
            for (UINT y=8;y<lensHeight-8;++y) for (UINT x=8;x<lensWidth-8;++x) {
                const size_t index=size_t(y)*lensWidth+x;
                const bool validRegion=x<lensWidth*2/3-8 &&
                    std::abs(int(x)-int(lensWidth/3))>8;
                if (validRegion) {
                    if (x<lensWidth/3) ++world;else ++dynamic;
                    for (size_t channel=0;channel<4;++channel) {
                        mapError=std::max(mapError,std::abs(double(active[index][channel])-expected[index][channel]));
                        if (sharpness==1)
                            neutralError=std::max(neutralError,std::abs(double(active[index][channel])-neutralCapture[index][channel]));
                    }
                    energy[sharpness]+=std::pow(double(active[index][0])-active[index-1][0],2);
                } else if (x>lensWidth*2/3+8) {
                    ++rejected;
                    for (size_t channel=0;channel<4;++channel)
                        fallbackError=std::max(fallbackError,std::abs(double(active[index][channel])-fallback[index][channel]));
                }
            }
            std::printf("LENS_SEMANTICS quality=%.2f sharpness=%u world=%zu dynamic=%zu fallback=%zu disabled_max=%.7f map_max=%.7f crop_jitter_max=%.7f neutral_max=%.7f gradient_energy=%.7f\n",
                quality,sharpness,world,dynamic,rejected,disabledError,mapError,fallbackError,neutralError,energy[sharpness]);
            require(world>10000 && dynamic>10000 && rejected>10000,"Lens fixture missed a required branch");
            require(mapError<.002,"Production lens RGB/map differs from v143 at the same immutable capture address");
            require(fallbackError<.00001,"Rejected map pixels do not use the expected main crop plus jitter");
            require(neutralError<.002,"Sharpness 1 changed the neutral immutable capture sample");
        }
        require(energy[0]<energy[1] && energy[1]<energy[2],"Sharpness slider no longer softens and sharpens around 1");
    }
    if (!gpu.software) {
        GpuTiming timer(gpu);
        auto benchmarkDestination=gpu.texture(lensWidth,lensHeight,{},DXGI_FORMAT_R8G8B8A8_UNORM);
        activeDestination=&benchmarkDestination;
        for (UINT y=0;y<sourceHeight;++y) for (UINT x=sourceWidth*2/3;x<sourceWidth;++x) {
            const float u=float((x+.5)/sourceWidth),v=float((y+.5)/sourceHeight);
            map[size_t(y)*sourceWidth+x]={.1f+.7f*u,.15f+.6f*v,10,-.25f};
        }
        auto allValidAddress=gpu.texture(sourceWidth,sourceHeight,map);
        for (float sharpness:{0.f,1.f,2.f}) {
            common(candidate,sharpness,1);common(baseline,sharpness,1);
            candidate.constant("scope_lense_motion",Pixel{1,.02f,.005f,4});
            for (const auto& item:std::array<std::pair<const char*,Shader*>,3>{{{"v143",&baseline},
                {"v144_map_all_valid",&candidate},{"v144_map_one_third_fallback",&candidate}}}) {
                activeAddress=std::string(item.first)=="v144_map_all_valid"?&allValidAddress:&address;
                for (unsigned i=0;i<4;++i) timer.sample([&]{draw(*item.second);});
                std::vector<double> values;
                for (unsigned i=0;i<21;++i) values.push_back(timer.sample([&]{draw(*item.second);}));
                std::sort(values.begin(),values.end());
                std::printf("GPU_COST_LENS %s draw=512x512 source_and_map=1920x1080 sharpness=%.0f median_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",
                    item.first,sharpness,values[10],values[19],values.back());
            }
        }
    }
    std::printf("PASS: actual production lens integration; maincrop+jitter, immutable world/owner samples, no extra camera warp, v143 disabled parity, quality/sharpness\n");
}

int main(int argc,char** argv)
{
    try {
        std::setvbuf(stdout,nullptr,_IONBF,0);
        require(argc>=2,"Usage: pip_motion_map_gpu_test.exe <engine-root> [--warp]");
        const fs::path root=fs::absolute(argv[1]);
        bool forceWarp=false,lensOnly=false,producerOnly=false;
        for (int i=2;i<argc;++i) {
            if (std::string(argv[i])=="--warp") forceWarp=true;
            else if (std::string(argv[i])=="--lens-only") lensOnly=true;
            else if (std::string(argv[i])=="--producer-only") producerOnly=true;
            else throw std::runtime_error("Unknown GPU test option");
        }
        Gpu gpu(forceWarp);
        if (producerOnly) {motionProducer(root,gpu);return 0;}
        if (lensOnly) {lensIntegration(root,gpu);return 0;}
        motionProducer(root,gpu);
        Includes includes({root/"gamedata/shaders/r3"});
        Shader map(gpu.device.Get(),root/"gamedata/shaders/r3/svp_motion_map.ps",includes);
        Shader depth(gpu.device.Get(),root/"gamedata/shaders/r3/svp_motion_depth.ps",includes);
        const auto consumerFile=writeConsumer(root);
        Shader address(gpu.device.Get(),consumerFile,includes,"address");
        Shader color(gpu.device.Get(),consumerFile,includes,"color");
        require(!map.hasTexture("s_second_vp") && !map.hasTexture("s_pip_capture_rgb"),
            "Motion map pass must not resample captured RGB");
        std::printf("WORKLOAD analytical world raycast; main=768x432 RGBA16F depth/motion; capture/map=256x144 RGBA32F addresses, R32F capture depth fromFP16gbuffer; no game-FPS claim\n");
        captureDepthPass(gpu,depth);
        mapSamplingSeams(gpu,address);

        for (double fraction:{0.0,.25,.5,.75,1.0}) {
            Fixture fixture=scenario(fraction);
            const auto output=execute(gpu,map,fixture,1);
            char name[128];
            std::snprintf(name,sizeof(name),"seed_camera_rotation_translation_npc_depth_fraction_%.2f",fraction);
            coordinates(name,fixture,output);
        }
        {
            Fixture fixture=scenario(.5,false,false);
            coordinates("static_plane",fixture,execute(gpu,map,fixture,1),.01);
            expectInvalid("depth_only",execute(gpu,map,fixture,0));
            expectInvalid("missing_previous_metadata",execute(gpu,map,fixture,1,nullptr,false));
            coordinates("rigid_world_recovers_detail_without_old_address",fixture,execute(gpu,map,fixture,2),.01);
            for(auto& pixel:fixture.motion) pixel[3]=-1.f;
            coordinates("foliage_depth_checked_detail_recovery",fixture,execute(gpu,map,fixture,2),.01);
            for(auto& pixel:fixture.captureOwner) pixel[0]=-.25f;
            expectInvalid("world_recovery_cannot_borrow_actor_detail",execute(gpu,map,fixture,2));
            fixture.generate();
            expectInvalid("unknown_mode",execute(gpu,map,fixture,3));
            expectInvalid("nonfinite_mode",execute(gpu,map,fixture,std::numeric_limits<float>::quiet_NaN()));
            fixture.tc=.15; // Missed epoch; accepting a clamped fraction would hide the error.
            expectInvalid("capture_outside_main_pose_bracket",execute(gpu,map,fixture,1));
        }
        {
            Fixture fixture=scenario(.5);
            for (auto& pixel:fixture.motion) pixel[2]=1;
            expectInvalid("HUD_motion_mask",execute(gpu,map,fixture,1),false);
            for (auto& pixel:fixture.motion) {pixel[0]=std::numeric_limits<float>::quiet_NaN();pixel[1]=0;pixel[2]=0;}
            expectInvalid("nonfinite_motion",execute(gpu,map,fixture,1));
            expectInvalid("depth_prime_without_motion",execute(gpu,map,fixture,0));
            fixture.generate();
            for (auto& pixel:fixture.motion) pixel[3]=2;
            expectInvalid("unknown_or_reactive_motion_history",execute(gpu,map,fixture,1));
            for (auto& pixel:fixture.motion) pixel[3]=std::numeric_limits<float>::quiet_NaN();
            expectInvalid("nonfinite_motion_history_tag",execute(gpu,map,fixture,1));
            fixture.generate();
            for (auto& pixel:fixture.motion) if (pixel[3]==0) pixel[3]=1;
            coordinates("grass_history_tag_allowed",fixture,execute(gpu,map,fixture,1));
        }
        {
            Fixture fixture=scenario(.5);
            for (auto& pixel:fixture.captureDepth) pixel[0]=1;
            expectInvalid("incompatible_capture_depth",execute(gpu,map,fixture,1));
        }
        {
            Fixture fixture=scenario(.5);
            fixture.previous.jitter={-.25/fixture.mainWidth,.35/fixture.mainHeight};
            fixture.current.jitter={.4/fixture.mainWidth,-.2/fixture.mainHeight};
            fixture.capture.jitter={.15/fixture.mapWidth,-.3/fixture.mapHeight};
            fixture.previous.center={.513,.492};
            fixture.current.center={.487,.507};
            fixture.previous.tangent=.71;
            fixture.current.tangent=.62;
            fixture.capture.center={.503,.496};
            fixture.generate();
            coordinates("jitter_projection_center_and_main_FOV",fixture,execute(gpu,map,fixture,1));
        }
        {
            Fixture fixture=scenario(.75);
            for (auto& pixel:fixture.previousMap) {pixel[0]=.92f;pixel[1]=.03f;}
            coordinates("new_epoch_ignores_old_RGB_addresses",fixture,execute(gpu,map,fixture,1));
        }
        missingCoverage(gpu,map);
        adjacentOwnerIds(gpu,map);
        {
            Fixture fixture=scenario(0);
            fixture.t0=-.0035;fixture.t1=0;fixture.tc=0;
            fixture.previous=mainCamera(fixture.t0);
            fixture.current=mainCamera(0);
            fixture.previousLens=lensCamera(fixture.previous);
            fixture.currentLens=lensCamera(fixture.current);
            fixture.capture=lensCamera(mainCamera(0));
            fixture.generate();
            auto previous=execute(gpu,map,fixture,1);
            for (unsigned step=1;step<=32;++step) {
                fixture.t0=(step-1)*.0035;fixture.t1=step*.0035;
                fixture.previous=mainCamera(fixture.t0);fixture.current=mainCamera(fixture.t1);
                fixture.previousLens=lensCamera(fixture.previous);fixture.currentLens=lensCamera(fixture.current);
                fixture.generate();
                auto next=execute(gpu,map,fixture,2,&previous);
                char name[64];std::snprintf(name,sizeof(name),"per_main_advection_step_%u",step);
                coordinates(name,fixture,next,.5,step<=8);
                previous=std::move(next);
            }
        }
        disocclusion(gpu,map);
        immutableRgb(gpu,map,color);
        benchmark(gpu,map);
        const bool gap0=coplanarDisocclusion(gpu,map,0);
        const bool gap1=coplanarDisocclusion(gpu,map,.01);
        const bool gap2=coplanarDisocclusion(gpu,map,.02);
        require(gap0 && gap1 && gap2,"Coplanar disocclusion accepted captured NPC addresses for newly exposed wall");
        std::printf("PASS: production motion-map analytical GPU fixtures\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}

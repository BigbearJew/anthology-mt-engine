// Timestamp the active PiP sampling branch and isolated flow pipeline on hardware.
// Synthetic offscreen workloads measure shader cost, not game FPS or scheduling.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
static bool costUsesWarp = false;
static HRESULT createCostDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE, HMODULE software,
    UINT flags, const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** device,
    D3D_FEATURE_LEVEL* feature, ID3D11DeviceContext** context)
{
    HRESULT result = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_HARDWARE, software, flags,
        levels, count, sdk, device, feature, context);
    if (FAILED(result)) {
        costUsesWarp = true;
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, count, sdk, device, feature, context);
    }
    return result;
}
#define D3D11CreateDevice createCostDevice
#define main original_pip_flow_gpu_test_main
#include "pip_flow_gpu_test.cpp"
#undef main
#undef D3D11CreateDevice
#include <chrono>
#include <functional>

class GpuTimer
{
    Gpu& gpu;
    ComPtr<ID3D11Query> disjoint, begin, end;
    template<class T> T wait(ID3D11Query* query)
    {
        T result{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        HRESULT status;
        while ((status = gpu.context->GetData(query, &result, sizeof(result), 0)) == S_FALSE) {
            require(std::chrono::steady_clock::now() < deadline, "GPU query timeout");
            Sleep(0);
        }
        check(status, "GPU timestamp GetData");
        return result;
    }
public:
    explicit GpuTimer(Gpu& target) : gpu(target)
    {
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        check(gpu.device->CreateQuery(&desc, &disjoint), "Create disjoint query");
        desc.Query = D3D11_QUERY_TIMESTAMP;
        check(gpu.device->CreateQuery(&desc, &begin), "Create begin timestamp");
        check(gpu.device->CreateQuery(&desc, &end), "Create end timestamp");
    }
    double sample(const std::function<void()>& draw)
    {
        gpu.context->Begin(disjoint.Get());
        gpu.context->End(begin.Get());
        draw();
        gpu.context->End(end.Get());
        gpu.context->End(disjoint.Get());
        gpu.context->Flush();
        const auto interval = wait<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(disjoint.Get());
        const auto first = wait<UINT64>(begin.Get()), last = wait<UINT64>(end.Get());
        require(!interval.Disjoint && interval.Frequency > 0, "Invalid/disjoint GPU timestamp sample");
        return double(last - first) * 1000.0 / double(interval.Frequency);
    }
    void report(const char* label, const std::function<void()>& draw)
    {
        for (UINT i = 0; i < 8; ++i) sample(draw);
        std::vector<double> values;
        for (UINT i = 0; i < 25; ++i) values.push_back(sample(draw));
        std::sort(values.begin(), values.end());
        std::printf("GPU_COST %s warmup=8 samples=25 min_ms=%.6f median_ms=%.6f p95_ms=%.6f max_ms=%.6f\n",
            label, values.front(), values[12], values[23], values.back());
    }
};

static void writeWrapper(const fs::path& path, bool oracle)
{
    std::ofstream source(path);
    source << "Texture2D<float4> s_second_vp, s_prev_frame, s_position;\n"
        "SamplerState smp_base;\n"
        "float4 screen_res, scope_lense_imaging, s3ds_param_3, markswitch_current;\n"
        "float4x4 scope_lense_reproject;\n";
    if (!oracle) source << "#include \"anthology_pip_temporal.h\"\n";
    else source <<
        "float4 scope_lense_detail;\n"
        "float4 sample_second_vp(float2 uv) {\n"
        "float4 clip=mul(scope_lense_reproject,float4(uv*float2(2,-2)+float2(-1,1),1,1));\n"
        "float2 coord=clip.w>0.0001?clip.xy/clip.w*float2(.5,-.5)+.5:uv;\n"
        "float edge=min(min(coord.x,coord.y),min(1-coord.x,1-coord.y));\n"
        "float valid=clip.w>0.0001?smoothstep(0,2*max(screen_res.z,screen_res.w),edge):0;\n"
        "return float4(s_second_vp.SampleLevel(smp_base,clamp(coord,screen_res.zw*.5,1-screen_res.zw*.5),0).rgb*valid,valid); }\n"
        "float4 sample_second_vp_detail(float2 uv) { float4 c=sample_second_vp(uv);\n"
        "float sharp=clamp(scope_lense_detail.x,0,2); if(abs(sharp-1)>.0001) {\n"
        "uint w,h;s_second_vp.GetDimensions(w,h);float2 t=1.0/float2(w,h);\n"
        "float3 a=sample_second_vp(uv-float2(t.x,0)).rgb,b=sample_second_vp(uv+float2(t.x,0)).rgb;\n"
        "float3 d=sample_second_vp(uv-float2(0,t.y)).rgb,e=sample_second_vp(uv+float2(0,t.y)).rgb;\n"
        "float3 low=(c.rgb*4+a+b+d+e)*.125; c.rgb=clamp(c.rgb+(sharp-1)*(c.rgb-low),min(c.rgb,min(min(a,b),min(d,e))),max(c.rgb,max(max(a,b),max(d,e))));} return c;}\n";
    source << "float4 main(float4 pos:SV_Position,float2 uv:TEXCOORD0):SV_Target { return sample_second_vp_detail(uv); }\n";
}

static void bindInputs(Gpu& gpu, Shader& shader, Texture& capture, Texture& mainImage,
    Texture& depth, Texture& mainPosition, Texture& flow)
{
    const std::pair<const char*, Texture*> inputs[] = {{"s_second_vp", &capture}, {"s_prev_frame", &mainImage},
        {"s_second_vp_depth", &depth}, {"s_position", &mainPosition}, {"s_second_vp_flow", &flow}};
    for (const auto& input : inputs) if (shader.hasTexture(input.first))
        shader.texture(gpu.context.Get(), input.first, *input.second);
}

int main(int argc, char** argv)
{
    try {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        require(argc == 3 || argc == 4, "Usage: pip_temporal_cost_gpu_test.exe <engine-root> <baseline-shader-root> [candidate-shader-root]");
        const fs::path root = fs::absolute(argv[1]), baselineRoot = fs::absolute(argv[2]);
        const fs::path outputRoot = root / "_build/v143-validation";
        fs::create_directories(outputRoot);
        const auto wrapper = outputRoot / "cost_lens_wrapper.ps", oracleWrapper = outputRoot / "cost_camera_oracle.ps";
        writeWrapper(wrapper, false);
        writeWrapper(oracleWrapper, true);
        Includes baselineIncludes({baselineRoot, root / "gamedata/shaders/r3"});
        Gpu gpu;
        ComPtr<IDXGIDevice> dxgi;
        check(gpu.device.As(&dxgi), "Query IDXGIDevice");
        ComPtr<IDXGIAdapter> adapter;
        check(dxgi->GetAdapter(&adapter), "Get GPU adapter");
        DXGI_ADAPTER_DESC desc{};
        check(adapter->GetDesc(&desc), "Get GPU description");
        char gpuName[512]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpuName, sizeof(gpuName), nullptr, nullptr);
        std::printf("ADAPTER %s mode=%s feature_level=0x%x dedicated_vram_mib=%zu\n", gpuName,
            costUsesWarp ? "WARP SOFTWARE FALLBACK" : "HARDWARE", gpu.device->GetFeatureLevel(), desc.DedicatedVideoMemory >> 20);
        std::printf("WORKLOAD full_source=1920x1080 lens_draw=512x512 R32G32B32A32_FLOAT; active PiP sampling branch only, no scene rendering, no circle mask; no game-FPS claim\n");
        Shader baseline(gpu.device.Get(), wrapper, baselineIncludes);
        Shader oracle(gpu.device.Get(), oracleWrapper, baselineIncludes);
        std::unique_ptr<Shader> candidate;
        if (argc == 4) {
            Includes candidateIncludes({fs::absolute(argv[3]), root / "gamedata/shaders/r3"});
            candidate = std::make_unique<Shader>(gpu.device.Get(), wrapper, candidateIncludes);
            candidate->constant("scope_lense_quality",std::array<float,4>{1,1920,1080,0});
            std::printf("CANDIDATE_DEPENDENCIES flow=%d capture_depth=%d main_depth=%d main_color=%d\n",
                candidate->hasTexture("s_second_vp_flow"),candidate->hasTexture("s_second_vp_depth"),
                candidate->hasTexture("s_position"),candidate->hasTexture("s_prev_frame"));
            require(!candidate->hasTexture("s_second_vp_flow") && !candidate->hasTexture("s_second_vp_depth") &&
                !candidate->hasTexture("s_position") && !candidate->hasTexture("s_prev_frame"),
                "Candidate still binds per-pixel temporal reconstruction dependencies");
        }
        constexpr UINT width = 1920, height = 1080, lensWidth = 512, lensHeight = 512;
        const std::array<float, 16> identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        baseline.constant("scope_lense_reproject", identity);
        baseline.constant("screen_res", std::array<float,4>{float(width),float(height),1.f/width,1.f/height});
        baseline.constant("scope_lense_camera", std::array<float,4>{0,0,0,0});
        baseline.constant("scope_lense_main_view", std::array<float,4>{1,1,.5f,.5f});
        baseline.constant("s3ds_param_3", std::array<float,4>{0,0,0,0});
        baseline.constant("markswitch_current", std::array<float,4>{1,0,0,0});
        baseline.constant("scope_lense_imaging", std::array<float,4>{0,0,0,0});
        baseline.constant("scope_lense_motion", std::array<float,4>{.5f,1,.0165f,0});
        auto capture = gpu.texture(width,height,scene(width,height,0,0,false));
        auto mainImage = gpu.texture(width,height,scene(width,height,0,0,false));
        auto depth = gpu.texture(width,height,std::vector<Pixel>(size_t(width)*height,{20,0,0,0}));
        auto mainPosition = gpu.texture(width,height,std::vector<Pixel>(size_t(width)*height,{0,0,20,0}));
        auto flow = gpu.texture(width/4,height/4,std::vector<Pixel>(size_t(width/4)*(height/4),{2.f/width,0,20,1}));
        auto output = gpu.texture(lensWidth,lensHeight);
        GpuTimer timer(gpu);
        for (const float sharpness : {1.f,0.f,2.f}) {
            baseline.constant("scope_lense_detail",std::array<float,4>{sharpness,0,0,0});
            oracle.copyConstants(baseline);
            if (candidate) candidate->copyConstants(baseline);
            const std::pair<const char*,Shader*> variants[] = {{"v142",&baseline},{"camera_oracle",&oracle},{"candidate",candidate.get()}};
            for (const auto& variant : variants) if (variant.second) {
                char label[128];
                std::snprintf(label,sizeof(label),"%s active_detail sharpness=%.0f",variant.first,sharpness);
                timer.report(label,[&] { bindInputs(gpu,*variant.second,capture,mainImage,depth,mainPosition,flow); gpu.draw(*variant.second,output); });
            }
        }
        if (candidate) for (const float quality : {1.f,.5f,.25f}) {
            candidate->constant("scope_lense_quality",std::array<float,4>{quality,width*quality,height*quality,0});
            std::array<double,3> gradientEnergy{};
            std::array<std::vector<Pixel>,3> samples;
            for (UINT sharpness=0;sharpness<3;++sharpness) {
                candidate->constant("scope_lense_detail",std::array<float,4>{float(sharpness),0,0,0});
                bindInputs(gpu,*candidate,capture,mainImage,depth,mainPosition,flow);
                gpu.draw(*candidate,output);
                samples[sharpness]=gpu.read(output);
                size_t count=0;
                for (UINT y=8;y<lensHeight-8;++y) for (UINT x=8;x<lensWidth-8;++x) {
                    const size_t index=size_t(y)*lensWidth+x;
                    const double horizontal=double(samples[sharpness][index][0])-samples[sharpness][index-1][0];
                    const double vertical=double(samples[sharpness][index][0])-samples[sharpness][index-lensWidth][0];
                    require(std::isfinite(horizontal) && std::isfinite(vertical),"Nonfinite candidate sharpness output");
                    gradientEnergy[sharpness]+=horizontal*horizontal+vertical*vertical;
                    ++count;
                }
                gradientEnergy[sharpness]/=double(count);
            }
            double neutralError=0;
            baseline.constant("scope_lense_detail",std::array<float,4>{1,0,0,0});
            oracle.copyConstants(baseline);
            bindInputs(gpu,oracle,capture,mainImage,depth,mainPosition,flow);
            gpu.draw(oracle,output);
            const auto neutral=gpu.read(output);
            for (size_t index=0;index<neutral.size();++index) for (size_t c=0;c<3;++c)
                neutralError=std::max(neutralError,std::abs(double(samples[1][index][c])-neutral[index][c]));
            std::printf("CANDIDATE_SHARPNESS quality=%.2f gradient_energy_soft=%.8f neutral=%.8f sharp=%.8f neutral_max_error=%.8f\n",
                quality,gradientEnergy[0],gradientEnergy[1],gradientEnergy[2],neutralError);
            require(gradientEnergy[0]<gradientEnergy[1] && gradientEnergy[1]<gradientEnergy[2],
                "Sharpness does not soften below1 and sharpen above1");
            require(neutralError<.0002,"Sharpness1 is not an unfiltered camera projection");
        }
        if (candidate) candidate->constant("scope_lense_quality",std::array<float,4>{1,1920,1080,0});
        // A stationary textured plane must not move because an estimated flow
        // field changes. This deliberately injects plausible but wrong vectors;
        // it tests the sampling response, not the estimator's error frequency.
        baseline.constant("scope_lense_detail",std::array<float,4>{1,0,0,0});
        oracle.copyConstants(baseline);
        if (candidate) candidate->copyConstants(baseline);
        const std::pair<const char*,Shader*> variants[] = {{"v142",&baseline},{"camera_oracle",&oracle},{"candidate",candidate.get()}};
        for (const auto& variant : variants) if (variant.second) {
            std::vector<Pixel> previous;
            double totalAbsolute = 0, maximum = 0;
            size_t changed = 0, channels = 0;
            for (UINT frame = 0; frame < 8; ++frame) {
                std::vector<Pixel> vectors(size_t(width/4)*(height/4));
                for (UINT y=0;y<height/4;++y) for (UINT x=0;x<width/4;++x) {
                    const float phase=float(frame)*.8f;
                    vectors[size_t(y)*(width/4)+x]={2.f*std::sin(float(x)*.015f+phase)/width,
                        1.5f*std::cos(float(y)*.02f-phase)/height,20,1};
                }
                gpu.context->UpdateSubresource(flow.data.Get(),0,nullptr,vectors.data(),(width/4)*sizeof(Pixel),0);
                bindInputs(gpu,*variant.second,capture,mainImage,depth,mainPosition,flow);
                gpu.draw(*variant.second,output);
                const auto pixels=gpu.read(output);
                if (!previous.empty()) for (UINT y=8;y<lensHeight-8;++y) for (UINT x=8;x<lensWidth-8;++x) {
                    const size_t index=size_t(y)*lensWidth+x;
                    for (size_t c=0;c<3;++c) {
                        const double difference=std::abs(double(pixels[index][c])-previous[index][c]);
                        totalAbsolute+=difference; maximum=std::max(maximum,difference); ++channels;
                        if (difference>1.0/255.0) ++changed;
                    }
                }
                previous=pixels;
            }
            std::printf("STATIC_FLOW_WIGGLE %s frames=8 mean_abs_rgb=%.8f max_abs_rgb=%.8f changed_over_1_255=%.3f%%\n",
                variant.first,totalAbsolute/double(channels),maximum,100.0*double(changed)/double(channels));
            if (std::string(variant.first)!="v142") require(maximum<.0002,"Camera-only sampling still responds to a changing false flow field");
        }
        if (candidate) {
            // Independent analytic ray rotation checks a moving camera against a
            // linear RGB image. Unlike the identity/false-flow test, every frame
            // requires a different non-affine homography and perspective divide.
            std::vector<Pixel> gradient(size_t(width)*height);
            for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
                const float u=(float(x)+.5f)/width,v=(float(y)+.5f)/height;
                gradient[size_t(y)*width+x]={u,v,.2f+.3f*u+.4f*v,1};
            }
            auto gradientTexture=gpu.texture(width,height,gradient);
            constexpr double pi=3.14159265358979323846;
            const double tangentY=std::tan(20.0*pi/360.0),tangentX=tangentY*double(width)/height;
            candidate->constant("scope_lense_detail",std::array<float,4>{1,0,0,0});
            double overallMaximum=0, minimumMotion=1;
            size_t totalChecks=0;
            for (UINT frame=0;frame<8;++frame) {
                const double yaw=(-.9+.26*frame)*pi/180.0,pitch=.65*std::sin(double(frame)*.67)*pi/180.0;
                const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
                const double rotation[3][3]={{cy,sy*sp,sy*cp},{0,cp,-sp},{-sy,cy*sp,cy*cp}};
                const double rows[4][4]={{rotation[0][0],rotation[0][1]*tangentY/tangentX,rotation[0][2]/tangentX,0},
                    {rotation[1][0]*tangentX/tangentY,rotation[1][1],rotation[1][2]/tangentY,0},
                    {0,0,1,0},{rotation[2][0]*tangentX,rotation[2][1]*tangentY,rotation[2][2],0}};
                std::array<float,16> homography{};
                for (size_t row=0;row<4;++row) for (size_t column=0;column<4;++column)
                    homography[column*4+row]=float(rows[row][column]);
                candidate->constant("scope_lense_reproject",homography);
                bindInputs(gpu,*candidate,gradientTexture,mainImage,depth,mainPosition,flow);
                gpu.draw(*candidate,output);
                const auto pixels=gpu.read(output);
                double maximum=0,totalMotion=0;
                size_t checks=0;
                for (UINT y=8;y<lensHeight-8;++y) for (UINT x=8;x<lensWidth-8;++x) {
                    const double u=(double(x)+.5)/lensWidth,v=(double(y)+.5)/lensHeight;
                    const double rayX=(2*u-1)*tangentX,rayY=(1-2*v)*tangentY;
                    // Apply pitch, then yaw directly, without using the uploaded matrix.
                    const double afterPitchY=cp*rayY-sp,afterPitchZ=sp*rayY+cp;
                    const double capturedX=cy*rayX+sy*afterPitchZ,capturedZ=-sy*rayX+cy*afterPitchZ;
                    const double mappedU=.5+.5*capturedX/(capturedZ*tangentX);
                    const double mappedV=.5-.5*afterPitchY/(capturedZ*tangentY);
                    if (mappedU<.02 || mappedU>.98 || mappedV<.02 || mappedV>.98) continue;
                    const double expected[3]={mappedU,mappedV,.2+.3*mappedU+.4*mappedV};
                    for (size_t c=0;c<3;++c) {
                        require(std::isfinite(pixels[size_t(y)*lensWidth+x][c]),"Nonfinite rotated candidate image");
                        maximum=std::max(maximum,std::abs(double(pixels[size_t(y)*lensWidth+x][c])-expected[c]));
                    }
                    totalMotion+=std::hypot(mappedU-u,mappedV-v);
                    ++checks;
                }
                require(checks>200000,"Rotated-camera check has too little valid image coverage");
                require(maximum<.00001,"Production projection differs from analytic yaw/pitch mapping");
                const double meanMotion=totalMotion/double(checks);
                require(meanMotion>.001,"Nonidentity camera case did not actually move the image");
                overallMaximum=std::max(overallMaximum,maximum);minimumMotion=std::min(minimumMotion,meanMotion);
                totalChecks+=checks;
                std::printf("CANDIDATE_CAMERA_HOMOGRAPHY frame=%u yaw_deg=%.4f pitch_deg=%.4f checked=%zu max_rgb_error=%.8f mean_uv_displacement=%.8f\n",
                    frame,yaw*180/pi,pitch*180/pi,checks,maximum,meanMotion);
            }
            std::printf("CANDIDATE_CAMERA_HOMOGRAPHY_TOTAL frames=8 checked=%zu max_rgb_error=%.8f minimum_mean_uv_displacement=%.8f\n",
                totalChecks,overallMaximum,minimumMotion);
            candidate->constant("scope_lense_reproject",identity);
        }
        Shader pyramidShader(gpu.device.Get(),baselineRoot/"svp_temporal_pyramid.ps",baselineIncludes);
        Shader flowShader(gpu.device.Get(),baselineRoot/"svp_temporal_flow.ps",baselineIncludes);
        std::array<std::array<Texture,3>,2> pyramid;
        std::array<Texture,3> levels;
        for (UINT history=0;history<2;++history) for (UINT level=0;level<3;++level)
            pyramid[history][level]=gpu.texture(width/(4u<<level),height/(4u<<level));
        for (UINT level=0;level<3;++level) levels[level]=gpu.texture(width/(16u>>level),height/(16u>>level));
        auto previousColor=gpu.texture(width,height,scene(width,height,-4,0,false));
        auto previousDepth=gpu.texture(width,height,std::vector<Pixel>(size_t(width)*height,{20,0,0,0}));
        auto emptySeed=gpu.texture(1,1,{{0,0,0,0}});
        const std::array<float,16> projection={1,0,0,0,0,1,0,0,0,0,1.01f,1,0,0,-.1f,0};
        flowShader.constant("svp_capture_projection",std::array<float,4>{1,1,0,0});
        flowShader.constant("svp_capture_to_previous",projection);
        auto pipeline=[&] {
            for (UINT history=0;history<2;++history) for (UINT level=0;level<3;++level) {
                Texture& input=level?pyramid[history][level-1]:history?previousColor:capture;
                const float kernel=level?.5f:1.f;
                pyramidShader.constant("svp_pyramid_step",std::array<float,4>{kernel/float(input.width),kernel/float(input.height),0,0});
                pyramidShader.texture(gpu.context.Get(),"s_svp_pyramid_source",input);
                gpu.draw(pyramidShader,pyramid[history][level]);
            }
            for (UINT level=0;level<3;++level) {
                const UINT divisor=16u>>level;
                flowShader.constant("svp_flow_res",std::array<float,4>{float(divisor)/width,float(divisor)/height,float(level),1});
                flowShader.texture(gpu.context.Get(),"s_svp_current",pyramid[0][2-level]);
                flowShader.texture(gpu.context.Get(),"s_svp_previous",pyramid[1][2-level]);
                flowShader.texture(gpu.context.Get(),"s_svp_depth",depth);
                flowShader.texture(gpu.context.Get(),"s_svp_previous_depth",previousDepth);
                flowShader.texture(gpu.context.Get(),"s_svp_flow_seed",level?levels[level-1]:emptySeed);
                gpu.draw(flowShader,levels[level]);
            }
        };
        timer.report("v142 six_pyramid_plus_three_flow_passes fullhd",pipeline);
        auto copyColor=gpu.texture(width,height),copyDepth=gpu.texture(width,height);
        timer.report("two_fullhd_history_copies R32G32B32A32_FLOAT",[&] {
            gpu.context->CopyResource(copyColor.data.Get(),capture.data.Get());
            gpu.context->CopyResource(copyDepth.data.Get(),depth.data.Get());
        });
        std::printf("PASS bounded timestamp benchmark and stationary-sequence invariance; timings exclude engine rendering and are not gameplay FPS.\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}

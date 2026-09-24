#define main legacy_analytical_main
#include "pip_motion_map_gpu_test.cpp"
#undef main

static void captureFilter(Gpu& gpu, const fs::path& root, Includes& includes)
{
    const auto file=root/"v155_capture_filter_probe.ps";
    std::ofstream(file) << "Texture2D s_second_vp,s_prev_frame; SamplerState smp_base;\n"
        "float4 screen_res,probe_uv; float4x4 scope_lense_reproject;\n"
        "#include \"anthology_pip_temporal.h\"\n"
        "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target { return float4(probe_uv.z > 0.5 ? pip_capture_detail(probe_uv.xy,false) : pip_capture_color(probe_uv.xy),1); }\n";
    Shader shader(gpu.device.Get(),file,includes);
    constexpr int size=32;
    std::vector<Pixel> pixels(size*size);
    for(int y=0;y<size;++y) for(int x=0;x<size;++x)
        pixels[y*size+x]=Pixel{.5f+.4f*sinf(.7f*x),x<16?0.f:1.f,.37f,1};
    auto source=gpu.texture(size,size,pixels), output=gpu.texture(1,1);
    shader.constant("screen_res",Pixel{size,size,1.f/size,1.f/size});
    shader.constant("scope_lense_detail",Pixel{1,0,0,0});
    shader.constant("scope_lense_quality",Pixel{1,size,size,0});
    double errors[2]{};
    for(int mode=0;mode<2;++mode) for(int x=4;x<28;++x) for(float offset:{.25f,.5f,.75f}) {
        shader.constant("probe_uv",Pixel{(x+.5f+offset)/size,.5f,float(mode),0});
        shader.texture(gpu.context.Get(),"s_second_vp",source);
        gpu.draw(shader,output);
        const auto p=gpu.read(output)[0];
        errors[mode]+=fabs(double(p[0])-(.5+.4*sin(.7*(x+offset))));
        require(p[1]>=0 && p[1]<=1,"Capture filter introduced a silhouette halo");
        require(fabsf(p[2]-.37f)<1e-5f,"Capture filter changed a flat surface");
    }
    require(errors[1]<errors[0]*.6,"Static-world capture reconstruction still blurs subpixels");
    printf("PASS: capture filter error bilinear=%.6f cubic=%.6f; 144 bounded silhouette/flat samples\n",errors[0]/72,errors[1]/72);
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Usage: test_temporal_inputs SHADERS");
        fs::path root = argv[1];
        Gpu gpu(false);
        Includes includes({root});
        std::vector<Pixel> values(64);
        for (size_t i=0; i<values.size(); ++i)
            values[i] = Pixel{float(i)/63, float(63-i)/63, .25f, 1};
        auto input=gpu.texture(8,8,values), output=gpu.texture(8,8);
        Shader prepare(gpu.device.Get(), root/"anthology_upscale_prepare_depth.ps", includes);
        auto depth=gpu.texture(8,8,std::vector<Pixel>(64,Pixel{.75f,0,0,0}),DXGI_FORMAT_R32_FLOAT);
        auto depthOutput=gpu.texture(8,8);
        for (int linear=0; linear<2; ++linear) {
            prepare.constant("anthology_upscale_linear",Pixel{float(linear),0,0,0});
            prepare.texture(gpu.context.Get(),"s_image",input);
            prepare.texture(gpu.context.Get(),"s_depth",depth);
            gpu.draw(prepare,output,&depthOutput);
            auto result=gpu.read(output);
            for (size_t i=0;i<values.size();++i) for(int c=0;c<3;++c) {
                float expected=linear?powf(values[i][c],2.2f):values[i][c];
                require(fabsf(expected-result[i][c])<1e-5f,"Vendor color contract or pixel alignment changed");
            }
            auto z=gpu.read(depthOutput);
            for(const auto& v:z)require(fabsf(v[0]-.75f)<1e-6f,"Device depth export changed");
        }
        std::ofstream(root/"v155_hud_motion_probe.ps") << "#include \"screenspace_mvectors.h\"\n"
            "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target { return ssfx_mv_calc(float4(.2,.1,0,1),float4(0,0,0,1),1,0); }\n";
        Shader hud(gpu.device.Get(),root/"v155_hud_motion_probe.ps",includes);
        hud.constant("pip_motion_history",Pixel{0,-.125f,0,0});
        gpu.draw(hud,output);
        auto motion=gpu.read(output);
        for(const auto& v:motion) {
            require(fabsf(v[0]-.1f)<1e-6f && fabsf(v[1]+.05f)<1e-6f,"HUD motion erased");
            require(v[2]==1 && v[3]==1,"HUD blur/TAA classification lost");
        }
        puts("PASS: 384 color channels, 128 depth exports, 64 HUD motion/mask samples");
        captureFilter(gpu,root,includes);
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

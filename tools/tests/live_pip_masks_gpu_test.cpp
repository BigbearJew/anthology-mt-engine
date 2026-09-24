#define main legacy_analytical_main
#include "pip_motion_map_gpu_test.cpp"
#undef main

static void reactiveInputs(Gpu& gpu, const fs::path& root, Includes& includes)
{
    const auto file=root/"v156_reactive_probe.ps";
    std::ofstream(file) << "#define main production_prepare\n#include \"anthology_upscale_prepare_depth.ps\"\n"
        "#undef main\nfloat4 main(float4 p:SV_Position):SV_Target { return production_prepare(p).reactive.xxxx; }\n";
    Shader shader(gpu.device.Get(),file,includes);
    std::vector<Pixel> deltas{{0,0,0,0},{.01f,0,0,0},{0,-.2f,0,0},{0,0,.9f,0}};
    auto input=gpu.texture(4,1,deltas),output=gpu.texture(4,1);
    for (int linear=0;linear<2;++linear) for(int enabled=0;enabled<2;++enabled) {
        shader.constant("anthology_upscale_linear",Pixel{float(linear),float(enabled),0,0});
        shader.texture(gpu.context.Get(),"s_forward_delta",input);
        gpu.draw(shader,output);
        const auto actual=gpu.read(output);
        const float fsr[]{0,.04f,.8f,.9f},dlss[]{0,0,1,1};
        for(int i=0;i<4;++i) require(fabsf(actual[i][0]-enabled*(linear?fsr[i]:dlss[i]))<1e-5f,
            "Reactive mask contract/gate changed");
    }
    puts("PASS: production DLSS binary / FSR bounded reactive masks, signed delta, disabled gate");
}

static void lensHistory(Gpu& gpu,const fs::path& root,Includes& includes)
{
    const auto file=root/"v156_live_taa_probe.ps";
    std::ofstream(file)<<"#pragma warning(disable:3206)\n#include \"svp_live_taa.ps\"\n";
    Shader shader(gpu.device.Get(),file,includes);
    constexpr int size=16,center=8*size+8;
    std::vector<Pixel> colors(size*size,Pixel{.2f,.2f,.2f,1});colors[center]=Pixel{.8f,.8f,.8f,1};
    auto current=gpu.texture(size,size,colors),output=gpu.texture(size,size);
    auto position=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{0,0,10,0}));
    shader.constant("ssfx_jitter",Pixel{0,0,0,0});
    shader.constant("svp_taa_previous_view",std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
    if(shader.hasConstant("pos_decompression_params"))shader.constant("pos_decompression_params",Pixel{1,1,2.f/size,2.f/size});
    for(int test=0;test<6;++test) {
        auto history=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{.4f,.4f,.4f,test==2?20.f:10.f}));
        auto motion=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{test==3?2.f:0.f,0,0,test==4?1.f:0.f}));
        auto reactive=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{test==5?1.f:0.f,0,0,0}));
        shader.constant("svp_taa_control",Pixel{test==1?0.f:1.f,1,0,0});
        shader.texture(gpu.context.Get(),"s_svp_scene",current);
        shader.texture(gpu.context.Get(),"s_svp_history",history);
        shader.texture(gpu.context.Get(),"s_svp_motion",motion);
        shader.texture(gpu.context.Get(),"s_svp_reactive",reactive);
        shader.texture(gpu.context.Get(),"s_position",position);
        gpu.draw(shader,output);const auto result=gpu.read(output);
        require(fabsf(result[center][3]-10.f)<1e-5f,"Lens history stores incorrect depth");
        if(test==0)require(result[center][0]>.4f && result[center][0]<.65f,"Valid static lens history was not accumulated");
        else require(fabsf(result[center][0]-.8f)<1e-5f,"Stale/disoccluded/reactive lens history leaked into current frame");
        for(const auto& p:result)require(p[0]>=.1999f && p[0]<=.8001f,"Lens history introduced a silhouette halo");
    }
    puts("PASS: production PiP TAA accumulation, reset, depth rejection, out-of-view, unknown motion, reactive rejection; bounded silhouettes");
}

int main(int argc,char** argv)
{
    try {
        require(argc==2,"Usage: test_v156_gpu SHADER_STAGE");
        Gpu gpu(false);fs::path root=argv[1];Includes includes({root});
        reactiveInputs(gpu,root,includes);lensHistory(gpu,root,includes);return 0;
    } catch(const std::exception& e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

#define main legacy_analytical_main
#include "pip_motion_map_gpu_test.cpp"
#undef main

// Render the production TAA shader over stationary non-jittered UI texels.
// The old resolve moves those texels as the scene jitter changes.
int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Usage: pda_taa_gpu_test SHADERS");
        fs::path root=argv[1];
        Gpu gpu(false);
        Includes includes({root});
        // The shared legacy lighting helpers have known vector truncations.
        std::ofstream(root/"pda_taa_probe.ps") << "#pragma warning(disable:3206)\n#include \"ssfx_taa.ps\"\n";
        Shader shader(gpu.device.Get(),root/"pda_taa_probe.ps",includes);
        constexpr int size=16;
        std::vector<Pixel> pixels(size*size);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x)
            pixels[y*size+x]=Pixel{float(x%2),float(y%2),float(x)/size,1};
        auto current=gpu.texture(size,size,pixels), history=gpu.texture(size,size);
        auto depth=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{.5f,0,0,0}));
        auto motion=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{0,0,2,1}));
        auto prepared=gpu.texture(size,size,std::vector<Pixel>(size*size,Pixel{0,0,1,0}));
        auto output=gpu.texture(size,size);
        shader.constant("screen_res",Pixel{size,size,1.f/size,1.f/size});
        unsigned checked=0;
        for(float jx:{-.875f,-.625f,-.25f,0.f,.25f,.625f,.875f})
            for(float jy:{-.875f,0.f,.875f}) {
                shader.constant("ssfx_jitter",Pixel{2*jx/size,-2*jy/size,1,0});
                shader.texture(gpu.context.Get(),"s_current",current);
                shader.texture(gpu.context.Get(),"s_previous",history);
                shader.texture(gpu.context.Get(),"s_depth",depth);
                shader.texture(gpu.context.Get(),"s_motion_vectors",prepared);
                shader.texture(gpu.context.Get(),"s_mv",motion);
                gpu.draw(shader,output);
                auto result=gpu.read(output);
                for(int y=1;y<size-1;++y) for(int x=1;x<size-1;++x) for(int c=0;c<3;++c) {
                    require(fabsf(result[y*size+x][c]-pixels[y*size+x][c])<1e-5f,
                        "Stationary PDA pixels move with scene TAA jitter");
                    ++checked;
                }
            }
        printf("PASS: %u stationary PDA color samples across 21 scene jitter offsets\n",checked);
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

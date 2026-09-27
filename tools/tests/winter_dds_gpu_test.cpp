#define main legacy_analytical_main
#include "pip_motion_map_gpu_test.cpp"
#undef main
#include <D3DX11tex.h>
int main(int argc,char** argv)
{
    try {
        require(argc==3,"Usage: winter_dds_gpu_test TRUNCATED REPAIRED");
        Gpu gpu(false);
        for(int i=1;i<=2;++i) {
            fs::path p=argv[i];std::ifstream f(p,std::ios::binary);
            std::vector<char> data{std::istreambuf_iterator<char>(f),{}};
            require(!data.empty(),"DDS input missing");
            ComPtr<ID3D11Resource> texture;
            HRESULT hr=D3DX11CreateTextureFromMemory(gpu.device.Get(),data.data(),data.size(),nullptr,nullptr,&texture,nullptr);
            printf("DDS %d: HRESULT=0x%08lx bytes=%zu\n",i,hr,data.size());
            require(i==1?FAILED(hr):SUCCEEDED(hr),"DDS regression result unexpected");
        }
        puts("PASS: original DDS rejected; complete-mip DDS created by D3D11");return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

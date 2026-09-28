#include "../../src/xrCore/SnowField.h"
#include <cassert>
#include <memory>
#include <cmath>
using namespace anthology::snow;
int main()
{
    auto cache=std::make_unique<ContactCache>();
    Set(Height,.6f);Set(Density,1.f);Set(Variation,0.f);
    assert(Depth(-45.f,23.f)==.6f);
    assert(cache->At(.25f,0.f,.25f)==0.f);
    float ground, remaining;
    assert(!cache->Read(-1,-1,ground,remaining));
    for(int z=0;z<2;++z)for(int x=0;x<2;++x) cache->Publish(x,z,0.f,.5f);
    assert(std::abs(cache->At(.25f,0.f,.25f)-.3f)<1e-6f);
    assert(cache->At(.25f,1.f,.25f)==0.f); // Jumping/another floor.
    cache->Publish(256,0,0.f,1.f); // A distant tile cannot alias a near tile.
    assert(cache->At(.25f,0.f,.25f)==0.f);
    assert(Depth(15.f,15.f,.5f,0.f,.8f,4.f)==0.f);
    assert(SpeedFactor(0.f,.75f)==1.f);
    assert(SpeedFactor(10.f,.75f)==.25f);
    for(int x=-500;x<500;++x) {
        float d=Depth(float(x)*.173f,-2.9f,.5f,.8f,.7f,4.f);
        assert(std::isfinite(d)&&d>=0.f&&d<=.5f);
    }
    cache->Clear(1.f);
    assert(cache->At(.25f,0.f,.25f)==0.f);
}

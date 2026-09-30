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

    // Movement applies resistance without stamping an idle actor every tick.
    anthology::set_runtime_season(4);
    Contacts().Clear(.5f);
    for(int z=0;z<2;++z)for(int x=0;x<2;++x) Contacts().Publish(x,z,0.f,1.f);
    Contact events[256];
    for(int i=0;i<1000;++i) assert(Movement(.25f,0.f,.25f)<1.f);
    assert(Touches().Drain(events)==0);
    Footstep(.25f,0.f,.25f,1.f,0.f);
    Impact(.25f,0.f,.25f);
    Footstep(.25f,2.f,.25f,0.f,1.f); // Another floor must not leave a print.
    assert(Touches().Drain(events)==2);
    assert(!events[0].bullet && events[0].forwardX==1.f && events[1].bullet);
    anthology::set_runtime_season(1);
    Impact(.25f,0.f,.25f);
    assert(Touches().Drain(events)==0 && Movement(.25f,0.f,.25f)==1.f);
    ContactQueue bounded;
    for(int i=0;i<1000;++i) bounded.Push(float(i),0.f,0.f,.1f);
    assert(bounded.Drain(events)==256 && bounded.Drain(events)==0);
}

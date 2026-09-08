#include "../src/CoverLight.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace cover_light;
bool dark(const Frame& frame){for(auto p:frame)if(p.r||p.g||p.b)return false;return true;}
int main(){
    Flow f;
    Frame last{};bool moved=false;
    for(int n=0;n<2000;++n){
        auto current=f.frame({20,160,90},true,true,false,true,0,.04f);
        for(int i=0;i<10;++i){auto p=current[i];assert(p.r<=channelCap&&p.g<=channelCap&&p.b<=channelCap);
            if(n>100 && p.g!=last[i].g)moved=true;
        }
        last=current;
    }
    assert(moved&&!dark(last));
    assert(dark(f.frame({20,160,90},false,true,false,true,0,.04f)));
    assert(dark(f.frame({20,160,90},true,false,false,true,0,.04f)));
    assert(dark(f.frame({20,160,90},true,true,true,true,0,.04f)));
    assert(dark(f.frame({20,160,90},true,true,false,false,0,.04f)));
    assert(dark(f.frame({20,160,90},true,true,false,true,6001,.04f)));
    assert(dark(f.frame({},true,true,false,true,0,.04f)));
    for(int n=0;n<300;++n)last=f.frame({255,255,255},true,true,false,true,0,.04f);
    for(auto p:last){assert(p.r==p.g&&p.g==p.b);assert(p.r<=channelCap);}
    assert(!dark(probe(0))&&!dark(probe(2999))&&dark(probe(3000))&&dark(probe(UINT32_MAX)));
    for(int i=1;i<10;++i){auto p=probe(0)[i];assert(!p.r&&!p.g&&!p.b);}
    uint32_t start=UINT32_MAX-500;assert(dark(probe(uint32_t(3000+start)-start)));
    auto safe=f.frame({255,0,0},true,true,false,true,0,std::numeric_limits<float>::quiet_NaN());
    for(auto p:safe)assert(p.r<=channelCap&&p.g<=channelCap&&p.b<=channelCap);
    std::cout<<"PASS: bounded flow, neutral palette, immediate pause/stale/disconnect blackout, timed one-pixel probe\n";
}

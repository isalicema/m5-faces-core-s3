#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cover_light {
struct Pixel { uint8_t r=0,g=0,b=0; };
using Frame=std::array<Pixel,10>;
constexpr uint8_t channelCap=8;
constexpr uint32_t freshForMs=6000;

// The Mac plays the audio. This is cover-colour ambience, not beat detection.
class Flow {
    float colour[3]={0,0,0},phase=0,level=0;
public:
    Frame frame(Pixel base,bool playing,bool available,bool stale,bool connected,
                uint32_t age,float dt) {
        Frame result{};
        dt=std::isfinite(dt)?std::clamp(dt,0.f,.1f):0.f;
        if(!playing||!available||stale||!connected||age>freshForMs){level=0;return result;}
        const int brightest=std::max({base.r,base.g,base.b});
        if(!brightest){level=0;return result;}
        const uint8_t values[]={base.r,base.g,base.b};
        const float blend=1.f-std::exp(-dt/1.2f);
        for(int c=0;c<3;++c)colour[c]+=(float(values[c])/brightest-colour[c])*blend;
        level=std::min(1.f,level+dt/1.2f);
        phase=std::fmod(phase+dt/8.f,1.f);
        for(int i=0;i<10;++i){
            // Mirror the two halves; physical flow direction awaits visual acceptance.
            const int position=i<5?i:9-i;
            float wave=.16f+.84f*std::pow(.5f+.5f*std::cos(6.2831853f*(phase-position*.14f)),2.f);
            uint8_t out[3];
            for(int c=0;c<3;++c)out[c]=uint8_t(std::clamp(std::lround(colour[c]*wave*level*channelCap),0l,long(channelCap)));
            result[i]={out[0],out[1],out[2]};
        }
        return result;
    }
};

// First hardware validation: one dim pixel, bounded even across millis rollover.
inline Frame probe(uint32_t elapsed){
    Frame result{};
    if(elapsed<3000)result[0]={0,4,0};
    return result;
}
}

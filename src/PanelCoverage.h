#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "MusicPaper.h"

// Preserve the 4x4 coverage used by the paper UI, but calculate geometry only
// when a panel's dimensions change. Colour and opacity stay dynamic.
namespace music_paper {
struct Coverage { uint8_t outer=0, inner=0; };
inline Coverage panelCoverage(int x,int y,int w,int h,float r,float border){
    Coverage c;
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
        float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4;
        c.outer+=rounded(px,py,w,h,r);
        c.inner+=rounded(px-border,py-border,w-2*border,h-2*border,std::max(0.f,r-border));
    }
    return c;
}
class PanelCoverageCache {
    struct Entry { int w=0,h=0;float r=0,b=0;std::vector<Coverage> pixels; };
    Entry entries[16];unsigned next=0;
public:
    const std::vector<Coverage>& get(int w,int h,float r,float b){
        for(auto& e:entries)if(e.w==w&&e.h==h&&e.r==r&&e.b==b)return e.pixels;
        auto& e=entries[next++%16];e.w=w;e.h=h;e.r=r;e.b=b;e.pixels.resize(w*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)e.pixels[y*w+x]=panelCoverage(x,y,w,h,r,b);
        return e.pixels;
    }
};
}

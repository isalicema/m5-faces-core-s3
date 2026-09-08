#pragma once
#include <algorithm>
namespace music {
// Pixel coverage for a one-pixel rounded outline. Only cached stage generation
// uses this; artwork/lyric animation frames reuse the resulting surface.
inline bool insideRounded(float px,float py,float w,float h,float inset,float radius){
    if(px<inset||py<inset||px>=w-inset||py>=h-inset)return false;
    float cx=std::max(inset+radius,std::min(w-inset-radius,px));
    float cy=std::max(inset+radius,std::min(h-inset-radius,py));
    float dx=px-cx,dy=py-cy;
    return dx*dx+dy*dy<=radius*radius;
}
inline float buttonOpacity(int x,int y,int w,int h){
    constexpr float fill=18.f/255,edge=56.f/255;
    // Straight edges and interior have exact full-pixel coverage.
    if((x>=4&&x<w-4)||(y>=4&&y<h-4))
        return x==0||y==0||x==w-1||y==h-1?fill+edge*(1-fill):fill;
    int outer=0,inner=0;
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
        float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4;
        outer+=insideRounded(px,py,w,h,0,4);
        inner+=insideRounded(px,py,w,h,1,3);
    }
    // CSS white fill 0x12/255 and border 0x38/255, composited over the
    // actual backdrop at this pixel (never a single centre colour).
    return (inner*fill+(outer-inner)*(fill+edge*(1-fill)))/16;
}
inline int buttonChannel(int value,float opacity){
    return std::min(255,int(value+(255-value)*opacity+.5f));
}
}

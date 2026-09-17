#pragma once
#include <cmath>
#include <cstdint>
namespace music_paper {
constexpr uint32_t paper=0xf4e7d3,ink=0x111111,muted=0x302d29;
inline uint32_t accentInk(uint32_t rgb){
    auto linear=[](int v){float c=v/255.f;return c<=.04045f?c/12.92f:powf((c+.055f)/1.055f,2.4f);};
    float l=.2126f*linear((rgb>>16)&255)+.7152f*linear((rgb>>8)&255)+.0722f*linear(rgb&255);
    return l>.179f?0x000000:0xffffff;
}
inline float veil(int x,int y){
    // Same 160deg gradient as preview; lighter paper veil compensates LCD colour loss.
    float t=(x*.342020f+y*.939693f)/(319*.342020f+239*.939693f);
    return t<.52f?.35f+.10f*t/.52f:.45f+.15f*(t-.52f)/.48f;
}
inline bool rounded(float x,float y,float w,float h,float r){
    if(x<0||y<0||x>=w||y>=h)return false;
    float dx=fmaxf(fmaxf(r-x,x-(w-r)),0),dy=fmaxf(fmaxf(r-y,y-(h-r)),0);
    return dx*dx+dy*dy<=r*r;
}
}

#include "PanelCoverage.h"
#include <cassert>
#include <cstdio>
int main(){
 music_paper::PanelCoverageCache cache;
 for(int w:{29,43,44,52,54,56,91,300})for(int h:{4,20,29,30})for(float b:{0.f,1.f,1.5f}){
  auto &p=cache.get(w,h,8,b);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){
   int outer=0,inner=0;
   for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
    float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4;
    outer+=music_paper::rounded(px,py,w,h,8);
    inner+=music_paper::rounded(px-b,py-b,w-2*b,h-2*b,fmaxf(0,8-b));
   }
   assert(p[y*w+x].outer==outer&&p[y*w+x].inner==inner);
  }
  assert(&cache.get(w,h,8,b)==&p);
 }
 puts("PASS exact 4x4 coverage and cache reuse across dimensions/evictions");
}

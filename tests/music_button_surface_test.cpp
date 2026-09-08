#include "../src/MusicButtonSurface.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
 for(int w:{43,44,52,56}){
  int partial=0;
  for(int y=0;y<30;++y)for(int x=0;x<w;++x){
   float a=music::buttonOpacity(x,y,w,30);
   assert(a>=0&&a<=1);
   assert(std::abs(a-music::buttonOpacity(w-1-x,y,w,30))<1e-6);
   assert(std::abs(a-music::buttonOpacity(x,29-y,w,30))<1e-6);
   if(x<4&&y<4&&a>0&&a<music::buttonOpacity(w/2,0,w,30))++partial;
  }
  assert(partial>0);assert(music::buttonOpacity(0,0,w,30)==0);
  assert(std::abs(music::buttonOpacity(w/2,15,w,30)-18.f/255)<1e-6);
 }
 assert(music::buttonChannel(24,.2f)<music::buttonChannel(180,.2f));
 std::cout<<"PASS partial corner coverage, symmetry, fill and backdrop preservation\n";
}

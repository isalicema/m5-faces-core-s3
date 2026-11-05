#ifndef SHELF_GEOMETRY
#define SHELF_GEOMETRY "../src/MusicShelfGeometry.h"
#endif
#include SHELF_GEOMETRY
#include <vector>
#include <chrono>
#include <iostream>
int main(){
 std::vector<uint16_t> frame(320*240),texture(160*160);
 for(size_t i=0;i<texture.size();++i)texture[i]=uint16_t(i*97);
 auto started=std::chrono::steady_clock::now();unsigned checksum=0;
 for(int n=0;n<100;++n){std::fill(frame.begin(),frame.end(),0xf79c);float position=(n%10)*.07f;
  for(int i:{-2,2,-1,1,0})music_shelf::rasterCard(frame.data(),320,240,texture.data(),160,music_shelf::geometry(i-position),false,[]{});
  checksum+=frame[100*320+160];
 }
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
 std::cout<<"100 frames us="<<us<<" checksum="<<checksum<<" (host only, not device FPS)\n";
}

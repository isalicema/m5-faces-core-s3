#include <cassert>
#include <vector>
#include "CompanionModel.h"
int main(){
 using namespace companion;
 Model m;m.step(100);assert(m.step(46000).mood==Mood::Sleepy);
 m.pet(46010);assert(m.step(46010).mood==Mood::Happy);assert(m.step(48500).mood==Mood::Neutral);
 m.imu(50000,2,0,0,0,0,0);assert(m.step(50000).mood==Mood::Dizzy);
 m.imu(52000,2,0,0,0,0,0);assert(m.step(52700).mood==Mood::Neutral);
 m.proximity(53000,5);m.proximity(53100,180);assert(m.step(53100).mood==Mood::Shy);
 m.proximity(54000,-1);m.proximity(55000,5);assert(m.step(55000).mood==Mood::Neutral);
 m.motion(56000,100,-100);auto p=m.step(56000);assert(std::abs(p.x)<=28&&std::abs(p.y)<=23);
 Model tracking;tracking.step(100);tracking.motion(200,.7f,0);
 for(int now=200;now<1500;now+=33)tracking.step(now);
 assert(tracking.pose.x>24); // gaze remains visibly off-centre after a brief wave
 for(int now=1500;now<7000;now+=33)tracking.step(now);
 assert(std::abs(tracking.pose.x)<6);
 Model close;close.step(100);close.proximity(100,0);close.proximity(200,25);
 assert(close.step(200).mood==Mood::Shy);close.proximity(300,10);
 assert(close.step(2200).mood==Mood::Shy);close.motion(2210,.5f,.1f);assert(close.step(2210).mood==Mood::Shy);close.proximity(2300,3);
 assert(close.step(3400).mood==Mood::Neutral);
 Model wrap;wrap.step(0xffffff00);wrap.pet(0xfffffff0);assert(wrap.step(20).mood==Mood::Happy);
 MotionGrid grid;float x=0,y=0;std::vector<uint8_t> image(160*120*2,0);
 assert(!grid.sample(nullptr,0,160,120,x,y));assert(!grid.sample(image.data(),4,160,120,x,y));
 assert(!grid.sample(image.data(),image.size(),160,120,x,y));
 for(int row=30;row<70;row++)for(int col=110;col<150;col++){size_t i=(row*160+col)*2;image[i]=image[i+1]=255;}
 assert(grid.sample(image.data(),image.size(),160,120,x,y));assert(x>.3f);
 assert(!grid.sample(image.data(),image.size(),160,120,x,y));
 // Low-contrast contiguous hand region is visible; isolated noisy pixels aren't.
 grid.reset();std::fill(image.begin(),image.end(),0);grid.sample(image.data(),image.size(),160,120,x,y);
 for(int row=30;row<70;row++)for(int col=110;col<150;col++){size_t i=(row*160+col)*2;image[i]=0x10;image[i+1]=0x82;}
 assert(grid.sample(image.data(),image.size(),160,120,x,y)&&x>.3f);
 grid.reset();std::fill(image.begin(),image.end(),0);grid.sample(image.data(),image.size(),160,120,x,y);
 for(int row=2;row<120;row+=20)for(int col=2;col<160;col+=20){size_t i=(row*160+col)*2;image[i]=image[i+1]=255;}
 assert(!grid.sample(image.data(),image.size(),160,120,x,y));
 grid.reset();std::fill(image.begin(),image.end(),0);grid.sample(image.data(),image.size(),160,120,x,y);
 std::fill(image.begin(),image.end(),255);assert(!grid.sample(image.data(),image.size(),160,120,x,y));
}

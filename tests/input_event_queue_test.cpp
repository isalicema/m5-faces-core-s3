#include "InputEventQueue.h"
#include "DirectInput.h"
#include <array>
#include <cassert>
#include <cstdio>
std::array<uint8_t,10> frame(bool space){
 std::array<uint8_t,10> f={10,3,255,0x13,255,0x23,255,0x30,uint8_t(space?0x1b:0x1f),0};
 for(int i=0;i<9;++i)f[9]-=f[i];return f;
}
int main(){
 // A 60 ms tap entirely inside a 400 ms render must survive until dispatch.
 for(unsigned tap=10;tap<330;tap+=7){
  DirectInput input;InputEventQueue queue;
  for(unsigned now=0;now<=400;now+=5){auto raw=frame(now>=tap&&now<tap+60);input.feed(raw.data(),now);
   input.tick(now,[&](uint8_t index,uint8_t layer){assert(index==29&&layer==0);assert(queue.push({false,' ',0,0}));});}
  InputEventQueue::Event e;assert(queue.pop(e)&&!e.touch&&e.key==' ');assert(!queue.pop(e));
 }
 InputEventQueue q;InputEventQueue::Event e;
 for(int round=0;round<3;++round){
  for(int i=0;i<16;++i)assert(q.push({bool(i%2),uint8_t(i),int16_t(i+10),int16_t(i+20)}));
  assert(!q.push({}));
  for(int i=0;i<16;++i){assert(q.pop(e));assert(e.key==i&&e.x==i+10&&e.y==i+20&&e.touch==bool(i%2));}
  assert(!q.pop(e));
 }
 puts("PASS 60ms taps during 400ms renders, single dispatch, mixed FIFO, bounded overflow and wraparound");
}

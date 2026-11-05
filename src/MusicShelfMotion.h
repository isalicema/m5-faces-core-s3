#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace music_shelf {
inline int modulo(int value,int count){return count>0?((value%count)+count)%count:0;}
inline int nearest(float value){return int(std::floor(value+.5f));} // JS Math.round, including negative wrap.
struct Motion {
    float position=0,target=0,velocity=0;
    int anchor=0;
    uint32_t lastAt=0;
    bool running=false,dragging=false;
    void syncAnchor(){while(position>=anchor+1-.0001f)++anchor;while(position<=anchor-1+.0001f)--anchor;}
    void reset(float value,uint32_t now){position=target=value;velocity=0;anchor=nearest(value);lastAt=now;running=dragging=false;}
    bool moving()const{return running||dragging||std::fabs(target-position)>.002f;}
    void move(int direction,uint32_t now){
        if(!direction)return;direction=direction<0?-1:1;
        dragging=false;
        float delta=target-position;
        int current=delta>0?1:delta<0?-1:velocity>0?1:velocity<0?-1:0;
        target=current&&current!=direction?nearest(position)+direction:target+direction;
        if(std::fabs(target-position)>4)target=direction>0?std::floor(position+4):std::ceil(position-4);
        if(!running)lastAt=now;
        running=true;
    }
    void settle(){position=target;velocity=0;running=false;syncAnchor();}
    bool tick(uint32_t now){
        if(!running||dragging){lastAt=now;return false;}
        float remaining=std::min(.064f,float(uint32_t(now-lastAt))/1000.f);lastAt=now;
        if(remaining<=0)return false;
        // Bounded substeps retain the web spring (185/24) even on a slow LCD frame.
        while(remaining>0){
            float dt=std::min(.008f,remaining);remaining-=dt;
            float error=target-position;
            velocity+=(error*185.f-velocity*24.f)*dt;position+=velocity*dt;
        }
        if(std::fabs(target-position)<.0015f&&std::fabs(velocity)<.018f)settle();
        syncAnchor();return true;
    }
};
struct Gesture {
    bool active=false,flow=false,moved=false,cancelled=false,startedMoving=false;
    int startX=0,startY=0,lastX=0;
    float startPosition=0,velocity=0;
    uint32_t lastAt=0;
    void begin(int x,int y,uint32_t now,bool canDrag,Motion& motion){
        active=true;flow=canDrag;moved=cancelled=false;startedMoving=motion.moving();
        startX=lastX=x;startY=y;startPosition=motion.position;lastAt=now;velocity=0;
        if(flow){motion.running=false;motion.velocity=0;motion.target=motion.position;motion.dragging=true;}
    }
    void update(int x,int y,uint32_t now,Motion& motion){
        if(!active)return;
        int dx=x-startX,dy=y-startY;
        if(std::abs(dy)>=6&&!moved&&std::abs(dy)>std::abs(dx))cancelled=true;
        if(!flow){if(std::abs(dx)>=5||std::abs(dy)>=5)cancelled=true;return;}
        if(!moved&&std::abs(dx)<5)return;
        if(cancelled)return;
        if(moved&&x==lastX)return; // Stationary samples and pointer-up must not erase flick velocity.
        moved=true;
        float dt=std::max(.008f,float(uint32_t(now-lastAt))/1000.f);
        velocity=std::max(-12.f,std::min(12.f,(lastX-x)/84.f/dt));
        lastX=x;lastAt=now;
        motion.position=std::max(startPosition-2.15f,std::min(startPosition+2.15f,startPosition-dx/84.f));
        motion.target=motion.position;motion.syncAnchor();
    }
    bool finish(int x,int y,uint32_t now,Motion& motion,bool abort=false){
        if(!active)return false;
        update(x,y,now,motion);active=false;
        bool tap=!abort&&!cancelled&&!moved&&!startedMoving&&std::abs(x-startX)<5&&std::abs(y-startY)<5;
        if(flow){
            motion.dragging=false;
            // A held finger has no release velocity; projected target always lands on an integer.
            if(uint32_t(now-lastAt)>100)velocity=0;
            int low=int(std::ceil(startPosition-2)),high=int(std::floor(startPosition+2));
            motion.target=std::max(low,std::min(high,nearest(motion.position+(moved&&!abort?velocity*.12f:0))));
            motion.velocity=moved&&!abort?velocity:0;motion.lastAt=now;
            motion.running=std::fabs(motion.target-motion.position)>.0015f||std::fabs(motion.velocity)>.018f;
            if(!motion.running)motion.settle();
        }
        return tap;
    }
    void cancel(Motion& motion,uint32_t now){if(active)finish(lastX,startY,now,motion,true);cancelled=true;}
};
struct TouchEvent {enum Phase:uint8_t{Down,Move,Up,Cancel} phase=Cancel;int16_t x=0,y=0;uint32_t at=0;};
class TouchQueue {
    TouchEvent events[16];unsigned head=0,count=0;
public:
    void clear(){head=count=0;}
    bool push(TouchEvent e){
        if(e.phase==TouchEvent::Move&&count&&events[(head+count-1)%16].phase==TouchEvent::Move){events[(head+count-1)%16]=e;return true;}
        if(count==16){clear();events[count++]={TouchEvent::Cancel,e.x,e.y,e.at};return false;}
        events[(head+count)%16]=e;++count;return true;
    }
    bool pop(TouchEvent& e){if(!count)return false;e=events[head];head=(head+1)%16;--count;return true;}
};
}

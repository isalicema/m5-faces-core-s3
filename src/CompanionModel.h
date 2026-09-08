#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
// Default face proportions/timing adapted from M5Stack StackChan (MIT).
// See third_party/stackchan/README.md for pinned upstream and port boundary.
namespace companion {
inline float clamp(float v,float a,float b){return std::max(a,std::min(b,v));}
inline bool before(uint32_t now,uint32_t end){return int32_t(end-now)>0;}
enum class Mood { Neutral, Happy, Curious, Dizzy, Sleepy, Shy };
struct Pose { float x=0,y=0,openness=1,mouth=0; Mood mood=Mood::Neutral; };
class Model {
    uint32_t lastActive=0,emotionAt=0,emotionDuration=0,lastShake=0,lastStep=0;
    uint32_t blinkAt=0,blinkDelay=5400,touchAt=0;
    bool started=false,touched=false,shaken=false,tracked=false;
    uint32_t motionAt=0;
    float tiltX=0,tiltY=0,lookX=0,lookY=0,basePs=0;
    bool psReady=false,near=false;
    Mood emotion=Mood::Neutral;
public:
    Pose pose;
    void wake(uint32_t now){lastActive=now;}
    void react(Mood mood,uint32_t now,uint32_t duration){wake(now);emotion=mood;emotionAt=now;emotionDuration=duration;}
    void pet(uint32_t now,float x=0,float y=0){touchAt=now;touched=true;lookX=clamp(x,-16,16);lookY=clamp(y,-16,16);react(Mood::Happy,now,2200);}
    void imu(uint32_t now,float ax,float ay,float az,float gx,float gy,float gz){
        if(!std::isfinite(ax+ay+az+gx+gy+gz))return;
        tiltX=clamp(ax*22,-14,14);tiltY=clamp((ay+.75f)*14,-10,10);
        float g=std::sqrt(ax*ax+ay*ay+az*az),spin=std::sqrt(gx*gx+gy*gy+gz*gz);
        if(spin>18)wake(now);
        if((g>1.65f||spin>210)&&(!shaken||now-lastShake>4000)){
            lastShake=now;shaken=true;react(Mood::Dizzy,now,2600);
        }
    }
    void proximity(uint32_t now,int value){
        if(value<0||value>2047)return;
        if(!psReady){basePs=value;psReady=true;return;}
        if(value<basePs)basePs=value;
        float delta=value-basePs;
        if(!near&&delta>20){near=true;wake(now);if(emotion!=Mood::Dizzy||now-emotionAt>=emotionDuration)react(Mood::Shy,now,800);}
        if(near){wake(now);if(delta<8){near=false;if(emotion==Mood::Shy)emotionDuration=0;}}
        if(!near)basePs+=(value-basePs)*.005f;
    }
    void motion(uint32_t now,float x,float y){
        if(!std::isfinite(x+y))return;
        wake(now);if(touched&&now-touchAt<2200)return;
        motionAt=now;tracked=true;
        lookX=clamp(x/.65f,-1,1)*28;lookY=clamp(y/.75f,-1,1)*18;
        if(emotion!=Mood::Dizzy||now-emotionAt>=emotionDuration)react(Mood::Curious,now,1100);
    }
    Pose step(uint32_t now){
        if(!started){started=true;lastActive=now;lastStep=now;blinkAt=now;}
        float dt=std::min<uint32_t>(now-lastStep,100)/1000.f;lastStep=now;
        bool sleeping=now-lastActive>45000;
        bool dizzy=emotion==Mood::Dizzy&&now-emotionAt<emotionDuration;
        pose.mood=dizzy?Mood::Dizzy:near?Mood::Shy:now-emotionAt<emotionDuration?emotion:sleeping?Mood::Sleepy:Mood::Neutral;
        if(now-blinkAt>blinkDelay+200){blinkAt=now;blinkDelay=5200+((now*1664525u+1013904223u)%501);}
        float phase=float(now-blinkAt)-blinkDelay;
        float blink=phase<0||phase>200?1:1-.90f*std::sin(phase*3.14159265f/200);
        float targetOpen=pose.mood==Mood::Sleepy?.24f:pose.mood==Mood::Happy?.72f:1.f;
        pose.openness=targetOpen*blink;
        float idle=std::sin(now*.00042f)*4;
        float tx=sleeping?0:clamp(lookX+tiltX*.35f+idle,-28,28);
        float ty=clamp(lookY+tiltY*.35f,-20,20)+std::sin(now*6.2831853f/6600)*2.56f;
        if(pose.mood==Mood::Shy){tx=0;ty=8;}
        if(pose.mood==Mood::Dizzy){tx=std::sin((now-emotionAt)*.012f)*12;ty+=std::cos((now-emotionAt)*.012f)*6;}
        float blend=1-std::exp(-dt*7);pose.x+=(tx-pose.x)*blend;pose.y+=(ty-pose.y)*blend;
        float mouth=pose.mood==Mood::Curious?.55f:pose.mood==Mood::Happy?.25f:pose.mood==Mood::Sleepy?.05f:0;
        pose.mouth+=(mouth-pose.mouth)*blend;
        if(!tracked||now-motionAt>1600){lookX*=std::exp(-dt*1.2f);lookY*=std::exp(-dt*1.2f);}
        return pose;
    }
};
// Coarse movement only, not face recognition. No frames leave RAM.
class MotionGrid {
    uint8_t previous[32*24]={};bool ready=false;
public:
    int changedCells=0,clusterCells=0,brightness=0;
    void reset(){ready=false;}
    bool sample(const uint8_t* pixels,size_t length,int width,int height,float& x,float& y){
        if(!pixels||width<32||height<24||width>640||height>480||length<size_t(width)*height*2)return false;
        uint8_t next[32*24];int16_t differences[768];
        for(int row=0;row<24;row++)for(int col=0;col<32;col++){
            size_t p=(size_t((row*height+height/2)/24)*width+(col*width+width/2)/32)*2;
            uint16_t rgb=(uint16_t(pixels[p])<<8)|pixels[p+1];
            int n=row*32+col;next[n]=(((rgb>>11)&31)*2+((rgb>>5)&63)*3+(rgb&31)*2)*255/313;
            differences[n]=int(next[n])-previous[n];
        }
        std::nth_element(differences,differences+384,differences+768);
        float mean=differences[384]; // robust global exposure shift, unaffected by a small hand
        uint8_t weights[768]={};int sum=0;changedCells=0;clusterCells=0;
        for(int n=0;n<768;n++){
            sum+=next[n];float delta=std::abs(float(next[n])-previous[n]-mean);
            if(ready&&delta>12){weights[n]=uint8_t(std::min(delta,255.f));changedCells++;}
        }
        brightness=sum/768;std::copy(next,next+768,previous);ready=true;
        if(changedCells<6||changedCells>530)return false;
        // Follow the strongest contiguous moving region instead of averaging
        // unrelated changes/noisy pixels across the whole image.
        uint16_t queue[768];float best=0,bestX=0,bestY=0;
        for(int seed=0;seed<768;seed++){
            if(!weights[seed])continue;
            int head=0,tail=0;float total=0,sx=0,sy=0;
            auto add=[&](int n){if(!weights[n])return;float w=weights[n];weights[n]=0;queue[tail++]=n;total+=w;sx+=w*((n%32)/15.5f-1);sy+=w*((n/32)/11.5f-1);};
            add(seed);
            while(head<tail){int n=queue[head++];if(n%32)add(n-1);if(n%32<31)add(n+1);if(n>=32)add(n-32);if(n<736)add(n+32);}
            if(tail>=6&&total>best){best=total;bestX=sx/total;bestY=sy/total;clusterCells=tail;}
        }
        if(best==0)return false;x=bestX;y=bestY;return true;
    }
};
}

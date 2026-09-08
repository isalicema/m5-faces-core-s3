#pragma once
#include <algorithm>
#include <cstdint>
namespace screen_power {
enum class Phase { Awake, SleepOut, Dark, WakeIn, ShutdownHold };
class Model {
    Phase phase_=Phase::Awake;
    bool touchGuard_=false, beforeHoldDark_=false;
    uint8_t brightness_=85;
    uint32_t at_=0, shutdownMs_=3000;
public:
    bool update(bool clicked,bool touching,uint8_t current,uint32_t now,
                bool held=false,bool released=false,bool edgeReliable=true,uint32_t shutdownMs=3000){
        Phase old=phase_;
        if(phase_==Phase::ShutdownHold){
            if(released||!edgeReliable){phase_=beforeHoldDark_?Phase::Dark:Phase::WakeIn;at_=now;touchGuard_=true;}
        }else if(held&&!released&&edgeReliable){
            beforeHoldDark_=phase_==Phase::Dark||phase_==Phase::SleepOut;
            if(phase_==Phase::Awake&&current)brightness_=current;
            phase_=Phase::ShutdownHold;at_=now;shutdownMs_=std::max<uint32_t>(1000,shutdownMs);touchGuard_=true;
        }else if(clicked){
            if(phase_==Phase::Awake||phase_==Phase::WakeIn){
                if(phase_==Phase::Awake&&current)brightness_=current;
                phase_=Phase::SleepOut;
            }else phase_=Phase::WakeIn;
            at_=now;touchGuard_=true;
        }else{
            if(phase_==Phase::SleepOut&&now-at_>=850)phase_=Phase::Dark;
            if(phase_==Phase::WakeIn&&now-at_>=300)phase_=Phase::Awake;
            if(!touching)touchGuard_=false;
        }
        return phase_!=old;
    }
    Phase phase() const{return phase_;}
    bool dark() const{return phase_!=Phase::Awake;}
    bool appVisible() const{return phase_==Phase::Awake||phase_==Phase::WakeIn;}
    bool overlay() const{return phase_==Phase::SleepOut||phase_==Phase::ShutdownHold;}
    bool touchEnabled() const{return phase_==Phase::Awake&&!touchGuard_;}
    uint32_t elapsed(uint32_t now) const{return now-at_;}
    float progress(uint32_t now) const{return std::min(1.f,float(now-at_)/(phase_==Phase::ShutdownHold?shutdownMs_:850));}
    uint8_t brightness(uint32_t now) const{
        float p=1;
        if(phase_==Phase::Dark)return 0;
        if(phase_==Phase::WakeIn)p=std::min(1.f,float(now-at_)/300);
        if(phase_==Phase::SleepOut)p=1-std::clamp((float(now-at_)-550)/300,0.f,1.f);
        // Stay visibly in the shutdown gesture until the PMIC actually cuts power.
        if(phase_==Phase::ShutdownHold)p=1-.75f*progress(now)*progress(now);
        p=p*p*(3-2*p);
        return std::max<uint8_t>(phase_==Phase::ShutdownHold?12:0,uint8_t(brightness_*p));
    }
};
}

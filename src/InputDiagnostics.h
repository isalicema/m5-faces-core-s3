#pragma once
#include <M5Unified.h>
// Bounded, content-free timing counters for physical input diagnostics.
namespace input_diagnostics {
inline uint32_t previous=0,reported=0,maxGap=0,touches=0,keys=0;
inline void sample(const char* app,bool touchEnabled){
    uint32_t now=millis();if(previous)maxGap=std::max(maxGap,now-previous);previous=now;
    auto t=M5.Touch.getDetail();
    if(t.wasPressed()){
        ++touches;if(Serial.availableForWrite()>=100)Serial.printf("INPUT %s touch=%d,%d enabled=%d\n",app,t.x,t.y,touchEnabled);
    }
    if(now-reported>=5000){
        if(Serial.availableForWrite()>=180)Serial.printf("INPUT %s max_gap_ms=%lu touches=%lu keys=%lu touch_enabled=%d count=%u\n",app,
            (unsigned long)maxGap,(unsigned long)touches,(unsigned long)keys,touchEnabled,M5.Touch.getCount());
        maxGap=0;reported=now;

    }
}
inline void key(){++keys;}
}

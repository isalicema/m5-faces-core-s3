#pragma once
#include <M5Unified.h>
#include <utility/led/LED_Strip_Class.hpp>
#include "CoverLight.h"

namespace faces_lights {
// GPIO5 routing verified on hardware: dim green pixel, automatic 3-second off.
// Keep lighting opt-in per music session; Home and every reboot stay dark.
inline constexpr bool probeOnly=false;
inline m5::LED_Strip_Class strip;
inline bool ready=false,enabled=false;
inline uint32_t started=0,lastFrame=0;
inline cover_light::Flow flow;

inline void write(const cover_light::Frame& frame){
    if(!ready)return;
    RGBColor colours[10];
    for(int i=0;i<10;++i)colours[i]=RGBColor(frame[i].r,frame[i].g,frame[i].b);
    strip.setColors(colours,0,10);strip.display();
}
inline void off(){enabled=false;write({});}
inline bool begin(){
    if(ready){off();return true;}
    auto board=M5.getBoard();
    if(board!=m5::board_t::board_M5StackCoreS3&&board!=m5::board_t::board_M5StackCoreS3SE&&board!=m5::board_t::board_M5StackChan)return false;
    auto bus=std::make_shared<m5::LedBus_RMT>();
    auto bc=bus->config();bc.pin_data=5; // Legacy G25. Never probe speaker GPIO13.
    bc.t0h_ns=300;bc.t0l_ns=900;bc.t1h_ns=600;bc.t1l_ns=600;bc.reset_us=280;
    bus->config(bc);
    auto lc=strip.config();lc.led_count=10;lc.byte_per_led=3;lc.color_order=m5::LED_Strip_Class::config_t::color_order_grb;
    strip.config(lc);strip.setBus(bus);strip.setBrightness(255); // Absolute per-channel cap is applied before transmission.
    ready=strip.begin();off();
    Serial.printf("LIGHT ready=%d gpio=5 probe_only=%d\n",ready,probeOnly);
    return ready;
}
inline const char* toggle(uint32_t now){
    if(!ready)return "LIGHT ERROR";
    if(enabled){off();return "LIGHT OFF";}
    enabled=true;started=now;lastFrame=now;flow=cover_light::Flow{};
    if(probeOnly)write(cover_light::probe(0));
    Serial.printf("LIGHT mode=%s\n",probeOnly?"probe":"flow");
    return probeOnly?"LIGHT TEST":"LIGHT FLOW";
}
inline void tick(uint32_t now,cover_light::Pixel base,bool playing,bool available,bool stale,bool connected,uint32_t age){
    if(!ready||!enabled)return;
    // Timeout is checked before frame throttling, including millis rollover.
    if(probeOnly&&uint32_t(now-started)>=3000){off();Serial.println("LIGHT probe complete / off");return;}
    if(uint32_t(now-lastFrame)<40)return;
    float dt=uint32_t(now-lastFrame)/1000.f;lastFrame=now;
    write(probeOnly?cover_light::probe(now-started):flow.frame(base,playing,available,stale,connected,age,dt));
}
}

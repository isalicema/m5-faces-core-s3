#pragma once
#include <M5Unified.h>
#include <cmath>
#include "ScreenPowerModel.h"
#include "SmoothMusicFonts.h"
namespace faces_screen {
inline screen_power::Model state;
inline M5Canvas overlayCanvas(&M5.Display);
inline bool initialized=false,edgesReady=false,edgeHealthy=false,canvasReady=false,redraw=false;
inline uint32_t lastEdge=0,lastFrame=0,shutdownMs=3000;
inline void begin(){
    initialized=true;
    uint8_t enable=0,timing=0;
    // AXP2101: enable only PWRON rising/falling events; preserve other IRQ masks.
    // Do not change OFFLEVEL, charging, output rails, or call software powerOff.
    edgesReady=M5.In_I2C.readRegister(0x34,0x41,&enable,1,100000)
        &&M5.In_I2C.readRegister(0x34,0x27,&timing,1,100000)
        &&M5.In_I2C.writeRegister8(0x34,0x49,0x03,100000)
        &&M5.In_I2C.writeRegister8(0x34,0x41,enable|0x03,100000);
    shutdownMs=4000+((timing>>2)&3)*2000-(1000+((timing>>4)&3)*500);
    Serial.printf("SCREEN motion edges=%d remaining_hold_ms=%lu\n",edgesReady,(unsigned long)shutdownMs);
}
inline bool tick(){
    if(!initialized)begin();
    uint32_t now=millis();bool released=false;
    // M5.update() consumes only short/long bits (0x0c); leave those untouched.
    if(edgesReady&&now-lastEdge>=10){
        lastEdge=now;
        uint8_t edges=0;
        edgeHealthy=M5.In_I2C.readRegister(0x34,0x49,&edges,1,100000);
        if(edgeHealthy){
            edges&=3;released=edges&1;
            if(edges)edgeHealthy=M5.In_I2C.writeRegister8(0x34,0x49,edges,100000);
            if(edges)Serial.printf("SCREEN edge=%u\n",edges);
        }
    }
    bool changed=state.update(M5.BtnPWR.wasClicked(),M5.Touch.getCount()>0,
        M5.Display.getBrightness(),now,M5.BtnPWR.wasHold(),released,edgesReady&&edgeHealthy&&now-lastEdge<150,shutdownMs);
    if(changed){redraw=true;lastFrame=0;Serial.printf("SCREEN phase=%d\n",int(state.phase()));}
    // On wake keep backlight off until the app has repainted the previous overlay.
    if(!state.appVisible())M5.Display.setBrightness(state.brightness(now));
    return changed;
}
inline bool dark(){return state.dark();}
inline bool touchEnabled(){return state.touchEnabled();}
inline bool appVisible(){return state.appVisible();}
inline bool takeRedraw(){bool value=redraw;redraw=false;return value;}
inline void render(){
    uint32_t now=millis();
    if(state.appVisible()){M5.Display.setBrightness(state.brightness(now));return;}
    if(!state.overlay()||now-lastFrame<33)return;
    lastFrame=now;
    if(!canvasReady){overlayCanvas.setColorDepth(16);canvasReady=overlayCanvas.createSprite(320,240)!=nullptr;}
    if(!canvasReady)return; // Backlight still works if an overlay cannot be allocated.
    auto& c=overlayCanvas;c.setTextWrap(false);
    uint16_t bg=c.color565(20,25,27),cream=c.color565(246,237,215);
    c.fillSprite(bg);
    bool shutdown=state.phase()==screen_power::Phase::ShutdownHold;
    float p=state.progress(now);
    if(shutdown){
        uint16_t amber=c.color565(238,174,115);
        float radius=30-10*p;
        for(int deg=45;deg<315;deg+=3){
            float a=deg*.017453293f,b=(deg+3)*.017453293f;
            c.drawWideLine(160+radius*std::sin(a),83-radius*std::cos(a),160+radius*std::sin(b),83-radius*std::cos(b),3,amber);
        }
        c.drawWideLine(160,50+10*p,160,77,3,amber);
        c.fillRoundRect(110,192,100,3,1,c.color565(61,55,47));
        c.fillRoundRect(110,192,int(100*p),3,1,amber);
    }else{
        float drift=std::sin(std::min(1.f,p)*1.5708f)*6;
        c.fillSmoothCircle(155,81+drift,24,cream);
        c.fillSmoothCircle(166,73+drift,22,bg);
        c.fillSmoothCircle(186,67,2,cream);c.fillSmoothCircle(192,89,1.3f,cream);
    }
    auto centered=[&](const char* value,int y,uint16_t color){c.setTextColor(color);c.setCursor((320-c.textWidth(value))/2,y);c.print(value);};
    c.setFont(music_fonts::large());centered(shutdown?(p>.84f?"再见":"准备关机"):"休息一下",126,cream);
    c.setFont(music_fonts::small());centered(shutdown?"继续按住，松手取消":"已息屏，短按电源键唤醒",157,c.color565(180,188,181));
    M5.Display.clearClipRect();c.pushSprite(0,0);
    M5.Display.setBrightness(state.brightness(millis()));
}
}

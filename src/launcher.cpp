#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <esp_system.h>
#include <WiFi.h>
#include "DirectInput.h"
#include "InputDiagnostics.h"
#include "KeyboardIndicators.h"
#include "Launcher.h"
#include "FacesOta.h"
#include "SmoothMusicFonts.h"
#include "FacesSideLights.h"
#include "FacesPower.h"
#include "FacesBatteryIcon.h"
#include "FacesScreenPower.h"
extern const uint8_t homeStageStart[] asm("_binary_assets_home_stage_png_start");
extern const uint8_t homeStageEnd[] asm("_binary_assets_home_stage_png_end");

namespace {
RTC_NOINIT_ATTR suite::Ticket bootTicket;
suite::App active=suite::App::Home;
M5Faces_Keyboard3 keyboard;
DirectInput input;
KeyboardIndicators indicators;
void syncIndicators(){indicators.sync(input,[](uint8_t mode){return keyboard.setLED(mode)==M5FACES_OK;});}
M5Canvas menu(&M5.Display);
M5Canvas homeStage(&M5.Display);
bool homeStageOK=false;
bool keyboardOK=false;
uint32_t lastPoll=0,lastHealth=0,readyAt=0,lastInput=0;
uint32_t lastPowerRead=0;
faces_power::State homePower;
bool connectKeyboard(){
    if(keyboard.begin(&M5.In_I2C,M5FACES_BOTTOM3_ADDR,100000)!=M5FACES_OK)return false;
    m5faces_mode_t mode;
    if(keyboard.getMode(&mode)!=M5FACES_OK)return false;
    if(mode!=M5FACES_MODE_DIRECT&&keyboard.setMode(M5FACES_MODE_DIRECT)!=M5FACES_OK)return false;
    input=DirectInput{};indicators.reset();syncIndicators();return true;
}
void text(int x,int y,const char* value,uint16_t color=0xF79D){menu.setTextColor(color);menu.setCursor(x,y);menu.print(value);}
void draw(){
    if(!faces_screen::appVisible())return;
    menu.fillSprite(menu.color565((home_layout::paper>>16)&255,(home_layout::paper>>8)&255,home_layout::paper&255));menu.setTextWrap(false);
    // Static artwork and typography share the browser's master, at LCD resolution.
    if(homeStageOK)homeStage.pushSprite(&menu,0,0);
    else menu.drawPng(homeStageStart,homeStageEnd-homeStageStart,0,0);
    menu.setFont(music_fonts::small());
    faces_power::drawBattery(menu,homePower,home_layout::powerRight,home_layout::powerY,true);
    if(!keyboardOK){
        menu.fillRect(0,216,320,24,menu.color565((home_layout::paper>>16)&255,(home_layout::paper>>8)&255,home_layout::paper&255));
        const char* hint="轻触选择 · 正在连接键盘";
        text((320-menu.textWidth(hint))/2,220,hint,menu.color565(85,82,76));
    }
    menu.pushSprite(0,0);
}
// Match the preview: inset each edge by 2 px, then sink by 1 px.
// The bottom retains a visible 1 px gap instead of being clipped.
// Only Home launches use this; other apps and automatic idle entry stay instant.
void pressCard(suite::App app){
    const int index=int(app)-1;
    if(index<0||index>=3||!faces_screen::appVisible())return;
    const auto& c=home_layout::cards[index];
    M5Canvas card(&M5.Display);card.setColorDepth(16);
    if(!card.createSprite(c.w,c.h))return;
    if(homeStageOK)homeStage.pushSprite(&card,-c.x,-c.y);
    else card.drawPng(homeStageStart,homeStageEnd-homeStageStart,-c.x,-c.y);
    card.setPivot(c.w/2.0f,c.h/2.0f);
    const auto paper=menu.color565((home_layout::paper>>16)&255,(home_layout::paper>>8)&255,home_layout::paper&255);
    for(int frame=1;frame<=4;++frame){
        const float t=frame/4.0f;
        const float scaleX=1.0f-4.0f*t/c.w,scaleY=1.0f-4.0f*t/c.h;
        menu.fillRect(c.x,c.y,c.w,c.h,paper);
        menu.fillRoundRect(c.x,c.y,c.w,c.h,8,menu.color565(205,193,175));
        menu.setClipRect(c.x,c.y,c.w,c.h);
        card.pushRotateZoomWithAA(&menu,c.x+c.w/2.0f,c.y+c.h/2.0f+t,0,scaleX,scaleY);
        menu.clearClipRect();menu.pushSprite(0,0);delay(20);
    }
    delay(80);card.deleteSprite();
}
}
namespace suite {
bool inputReady(){return int32_t(millis()-readyAt)>=0;}
[[noreturn]] void open(App app){
    faces_lights::off(); // SK6812 retains the last colour across software resets.
    if(M5.In_I2C.isEnabled())M5.In_I2C.writeRegister8(M5FACES_BOTTOM3_ADDR,M5FACES_REG_LED,M5FACES_LED_OFF,100000);
    bootTicket=ticket(app);
    // A full software reboot disposes all tasks, sockets, decoder state and I2S.
    // The ticket is one-shot; crash/watchdog/power resets always return Home.
    ESP.restart();
    while(true)delay(1000);
}
}
void setup(){
    active=suite::consume(bootTicket,esp_reset_reason()==ESP_RST_SW);
    if(active==suite::App::Connection){connection_app::setup();readyAt=millis()+700;return;}
    if(active==suite::App::Music){cloud_app::setup();readyAt=millis()+700;return;}
    if(active==suite::App::Companion){companion_app::setup();readyAt=millis()+700;return;}
    if(active==suite::App::Radio){radio_app::setup();readyAt=millis()+700;return;}
    Serial.begin(115200);
    auto cfg=M5.config();cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk=false;cfg.internal_mic=false;cfg.internal_imu=false;M5.begin(cfg);
    faces_lights::begin(); // Clear any retained LED frame after reset; Home stays dark.
    M5.Display.setRotation(1);M5.Display.setBrightness(85);
    menu.setColorDepth(16);if(!menu.createSprite(320,240)){M5.Display.print("Memory error");while(true)delay(1000);}
    homeStage.setColorDepth(16);homeStage.setPsram(true);
    homeStageOK=homeStage.createSprite(320,240)!=nullptr;
    if(homeStageOK)homeStageOK=homeStage.drawPng(homeStageStart,homeStageEnd-homeStageStart,0,0);
    if(!M5.In_I2C.isEnabled())M5.In_I2C.begin(I2C_NUM_1,12,11);
    bool fontsOK=music_fonts::begin();
    Serial.printf("HOME smooth_fonts=%d\n",fontsOK);
    keyboardOK=connectKeyboard();homePower=faces_power::read();lastPowerRead=millis();readyAt=millis()+700;draw();
    Serial.println("BOOT faces-suite 0.1 home / no audio / side LEDs cleared on GPIO5");
    faces_ota::acceptBoot(fontsOK);faces_ota::begin();lastInput=millis();
}
void loop(){
    if(active==suite::App::Connection){connection_app::loop();return;}
    if(active==suite::App::Music){cloud_app::loop();return;}
    if(active==suite::App::Companion){companion_app::loop();return;}
    if(active==suite::App::Radio){radio_app::loop();return;}
    M5.update();uint32_t now=millis();
    bool screenChanged=faces_screen::tick();
    input_diagnostics::sample("home",faces_screen::touchEnabled());
    if(screenChanged||faces_screen::dark())lastInput=now;
    if(screenChanged&&faces_screen::appVisible())draw();
    // Exact physical-USB diagnostic entry; ignore unrelated Bridge traffic.
    static String serialCommand;
    static bool serialOverflow=false;
    while(Serial.available()){
        char c=Serial.read();
        if(c=='\n'){
            if(!serialOverflow&&serialCommand=="HOME STATUS")Serial.printf("HOME wifi=%d rssi=%d idle_ms=%lu touch=%d keyboard=%d\n",int(WiFi.status()),WiFi.status()==WL_CONNECTED?WiFi.RSSI():0,(unsigned long)(millis()-lastInput),M5.Touch.getCount(),keyboardOK);
            if(!serialOverflow&&serialCommand=="MUSIC OPEN"&&suite::inputReady())suite::open(suite::App::Music);
            if(!serialOverflow&&serialCommand=="RADIO OPEN"&&suite::inputReady())suite::open(suite::App::Radio);
            if(!serialOverflow&&serialCommand=="COMPANION OPEN"&&suite::inputReady())suite::open(suite::App::Companion);
            serialCommand="";serialOverflow=false;
        }else if(c!='\r'){
            if(serialCommand.length()<32&&!serialOverflow)serialCommand+=c;
            else serialOverflow=true;
        }
    }
    if(now-lastPowerRead>=500){
        lastPowerRead=now;auto next=faces_power::read();
        bool changed=next.valid!=homePower.valid||next.present!=homePower.present||next.percent!=homePower.percent||next.charging!=homePower.charging;
        homePower=next;if(changed)draw();
    }
    if(keyboardOK&&now-lastPoll>=5){
        lastPoll=now;uint8_t raw[10]={};
        bool fresh=keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK&&input.feed(raw,now);
        if(fresh&&input.anyPressed())lastInput=now;
        bool changed=input.tick(now,[now](uint8_t index,uint8_t layer){
            input_diagnostics::key();
            lastInput=millis();auto app=suite::key(M5Faces_Keyboard3::KEYMAP[index][layer]);
            if(int32_t(now-readyAt)>=0&&app!=suite::App::Home){pressCard(app);suite::open(app);}
        });
        if(changed)lastInput=now; // Includes modifier-only presses and releases.
        syncIndicators();
    }
    auto touch=M5.Touch.getDetail();if(M5.Touch.getCount()>0||touch.wasReleased())lastInput=now;if(faces_screen::touchEnabled()&&touch.wasPressed()&&int32_t(now-readyAt)>=0){auto app=suite::touch(touch.x,touch.y);if(app!=suite::App::Home){pressCard(app);suite::open(app);}}
    if(now-lastHealth>5000){lastHealth=now;uint8_t id=0;keyboardOK=keyboardOK?keyboard.getModelID(&id)==M5FACES_OK&&id==M5Faces_Keyboard3::MODEL_ID:connectKeyboard();draw();}
    // OTA is synchronous and runs first; never switch away during an update.
    if(faces_ota::tick(!faces_screen::dark()&&now-lastInput>10000)){lastInput=millis();draw();return;}
    // Poll again next loop after a slow network check so new physical input wins.
    if(!faces_screen::dark()&&millis()-now<100&&suite::homeIdleDue(active,now,lastInput,M5.Touch.getCount()>0)){
        Serial.printf("HOME idle -> companion after %lu ms\n",(unsigned long)(now-lastInput));
        suite::open(suite::App::Companion);
    }
    faces_screen::render();delay(5);
}

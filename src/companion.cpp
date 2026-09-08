// Default avatar geometry adapted from M5Stack StackChan, MIT; see third_party/stackchan.
#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <esp_camera.h>
#include <WiFi.h>
#include "Launcher.h"
#include "CompanionModel.h"
#include "DirectInput.h"
#include "KeyboardIndicators.h"
#include "SmoothMusicFonts.h"
#include "FacesSideLights.h"
#include "FacesOta.h"
#include "FacesScreenPower.h"

namespace companion_app {
namespace {
M5Canvas canvas(&M5.Display);
M5Faces_Keyboard3 keyboard;
DirectInput input;
KeyboardIndicators indicators;
companion::Model model;
bool keyboardOK=false,proximityOK=false,cameraOK=false;
uint32_t lastKeys=0,lastSensors=0,lastFrame=0,lastHealth=0,hintAt=0,frames=0;
int proximity=-1,psLow=2047,psHigh=0;uint32_t psSamples=0,gazeEvents=0;
float ax=0,ay=0,az=0,gx=0,gy=0,gz=0;
portMUX_TYPE cameraLock=portMUX_INITIALIZER_UNLOCKED;
struct CameraResult {float x=0,y=0;uint32_t at=0,frames=0;bool moving=false;int changed=0,cluster=0,brightness=0;uint32_t stackFree=0;} cameraResult;

void syncIndicators(){indicators.sync(input,[](uint8_t mode){return keyboard.setLED(mode)==M5FACES_OK;});}
bool connectKeyboard(){
    if(keyboard.begin(&M5.In_I2C,M5FACES_BOTTOM3_ADDR,100000)!=M5FACES_OK)return false;
    m5faces_mode_t mode;
    if(keyboard.getMode(&mode)!=M5FACES_OK)return false;
    if(mode!=M5FACES_MODE_DIRECT&&keyboard.setMode(M5FACES_MODE_DIRECT)!=M5FACES_OK)return false;
    input=DirectInput{};indicators.reset();syncIndicators();return true;
}
void cameraWorker(void*){
    companion::MotionGrid grid;
    while(true){
        auto* fb=esp_camera_fb_get();
        if(fb){
            float x=0,y=0;
            bool moving=fb->format==PIXFORMAT_RGB565&&grid.sample(fb->buf,fb->len,fb->width,fb->height,x,y);
            esp_camera_fb_return(fb);
            portENTER_CRITICAL(&cameraLock);
            cameraResult={x,y,millis(),cameraResult.frames+1,moving,grid.changedCells,grid.clusterCells,grid.brightness,uint32_t(uxTaskGetStackHighWaterMark(nullptr))};
            portEXIT_CRITICAL(&cameraLock);
        }
        vTaskDelay(pdMS_TO_TICKS(240));
    }
}
bool beginCamera(){
    camera_config_t cfg={};
    cfg.pin_pwdn=-1;cfg.pin_reset=-1;cfg.pin_xclk=-1;
    cfg.pin_sccb_sda=-1;cfg.pin_sccb_scl=-1;
    cfg.pin_d0=39;cfg.pin_d1=40;cfg.pin_d2=41;cfg.pin_d3=42;
    cfg.pin_d4=15;cfg.pin_d5=16;cfg.pin_d6=48;cfg.pin_d7=47;
    cfg.pin_vsync=46;cfg.pin_href=38;cfg.pin_pclk=45;
    cfg.xclk_freq_hz=20000000;cfg.ledc_timer=LEDC_TIMER_0;cfg.ledc_channel=LEDC_CHANNEL_0;
    cfg.pixel_format=PIXFORMAT_RGB565;cfg.frame_size=FRAMESIZE_QQVGA;
    cfg.fb_count=1;cfg.fb_location=CAMERA_FB_IN_PSRAM;cfg.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
    cfg.sccb_i2c_port=M5.In_I2C.getPort();
    // Reuse M5's existing IDF I2C bus. Creating a second bus or releasing Wire1
    // would invalidate the shared touch/keyboard/IMU controller on Arduino 3.
    // Only setup accesses SCCB; the acquisition worker uses DVP exclusively.
    esp_err_t err=esp_camera_init(&cfg);
    Serial.printf("COMPANION camera_init=0x%x\n",unsigned(err));
    if(err!=ESP_OK)return false;
    if(xTaskCreatePinnedToCore(cameraWorker,"companion-camera",8192,nullptr,1,nullptr,0)!=pdPASS){esp_camera_deinit();return false;}
    return true;
}
bool beginProximity(){
    uint8_t id=0;
    if(!M5.In_I2C.readRegister(0x23,0x86,&id,1,100000)||id!=0x92)return false;
    // LTR553 datasheet near-field example: 100mA peak, four short pulses.
    // 60kHz / 100% pulse duty / 100ms measurement: ~0.067mA average LED current.
    return M5.In_I2C.writeRegister8(0x23,0x82,0x7C,100000)
        &&M5.In_I2C.writeRegister8(0x23,0x83,4,100000)
        &&M5.In_I2C.writeRegister8(0x23,0x84,3,100000)
        &&M5.In_I2C.writeRegister8(0x23,0x81,0x22,100000);
}
void draw(uint32_t now){
    if(!faces_screen::appVisible())return;
    auto p=model.step(now);canvas.fillSprite(TFT_BLACK);
    const uint16_t white=0xFFFF;
    int cx=160+std::lround(p.x),cy=104+std::lround(p.y);
    for(int side:{-1,1}){
        int x=cx+70*side;
        if(p.mood==companion::Mood::Dizzy){
            canvas.drawWideLine(x-7,cy-7,x+7,cy+7,1.5f,white);
            canvas.drawWideLine(x-7,cy+7,x+7,cy-7,1.5f,white);
        }else if(p.mood==companion::Mood::Shy){
            canvas.drawWideLine(x-8,cy-side*2,x+8,cy+side*2,1.5f,white);
        }else if(p.mood==companion::Mood::Happy){
            for(int i=-8;i<8;i++){
                int y=cy+std::lround(i*i/14.f);
                canvas.drawWideLine(x+i,y,x+i+1,cy+std::lround((i+1)*(i+1)/14.f),1.5f*p.openness+.5f,white);
            }
        }else{
            int size=p.mood==companion::Mood::Curious?26:20;
            int h=std::max(2,int(std::lround(size*p.openness)));
            canvas.fillSmoothRoundRect(x-size/2,cy-h/2,size,h,std::min(size/2,h/2),white);
        }
    }
    int w=std::lround(90-p.mouth*30),h=std::lround(6+p.mouth*44);
    if(p.mood==companion::Mood::Shy)canvas.fillSmoothCircle(cx,cy+42,6,white);
    else canvas.fillSmoothRoundRect(cx-w/2,cy+42-h/2,w,h,std::max(1,int(p.mouth*16)),white);
    if(p.mood==companion::Mood::Happy||p.mood==companion::Mood::Shy){
        for(int side:{-1,1})for(int i=0;i<3;i++)canvas.drawWideLine(cx+side*84+i*5,cy+24,cx+side*84+i*5-2,cy+30,.8f,uint16_t(p.mood==companion::Mood::Shy?0xF2AA:0xEBCF));
    }
    // The wide hit target remains available even when the quiet label fades.
    uint16_t label=now-hintAt<6000?0x8410:0x2104;
    canvas.drawWideLine(19,13,14,18,.8f,label);canvas.drawWideLine(14,18,19,23,.8f,label);
    canvas.setFont(music_fonts::small());canvas.setTextColor(label);canvas.setCursor(29,11);canvas.print("小伙伴");
    if(now-hintAt<6000){
        canvas.setCursor(95,215);canvas.print("摸摸我 · Q 返回");
    }
    // Camera-shaped status glyph: lit while observing, outline when disabled.
    canvas.drawRoundRect(291,12,12,9,2,cameraOK?0x7D55:0x4208);
    canvas.fillCircle(297,16,2,cameraOK?0x7D55:0x4208);
    static bool painted=false,previousHint=false,previousCamera=false;
    bool showHint=now-hintAt<6000;
    // Only the face moves. Keep header/footer on the LCD until their state changes.
    bool powerRedraw=faces_screen::takeRedraw();
    bool full=powerRedraw||!painted||showHint!=previousHint||cameraOK!=previousCamera;
    if(!full)M5.Display.setClipRect(32,52,264,150);
    canvas.pushSprite(0,0);M5.Display.clearClipRect();
    painted=true;previousHint=showHint;previousCamera=cameraOK;frames++;
}
void key(uint8_t c,uint32_t now){
    if(!suite::inputReady())return;
    if(c=='q'||c=='Q'||c==27)suite::open(suite::App::Home);
    model.pet(now);hintAt=now;
}
}
void setup(){
    Serial.begin(115200);auto cfg=M5.config();cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk=false;cfg.internal_mic=false;cfg.internal_imu=true;M5.begin(cfg);
    WiFi.mode(WIFI_OFF);faces_lights::begin();M5.Display.setRotation(1);M5.Display.setBrightness(75);
    canvas.setColorDepth(16);bool allocated=canvas.createSprite(320,240)!=nullptr;
    bool fonts=music_fonts::begin();
    if(!allocated){M5.Display.print("Memory error - returning Home");delay(1500);suite::open(suite::App::Home);}
    hintAt=millis();draw(hintAt);
    cameraOK=beginCamera();proximityOK=beginProximity();keyboardOK=connectKeyboard();
    Serial.printf("BOOT companion board=%d imu=%d proximity=%d camera=%d keyboard=%d heap=%u psram=%u\n",int(M5.getBoard()),M5.Imu.isEnabled(),proximityOK,cameraOK,keyboardOK,ESP.getFreeHeap(),ESP.getFreePsram());
    Serial.printf("LTR mode=%02x led=%02x pulses=%d rate=%02x status=%02x\n",M5.In_I2C.readRegister8(0x23,0x81,100000),M5.In_I2C.readRegister8(0x23,0x82,100000),M5.In_I2C.readRegister8(0x23,0x83,100000),M5.In_I2C.readRegister8(0x23,0x84,100000),M5.In_I2C.readRegister8(0x23,0x8C,100000));
    faces_ota::acceptBoot(allocated&&fonts);hintAt=millis();
}
void loop(){
    M5.update();if(faces_screen::tick())lastFrame=0;uint32_t now=millis();
    static String command;
    while(Serial.available()){
        char c=Serial.read();if(c=='\n'){if(command=="COMPANION HOME")suite::open(suite::App::Home);command="";}
        else if(c!='\r'){if(command.length()<40)command+=c;else command="INVALID";}
    }
    if(keyboardOK&&now-lastKeys>=5){
        lastKeys=now;uint8_t raw[10]={};if(keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK)input.feed(raw,now);
        input.tick(now,[now](uint8_t index,uint8_t layer){key(M5Faces_Keyboard3::KEYMAP[index][layer],now);});syncIndicators();
    }
    auto touch=M5.Touch.getDetail();
    if(faces_screen::touchEnabled()&&suite::inputReady()&&touch.wasPressed()){
        if(touch.x<112&&touch.y<55)suite::open(suite::App::Home);
        model.pet(now,(touch.x-160)/10.f,(touch.y-120)/8.f);hintAt=now;
    }
    if(now-lastSensors>=40){
        lastSensors=now;
        if(M5.Imu.isEnabled()){
            M5.Imu.update();auto data=M5.Imu.getImuData();
            ax=data.accel.x;ay=data.accel.y;az=data.accel.z;gx=data.gyro.x;gy=data.gyro.y;gz=data.gyro.z;
            model.imu(now,ax,ay,az,gx,gy,gz);
        }
        uint8_t ps[2]={},status=0;
        if(proximityOK&&M5.In_I2C.readRegister(0x23,0x8C,&status,1,100000)&&(status&1)
            &&M5.In_I2C.readRegister(0x23,0x8D,ps,2,100000)){
            proximity=(ps[1]&0x80)?2047:ps[0]|((ps[1]&7)<<8);
            psLow=std::min(psLow,proximity);psHigh=std::max(psHigh,proximity);psSamples++;
            model.proximity(now,proximity);
        }
        CameraResult result;portENTER_CRITICAL(&cameraLock);result=cameraResult;portEXIT_CRITICAL(&cameraLock);
        static uint32_t seen=0;
        if(result.frames!=seen){seen=result.frames;if(result.moving&&uint32_t(millis()-result.at)<1000&&std::abs(gx)+std::abs(gy)+std::abs(gz)<35){model.motion(now,-result.x,result.y);gazeEvents++;}}
    }
    if(now-lastFrame>=33){lastFrame=now;draw(now);}
    if(now-lastHealth>=5000){
        float fps=frames*1000.f/(now-lastHealth);frames=0;lastHealth=now;
        uint8_t id=0;keyboardOK=keyboardOK?keyboard.getModelID(&id)==M5FACES_OK&&id==M5Faces_Keyboard3::MODEL_ID:connectKeyboard();
        CameraResult result;portENTER_CRITICAL(&cameraLock);result=cameraResult;portEXIT_CRITICAL(&cameraLock);
        Serial.printf("COMPANION fps=%.1f imu=%.2f,%.2f,%.2f gyro=%.1f,%.1f,%.1f ps=%d camera_frames=%u camera_age=%u movement=%d keyboard=%d heap=%u\n",fps,ax,ay,az,gx,gy,gz,proximity,result.frames,result.frames?uint32_t(millis()-result.at):0,result.moving,keyboardOK,ESP.getFreeHeap());
        Serial.printf("SENSE ps_samples=%u ps_range=%d..%d changed=%d cluster=%d luma=%d target=%.2f,%.2f gaze_events=%u stack_free=%u pose=%.1f,%.1f mood=%d\n",psSamples,psSamples?psLow:-1,psSamples?psHigh:-1,result.changed,result.cluster,result.brightness,result.x,result.y,gazeEvents,result.stackFree,model.pose.x,model.pose.y,int(model.pose.mood));
        psLow=2047;psHigh=0;psSamples=0;
    }
    faces_screen::render();delay(2);
}
}

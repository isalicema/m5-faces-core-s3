#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <cJSON.h>
#include <atomic>
#include <vector>
#include "DirectInput.h"
#include "KeyboardIndicators.h"
#include "MusicControls.h"
#include "MusicButtonSurface.h"
#include "MusicLayout.h"
#include "MusicColors.h"
#include "MusicTextWrap.h"
#include "MusicVisualState.h"
#include "MusicSync.h"
#include "MusicConnection.h"
#include "SmoothMusicFonts.h"
#include "FacesSideLights.h"
#include "FacesScreenPower.h"

#include "Launcher.h"
#ifdef FACES_SUITE
namespace cloud_app {
#endif

namespace {
using namespace music_layout;
int heartTop=85;
M5Faces_Keyboard3 keyboard;
DirectInput input;
KeyboardIndicators indicators;
void syncIndicators(){indicators.sync(input,[](uint8_t mode){return keyboard.setLED(mode)==M5FACES_OK;});}
M5Canvas canvas(&M5.Display), stageCanvas(&M5.Display), coverCanvas(&M5.Display), coverStaging(&M5.Display);
uint32_t coverVersion=0, cachedCoverVersion=UINT32_MAX, coverDecodes=0;
bool coverCached=false, coverCanvasOK=false;
music::ArtworkState artworkState;
music::LyricFade lyricFade;
int lyricTop=120;
lgfx::bgr888_t lyricUnderlay[160*20];
uint32_t motionFrames=0, motionTotalMs=0, motionMaxMs=0;
bool stageOK=false;
uint32_t sceneVersion=0, drawnSceneVersion=UINT32_MAX;
Preferences prefs;
WebServer portal(80);
String host, token, ssid, password, apName, apPass;
uint16_t bridgePort = 8766;
bool setupMode = false, keyboardOK = false;
std::atomic<bool> dirty{true};
// Animate only on a confirmed playback change; connection loss holds the pose.
float recordReveal = 0, recordTarget = 0;
uint32_t recordStarted = 0;
bool recordMoving = false, lyricMoving = false;
uint32_t lastFullDraw=0;
uint32_t lastPoll = 0, lastDraw = 0, lastHealth = 0, rebootAt = 0;
std::atomic<bool> busy{false};
uint32_t connectionStarted=0;
struct LyricCue {float at=0;char text[364]="";};
struct State {
    cover_light::Pixel lightBase;
    LyricCue lyrics[8];uint8_t lyricCount=0;char lyricStatus[20]="loading";
    char title[384] = "在 Mac 播放音乐", artist[256] = "", album[256] = "", source[64] = "Faces Music";
    char notice[16] = "";
    char track[32] = "", artId[32] = "", sceneId[32] = "", message[160] = "等待连接音乐桥接";
    bool available = false, playing = false, stale = true, hasState = false;
    int favorite = -1;
    char repeatRequested[8]="";
    char repeatMode[8]="";
    uint16_t actions = 0;
    float position = 0, duration = 0;
    uint32_t received = 0, messageAt = 0;
};
State state;
std::vector<uint8_t> cover, scene;
SemaphoreHandle_t stateLock;
QueueHandle_t queue;
struct Command { music::Action action; char track[32], request[40]; };
constexpr uint16_t bg = 0x10E4, ink = music_colors::title, dim = music_colors::muted, accent = 0xE6D3;
const cJSON* field(const cJSON* j, const char* k) { return cJSON_GetObjectItemCaseSensitive(j, k); }
const char* text(const cJSON* j, const char* k) { auto v = field(j,k); return cJSON_IsString(v) ? v->valuestring : ""; }
bool flag(const cJSON* j, const char* k) { return cJSON_IsTrue(field(j,k)); }
float value(const cJSON* j, const char* k) { auto v=field(j,k); return cJSON_IsNumber(v) && isfinite(v->valuedouble) ? v->valuedouble : 0; }
void message(const char* m,const char* badge="CHECK MAC") {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    strlcpy(state.message,m,sizeof(state.message));strlcpy(state.notice,badge,sizeof(state.notice));state.messageAt=millis();
    dirty.store(true);
    xSemaphoreGive(stateLock);
}
State snapshot() { xSemaphoreTake(stateLock,portMAX_DELAY); State s=state; xSemaphoreGive(stateLock); return s; }
bool get(const String& path, std::vector<uint8_t>& body, size_t limit, const String& payload="") {
    WiFiClient client; HTTPClient http;
    http.setConnectTimeout(1200);
    // Mac dispatch may perform two bounded 4-second subprocess calls.
    http.setTimeout(payload.isEmpty()?1500:15000);
    if (!http.begin(client,"http://"+host+":"+String(bridgePort)+path)) return false;
    http.addHeader("Authorization","Bearer "+token);
    int code;
    if (payload.isEmpty()) code=http.GET();
    else { http.addHeader("Content-Type","application/json"); code=http.POST(payload); }
    int length=http.getSize(); bool ok=false;
    if (code > 0 && length > 0 && size_t(length)<=limit) {
        body.resize(length+1);
        size_t count=http.getStream().readBytes(body.data(),length);
        body[length]=0; ok=count==size_t(length);
        if (ok) body.resize(length);
    }
    http.end(); return ok && (code==200 || (!payload.isEmpty() && code==409));
}
void worker(void*) {
    uint32_t refreshAt=0;
    music::SyncCadence sync;
    bool wifiReported=false;
    for (;;) {
        if (WiFi.status()!=WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if(!wifiReported){wifiReported=true;Serial.printf("CONNECT wifi_ms=%lu\n",(unsigned long)(millis()-connectionStarted));}
        Command cmd;
        if (xQueueReceive(queue,&cmd,0)==pdTRUE) {
            cJSON* j=cJSON_CreateObject();
            cJSON_AddStringToObject(j,"action",music::name(cmd.action));
            cJSON_AddStringToObject(j,"track_id",cmd.track);
            cJSON_AddStringToObject(j,"request_id",cmd.request);
            char* json=cJSON_PrintUnformatted(j);
            std::vector<uint8_t> bytes;
            uint32_t actionAt=millis();const char* outcome="unconfirmed";
            bool ok=json && get("/api/action",bytes,2048,String(json));
            if(json)cJSON_free(json); cJSON_Delete(j);
            if(ok){
                bytes.push_back(0); j=cJSON_Parse(reinterpret_cast<char*>(bytes.data()));
                if(j){
                    if(flag(j,"ok")){
                        const char* badge=cmd.action==music::RepeatOne?"ONE SENT":cmd.action==music::RepeatAll?"ALL SENT":"";
                        message("",badge);outcome="accepted";
                    }
                    else{bool uncertain=!strcmp(text(j,"code"),"uncertain");message(text(j,"error"),uncertain?"UNCONFIRMED":"CHECK MAC");outcome=uncertain?"unconfirmed":"rejected";}
                    cJSON_Delete(j);
                }
                else message("返回无效，请检查 Mac");
            } else message("结果未知，请检查 Mac；未重试","UNCONFIRMED");
            Serial.printf("ACTION action=%s outcome=%s duration_ms=%lu\n",music::name(cmd.action),outcome,(unsigned long)(millis()-actionAt));
            busy.store(false);dirty.store(true); refreshAt=0;
            sync.afterAction(millis());
        }
        if (millis()-refreshAt>=(snapshot().hasState?sync.interval(millis()):500) || refreshAt==0) {
            refreshAt=millis(); std::vector<uint8_t> bytes;
            if(get("/api/state",bytes,8192)) {
                bytes.push_back(0); auto j=cJSON_Parse(reinterpret_cast<char*>(bytes.data()));
                if(j && flag(j,"ok")) {
                    State next=snapshot();
                    strlcpy(next.title,text(j,"title"),sizeof(next.title));
                    strlcpy(next.artist,text(j,"artist"),sizeof(next.artist));
                    strlcpy(next.album,text(j,"album"),sizeof(next.album));
                    strlcpy(next.source,text(j,"source_name"),sizeof(next.source));
                    strlcpy(next.track,text(j,"track_id"),sizeof(next.track));
                    strlcpy(next.artId,text(j,"artwork_id"),sizeof(next.artId));
                    strlcpy(next.sceneId,text(field(j,"theme"),"scene_id"),sizeof(next.sceneId));
                    const char* base=text(field(j,"theme"),"base");
                    unsigned rgb=0;
                    if(strlen(base)==7&&base[0]=='#'){
                        bool valid=true;for(int i=1;i<7;++i)if(!isxdigit(static_cast<unsigned char>(base[i])))valid=false;
                        if(valid&&sscanf(base+1,"%x",&rgb)==1)next.lightBase={uint8_t(rgb>>16),uint8_t(rgb>>8),uint8_t(rgb)};
                        else next.lightBase={};
                    }else next.lightBase={};
                    next.available=flag(j,"available"); next.playing=flag(j,"playing"); next.stale=flag(j,"stale");
                    auto liked=field(j,"favorite"); next.favorite=cJSON_IsBool(liked)?cJSON_IsTrue(liked):-1;
                    strlcpy(next.repeatRequested,text(j,"repeat_requested"),sizeof(next.repeatRequested));
                    strlcpy(next.repeatMode,text(j,"repeat_mode"),sizeof(next.repeatMode));
                    next.position=value(j,"position"); next.duration=value(j,"duration");
                    const cJSON* lyrics=field(j,"lyric");
                    strlcpy(next.lyricStatus,text(lyrics,"status"),sizeof(next.lyricStatus));next.lyricCount=0;
                    const cJSON* cue=nullptr;
                    cJSON_ArrayForEach(cue,field(lyrics,"lines")){
                        if(next.lyricCount>=8)break;
                        auto at=cJSON_GetArrayItem(cue,0),words=cJSON_GetArrayItem(cue,1);
                        if(cJSON_IsNumber(at)&&cJSON_IsString(words)&&isfinite(at->valuedouble)){
                            auto& item=next.lyrics[next.lyricCount++];item.at=at->valuedouble;
                            strlcpy(item.text,words->valuestring,sizeof(item.text));
                        }
                    }
                    next.actions=0; const cJSON* action=nullptr;
                    cJSON_ArrayForEach(action,field(j,"actions")) if(cJSON_IsString(action))
                        for(int a=1;a<=music::RepeatAll;++a) if(!strcmp(action->valuestring,music::name(music::Action(a)))) next.actions|=(1<<a);
                    if(millis()-next.messageAt>4000) strlcpy(next.message,text(j,"error"),sizeof(next.message));
                    next.received=millis();
                    if(!next.hasState)Serial.printf("CONNECT state_ms=%lu\n",(unsigned long)(millis()-connectionStarted));
                    next.hasState=true;
                    xSemaphoreTake(stateLock,portMAX_DELAY);
                    bool changed=strcmp(state.artId,next.artId)||strcmp(state.track,next.track);
                    if(!next.album[0]&&!strcmp(state.title,next.title)&&!strcmp(state.artist,next.artist)&&!strcmp(state.source,next.source))
                        strlcpy(next.album,state.album,sizeof(next.album));
                    std::string song=std::string(next.source)+"\n"+next.title+"\n"+next.artist+"\n"+next.album;
                    if(next.available && artworkState.update(song,next.artId)){cover.clear();++coverVersion;}
                    if(state.playing!=next.playing || state.favorite!=next.favorite || strcmp(state.repeatRequested,next.repeatRequested) || strcmp(state.repeatMode,next.repeatMode) || changed)dirty.store(true);
                    if(strcmp(state.sceneId,next.sceneId)){scene.clear();++sceneVersion;}
                    state=next;
                    bool needArt=next.available && artworkState.needsImage();
                    bool needScene=next.sceneId[0] && scene.empty();
                    xSemaphoreGive(stateLock);
                    if(needArt){
                        std::vector<uint8_t> image;
                        if(get("/api/artwork/"+String(next.artId),image,40000)) {
                            xSemaphoreTake(stateLock,portMAX_DELAY); cover=std::move(image); artworkState.accept(); ++coverVersion; dirty.store(true); xSemaphoreGive(stateLock);
                        }
                    }
                    if(needScene){
                        std::vector<uint8_t> image;
                        if(get("/api/artwork/"+String(next.sceneId),image,40000)) {
                            xSemaphoreTake(stateLock,portMAX_DELAY);scene=std::move(image);++sceneVersion;xSemaphoreGive(stateLock);
                        }
                    }
                }
                if(j)cJSON_Delete(j);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
void submit(music::Action action) {
    if(action==music::None||setupMode)return;
    State s=snapshot();
    if(s.stale||!s.available||millis()-s.received>7000||WiFi.status()!=WL_CONNECTED){message("等待音乐连接");return;}
    if(!(s.actions&(1<<action))){message("请先检查 Mac 控制权限");return;}
    if(busy.exchange(true))return;
    Command c{};c.action=action;strlcpy(c.track,s.track,sizeof(c.track));
    snprintf(c.request,sizeof(c.request),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
    if(xQueueSend(queue,&c,0)!=pdTRUE)busy.store(false);
    else message("正在发送…","SENDING");
}
void line(int x,int y,const char* s,uint16_t color=ink) {
    canvas.setTextColor(color);canvas.setCursor(x,y);canvas.print(s);
}
void glassPanel(int x,int y,int w,int h,M5Canvas& surface=stageCanvas){
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        float opacity=music::buttonOpacity(xx,yy,w,h);
        if(opacity<=0)continue;
        auto c=surface.readPixelRGB(x+xx,y+yy);
        surface.drawPixel(x+xx,y+yy,surface.color888(
            music::buttonChannel(c.r,opacity),music::buttonChannel(c.g,opacity),music::buttonChannel(c.b,opacity)));
    }
}
void cacheCover(){
    uint32_t version;std::vector<uint8_t> bytes;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    version=coverVersion;
    if(version!=cachedCoverVersion)bytes=cover;
    xSemaphoreGive(stateLock);
    if(version==cachedCoverVersion)return;
    if(bytes.empty())coverCached=false; // Explicit change to a different song.
    else if(coverCanvasOK){
        coverStaging.fillSprite(bg);
        // Publish only a complete decode. Failed replacement keeps the good image.
        if(coverStaging.drawJpg(bytes.data(),bytes.size(),0,0,144,144)){
            coverStaging.pushSprite(&coverCanvas,0,0);coverCached=true;
        }
        ++coverDecodes;
    }
    cachedCoverVersion=version;
}
void heartIcon(int x,int y,bool filled,uint16_t color){
    // Supersampled vector mask avoids font-dependent Unicode heart glyphs.
    auto inside=[](float a,float b){float q=a*a+b*b-1.f;return q*q*q-a*a*b*b*b<=0.f;};
    lgfx::rgb888_t rgb;rgb.r=((color>>11)&31)*255/31;rgb.g=((color>>5)&63)*255/63;rgb.b=(color&31)*255/31;
    for(int py=0;py<13;++py)for(int px=0;px<13;++px){
        int coverage=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
            float a=((px+(sx+.5f)/4)/13.f-.5f)*2.6f;
            float b=(.5f-(py+(sy+.5f)/4)/13.f)*2.6f;
            if(inside(a,b)&&(filled||!inside(a/.70f,b/.70f)))++coverage;
        }
        if(!coverage)continue;
        auto old=canvas.readPixelRGB(x+px,y+py);
        canvas.drawPixel(x+px,y+py,canvas.color888((rgb.r*coverage+old.r*(16-coverage))/16,(rgb.g*coverage+old.g*(16-coverage))/16,(rgb.b*coverage+old.b*(16-coverage))/16));
    }
}
void drawRecord(const State& s,bool stale){
    cacheCover();
        const uint32_t now=millis();
        if(!stale && s.available) {
            float target=s.playing?0.f:1.f;
            if(target!=recordTarget){recordTarget=target;recordStarted=now;recordMoving=true;}
        }
        if(recordMoving){
            float step=float(now-recordStarted)/1050.f;recordStarted=now;
            if(recordTarget>recordReveal)recordReveal=std::min(recordTarget,recordReveal+step);
            else recordReveal=std::max(recordTarget,recordReveal-step);
            recordMoving=recordReveal!=recordTarget;
        }
        auto eased=[](float t){t=std::max(0.f,std::min(1.f,t));return t*t*(3.f-2.f*t);};
        const float shrink=eased(recordReveal/.34f),roll=eased((recordReveal-.34f)/.66f);
        int sleeve=artSize-int((artSize-pausedSize)*shrink), sleeveY=artY+(artSize-sleeve)/2;
        if(roll>0.f){
            int cx=artX+sleeve/2+int(discTravel*roll*sleeve/artSize),cy=artY+artSize/2,r=discSize*sleeve/(artSize*2);
            canvas.fillCircle(cx,cy,r,0x0841);
            canvas.drawCircle(cx,cy,r,0x4A49);
            for(int groove=26;groove<r-2;groove+=4)canvas.drawCircle(cx,cy,groove,0x2104);
            // A quiet highlight and a warm paper label keep it legible at 144px.
            const float angle=27.f/58.f*roll;
            // Rotate an asymmetric sheen with the disc, not just concentric rings.
            for(int i=0;i<4;++i){
                float a=angle-.9f+i*.065f;
                canvas.drawLine(cx+int(cosf(a)*(r-23)),cy+int(sinf(a)*(r-23)),
                    cx+int(cosf(a)*(r-3)),cy+int(sinf(a)*(r-3)),i==1?0x630C:0x4208);
            }
            canvas.fillCircle(cx,cy,22,0xBC6C);canvas.fillCircle(cx,cy,17,0xDD91);
            canvas.drawCircle(cx,cy,12,0xBC6C);canvas.fillCircle(cx,cy,3,bg);
            canvas.fillCircle(cx+int(cosf(angle-1.f)*14),cy+int(sinf(angle-1.f)*14),2,0x7348);
        }
        canvas.fillRoundRect(artX+1,sleeveY+2,sleeve+2,sleeve,3,0x0841);
        canvas.fillRoundRect(artX,sleeveY,sleeve,sleeve,3,0x2948);
        if(coverCached) {
            coverCanvas.pushRotateZoomWithAA(&canvas,artX+sleeve/2.f,artY+artSize/2.f,0,sleeve/144.f,sleeve/144.f);
        } else {canvas.drawCircle(artX+sleeve/2,artY+artSize/2,sleeve/5,dim);canvas.fillCircle(artX+sleeve/2,artY+artSize/2,5,accent);}
 }
float playhead(const State& s,bool stale){
    float elapsed=std::max(0.f,s.position);
    if(!stale&&s.playing)elapsed+=(millis()-s.received)/1000.f;
    return s.duration>0?std::min(elapsed,s.duration):elapsed;
}
void drawLyric(const State& s,bool stale){
    canvas.setFont(music_fonts::small());
    float elapsed=playhead(s,stale);
    const char* lyric="暂无同步歌词";float lyricAt=-1;
    if(!s.hasState)lyric="等待音乐连接";
    else if(!strcmp(s.lyricStatus,"synced")){
        lyric="♪";
        for(int i=0;i<s.lyricCount;++i)if(s.lyrics[i].at<=elapsed){lyric=s.lyrics[i].text;lyricAt=s.lyrics[i].at;}
    }else if(!strcmp(s.lyricStatus,"loading"))lyric="正在找歌词…";
    else if(!strcmp(s.lyricStatus,"retrying"))lyric="歌词暂不可用";
    else if(!strcmp(s.lyricStatus,"instrumental"))lyric="纯音乐";
    int lyricOverflow=std::max(0,int(canvas.textWidth(lyric))-metaWidth);
    int lyricScroll=std::min(lyricOverflow,int(std::max(0.f,elapsed-lyricAt-1.2f)*20));
    lyricMoving=!stale&&s.playing&&lyricOverflow>0&&lyricScroll<lyricOverflow;
    std::string key=std::string(s.track)+":"+std::to_string(lyricAt)+":"+lyric;
    // Placeholder/message keys do not depend on the advancing playhead.
    if(strcmp(s.lyricStatus,"synced"))key=std::string(s.track)+":"+lyric;
    lyricFade.select(key,millis());float alpha=lyricFade.opacity(millis());
    canvas.setClipRect(metaX,lyricTop,metaWidth,lyricHeight);
    if(alpha>0.f){
        if(alpha<1.f)canvas.readRectRGB(metaX,lyricTop,metaWidth,lyricHeight,lyricUnderlay);
        line(metaX-lyricScroll,lyricTop+int(5.f*(1.f-alpha)),lyric,music_colors::lyric);
        if(alpha<1.f)for(int y=0;y<lyricHeight;++y)for(int x=0;x<metaWidth;++x){
            auto c=canvas.readPixelRGB(metaX+x,lyricTop+y);const auto& base=lyricUnderlay[y*metaWidth+x];
            canvas.drawPixel(metaX+x,lyricTop+y,canvas.color888(
                uint8_t(base.r+(c.r-base.r)*alpha),uint8_t(base.g+(c.g-base.g)*alpha),uint8_t(base.b+(c.b-base.b)*alpha)));
        }
    }
    canvas.clearClipRect();
}
void draw(bool motionOnly=false) {
    if(!faces_screen::appVisible())return;
    uint32_t frameAt=millis();bool wasMoving=recordMoving;
    auto finishMotion=[&](){
        if(wasMoving||recordMoving){uint32_t cost=millis()-frameAt;++motionFrames;motionTotalMs+=cost;motionMaxMs=std::max(motionMaxMs,cost);}
        if(wasMoving&&!recordMoving&&motionFrames){
            Serial.printf("MOTION frames=%lu render_avg_ms=%lu max_ms=%lu cover_decodes=%lu\n",(unsigned long)motionFrames,(unsigned long)(motionTotalMs/motionFrames),(unsigned long)motionMaxMs,(unsigned long)coverDecodes);
            motionFrames=motionTotalMs=motionMaxMs=0;
        }
    };
    if(motionOnly&&!setupMode&&stageOK){
        State s=snapshot();bool stale=s.stale||millis()-s.received>7000||WiFi.status()!=WL_CONNECTED;
        if(recordMoving){
            canvas.setClipRect(0,28,150,146);stageCanvas.pushSprite(&canvas,0,0);drawRecord(s,stale);canvas.clearClipRect();
            M5.Display.setClipRect(0,28,150,146);canvas.pushSprite(0,0);M5.Display.clearClipRect();
        }
        canvas.setClipRect(metaX,lyricTop,metaWidth,lyricHeight);stageCanvas.pushSprite(&canvas,0,0);canvas.clearClipRect();
        drawLyric(s,stale);
        M5.Display.setClipRect(metaX,lyricTop,metaWidth,lyricHeight);canvas.pushSprite(0,0);M5.Display.clearClipRect();
        finishMotion();return;
    }
    dirty.store(false);
    lastFullDraw=millis();
    lyricMoving=false;
    canvas.fillSprite(bg);canvas.setFont(music_fonts::small());canvas.setTextSize(1);
    if(setupMode){
        #ifdef FACES_SUITE
        line(34,12,"连接音乐控制器");
#else
        line(12,12,"FACES / 连接音乐控制器");
#endif
        line(12,44,apName.c_str());
        line(12,66,("密码: "+apPass).c_str());line(12,93,"手机连接以上 Wi-Fi");
        line(12,114,"浏览器打开 192.168.4.1");line(12,144,"填写 Wi-Fi 和 Mac 桥接信息");
        line(12,190,"音乐在 Mac 播放，设备只作遥控");
    }else{
        State s=snapshot();bool stale=s.stale||millis()-s.received>7000||WiFi.status()!=WL_CONNECTED;
        if(stageOK){
            xSemaphoreTake(stateLock,portMAX_DELAY);
            if(drawnSceneVersion!=sceneVersion){
                stageCanvas.fillSprite(bg);
                if(!scene.empty())stageCanvas.drawJpg(scene.data(),scene.size(),0,0,320,240);
                for(int i=0;i<buttonCount;++i)glassPanel(buttonX[i],buttonY,buttonWidth[i],buttonHeight);
                drawnSceneVersion=sceneVersion;
            }
            xSemaphoreGive(stateLock);
            stageCanvas.pushSprite(&canvas,0,0);
        }
        canvas.setFont(music_fonts::tiny());
#ifdef FACES_SUITE
        line(6,7,"<",music_colors::header);
        line(27,7,s.source[0]?s.source:"FACES MUSIC",music_colors::header);
#else
        line(10,7,s.source[0]?s.source:"FACES MUSIC",music_colors::header);
#endif
        auto connection=music::connection(s.hasState,WiFi.status()==WL_CONNECTED,s.stale,s.available,
                                           millis()-connectionStarted,millis()-s.received);
        if(!s.hasState)strlcpy(s.title,music::connectionTitle(connection),sizeof(s.title));
        const char* status=busy.load()?"SENDING":s.notice[0]&&millis()-s.messageAt<4000?s.notice:
            connection!=music::Connection::Ready?music::connectionBadge(connection):s.playing?"PLAYING":"PAUSED";
        line(310-canvas.textWidth(status),7,status,music_colors::header);
        drawRecord(s,stale);
        canvas.setClipRect(metaX,36,metaWidth,128);
        canvas.setFont(music_fonts::large());
        auto title=music::titleLines(s.title,metaWidth,[](const char* v){return canvas.textWidth(v);});
        for(size_t i=0;i<title.size();++i)line(metaX,metaY-3+i*titleLine,title[i].c_str());
        int artistY=metaY+int(title.size())*titleLine+artistGap;
        canvas.setFont(music_fonts::small());line(metaX,artistY-2,s.artist,dim);
        heartTop=artistY+artistHeight;
        heartIcon(metaX,heartTop+6,s.favorite==1,s.favorite==1?music_colors::favorite:music_colors::unliked);
        line(metaX+16,heartTop+3,"喜欢",s.favorite==1?music_colors::favorite:music_colors::unliked);
        lyricTop=heartTop+heartHeight+lyricGap;
        canvas.clearClipRect();drawLyric(s,stale);
        char timing[32];float elapsed=playhead(s,stale);
        // One full-width progress bar, then elapsed at left and duration at right.
        for(int yy=timelineY;yy<timelineY+timelineHeight;++yy)for(int xx=timelineX;xx<timelineX+timelineWidth;++xx){
            auto c=canvas.readPixelRGB(xx,yy);
            canvas.drawPixel(xx,yy,canvas.color888((c.r*7+255)/8,(c.g*7+255)/8,(c.b*7+255)/8));
        }
        int played=s.duration>0?std::max(0,std::min(timelineWidth,int(elapsed/s.duration*timelineWidth))):0;
        if(played)canvas.fillRect(timelineX,timelineY,played,timelineHeight,music_colors::progress);
        canvas.setFont(music_fonts::tiny());
        snprintf(timing,sizeof(timing),"%d:%02d",int(elapsed)/60,int(elapsed)%60);line(10,timeY-2,timing,dim);
        snprintf(timing,sizeof(timing),"%d:%02d",int(s.duration)/60,int(s.duration)%60);line(310-canvas.textWidth(timing),timeY-2,timing,dim);
        canvas.setFont(music_fonts::small());
        const char* labels[]={"J 上一首",s.playing?"空格暂停":"空格播放","K 下一首","H 喜欢","C 单曲","V 列表"};
        const music::Action acts[]={music::Previous,music::Toggle,music::Next,music::Favorite,music::RepeatOne,music::RepeatAll};
        for(int i=0;i<buttonCount;++i){
            if(!stageOK)glassPanel(buttonX[i],buttonY,buttonWidth[i],buttonHeight,canvas);
            bool enabled=!stale && (s.actions&(1<<acts[i]));
            bool selected=enabled && ((acts[i]==music::RepeatOne && !strcmp(s.repeatMode,"one")) || (acts[i]==music::RepeatAll && !strcmp(s.repeatMode,"all")));
            line(buttonX[i]+(buttonWidth[i]-canvas.textWidth(labels[i]))/2,buttonY+7,labels[i],selected?music_colors::progress:enabled?music_colors::button:0x9CF3);
            if(selected)canvas.fillRect(buttonX[i]+4,buttonY+buttonHeight-3,buttonWidth[i]-8,2,music_colors::progress);
        }

    }
#ifdef FACES_SUITE
    if(setupMode)line(6,12,"<",ink);
#endif
    canvas.pushSprite(0,0);finishMotion();
}
bool connectKeyboard(){
    if(keyboard.begin(&M5.In_I2C,M5FACES_BOTTOM3_ADDR,100000)!=M5FACES_OK)return false;
    m5faces_mode_t mode;
    if(keyboard.getMode(&mode)!=M5FACES_OK)return false;
    if(mode!=M5FACES_MODE_DIRECT && keyboard.setMode(M5FACES_MODE_DIRECT)!=M5FACES_OK)return false;
    input=DirectInput{};indicators.reset();syncIndicators();return true;
}
void startPortal(){
    setupMode=true; WiFi.mode(WIFI_AP_STA);
    apName="Faces-Music-"+String(uint32_t(ESP.getEfuseMac()),HEX).substring(0,4);
    char pass[12];snprintf(pass,sizeof(pass),"%08lx",(unsigned long)esp_random());apPass=pass;
    WiFi.softAP(apName.c_str(),apPass.c_str());
    portal.on("/",HTTP_GET,[]{portal.send(200,"text/html; charset=utf-8",R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><h2>Faces 音乐控制器</h2><form method="post" action="/save"><p>Wi-Fi 名称 <input name="ssid" maxlength="32" required></p><p>Wi-Fi 密码 <input name="password" type="password" maxlength="63"></p><p>Mac 局域网 IPv4 地址 <input name="host" placeholder="192.168.1.10" required></p><p>桥接端口 <input name="port" type="number" value="8766" required></p><p>配对密钥 <input name="token" type="password" required></p><button>保存并连接</button></form><p>密钥在 Mac 音乐桥接预览页的“连接 Faces”中显示。</p>)HTML");});
    portal.on("/save",HTTP_POST,[]{
        IPAddress ip;String h=portal.arg("host"),t=portal.arg("token"),s=portal.arg("ssid"),p=portal.arg("password");
        int port=portal.arg("port").toInt();bool valid=ip.fromString(h)&&port>0&&port<=65535&&s.length()>0&&s.length()<=32&&p.length()<=63&&t.length()>=32&&t.length()<=128;
        for(char c:t)if(!isalnum(c)&&c!='-'&&c!='_')valid=false;
        if(!valid){portal.send(400,"text/plain; charset=utf-8","请检查 Wi-Fi、IPv4、端口和密钥");return;}
        prefs.putString("ssid",s);prefs.putString("pass",p);prefs.putString("host",h);prefs.putString("token",t);prefs.putUShort("port",port);
        portal.send(200,"text/plain; charset=utf-8","已保存，正在重新连接。");rebootAt=millis()+1000;
    });portal.begin();dirty=true;
}
}
void setup(){
    Serial.begin(115200);
    auto cfg=M5.config();cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk=false;cfg.internal_mic=false;cfg.internal_imu=false;
    M5.begin(cfg); // Speaker, microphone and camera stay disabled.
    connectionStarted=millis();
    stateLock=xSemaphoreCreateMutex();queue=xQueueCreate(1,sizeof(Command));
    if(!stateLock||!queue){M5.Display.print("Memory error");while(true)delay(1000);}
    prefs.begin("faces-music",false);
    ssid=prefs.getString("ssid");password=prefs.getString("pass");host=prefs.getString("host");token=prefs.getString("token");bridgePort=prefs.getUShort("port",8766);
    // Reuse only Wi-Fi from the radio namespace; never mutate its configuration.
    if(ssid.isEmpty()){Preferences radio;radio.begin("faces-radio",true);ssid=radio.getString("ssid");password=radio.getString("pass");radio.end();}
    // Associate Wi-Fi and fetch state while fonts, sprites and keyboard initialize.
    WiFi.mode(WIFI_STA);WiFi.setSleep(true);WiFi.setAutoReconnect(true);
    if(!ssid.isEmpty())WiFi.begin(ssid.c_str(),password.c_str());
    if(!host.isEmpty()&&!token.isEmpty()&&!ssid.isEmpty())
        if(xTaskCreatePinnedToCore(worker,"music-bridge",16384,nullptr,1,nullptr,0)!=pdPASS)message("无法启动连接任务");
    faces_lights::begin(); // Sends black; ambient flow needs an explicit L press.
    M5.Display.setRotation(1);M5.Display.setBrightness(85);
    canvas.setColorDepth(16);if(!canvas.createSprite(320,240)){while(true)delay(1000);}
    canvas.setTextWrap(false);
    bool fontsOK=music_fonts::begin();
    coverCanvas.setColorDepth(16);coverCanvasOK=coverCanvas.createSprite(144,144)!=nullptr;
    coverCanvas.setPivot(72,72);
    coverStaging.setColorDepth(16);coverCanvasOK=coverStaging.createSprite(144,144)!=nullptr&&coverCanvasOK;
    Serial.printf("RENDER smooth_fonts=%d cover_cache=%d psram_free=%u\n",fontsOK,coverCanvasOK,ESP.getFreePsram());
    stageCanvas.setColorDepth(16);stageOK=stageCanvas.createSprite(320,240)!=nullptr;
    if(!M5.In_I2C.isEnabled())M5.In_I2C.begin(I2C_NUM_1,12,11);
    keyboardOK=connectKeyboard();
    if(host.isEmpty()||token.isEmpty()||ssid.isEmpty())startPortal();
    Serial.println("BOOT faces-music 0.1 / speaker disabled / LED GPIO5 off until L");draw();
}
void loop(){
    M5.update();if(faces_screen::tick()){dirty=true;lastDraw=0;}auto now=millis();
    if(setupMode)portal.handleClient();
    if(rebootAt && int32_t(now-rebootAt)>=0){
        faces_lights::off();
#ifdef FACES_SUITE
        suite::open(suite::App::Music);
#else
        ESP.restart();
#endif
    }
    if(keyboardOK && now-lastPoll>=5){lastPoll=now;uint8_t raw[10]={};if(keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK)input.feed(raw,now);
        input.tick(now,[](uint8_t index,uint8_t layer){auto k=M5Faces_Keyboard3::KEYMAP[index][layer];
#ifdef FACES_SUITE
            if(!suite::inputReady())return;
            if(k==KEYBOARD3_KEY_ESC||k=='q'||k=='Q')suite::open(suite::App::Home);
#endif
            if(k=='l'||k=='L'){if(!setupMode)message("",faces_lights::toggle(millis()));}
            else if(k=='s'||k=='S'){faces_lights::off();if(!setupMode)startPortal();}else submit(music::key(k));});
        syncIndicators();}
    if(now-lastHealth>5000){lastHealth=now;uint8_t id=0;keyboardOK=keyboardOK?keyboard.getModelID(&id)==M5FACES_OK&&id==M5Faces_Keyboard3::MODEL_ID:connectKeyboard();}
    auto touch=M5.Touch.getDetail();if(faces_screen::touchEnabled()&&touch.wasPressed()){
#ifdef FACES_SUITE
        if(!suite::inputReady())return;
        if((!setupMode&&music::homeTouch(touch.x,touch.y)) ||
           (setupMode&&touch.x>=0&&touch.x<28&&touch.y>=0&&touch.y<28))suite::open(suite::App::Home);
#endif
        if(!setupMode)submit(music::touch(touch.x,touch.y,heartTop));dirty=true;}
    if(!setupMode){
        xSemaphoreTake(stateLock,portMAX_DELAY);
        auto base=state.lightBase;bool playing=state.playing,available=state.available,stale=state.stale;
        uint32_t age=millis()-state.received;
        xSemaphoreGive(stateLock);
        faces_lights::tick(millis(),base,playing,available,stale,WiFi.status()==WL_CONNECTED,age);
    }
    bool animating=!setupMode&&(recordMoving||lyricMoving||lyricFade.active(now));
    if(dirty.load()||now-lastDraw>(animating?33:200)){lastDraw=now;draw(animating&&!dirty.load()&&now-lastFullDraw<500);}faces_screen::render();delay(2);
}

#ifdef FACES_SUITE
} // namespace cloud_app
#endif

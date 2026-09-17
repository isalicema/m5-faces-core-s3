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
#include "PanelCoverage.h"
#include "InputDiagnostics.h"
#include "InputEventQueue.h"
#include "KeyboardIndicators.h"
#include "MusicControls.h"
#include "MusicButtonSurface.h"
#include "MusicLayout.h"
#include "MusicColors.h"
#include "MusicPaper.h"
#include "MusicIcons.h"
#include "MusicTextWrap.h"
#include "MusicVisualState.h"
#include "MusicSync.h"
#include "MusicConnection.h"
#include "SmoothMusicFonts.h"
#include "FacesSideLights.h"
#include "FacesScreenPower.h"
#include "FacesPower.h"
#include "FacesBatteryIcon.h"

#include "Launcher.h"
#ifdef FACES_SUITE
#include "FacesNetwork.h"
#endif
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
M5Canvas textMask(&M5.Display);
bool textMaskOK=false;
M5Canvas canvas(&M5.Display), stageCanvas(&M5.Display), stageStaging(&M5.Display), coverCanvas(&M5.Display), coverStaging(&M5.Display);
uint32_t coverVersion=0, cachedCoverVersion=UINT32_MAX, coverDecodes=0;
bool coverCached=false, coverCanvasOK=false;
music::ArtworkState artworkState;
music::SceneState sceneState;
music::LyricFade lyricFade;
int lyricTop=120;
lgfx::bgr888_t lyricUnderlay[metaWidth*lyricHeight];
uint32_t motionFrames=0, motionTotalMs=0, motionMaxMs=0;
bool stageOK=false;
uint32_t sceneVersion=0, drawnSceneVersion=UINT32_MAX;
Preferences prefs;
WebServer portal(80);
String host, token, ssid, password, apName, apPass;
uint16_t bridgePort = 8766;
bool setupMode = false, keyboardOK = false;
std::atomic<bool> dirty{true};
faces_power::State musicPower;
uint32_t powerReadAt=0;
// Animate only on a confirmed playback change; connection loss holds the pose.
float recordReveal = 0, recordTarget = 0;
uint32_t recordStarted = 0;
bool recordMoving = false, lyricMoving = false;
uint32_t lastFullDraw=0;
char drawnTrack[32]="",drawnArtId[32]="";
uint32_t lastPoll = 0, lastDraw = 0, lastHealth = 0, rebootAt = 0;
std::atomic<bool> busy{false};
uint32_t connectionStarted=0;
InputEventQueue pendingInput;
bool inputPumpReady=false;
void pollInput(){
    if(!inputPumpReady)return;
    auto now=millis();if(now-lastPoll<5)return;lastPoll=now;
    M5.update();if(faces_screen::tick()){dirty=true;lastDraw=0;}
    input_diagnostics::sample("music",faces_screen::touchEnabled());
    if(keyboardOK){
        uint8_t raw[10]={};
        if(keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK)input.feed(raw,now);
        input.tick(now,[](uint8_t index,uint8_t layer){
#ifdef FACES_SUITE
            if(!suite::inputReady())return;
#endif
            input_diagnostics::key();
            pendingInput.push({false,M5Faces_Keyboard3::KEYMAP[index][layer],0,0});
        });
        syncIndicators();
    }
    auto t=M5.Touch.getDetail();
    if(faces_screen::touchEnabled()&&t.wasPressed()){
#ifdef FACES_SUITE
        if(!suite::inputReady())return;
#endif
        pendingInput.push({true,0,t.x,t.y});
    }
}
struct LyricCue {float at=0;char text[364]="";};
struct State {
    cover_light::Pixel lightBase;
    uint32_t accentRgb=0xd7c9b2;
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
constexpr uint16_t bg = music_colors::rgb565(music_paper::paper), ink = music_colors::rgb565(music_paper::ink), dim = music_colors::rgb565(music_paper::muted), accent = 0xE6D3;
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
    // Connection establishment may need a TCP retransmission on the LAN.
    // This never retries an HTTP action whose outcome may already have changed.
    http.setConnectTimeout(payload.isEmpty()?1200:3000);
    // Mac dispatch may perform two bounded 4-second subprocess calls.
    http.setTimeout(payload.isEmpty()?1500:15000);
    if (!http.begin(client,"http://"+host+":"+String(bridgePort)+path)) return false;
    http.addHeader("Authorization","Bearer "+token);
    if(path=="/api/state"){
        xSemaphoreTake(stateLock,portMAX_DELAY);auto power=musicPower;xSemaphoreGive(stateLock);
        auto j=cJSON_CreateObject();faces_power::addJson(j,power);
        char* raw=cJSON_PrintUnformatted(j);cJSON_Delete(j);
        if(raw){http.addHeader("X-Faces-Power",raw);cJSON_free(raw);}
    }
    uint32_t requestAt=millis();int code;
    if (payload.isEmpty()) code=http.GET();
    else { http.addHeader("Content-Type","application/json"); code=http.POST(payload); }
    int length=http.getSize(); bool ok=false;size_t count=0;
    if (code > 0 && length > 0 && size_t(length)<=limit) {
        body.resize(length+1);
        count=http.getStream().readBytes(body.data(),length);
        body[length]=0; ok=count==size_t(length);
        if (ok) body.resize(length);
    }
    if(!payload.isEmpty())Serial.printf("HTTP action code=%d bytes=%u expected=%d elapsed_ms=%lu rssi=%d\n",code,unsigned(count),length,(unsigned long)(millis()-requestAt),WiFi.RSSI());
    http.end(); return ok && (code==200 || (!payload.isEmpty() && code==409));
}
void worker(void*) {
    uint32_t refreshAt=0;
    music::SyncCadence sync;
    bool wifiReported=false;
    for (;;) {
#ifdef FACES_SUITE
        faces_network::tick();host=faces_network::host();token=faces_network::token();bridgePort=faces_network::port();
        if(host.isEmpty()||token.isEmpty()){vTaskDelay(pdMS_TO_TICKS(100));continue;}
#endif
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
                    const char* dominant=text(field(j,"theme"),"dominant");
                    next.accentRgb=0xd7c9b2;
                    if(strlen(dominant)==7&&dominant[0]=='#'){
                        bool valid=true;for(int i=1;i<7;++i)if(!isxdigit(static_cast<unsigned char>(dominant[i])))valid=false;
                        if(valid&&sscanf(dominant+1,"%x",&rgb)==1)next.accentRgb=rgb;
                    }
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
                    sceneState.update(next.sceneId);
                    state=next;
                    bool needArt=next.available && artworkState.needsImage();
                    bool needScene=sceneState.needsImage();
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
                            xSemaphoreTake(stateLock,portMAX_DELAY);
                            if(sceneState.accept(next.sceneId)){scene=std::move(image);++sceneVersion;dirty.store(true);}
                            xSemaphoreGive(stateLock);
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
int measureText(const char* text){
    float scaleX=canvas.getTextSizeX(),scaleY=canvas.getTextSizeY();
    canvas.setTextSize(1);int nativeWidth=canvas.textWidth(text);canvas.setTextSize(scaleX,scaleY);
    return int(ceilf(nativeWidth*scaleX));
}
void line(int x,int y,const char* s,uint16_t color=ink) {
    pollInput();
    float scale=canvas.getTextSizeX();
    if(!textMaskOK||fabsf(scale-1.f)<.001f){canvas.setTextColor(color);canvas.setCursor(x,y);canvas.print(s);return;}
    // VLW fractional rendering can drop strokes. Rasterize at native size, then
    // area-sample the alpha mask onto the actual stage instead of skipping pixels.
    struct Raster {
        std::string text;const lgfx::IFont* font=nullptr;float scale=0;
        int offset=0,width=0,height=0;std::vector<float> alpha;
    };
    static Raster rasters[32];static unsigned replace=0;
    int sourceOffset=x<0?int(-x/scale):0;
    int origin=x+int(sourceOffset*scale);
    textMask.setFont(canvas.getFont());textMask.setTextSize(1);
    int width=std::max(0,std::min(320-origin,int(ceilf(std::min(768,int(textMask.textWidth(s))-sourceOffset)*scale))));
    int height=std::max(0,std::min(240-y,int(ceilf(std::min(32,int(textMask.fontHeight()))*scale))));
    Raster* raster=nullptr;
    for(auto& r:rasters)if(r.font==canvas.getFont()&&r.scale==scale&&r.offset==sourceOffset&&r.width==width&&r.height==height&&r.text==s){raster=&r;break;}
    if(!raster){
        raster=&rasters[replace++%32];raster->text=s;raster->font=canvas.getFont();raster->scale=scale;
        raster->offset=sourceOffset;raster->width=width;raster->height=height;raster->alpha.resize(width*height);
        textMask.fillSprite(0);textMask.setTextColor(0xffff);textMask.setTextWrap(false);
        textMask.setCursor(-sourceOffset,0);textMask.print(s);
        for(int py=0;py<height;++py)for(int px=0;px<width;++px){
        if(px==0)pollInput();
            float x0=px/scale,x1=(px+1)/scale,y0=py/scale,y1=(py+1)/scale,coverage=0;
            for(int sy=int(y0);sy<int(ceilf(y1));++sy)for(int sx=int(x0);sx<int(ceilf(x1));++sx){
                float weight=(fminf(x1,sx+1.f)-fmaxf(x0,float(sx)))*(fminf(y1,sy+1.f)-fmaxf(y0,float(sy)));
                coverage+=(textMask.readPixel(sx,sy)&31)/31.f*weight;
            }
            raster->alpha[py*width+px]=coverage*scale*scale;
        }
    }
    int red=((color>>11)&31)*255/31,green=((color>>5)&63)*255/63,blue=(color&31)*255/31;
    for(int py=0;py<height;++py)for(int px=0;px<width;++px){
        if(px==0)pollInput();
        float alpha=raster->alpha[py*width+px];if(alpha<=0)continue;
        auto old=canvas.readPixelRGB(origin+px,y+py);
        canvas.drawPixel(origin+px,y+py,canvas.color888(old.r+(red-old.r)*alpha,old.g+(green-old.g)*alpha,old.b+(blue-old.b)*alpha));
    }
}
void blendPixel(M5Canvas& target,int x,int y,uint32_t rgb,float alpha){
    auto old=target.readPixelRGB(x,y);
    target.drawPixel(x,y,target.color888(old.r+(int((rgb>>16)&255)-old.r)*alpha,
        old.g+(int((rgb>>8)&255)-old.g)*alpha,old.b+(int(rgb&255)-old.b)*alpha));
}
void paperPanel(int x,int y,int w,int h,float radius,uint32_t fill,float opacity=1.f,float border=1.5f){
    if(w<=0||h<=0)return;
    static music_paper::PanelCoverageCache masks;
    // Progress width changes continuously: its four-pixel silhouette is cheap
    // and must not evict the controls from the cache.
    const auto* mask=h>4?&masks.get(w,h,radius,border):nullptr;
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        if(xx==0)pollInput();
        auto coverage=mask?(*mask)[yy*w+xx]:music_paper::panelCoverage(xx,yy,w,h,radius,border);
        int outer=coverage.outer,inner=coverage.inner;
        if(!outer)continue;
        // Combine fill and border coverage against the same background.
        auto old=canvas.readPixelRGB(x+xx,y+yy);
        float fi=inner/16.f*opacity,bo=(outer-inner)/16.f;
        canvas.drawPixel(x+xx,y+yy,canvas.color888(
            old.r*(1-fi-bo)+((fill>>16)&255)*fi+17*bo,
            old.g*(1-fi-bo)+((fill>>8)&255)*fi+17*bo,
            old.b*(1-fi-bo)+(fill&255)*fi+17*bo));
    }
}
void paperIcon(const uint8_t* mask,int x,int y,uint32_t color,int size=13){
    for(int yy=0;yy<size;++yy)for(int xx=0;xx<size;++xx)
        if(mask[yy*size+xx])blendPixel(canvas,x+xx,y+yy,color,mask[yy*size+xx]/255.f);
}
struct LabelInkBounds {int left=0,top=0,right=0,bottom=0;};
LabelInkBounds buttonLabelBounds(const char* text){
    // Cache by caller: measure visible glyph alpha, not font advance/ascender.
    textMask.fillSprite(0);textMask.setFont(music_fonts::tiny());textMask.setTextSize(1);
    textMask.setTextColor(0xffff);textMask.setCursor(0,0);textMask.print(text);
    LabelInkBounds bounds{768,32,0,0};
    int width=std::min(768,int(textMask.textWidth(text))),height=std::min(32,int(textMask.fontHeight()));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)if((textMask.readPixel(x,y)&31)>2){
        bounds.left=std::min(bounds.left,x);bounds.top=std::min(bounds.top,y);
        bounds.right=std::max(bounds.right,x+1);bounds.bottom=std::max(bounds.bottom,y+1);
    }
    if(bounds.right<=bounds.left)return {0,0,width,height};
    return bounds;
}
void paperText(int x,int y,const char* value,float size,uint16_t color=ink,bool bold=false){
    canvas.setFont(bold?music_fonts::large():music_fonts::small());
    canvas.setTextSize(size/(bold?16.f:12.f));line(x,y,value,color);canvas.setTextSize(1);
}
void cacheScene(){
    uint32_t version;std::vector<uint8_t> bytes;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    version=sceneVersion;
    if(version!=drawnSceneVersion)bytes=scene;
    xSemaphoreGive(stateLock);
    if(version==drawnSceneVersion)return;
    // Decode and tint off-screen. Empty metadata or a bad JPEG never wipes
    // the last complete stage. Input continues to be sampled during tinting.
    if(!bytes.empty()&&stageStaging.drawJpg(bytes.data(),bytes.size(),0,0,320,240)){
        for(int y=0;y<240;++y){pollInput();for(int x=0;x<320;++x)blendPixel(stageStaging,x,y,music_paper::paper,music_paper::veil(x,y));}
        stageStaging.pushSprite(&stageCanvas,0,0);
    }
    drawnSceneVersion=version;
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
    pollInput();
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
    canvas.setFont(music_fonts::small());canvas.setTextSize(11.f/12);
    float elapsed=playhead(s,stale);
    const char* lyric="暂无同步歌词";float lyricAt=-1;
    if(!s.hasState)lyric="等待音乐连接";
    else if(!strcmp(s.lyricStatus,"synced")){
        lyric="♪";
        for(int i=0;i<s.lyricCount;++i)if(s.lyrics[i].at<=elapsed){lyric=s.lyrics[i].text;lyricAt=s.lyrics[i].at;}
    }else if(!strcmp(s.lyricStatus,"loading"))lyric="正在找歌词…";
    else if(!strcmp(s.lyricStatus,"retrying"))lyric="歌词暂不可用";
    else if(!strcmp(s.lyricStatus,"instrumental"))lyric="纯音乐";
    int lyricOverflow=std::max(0,int(measureText(lyric))-metaWidth);
    int lyricScroll=std::min(lyricOverflow,int(std::max(0.f,elapsed-lyricAt-1.2f)*20));
    lyricMoving=!stale&&s.playing&&lyricOverflow>0&&lyricScroll<lyricOverflow;
    std::string key=std::string(s.track)+":"+std::to_string(lyricAt)+":"+lyric;
    // Placeholder/message keys do not depend on the advancing playhead.
    if(strcmp(s.lyricStatus,"synced"))key=std::string(s.track)+":"+lyric;
    lyricFade.select(key,millis());float alpha=lyricFade.opacity(millis());
    canvas.setClipRect(metaX,lyricTop,metaWidth,lyricHeight);
    if(alpha>0.f){
        if(alpha<1.f)canvas.readRectRGB(metaX,lyricTop,metaWidth,lyricHeight,lyricUnderlay);
        line(metaX-lyricScroll,lyricTop+int(5.f*(1.f-alpha)),lyric,dim);
        if(alpha<1.f)for(int y=0;y<lyricHeight;++y)for(int x=0;x<metaWidth;++x){
            if(x==0)pollInput();
            auto c=canvas.readPixelRGB(metaX+x,lyricTop+y);const auto& base=lyricUnderlay[y*metaWidth+x];
            canvas.drawPixel(metaX+x,lyricTop+y,canvas.color888(
                uint8_t(base.r+(c.r-base.r)*alpha),uint8_t(base.g+(c.g-base.g)*alpha),uint8_t(base.b+(c.b-base.b)*alpha)));
        }
    }
    canvas.clearClipRect();canvas.setTextSize(1);
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
            canvas.setClipRect(0,40,150,122);stageCanvas.pushSprite(&canvas,0,0);drawRecord(s,stale);canvas.clearClipRect();
            M5.Display.setClipRect(0,40,150,122);canvas.pushSprite(0,0);M5.Display.clearClipRect();
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
        strlcpy(drawnTrack,s.track,sizeof(drawnTrack));strlcpy(drawnArtId,s.artId,sizeof(drawnArtId));
        if(stageOK){
            cacheScene();
            stageCanvas.pushSprite(&canvas,0,0);
        }
        // Header: actual source, playback status and battery, sharing one baseline.
        paperPanel(8,4,29,29,14.5f,music_paper::paper,.55f);
        paperIcon(music_icons::back,14,10,music_paper::ink,17);
        canvas.setClipRect(44,0,sourceRight-44,35);
        paperText(44,3,s.source[0]?s.source:"Faces Music",14,ink,true);
        paperText(44,23,"MAC REMOTE",6,ink);
        canvas.clearClipRect();
        for(int x=8;x<312;++x)blendPixel(canvas,x,38,music_paper::ink,.6f);
        auto connection=music::connection(s.hasState,WiFi.status()==WL_CONNECTED,s.stale,s.available,
                                           millis()-connectionStarted,millis()-s.received);
        if(!s.hasState)strlcpy(s.title,music::connectionTitle(connection),sizeof(s.title));
        const char* status=busy.load()?"SENDING":s.notice[0]&&millis()-s.messageAt<4000?s.notice:
            connection!=music::Connection::Ready?music::connectionBadge(connection):s.playing?"PLAYING":"PAUSED";
        uint16_t accentColor=music_colors::rgb565(s.accentRgb);
        uint32_t accentInk=music_paper::accentInk(s.accentRgb);
        // Use the exact native font and scale used by Home battery rendering.
        canvas.setFont(music_fonts::small());canvas.setTextSize(1);
        xSemaphoreTake(stateLock,portMAX_DELAY);auto power=musicPower;xSemaphoreGive(stateLock);
        int statusRight=faces_power::drawBattery(canvas,power,powerRight,powerY,true)-powerGap;
        canvas.setFont(music_fonts::tiny());canvas.setTextSize(.7f);
        auto statusBounds=textMaskOK?buttonLabelBounds(status):LabelInkBounds{0,0,int(measureText(status)/.7f),10};
        int availableWidth=std::min(65,statusRight-statusLeft);
        float statusScale=std::min(.7f,float(availableWidth-8)/std::max(1,statusBounds.right-statusBounds.left));
        int statusWidth=std::min(availableWidth,int(ceilf((statusBounds.right-statusBounds.left)*statusScale))+8);
        int statusX=statusRight-statusWidth;
        paperPanel(statusX,12,statusWidth,14,7,s.accentRgb,1,0);
        canvas.setTextSize(statusScale);canvas.setClipRect(statusX+3,12,statusWidth-6,14);
        int statusTextX=int(lroundf(statusX+statusWidth/2.f-(statusBounds.left+statusBounds.right)*statusScale/2));
        int statusTextY=int(lroundf(19-(statusBounds.top+statusBounds.bottom)*statusScale/2));
        line(statusTextX,statusTextY,status,music_colors::rgb565(accentInk));canvas.clearClipRect();canvas.setTextSize(1);
        drawRecord(s,stale);
        canvas.setClipRect(metaX,44,metaWidth,118);
        paperText(metaX,46,"NOW PLAYING",7,dim);
        canvas.setFont(music_fonts::large());canvas.setTextSize(18.f/16);
        auto title=music::titleLines(s.title,metaWidth,[](const char* v){return measureText(v);});
        for(size_t i=0;i<title.size();++i)line(metaX,metaY-3+i*titleLine,title[i].c_str(),ink);
        int artistY=metaY+int(title.size())*titleLine+artistGap;
        paperText(metaX,artistY-2,s.artist,11,dim);
        heartTop=artistY+artistHeight+5;
        paperPanel(metaX,heartTop,54,20,10,music_paper::paper,.55f,1);
        paperIcon(s.favorite==1?music_icons::heart:music_icons::heartOutline,metaX+6,heartTop+3,s.favorite==1?0xef263f:music_paper::ink);
        paperText(metaX+23,heartTop+3,"喜欢",9,s.favorite==1?music_colors::rgb565(0xef263f):ink);
        lyricTop=145;
        canvas.clearClipRect();drawLyric(s,stale);
        float elapsed=playhead(s,stale);char timing[32];
        paperPanel(timelineX,timelineY,timelineWidth,timelineHeight,2,0xd1c5b2,1,0);
        int played=s.duration>0?std::max(0,std::min(timelineWidth,int(elapsed/s.duration*timelineWidth))):0;
        if(played)paperPanel(timelineX,timelineY,played,timelineHeight,2,s.accentRgb,1,0);
        canvas.setFont(music_fonts::small());canvas.setTextSize(8.f/12);
        snprintf(timing,sizeof(timing),"%d:%02d",int(elapsed)/60,int(elapsed)%60);line(10,timeY-2,timing,ink);
        snprintf(timing,sizeof(timing),"%d:%02d",int(s.duration)/60,int(s.duration)%60);line(310-measureText(timing),timeY-2,timing,ink);
        const char* repeatLabel=!stale&&!strcmp(s.repeatMode,"one")?"单曲循环":!stale&&!strcmp(s.repeatMode,"all")?"列表循环":nullptr;
        if(repeatLabel){int w=measureText(repeatLabel)+12;paperPanel((320-w)/2,176,w,15,7,s.accentRgb,1,0);line((320-w)/2+6,177,repeatLabel,music_colors::rgb565(accentInk));}
        const char* labels[]={"J","SPC","K","H","C","V"};
        const uint8_t* icons[]={music_icons::previous,s.playing?music_icons::pause:music_icons::play,music_icons::next,
            s.favorite==1?music_icons::heart:music_icons::heartOutline,music_icons::one,music_icons::all};
        const music::Action acts[]={music::Previous,music::Toggle,music::Next,music::Favorite,music::RepeatOne,music::RepeatAll};
        const bool modeKnown=!strcmp(s.repeatMode,"one")||!strcmp(s.repeatMode,"all")||!strcmp(s.repeatMode,"off");
        const char* mode=modeKnown?s.repeatMode:s.repeatRequested;
        for(int i=0;i<buttonCount;++i){
            bool enabled=!stale&&(s.actions&(1<<acts[i]));
            bool selected=(i==1)||(enabled&&((i==3&&s.favorite==1)||(i==4&&!strcmp(mode,"one"))||(i==5&&!strcmp(mode,"all"))));
            paperPanel(buttonX[i],buttonY,buttonWidth[i],buttonHeight,8,selected?s.accentRgb:music_paper::paper,selected?1.f:.7f);
            uint32_t fg=selected?accentInk:music_paper::ink;
            canvas.setFont(music_fonts::tiny());canvas.setTextSize(.8f);
            static LabelInkBounds labelBounds[buttonCount];
            static bool boundsReady=false;
            if(!boundsReady&&textMaskOK){for(int n=0;n<buttonCount;++n)labelBounds[n]=buttonLabelBounds(labels[n]);boundsReady=true;}
            const auto bounds=labelBounds[i];
            float labelWidth=boundsReady?(bounds.right-bounds.left)*.8f:measureText(labels[i]);
            int left=int(lroundf(buttonX[i]+(buttonWidth[i]-(13+4+labelWidth))/2.f));
            const int iconY=buttonY+(buttonHeight-13)/2;
            paperIcon(icons[i],left,iconY,fg);
            int labelX=int(lroundf(left+17-bounds.left*.8f));
            int labelY=int(lroundf(iconY+6.5f-(bounds.top+bounds.bottom)*.4f));
            line(labelX,labelY,labels[i],music_colors::rgb565(fg));
        }
        canvas.setTextSize(1);

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
#ifdef FACES_SUITE
    suite::open(suite::App::Connection);
#else
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
#endif
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
#ifdef FACES_SUITE
    faces_network::begin(false); // Interactive controller: avoid modem-sleep latency.
    if(!faces_network::hasWifi()||!faces_network::hasBridge())suite::open(suite::App::Connection);
    if(xTaskCreatePinnedToCore(worker,"music-bridge",16384,nullptr,1,nullptr,0)!=pdPASS)message("无法启动连接任务");
#else
    prefs.begin("faces-music",false);
    ssid=prefs.getString("ssid");password=prefs.getString("pass");host=prefs.getString("host");token=prefs.getString("token");bridgePort=prefs.getUShort("port",8766);
    // Reuse only Wi-Fi from the radio namespace; never mutate its configuration.
    if(ssid.isEmpty()){Preferences radio;radio.begin("faces-radio",true);ssid=radio.getString("ssid");password=radio.getString("pass");radio.end();}
    // Associate Wi-Fi and fetch state while fonts, sprites and keyboard initialize.
    WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
    if(!ssid.isEmpty())WiFi.begin(ssid.c_str(),password.c_str());
    if(!host.isEmpty()&&!token.isEmpty()&&!ssid.isEmpty())
        if(xTaskCreatePinnedToCore(worker,"music-bridge",16384,nullptr,1,nullptr,0)!=pdPASS)message("无法启动连接任务");
#endif
    faces_lights::begin(); // Sends black; ambient flow needs an explicit L press.
    M5.Display.setRotation(1);M5.Display.setBrightness(85);
    canvas.setColorDepth(16);if(!canvas.createSprite(320,240)){while(true)delay(1000);}
    canvas.setTextWrap(false);
    textMask.setColorDepth(16);textMaskOK=textMask.createSprite(768,32)!=nullptr;
    bool fontsOK=music_fonts::begin();
    coverCanvas.setColorDepth(16);coverCanvasOK=coverCanvas.createSprite(144,144)!=nullptr;
    coverCanvas.setPivot(72,72);
    coverStaging.setColorDepth(16);coverCanvasOK=coverStaging.createSprite(144,144)!=nullptr&&coverCanvasOK;
    Serial.printf("RENDER smooth_fonts=%d cover_cache=%d psram_free=%u\n",fontsOK,coverCanvasOK,ESP.getFreePsram());
    stageCanvas.setColorDepth(16);stageOK=stageCanvas.createSprite(320,240)!=nullptr;
    stageStaging.setColorDepth(16);stageOK=stageStaging.createSprite(320,240)!=nullptr&&stageOK;
    if(stageOK)stageCanvas.fillSprite(bg);
    if(!M5.In_I2C.isEnabled())M5.In_I2C.begin(I2C_NUM_1,12,11);
    keyboardOK=connectKeyboard();
#ifndef FACES_SUITE
    if(host.isEmpty()||token.isEmpty()||ssid.isEmpty())startPortal();
#endif
    Serial.println("BOOT faces-music paper-v1 / speaker disabled / LED GPIO5 off until L");draw();inputPumpReady=true;
}
void loop(){
    // USB-only frame diagnostic: inspect the actual composed LCD buffer.
    static String diagnostic;
    while(Serial.available()){
        char c=Serial.read();
        if(c=='\n'){
#ifdef FACES_SUITE
            if(diagnostic=="HOME OPEN")suite::open(suite::App::Home);
#endif
            if(diagnostic=="MUSIC FRAME"&&faces_screen::appVisible()){
                draw();Serial.println("FACES_FRAME 320 240 RGB888");
                static lgfx::bgr888_t row[320];static uint8_t rgb[320*3];
                for(int y=0;y<240;++y){canvas.readRectRGB(0,y,320,1,row);for(int x=0;x<320;++x){rgb[x*3]=row[x].r;rgb[x*3+1]=row[x].g;rgb[x*3+2]=row[x].b;}Serial.write(rgb,sizeof(rgb));}
                Serial.flush();
            }
            diagnostic="";
        }else if(c!='\r'){if(diagnostic.length()<32)diagnostic+=c;else diagnostic="invalid";}
    }
    pollInput();auto now=millis();
    if(powerReadAt==0||now-powerReadAt>=1000){
        powerReadAt=now;auto power=faces_power::read();
        xSemaphoreTake(stateLock,portMAX_DELAY);musicPower=power;xSemaphoreGive(stateLock);
        dirty=true;
    }
    if(setupMode)portal.handleClient();
    if(rebootAt && int32_t(now-rebootAt)>=0){
        faces_lights::off();
#ifdef FACES_SUITE
        suite::open(suite::App::Music);
#else
        ESP.restart();
#endif
    }
    InputEventQueue::Event event;
    while(pendingInput.pop(event)){
        if(!event.touch){
            auto k=event.key;
#ifdef FACES_SUITE
            if(k==KEYBOARD3_KEY_ESC||k=='q'||k=='Q')suite::open(suite::App::Home);
#endif
            if(k=='l'||k=='L'){if(!setupMode)message("",faces_lights::toggle(millis()));}
            else if(k=='s'||k=='S'){faces_lights::off();if(!setupMode)startPortal();}
            else submit(music::key(k));
        }else{
#ifdef FACES_SUITE
            if((!setupMode&&music::homeTouch(event.x,event.y))||
               (setupMode&&event.x>=0&&event.x<28&&event.y>=0&&event.y<28))suite::open(suite::App::Home);
#endif
            if(!setupMode)submit(music::touch(event.x,event.y,heartTop));dirty=true;
        }
    }
    if(now-lastHealth>5000){lastHealth=now;uint8_t id=0;keyboardOK=keyboardOK?keyboard.getModelID(&id)==M5FACES_OK&&id==M5Faces_Keyboard3::MODEL_ID:connectKeyboard();}
    if(!setupMode){
        xSemaphoreTake(stateLock,portMAX_DELAY);
        auto base=state.lightBase;bool playing=state.playing,available=state.available,stale=state.stale;
        uint32_t age=millis()-state.received;
        xSemaphoreGive(stateLock);
        faces_lights::tick(millis(),base,playing,available,stale,WiFi.status()==WL_CONNECTED,age);
    }
    bool animating=!setupMode&&(recordMoving||lyricMoving||lyricFade.active(now));
    // Battery/command acknowledgements must not insert a slow full-page render
    // halfway through the 1.05 s record motion. Keep dirty pending until it ends.
    // A real track/cover/background replacement still needs a complete frame.
    bool recordOnly=false;
    if(recordMoving&&!setupMode&&stageOK){
        xSemaphoreTake(stateLock,portMAX_DELAY);
        recordOnly=!strcmp(drawnTrack,state.track)&&!strcmp(drawnArtId,state.artId)
            &&drawnSceneVersion==sceneVersion&&cachedCoverVersion==coverVersion;
        xSemaphoreGive(stateLock);
    }
    if((!recordOnly&&dirty.load())||now-lastDraw>(animating?33:200)){
        lastDraw=now;draw(recordOnly||(animating&&!dirty.load()&&now-lastFullDraw<500));
    }
    faces_screen::render();delay(2);
}

#ifdef FACES_SUITE
} // namespace cloud_app
#endif

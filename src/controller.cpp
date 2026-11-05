#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <cJSON.h>
#include <atomic>
#include <esp_heap_caps.h>
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
#include "MusicShelf.h"
#include "MusicShelfCache.h"
#include "MusicShelfMotion.h"
#include "MusicShelfGeometry.h"
#include "MusicShelfAssets.h"
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
int contentPressed=-1;uint32_t contentPressedUntil=0;
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
uint32_t shelfUpdateUs=0,shelfKeyboardUs=0,shelfInputPolls=0;
bool shelfActiveForInput();
void captureShelfTouch(int x,int y,bool down,bool began,bool ended,bool enabled,uint32_t now);
void pollInput(){
    if(!inputPumpReady)return;
    bool onShelf=shelfActiveForInput();
    auto now=millis();if(now-lastPoll<(onShelf?8:5))return;lastPoll=now;
    uint32_t inputAt=micros();M5.update();if(onShelf){shelfUpdateUs+=micros()-inputAt;++shelfInputPolls;}if(faces_screen::tick()){dirty=true;lastDraw=0;}
    input_diagnostics::sample("music",faces_screen::touchEnabled());
    if(keyboardOK){
        uint8_t raw[10]={};uint32_t keyboardAt=micros();
        if(keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK)input.feed(raw,now);
        if(onShelf)shelfKeyboardUs+=micros()-keyboardAt;
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
    if(shelfActiveForInput()){
#ifdef FACES_SUITE
        if(!suite::inputReady())return;
#endif
        captureShelfTouch(t.x,t.y,t.isPressed(),t.wasPressed(),t.wasReleased(),faces_screen::touchEnabled(),now);
        return;
    }
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
struct Command { music::Action action; char track[32], request[40]; bool playlist=false; uint32_t shelfEpoch=0; };
struct ShelfEntry { std::string id,title; int tracks=0; };
struct ShelfArt { std::string id; std::vector<uint8_t> bytes; uint32_t retryAt=0,used=0; };
struct ShelfState {
    std::vector<ShelfEntry> items;
    ShelfArt art[music_shelf::jpegCapacity];
    size_t selected=0; bool loading=true,fetch=true,refresh=false,truncated=false,stale=false;
    uint32_t pollAt=0,epoch=0,revision=0;int anchor=0,direction=1;
    std::string selectedId,error;
};
ShelfState shelf;
std::atomic<bool> shelfOpen{false};
std::atomic<uint32_t> shelfPlayed{0};
Preferences shelfPrefs;
music_shelf::Motion shelfMotion;
music_shelf::Gesture shelfGesture;
music_shelf::TouchQueue shelfTouches;
bool shelfFingerDown=false,shelfIgnoreEntryTouch=false;
bool shelfReturnReady=false,shelfEntryFirst=false,shelfEntryWarm=false,shelfNormalFrame=false;
uint16_t* shelfBeforePlay=nullptr;
uint32_t shelfBeforePlayEpoch=0,shelfBeforePlayRevision=0,shelfShownRevision=0;
size_t shelfBeforePlaySelected=0,shelfShownSelected=0;
void releaseShelfPlaySnapshot(){if(shelfBeforePlay)heap_caps_free(shelfBeforePlay);shelfBeforePlay=nullptr;}
uint32_t shelfReturnRevision=0,shelfReturnEpoch=0,shelfEntryAt=0;
size_t shelfReturnSelected=0;
void logShelfVisible(){if(!shelfEntryFirst)return;Serial.printf("SHELF_OPEN mode=%s first_visible_us=%lu\n",shelfEntryWarm?"warm":"cold",(unsigned long)(micros()-shelfEntryAt));shelfEntryFirst=false;}
uint32_t shelfUiRevision=UINT32_MAX;
M5Canvas shelfBackdrop(&M5.Display),shelfDecor(&M5.Display),shelfDecoder(&M5.Display);
struct ShelfPixels { std::string id;uint16_t* pixels=nullptr;bool attempted=false,ready=false;uint32_t used=0; };
ShelfPixels shelfPixels[music_shelf::decodedCapacity];
bool shelfRendererReady=false,shelfRawSwapped=true,shelfDecorValid=false;
std::string shelfDecorKey;
uint32_t shelfDecodeCount=0,shelfDecodeUs=0,shelfDecodeMaxUs=0;
struct ShelfPerf {uint32_t at=0,frames=0,total=0,max=0,raster=0,push=0,decor=0,pump=0,overBudget=0;} shelfPerf;
bool beginShelfRenderer();
void advanceShelf(uint32_t now);
bool shelfActiveForInput(){return shelfOpen.load();}
void captureShelfTouch(int x,int y,bool down,bool began,bool ended,bool enabled,uint32_t now){
    if(shelfIgnoreEntryTouch){if(!down)shelfIgnoreEntryTouch=false;return;}
    if(!enabled){if(shelfFingerDown)shelfTouches.push({music_shelf::TouchEvent::Cancel,int16_t(x),int16_t(y),now});shelfFingerDown=false;return;}
    if(down&&!shelfFingerDown){shelfFingerDown=true;shelfTouches.push({music_shelf::TouchEvent::Down,int16_t(x),int16_t(y),now});}
    else if(down)shelfTouches.push({music_shelf::TouchEvent::Move,int16_t(x),int16_t(y),now});
    if((ended||!down)&&shelfFingerDown){shelfFingerDown=false;shelfTouches.push({music_shelf::TouchEvent::Up,int16_t(x),int16_t(y),now});}
}


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
// The UI only changes intent. All HTTP and JPEG retrieval run in the worker.
void shelfWork() {
    if(!shelfOpen.load())return;
    uint32_t epoch;bool fetch=false,refresh=false;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    epoch=shelf.epoch;
    if(shelf.fetch&&int32_t(millis()-shelf.pollAt)>=0){fetch=true;refresh=shelf.refresh;shelf.refresh=false;}
    xSemaphoreGive(stateLock);
    if(fetch){
        std::vector<uint8_t> bytes;
        bool ok=get(refresh?"/api/playlists?refresh=1":"/api/playlists",bytes,256000);
        bytes.push_back(0);auto j=ok?cJSON_Parse(reinterpret_cast<char*>(bytes.data())):nullptr;
        std::vector<ShelfEntry> entries;bool valid=j&&flag(j,"ok")&&cJSON_IsArray(field(j,"items"));
        bool truncated=false;
        if(valid){
            const cJSON* item=nullptr;
            cJSON_ArrayForEach(item,field(j,"items")){
                const char* id=text(item,"id");
                if(!music_shelf::validId(id))continue;
                if(entries.size()>=music_shelf::maxItems){truncated=true;break;}
                std::string title=text(item,"title");
                if(title.size()>384){size_t end=384;while(end&&(static_cast<unsigned char>(title[end])&0xc0)==0x80)--end;title.resize(end);}
                entries.push_back({id,title,int(value(item,"track_count"))});
            }
        }
        xSemaphoreTake(stateLock,portMAX_DELAY);
        if(shelfOpen.load()&&shelf.epoch==epoch){
            shelf.loading=j&&flag(j,"loading");shelf.stale=(j&&flag(j,"stale"))||(!valid&&!shelf.items.empty());shelf.fetch=shelf.loading;shelf.pollAt=millis()+1200;
            if(valid){
                shelf.items=std::move(entries);shelf.selected=0;shelf.truncated=truncated;shelf.error=shelf.stale?text(j,"message"):"";
                shelf.selected=music_shelf::restoredSelection(shelf.items,shelf.selectedId);
                if(!shelf.items.empty())shelf.selectedId=shelf.items[shelf.selected].id;
                shelf.anchor=int(shelf.selected);++shelf.revision;
            }else shelf.error=j&&text(j,"message")[0]?text(j,"message"):"无法读取歌单，请检查 Mac";
            dirty=true;
        }
        xSemaphoreGive(stateLock);
        if(j)cJSON_Delete(j);
        return; // Give normal playback metadata a chance before images.
    }
    // Keep recently viewed JPEGs; protect visible + directional lookahead IDs.
    std::string needed[music_shelf::prefetchCount],download;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    if(!shelf.items.empty()){
        for(size_t i=0;i<music_shelf::prefetchCount;++i)
            needed[i]=shelf.items[music_shelf::modulo(shelf.anchor+music_shelf::prefetchOffset(i,shelf.direction),int(shelf.items.size()))].id;
        for(auto& id:needed){
            auto* found=music_shelf::cacheSlot(shelf.art,id,needed);if(!found)continue;
            if(found->id!=id){*found=ShelfArt{};found->id=id;}
            found->used=millis();
            if(download.empty()&&found->bytes.empty()&&int32_t(millis()-found->retryAt)>=0){download=id;found->retryAt=millis()+3000;}
        }
    }
    xSemaphoreGive(stateLock);
    if(download.empty())return;
    std::vector<uint8_t> bytes;
    if(get("/api/playlist-artwork/"+String(download.c_str()),bytes,40000)){
        xSemaphoreTake(stateLock,portMAX_DELAY);
        if(shelfOpen.load()&&shelf.epoch==epoch)for(auto& art:shelf.art)if(art.id==download){art.bytes=std::move(bytes);dirty=true;break;}
        xSemaphoreGive(stateLock);
    }
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
            if(cmd.playlist)cJSON_AddStringToObject(j,"id",cmd.track);
            else {cJSON_AddStringToObject(j,"action",music::name(cmd.action));cJSON_AddStringToObject(j,"track_id",cmd.track);}
            cJSON_AddStringToObject(j,"request_id",cmd.request);
            char* json=cJSON_PrintUnformatted(j);
            std::vector<uint8_t> bytes;
            uint32_t actionAt=millis();const char* outcome="unconfirmed";
            bool ok=json && get(cmd.playlist?"/api/playlists/play":"/api/action",bytes,2048,String(json));
            if(json)cJSON_free(json); cJSON_Delete(j);
            if(ok){
                bytes.push_back(0); j=cJSON_Parse(reinterpret_cast<char*>(bytes.data()));
                if(j){
                    if(flag(j,"ok")){
                        if(cmd.playlist)shelfPlayed.store(cmd.shelfEpoch);
                        const char* badge=cmd.action==music::RepeatOne?"ONE SENT":cmd.action==music::RepeatAll?"ALL SENT":"";
                        message("",badge);outcome="accepted";
                    }
                    else{bool uncertain=!strcmp(text(j,"code"),"uncertain");message(cmd.playlist?text(j,"message"):text(j,"error"),uncertain?"UNCONFIRMED":"CHECK MAC");outcome=uncertain?"unconfirmed":"rejected";}
                    cJSON_Delete(j);
                }
                else message("返回无效，请检查 Mac");
            } else message("结果未知，请检查 Mac；未重试","UNCONFIRMED");
            Serial.printf("ACTION action=%s outcome=%s duration_ms=%lu\n",music::name(cmd.action),outcome,(unsigned long)(millis()-actionAt));
            if(cmd.playlist&&strcmp(outcome,"accepted")){
                auto failed=snapshot();xSemaphoreTake(stateLock,portMAX_DELAY);
                if(shelf.epoch==cmd.shelfEpoch)shelf.error=failed.message;
                xSemaphoreGive(stateLock);
            }
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
        shelfWork();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
void syncShelfSelection(){
    xSemaphoreTake(stateLock,portMAX_DELAY);
    if(shelfUiRevision==shelf.revision&&!shelf.items.empty()){
        shelf.selected=music_shelf::modulo(music_shelf::nearest(shelfMotion.position),int(shelf.items.size()));
        shelf.anchor=shelfMotion.anchor;
        float travel=shelfMotion.target-shelfMotion.position;
        if(fabsf(travel)>.001f||fabsf(shelfMotion.velocity)>.01f)shelf.direction=(fabsf(travel)>.001f?travel:shelfMotion.velocity)<0?-1:1;
        shelf.selectedId=shelf.items[shelf.selected].id;
    }
    xSemaphoreGive(stateLock);
}
void advanceShelf(uint32_t now){
    if(!shelfOpen.load())return;
    xSemaphoreTake(stateLock,portMAX_DELAY);auto revision=shelf.revision;auto selected=shelf.selected;xSemaphoreGive(stateLock);
    if(shelfUiRevision!=revision){
        shelfUiRevision=revision;shelfGesture.cancel(shelfMotion,now);shelfMotion.reset(selected,now);dirty=true;
    }
    if(shelfMotion.tick(now))dirty=true;
    static uint32_t spinAt=0;
    xSemaphoreTake(stateLock,portMAX_DELAY);bool loading=shelf.loading;xSemaphoreGive(stateLock);
    if(loading&&uint32_t(now-spinAt)>=80){spinAt=now;dirty=true;}
    syncShelfSelection();
}
void openShelf(){
    if(setupMode||busy.load())return;
    if(!shelfEntryFirst){shelfEntryAt=micros();shelfEntryFirst=true;}
    shelfEntryWarm=false;
    // Allocation occurs once on entry, never in the per-frame rasterizer.
    Serial.printf("SHELF_ENTER before_renderer stack_free=%u psram_free=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)),ESP.getFreePsram());
    bool rendererOK=beginShelfRenderer();
    Serial.printf("SHELF_ENTER renderer=%d stack_free=%u psram_free=%u\n",rendererOK,unsigned(uxTaskGetStackHighWaterMark(nullptr)),ESP.getFreePsram());
    xSemaphoreTake(stateLock,portMAX_DELAY);
    bool restore=music_shelf::canRestoreFrame(shelfReturnReady,shelfReturnEpoch,shelf.epoch,shelfReturnRevision,shelf.revision,shelfReturnSelected,shelf.selected);
    ++shelf.epoch;if(!shelf.epoch)++shelf.epoch;
    shelf.error.clear();shelf.fetch=shelf.items.empty();shelf.loading=shelf.fetch;shelf.pollAt=0;
    shelfMotion.reset(shelf.selected,millis());shelf.anchor=shelfMotion.anchor;shelfUiRevision=shelf.revision;
    shelfOpen=true;
    xSemaphoreGive(stateLock);
    shelfTouches.clear();shelfFingerDown=false;shelfGesture={};
    if(restore&&rendererOK){
        memcpy(canvas.getBuffer(),shelfDecor.getBuffer(),320*240*2);
        canvas.pushSprite(0,0);shelfEntryWarm=true;logShelfVisible();
    }else shelfDecorValid=false;
    shelfReturnReady=false;dirty=true;
}
void closeShelf(bool afterPlay=false){
    xSemaphoreTake(stateLock,portMAX_DELAY);
    bool beforePlayMatches=shelfBeforePlay&&shelfBeforePlayEpoch==shelf.epoch&&shelfBeforePlayRevision==shelf.revision&&shelfBeforePlaySelected==shelf.selected;
    bool shownMatches=shelfShownRevision==shelf.revision&&shelfShownSelected==shelf.selected;
    auto capturedRevision=shelf.revision;auto capturedSelected=shelf.selected;
    xSemaphoreGive(stateLock);
    bool reusable=shelfOpen.load()&&shelfDecorValid&&(afterPlay?beforePlayMatches:(shelfNormalFrame&&shownMatches&&!busy.load()&&!shelfMotion.moving()&&!shelfGesture.active));
    if(reusable)memcpy(shelfDecor.getBuffer(),afterPlay?shelfBeforePlay:canvas.getBuffer(),320*240*2);
    releaseShelfPlaySnapshot();
    shelfGesture.cancel(shelfMotion,millis());shelfMotion.reset(music_shelf::nearest(shelfMotion.position),millis());syncShelfSelection();
    xSemaphoreTake(stateLock,portMAX_DELAY);
    shelfOpen=false;++shelf.epoch;auto id=shelf.selectedId;
    shelfReturnReady=reusable&&capturedRevision==shelf.revision&&capturedSelected==shelf.selected;shelfReturnRevision=shelf.revision;shelfReturnEpoch=shelf.epoch;shelfReturnSelected=shelf.selected;
    xSemaphoreGive(stateLock);
    shelfTouches.clear();shelfFingerDown=false;
    if(!id.empty()&&shelfPrefs.getString("selected")!=id.c_str())shelfPrefs.putString("selected",id.c_str());
    recordMoving=recordReveal!=recordTarget;recordStarted=millis();lyricMoving=false;dirty=true;lastFullDraw=0;
}
void returnShelfHome(){
    closeShelf();
#ifdef FACES_SUITE
    suite::open(suite::App::Home);
#endif
}
void moveShelf(int direction){
    if(busy.load()||!shelfRendererReady)return;
    xSemaphoreTake(stateLock,portMAX_DELAY);auto count=shelf.items.size();xSemaphoreGive(stateLock);
    if(count<2)return;
    shelfGesture.cancel(shelfMotion,millis());shelfMotion.move(direction,millis());dirty=true;
}
void refreshShelf(){
    if(busy.load())return;
    xSemaphoreTake(stateLock,portMAX_DELAY);bool loading=shelf.loading;xSemaphoreGive(stateLock);
    if(loading)return;
    shelfGesture.cancel(shelfMotion,millis());shelfMotion.reset(music_shelf::nearest(shelfMotion.position),millis());syncShelfSelection();
    xSemaphoreTake(stateLock,portMAX_DELAY);
    shelf.fetch=true;shelf.refresh=true;shelf.loading=true;shelf.pollAt=0;shelf.error.clear();
    xSemaphoreGive(stateLock);dirty=true;
}
void playShelf(){
    if(!shelfRendererReady||shelfMotion.moving()||shelfGesture.active)return;
    if(WiFi.status()!=WL_CONNECTED){xSemaphoreTake(stateLock,portMAX_DELAY);shelf.error="等待 Wi-Fi 连接";xSemaphoreGive(stateLock);dirty=true;return;}
    if(busy.exchange(true))return;
    Command cmd{};cmd.playlist=true;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    if(shelfUiRevision!=shelf.revision||shelf.items.empty()||shelf.loading||shelf.stale||!shelf.error.empty()){xSemaphoreGive(stateLock);busy=false;dirty=true;return;}
    strlcpy(cmd.track,shelf.items[shelf.selected].id.c_str(),sizeof(cmd.track));cmd.shelfEpoch=shelf.epoch;
    bool canSave=shelfNormalFrame&&shelfShownRevision==shelf.revision&&shelfShownSelected==shelf.selected;
    shelfBeforePlayEpoch=shelf.epoch;shelfBeforePlayRevision=shelf.revision;shelfBeforePlaySelected=shelf.selected;
    xSemaphoreGive(stateLock);
    releaseShelfPlaySnapshot();
    if(canSave){
        shelfBeforePlay=static_cast<uint16_t*>(heap_caps_malloc(320*240*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(shelfBeforePlay)memcpy(shelfBeforePlay,canvas.getBuffer(),320*240*2);
    }
    snprintf(cmd.request,sizeof(cmd.request),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
    if(xQueueSend(queue,&cmd,0)!=pdTRUE){busy=false;releaseShelfPlaySnapshot();}
    dirty=true;
}
void shelfTap(int x,int y){
    auto hit=music_shelf::touch(x,y);
    if(hit==music_shelf::Back){returnShelfHome();return;}
    if(hit==music_shelf::Refresh){refreshShelf();return;}
    if(shelfMotion.moving()||busy.load())return;
    xSemaphoreTake(stateLock,portMAX_DELAY);auto count=shelf.items.size();xSemaphoreGive(stateLock);
    if(!count){if(x>=105&&x<215&&y>=151&&y<180)refreshShelf();return;}
    if(hit==music_shelf::Previous){moveShelf(-1);return;}
    if(hit==music_shelf::Next){moveShelf(1);return;}
    if(hit==music_shelf::Play){playShelf();return;}
    // Test the visible projected cover, front to back; blank gaps never play.
    for(int offset: {0,-1,1}){
        if(count==1&&offset)continue;
        auto geometry=music_shelf::geometry(offset+shelfMotion.anchor-shelfMotion.position);
        if(music_shelf::contains(geometry,float(x),float(y))){if(!offset)playShelf();else moveShelf(offset);return;}
    }
}
void handleShelfTouches(){
    music_shelf::TouchEvent e;
    while(shelfTouches.pop(e)){
        if(!shelfOpen.load())continue;
        if(e.phase==music_shelf::TouchEvent::Down){
            xSemaphoreTake(stateLock,portMAX_DELAY);bool allowed=shelf.items.size()>1&&!shelf.loading;xSemaphoreGive(stateLock);
            bool flow=allowed&&shelfRendererReady&&!busy.load()&&e.y>=40&&e.y<175;
            shelfGesture.begin(e.x,e.y,e.at,flow,shelfMotion);dirty=true;
        }else if(e.phase==music_shelf::TouchEvent::Move){
            shelfGesture.update(e.x,e.y,e.at,shelfMotion);if(shelfGesture.moved)dirty=true;
        }else{
            int x=shelfGesture.startX,y=shelfGesture.startY;
            bool tap=shelfGesture.finish(e.x,e.y,e.at,shelfMotion,e.phase==music_shelf::TouchEvent::Cancel);
            syncShelfSelection();dirty=true;if(tap)shelfTap(x,y);
        }
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
                coverage+=(music_shelf::swap16(static_cast<const uint16_t*>(textMask.getBuffer())[sy*768+sx])&31)/31.f*weight;
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
LabelInkBounds buttonLabelBounds(const char* text,const lgfx::IFont* font=music_fonts::tiny()){
    // Cache by caller: measure visible glyph alpha, not font advance/ascender.
    textMask.fillSprite(0);textMask.setFont(font);textMask.setTextSize(1);
    textMask.setTextColor(0xffff);textMask.setCursor(0,0);textMask.print(text);
    LabelInkBounds bounds{768,32,0,0};
    int width=std::min(768,int(textMask.textWidth(text))),height=std::min(32,int(textMask.fontHeight()));
    auto* raw=static_cast<const uint16_t*>(textMask.getBuffer());
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)if((music_shelf::swap16(raw[y*768+x])&31)>2){
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
void drawContentPill(int x,int y,const uint8_t* icon,const char* label,uint32_t foreground,int index){
    bool pressed=contentPressed==index&&int32_t(millis()-contentPressedUntil)<0;
    int top=y+(pressed?1:0);
    paperPanel(x,top,music_content::width,music_content::height,10,pressed?0xeadbc3:music_paper::paper,pressed?1.f:.55f,1);
    canvas.setFont(music_fonts::small());canvas.setTextSize(9.f/12);int textWidth=measureText(label);canvas.setTextSize(1);
    int left=x+(music_content::width-13-4-textWidth)/2;
    paperIcon(icon,left,top+3,foreground,13);paperText(left+17,top+3,label,9,music_colors::rgb565(foreground));
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
bool beginShelfRenderer(){
    if(shelfRendererReady)return true;
    for(auto& cache:shelfPixels)if(!cache.pixels)cache.pixels=static_cast<uint16_t*>(heap_caps_malloc(160*160*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    auto sprite=[](M5Canvas& target,int w,int h){target.setPsram(true);target.setColorDepth(16);return target.createSprite(w,h)!=nullptr;};
    bool ok=sprite(shelfBackdrop,320,240)&&sprite(shelfDecor,320,240)&&sprite(shelfDecoder,160,160);
    for(const auto& cache:shelfPixels)ok=ok&&cache.pixels;
    if(!ok){
        for(auto& cache:shelfPixels){if(cache.pixels)heap_caps_free(cache.pixels);cache=ShelfPixels{};}
        shelfBackdrop.deleteSprite();shelfDecor.deleteSprite();shelfDecoder.deleteSprite();
        if(Serial.availableForWrite()>80)Serial.println("SHELF renderer unavailable: PSRAM allocation failed");
        return false;
    }
    // LGFX's 16-bit sprite storage is byte-swapped on this target; verify instead of assuming.
    shelfDecoder.drawPixel(0,0,0xf800);shelfRawSwapped=static_cast<uint16_t*>(shelfDecoder.getBuffer())[0]!=0xf800;
    canvas.clearClipRect();canvas.fillSprite(bg);
    for(int x=0;x<320;x+=6)for(int y=0;y<240;++y)blendPixel(canvas,x,y,0x111000,.02f);
    // Halo is flattened into the background before any card/shadow is composited.
    for(int y=33;y<173;++y){pollInput();for(int x=90;x<230;++x){float dx=x+.5f-160,dy=y+.5f-103;
        float coverage=std::max(0.f,std::min(1.f,70.5f-std::sqrt(dx*dx+dy*dy)));
        if(coverage>0)blendPixel(canvas,x,y,0xffd21f,.32f*coverage);
    }}
    paperPanel(7,5,29,29,14.5f,music_paper::paper,0,1.5f);paperIcon(music_icons::back,13,11,music_paper::ink,17);
    auto alphaMask=[](int x,int y,int width,int height,const uint8_t* data){
        for(int row=0;row<height;++row){pollInput();for(int col=0;col<width;++col)if(auto a=data[row*width+col])blendPixel(canvas,x+col,y+row,music_paper::ink,a/255.f);}
    };
    alphaMask(43,6,music_shelf_assets::collectionWidth,music_shelf_assets::collectionHeight,music_shelf_assets::collection);
    alphaMask(44,25,music_shelf_assets::subtitleWidth,music_shelf_assets::subtitleHeight,music_shelf_assets::subtitle);
    canvas.fillRect(7,38,306,2,ink);
    memcpy(shelfBackdrop.getBuffer(),canvas.getBuffer(),320*240*2);
    shelfRendererReady=true;shelfDecorValid=false;shelfPerf.at=millis();
    Serial.printf("SHELF renderer ready cache_bytes=%u psram_free=%u\n",unsigned(music_shelf::decodedCapacity*160*160*2+2*320*240*2+160*160*2),ESP.getFreePsram());
    return true;
}
struct ShelfTitlePixels {std::string id;uint16_t* pixels=nullptr;uint32_t used=0;};
ShelfTitlePixels shelfTitles[9];
void paintShelfTitle(const std::string& caption,bool sending){
    memcpy(static_cast<uint16_t*>(canvas.getBuffer())+175*320,static_cast<uint16_t*>(shelfBackdrop.getBuffer())+175*320,39*320*2);
            canvas.fillRect(13,178,6,34,music_colors::rgb565(0xffd21f));canvas.setClipRect(28,177,279,37);
            canvas.setFont(music_fonts::large());canvas.setTextSize(15.f/16);
            struct TitleLayout{std::string caption;std::vector<std::string> lines;LabelInkBounds bounds[2];int origin=178;};
            static TitleLayout layouts[12];static unsigned nextLayout=0;TitleLayout* layout=nullptr;
            for(auto& cached:layouts)if(cached.caption==caption&&!cached.lines.empty()){layout=&cached;break;}
            if(!layout){layout=&layouts[nextLayout++%12];layout->caption=caption;
                layout->lines=music::titleLines(caption,279,[](const char* t){return measureText(t);});
                float inkTop=1000,inkBottom=-1000;
                for(size_t i=0;i<layout->lines.size();++i){layout->bounds[i]=buttonLabelBounds(layout->lines[i].c_str(),music_fonts::large());
                    inkTop=std::min(inkTop,float(i*18)+layout->bounds[i].top*15.f/16);inkBottom=std::max(inkBottom,float(i*18)+layout->bounds[i].bottom*15.f/16);}
                layout->origin=int(lroundf(195-(inkTop+inkBottom)/2));}
            for(size_t i=0;i<layout->lines.size();++i)line(28-int(layout->bounds[i].left*15.f/16),layout->origin+int(i)*18,layout->lines[i].c_str(),sending?dim:ink);
            canvas.clearClipRect();canvas.setTextSize(1);
}
bool shelfTitleCached(const std::string& caption){for(auto& c:shelfTitles)if(c.id==caption&&c.pixels)return true;return false;}
void drawCachedShelfTitle(const std::string& caption,bool sending,const std::string (&protectedTitles)[9]){
    std::string id=caption+(sending?"\1":"");
    for(auto& c:shelfTitles)if(c.id==id&&c.pixels){c.used=millis();memcpy(static_cast<uint16_t*>(canvas.getBuffer())+175*320,c.pixels,39*320*2);return;}
    paintShelfTitle(caption,sending);
    auto* cache=music_shelf::cacheSlot(shelfTitles,id,protectedTitles);if(!cache)return;
    if(!cache->pixels)cache->pixels=static_cast<uint16_t*>(heap_caps_malloc(39*320*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!cache->pixels){static bool reported=false;if(!reported){Serial.println("SHELF title cache unavailable");reported=true;}return;}
    cache->id=id;cache->used=millis();memcpy(cache->pixels,static_cast<uint16_t*>(canvas.getBuffer())+175*320,39*320*2);
}
void drawShelf(){
    uint32_t frameStarted=micros();
    if(!shelfRendererReady){
        canvas.fillSprite(bg);paperText(28,65,"唱片架内存不足",16,ink,true);
        paperText(28,103,"按 Q 返回，再按 P 重试",12,ink);canvas.pushSprite(0,0);return;
    }
    struct Visible{std::string id;music_shelf::Geometry geometry;ShelfPixels* cache=nullptr;};
    Visible visible[5];std::string wanted[music_shelf::prefetchCount],wantedTitles[music_shelf::prefetchCount];size_t count,index;bool loading,truncated,stale;std::string error,title;faces_power::State power;
    uint32_t epoch,revision;int anchor=shelfMotion.anchor;float position=shelfMotion.position;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    count=shelf.items.size();index=shelf.selected;loading=shelf.loading;error=shelf.error;truncated=shelf.truncated;stale=shelf.stale;power=musicPower;epoch=shelf.epoch;revision=shelf.revision;
    if(count){title=shelf.items[index].title;
        for(size_t i=0;i<music_shelf::prefetchCount;++i){const auto& item=shelf.items[music_shelf::modulo(anchor+music_shelf::prefetchOffset(i,shelf.direction),int(count))];wanted[i]=item.id;wantedTitles[i]=item.title;}
        for(int i=0;i<5;++i)if(count>1||i==2)visible[i].id=shelf.items[music_shelf::modulo(anchor+i-2,int(count))].id;
    }
    xSemaphoreGive(stateLock);
    // A list refresh may complete between input processing and composition. Wait for the UI's
    // revision reset rather than combining a previous fractional position with a new library.
    if(revision!=shelfUiRevision){dirty=true;return;}
    // Reserve desired entries before drawing; old non-neighbours remain available for reversal.
    for(const auto& id:wanted){
        if(id.empty())continue;auto* cache=music_shelf::cacheSlot(shelfPixels,id,wanted);if(!cache)continue;
        if(cache->id!=id){cache->id=id;cache->ready=cache->attempted=false;}
        cache->used=millis();
    }
    for(int i=0;i<5;++i){
        auto& card=visible[i];card.geometry=music_shelf::geometry(i-2+anchor-position);if(card.id.empty())continue;
        for(auto& cache:shelfPixels)if(cache.id==card.id){card.cache=&cache;break;}
    }
    // One decode per frame, centre then visible then directional lookahead. Same ID is
    // still protected in both caches; background responses cannot replace its pixels.
    for(const auto& id:wanted){
        if(shelfEntryFirst)break; // Visible loading/cache frame precedes optional JPEG decoding.
        if(id.empty())continue;ShelfPixels* cache=nullptr;
        for(auto& c:shelfPixels)if(c.id==id){cache=&c;break;}
        if(!cache||cache->attempted)continue;
        std::vector<uint8_t> bytes;
        xSemaphoreTake(stateLock,portMAX_DELAY);
        if(shelf.epoch==epoch&&shelf.revision==revision)for(auto& art:shelf.art)if(art.id==id){bytes=art.bytes;break;}
        xSemaphoreGive(stateLock);if(bytes.empty())continue;
        pollInput();uint32_t at=micros();shelfDecoder.fillSprite(bg);
        bool decoded=shelfDecoder.drawJpg(bytes.data(),bytes.size(),0,0,160,160);
        if(decoded)memcpy(cache->pixels,shelfDecoder.getBuffer(),160*160*2);
        cache->attempted=true;cache->ready=decoded;
        uint32_t cost=micros()-at;++shelfDecodeCount;shelfDecodeUs+=cost;shelfDecodeMaxUs=std::max(shelfDecodeMaxUs,cost);
        pollInput();dirty=true;break;
    }
    bool moving=shelfMotion.moving(),sending=busy.load();
    char countLabel[28];snprintf(countLabel,sizeof(countLabel),"%u / %u%s",unsigned(count?index+1:0),unsigned(count),truncated?"+":"");
    std::string caption=sending?"正在请求播放…":!error.empty()&&!loading?error:title;
    if(stale&&error.empty())caption="收藏已过期，按 R 刷新";
    if(WiFi.status()!=WL_CONNECTED)caption="等待 Wi-Fi 连接";
    int pressedFooter=-1;
    if(shelfGesture.active&&!shelfGesture.cancelled&&shelfGesture.startY>=214&&shelfGesture.startY<239)
        for(int i=0;i<4;++i)if(shelfGesture.startX>=music_shelf::footerX[i]&&shelfGesture.startX<music_shelf::footerX[i]+music_shelf::footerW[i])pressedFooter=i;
    std::string key=std::string(countLabel)+"|"+caption+"|"+std::to_string(power.percent)+"|"+std::to_string(int(faces_power::tint(power)))+
        "|"+std::to_string(power.valid)+std::to_string(power.present)+std::to_string(loading)+std::to_string(moving)+std::to_string(sending)+std::to_string(count==0)+"|"+std::to_string(pressedFooter);
    uint32_t decorAt=micros();
    static std::string previousHeader,previousTitle,previousFooter;
    std::string headerKey=std::string(countLabel)+"|"+std::to_string(power.percent)+"|"+std::to_string(int(faces_power::tint(power)))+std::to_string(power.valid)+std::to_string(power.present);
    std::string titleKey=caption+"|"+std::to_string(sending)+std::to_string(count==0)+std::to_string(loading);
    std::string footerKey=std::to_string(loading)+std::to_string(moving)+std::to_string(sending)+std::to_string(count<2)+std::to_string(stale)+std::to_string(!error.empty())+std::to_string(pressedFooter);
    bool headerChanged=!shelfDecorValid||headerKey!=previousHeader;
    bool titleChanged=!shelfDecorValid||titleKey!=previousTitle;
    bool footerChanged=!shelfDecorValid||footerKey!=previousFooter;
    if(!shelfDecorValid||key!=shelfDecorKey){
        auto restoreRows=[](int y,int h){memcpy(static_cast<uint16_t*>(canvas.getBuffer())+y*320,static_cast<uint16_t*>(shelfBackdrop.getBuffer())+y*320,h*320*2);};
        if(headerChanged){
        restoreRows(0,40);
        canvas.setFont(music_fonts::small());canvas.setTextSize(1);
        int batteryLeft=faces_power::drawBattery(canvas,power,312,13,true);
        if(count){canvas.setFont(music_fonts::small());canvas.setTextSize(7.f/12);int countWidth=measureText(countLabel)+10;canvas.setTextSize(1);
            int right=batteryLeft-4;
            paperPanel(right-countWidth,13,countWidth,14,7,0xffd21f,1,0);paperText(right-countWidth+5,14,countLabel,7,ink);}
        }
        if(titleChanged){
        // Empty/loading messages also occupy the flow region; clear their old ink.
        restoreRows(count?175:40,count?39:174);
        if(count){
            drawCachedShelfTitle(caption,sending,wantedTitles);
        }else{
            paperText(78,82,loading?"正在读取收藏":error.empty()?"暂无收藏歌单":"读取收藏失败",14,ink,true);
            canvas.setClipRect(24,108,272,36);paperText(24,111,error.empty()?"在网易云收藏后，按 R 刷新":error.c_str(),11,dim);canvas.clearClipRect();
            if(!loading){paperPanel(105,151,110,29,5,0xfaefdd,1,1.5f);paperText(126,158,"R 重新读取",11,ink);}
        }
        }
        if(footerChanged){
        restoreRows(214,26);
        bool disabled[]={sending||loading,sending||moving||count<2,sending||moving||loading||stale||!error.empty()||!count,sending||moving||count<2};
        const uint8_t* icons[]={music_shelf_assets::refresh,music_icons::previous,music_icons::play,music_icons::next};
        const char* labels[]={"R","J","SPC","K"};
        for(int i=0;i<4;++i){
            int x=music_shelf::footerX[i],w=music_shelf::footerW[i],top=214+(pressedFooter==i&&!disabled[i]?1:0);
            paperPanel(x,top,w,25,8,i==2?0xffd21f:0xfaefdd,1,1.5f);
            int labelWidth=i==2?17:6,left=x+(w-(13+5+labelWidth))/2;
            paperIcon(icons[i],left,top+6,music_paper::ink,13);paperText(left+18,top+6,labels[i],8,ink);
            if(disabled[i])for(int y=top+2;y<std::min(240,top+23);++y)for(int px=x+2;px<x+w-2;++px)blendPixel(canvas,px,y,music_paper::paper,.45f);
        }
        }
        previousHeader=std::move(headerKey);previousTitle=std::move(titleKey);previousFooter=std::move(footerKey);
        auto saveRows=[](int y,int h){memcpy(static_cast<uint16_t*>(shelfDecor.getBuffer())+y*320,static_cast<uint16_t*>(canvas.getBuffer())+y*320,h*320*2);};
        if(headerChanged)saveRows(0,40);if(titleChanged)saveRows(count?175:40,count?39:174);if(footerChanged)saveRows(214,26);
        shelfDecorKey=std::move(key);shelfDecorValid=true;
    }
    if(loading){
        memcpy(static_cast<uint16_t*>(canvas.getBuffer())+214*320,static_cast<uint16_t*>(shelfDecor.getBuffer())+214*320,26*320*2);
        // Rotate only the same 13px ArrowClockwise mask; R remains visible and stationary.
        int left=music_shelf::footerX[0]+(music_shelf::footerW[0]-24)/2,top=220;
        canvas.fillRect(left-3,top-3,19,19,music_colors::rgb565(0xfaefdd));
        float angle=(millis()%800)*6.28318530718f/800,cs=cosf(angle),sn=sinf(angle);
        for(int yy=-3;yy<16;++yy)for(int xx=-3;xx<16;++xx){
            float dx=xx-6.f,dy=yy-6.f,sx=cs*dx+sn*dy+6.f,sy=-sn*dx+cs*dy+6.f;
            int ix=int(lroundf(sx)),iy=int(lroundf(sy));
            if(ix>=0&&ix<13&&iy>=0&&iy<13)blendPixel(canvas,left+xx,top+yy,music_paper::ink,music_shelf_assets::refresh[iy*13+ix]/255.f);
            blendPixel(canvas,left+xx,top+yy,music_paper::paper,.45f);
        }
    }
    shelfPerf.decor+=micros()-decorAt;
    static std::string lastFlow;std::string flowKey=std::to_string(position);
    for(const auto& card:visible)flowKey+="|"+card.id+std::to_string(card.cache&&card.cache->ready);
    bool flowChanged=flowKey!=lastFlow||titleChanged||headerChanged;
    uint32_t rasterAt=micros();
    if(count&&flowChanged){
        memcpy(static_cast<uint16_t*>(canvas.getBuffer())+40*320,static_cast<uint16_t*>(shelfBackdrop.getBuffer())+40*320,135*320*2);
        std::sort(std::begin(visible),std::end(visible),[](const Visible& a,const Visible& b){return fabsf(a.geometry.offset)>fabsf(b.geometry.offset);});
        for(const auto& card:visible)if(!card.id.empty())music_shelf::rasterCard(static_cast<uint16_t*>(canvas.getBuffer()),320,240,
            card.cache&&card.cache->ready?card.cache->pixels:nullptr,160,card.geometry,shelfRawSwapped,[]{uint32_t at=micros();pollInput();shelfPerf.pump+=micros()-at;});
    }
    lastFlow=std::move(flowKey);
    uint32_t rasterCost=micros()-rasterAt;pollInput();uint32_t pushAt=micros();
    auto pushRows=[](int y,int h){M5.Display.setClipRect(0,y,320,h);canvas.pushSprite(0,0);M5.Display.clearClipRect();pollInput();};
    if(headerChanged)pushRows(0,40);
    if(flowChanged)pushRows(40,135);
    if(titleChanged)pushRows(175,39);
    if(footerChanged||loading)pushRows(214,26);
    uint32_t pushCost=micros()-pushAt;
    shelfNormalFrame=!moving&&!sending&&!loading&&error.empty();shelfShownRevision=revision;shelfShownSelected=index;
    bool entering=shelfEntryFirst;logShelfVisible();
    // Gradually warm neighbours while stationary. A new touch stops further warming;
    // current on-screen title pixels are restored from the real decor strip afterwards.
    if(!entering&&count&&!moving&&!sending&&!loading&&!shelfFingerDown){
        for(const auto& next:wantedTitles)if(!next.empty()&&!shelfTitleCached(next)){
            drawCachedShelfTitle(next,false,wantedTitles);
            memcpy(static_cast<uint16_t*>(canvas.getBuffer())+175*320,static_cast<uint16_t*>(shelfDecor.getBuffer())+175*320,39*320*2);
            dirty=true;break;
        }
    }
    uint32_t total=micros()-frameStarted;++shelfPerf.frames;shelfPerf.total+=total;shelfPerf.max=std::max(shelfPerf.max,total);shelfPerf.raster+=rasterCost;shelfPerf.push+=pushCost;if(total>33000)++shelfPerf.overBudget;
    if(millis()-shelfPerf.at>=5000&&Serial.availableForWrite()>=220){
        Serial.printf("SHELF_PERF frames=%lu frame_avg_us=%lu max_us=%lu raster_avg_us=%lu push_avg_us=%lu decor_avg_us=%lu pump_avg_us=%lu update_total_us=%lu keyboard_total_us=%lu input_polls=%lu over_33ms=%lu decodes=%lu decode_total_us=%lu decode_max_us=%lu\n",
            (unsigned long)shelfPerf.frames,(unsigned long)(shelfPerf.total/shelfPerf.frames),(unsigned long)shelfPerf.max,(unsigned long)(shelfPerf.raster/shelfPerf.frames),
            (unsigned long)(shelfPerf.push/shelfPerf.frames),(unsigned long)(shelfPerf.decor/shelfPerf.frames),(unsigned long)(shelfPerf.pump/shelfPerf.frames),(unsigned long)shelfUpdateUs,(unsigned long)shelfKeyboardUs,(unsigned long)shelfInputPolls,(unsigned long)shelfPerf.overBudget,(unsigned long)shelfDecodeCount,(unsigned long)shelfDecodeUs,(unsigned long)shelfDecodeMaxUs);
        shelfPerf={};shelfPerf.at=millis();shelfUpdateUs=shelfKeyboardUs=shelfInputPolls=0;shelfDecodeCount=shelfDecodeUs=shelfDecodeMaxUs=0;
    }
}
// Keep the large Now Playing State snapshot off the Collection call stack.
// noinline is intentional: the installed build reserved 4576 bytes even for its shelf branch.
__attribute__((noinline)) void drawNowPlaying(bool motionOnly) {
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
        drawContentPill(music_content::likeX,heartTop,s.favorite==1?music_icons::heart:music_icons::heartOutline,"喜欢",s.favorite==1?0xef263f:music_paper::ink,0);
        drawContentPill(music_content::collectionX,heartTop,music_shelf_assets::collectionEntry,"P",music_paper::ink,1);
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
void draw(bool motionOnly=false){
    if(!faces_screen::appVisible())return;
    if(shelfOpen.load()){
        if(dirty.exchange(false)){
            static bool first=true;
            if(first)Serial.printf("SHELF_FRAME begin stack_free=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            drawShelf();
            if(first){Serial.printf("SHELF_FRAME complete stack_free=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));first=false;}
        }
        return;
    }
    drawNowPlaying(motionOnly);
}
void enterShelf(bool touch=false){
    if(setupMode||shelfOpen.load())return;
    if(busy.load()){
        message("正在发送，请稍候","SENDING");contentPressed=1;contentPressedUntil=millis()+200;
        drawContentPill(music_content::collectionX,heartTop,music_shelf_assets::collectionEntry,"…",music_paper::ink,1);
        M5.Display.setClipRect(music_content::collectionX-1,heartTop-1,music_content::width+2,music_content::height+3);
        canvas.pushSprite(0,0);M5.Display.clearClipRect();return;
    }
    shelfEntryAt=micros();shelfEntryFirst=true;
    shelfIgnoreEntryTouch=touch;
    if(touch&&!shelfReturnReady){
        contentPressed=1;contentPressedUntil=millis()+120;
        drawContentPill(music_content::collectionX,heartTop,music_shelf_assets::collectionEntry,"P",music_paper::ink,1);
        M5.Display.setClipRect(music_content::collectionX-1,heartTop-1,music_content::width+2,music_content::height+3);
        canvas.pushSprite(0,0);M5.Display.clearClipRect();
        Serial.printf("SHELF_OPEN feedback_us=%lu\n",(unsigned long)(micros()-shelfEntryAt));
    }
    contentPressed=-1;openShelf();
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
#ifdef FACES_SUITE
void setup(bool startInCollection){
#else
void setup(){
    constexpr bool startInCollection=false;
#endif
    Serial.begin(115200);
    auto cfg=M5.config();cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk=false;cfg.internal_mic=false;cfg.internal_imu=false;
    M5.begin(cfg); // Speaker, microphone and camera stay disabled.
    connectionStarted=millis();
    shelfPrefs.begin("faces-shelf",false);shelf.selectedId=shelfPrefs.getString("selected").c_str();
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
    Serial.println("BOOT faces-music paper-v1 / speaker disabled / LED GPIO5 off until L");
    if(startInCollection)openShelf();
    draw();inputPumpReady=true;
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
            if(diagnostic=="SHELF OPEN")enterShelf();
            if(diagnostic=="MUSIC VIEW"&&shelfOpen.load()){closeShelf();draw();}
            if(diagnostic=="SHELF NEXT"&&shelfOpen.load())moveShelf(1);
            if(diagnostic=="SHELF PREVIOUS"&&shelfOpen.load())moveShelf(-1);
            if(diagnostic=="SHELF STATUS"){
                unsigned ready=0,visibleReady=0,visibleTotal=0;for(const auto& cache:shelfPixels)ready+=cache.ready;
                xSemaphoreTake(stateLock,portMAX_DELAY);
                for(int i=-2;i<=2&&!shelf.items.empty();++i){if(shelf.items.size()==1&&i!=0)continue;++visibleTotal;auto id=shelf.items[music_shelf::modulo(shelf.anchor+i,int(shelf.items.size()))].id;
                    for(const auto& cache:shelfPixels)if(cache.id==id&&cache.ready){++visibleReady;break;}}
                xSemaphoreGive(stateLock);
                xSemaphoreTake(stateLock,portMAX_DELAY);auto id=shelf.selectedId;auto index=shelf.selected;auto count=shelf.items.size();xSemaphoreGive(stateLock);
                Serial.printf("SHELF_STATUS id=%s index=%u count=%u decoded=%u visible_ready=%u visible_total=%u stack_free=%u\n",id.c_str(),unsigned(index),unsigned(count),ready,visibleReady,visibleTotal,unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            }
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
    if(contentPressed>=0&&int32_t(now-contentPressedUntil)>=0){contentPressed=-1;dirty=true;}
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
    auto playedEpoch=shelfPlayed.exchange(0);
    if(playedEpoch){
        xSemaphoreTake(stateLock,portMAX_DELAY);bool close=shelfOpen.load()&&shelf.epoch==playedEpoch;xSemaphoreGive(stateLock);
        if(close){closeShelf(true);message("已请求播放歌单","PLAY SENT");}
    }
    if(shelfBeforePlay&&!busy.load()&&!shelfPlayed.load())releaseShelfPlaySnapshot();
    handleShelfTouches();advanceShelf(millis());
    InputEventQueue::Event event;
    while(pendingInput.pop(event)){
        if(shelfOpen.load()){
            // Touch input uses its own down/move/up queue; never play on touch-down.
            if(!event.touch){
                auto k=event.key;
                if(k==KEYBOARD3_KEY_ESC||k=='q'||k=='Q')returnShelfHome();
                else if(k=='j'||k=='J')moveShelf(-1);
                else if(k=='k'||k=='K')moveShelf(1);
                else if(k=='r'||k=='R')refreshShelf();
                else if(k=='s'||k=='S'){closeShelf();faces_lights::off();startPortal();}
                else if(k==' '||k==KEYBOARD3_KEY_ENTER)playShelf();
                else if(k=='l'||k=='L')faces_lights::toggle(millis());
            }
            continue;
        }
        if(!setupMode&&((!event.touch&&(event.key=='p'||event.key=='P'))||
            (event.touch&&music_shelf::entryTouch(event.x,event.y,heartTop)))){
            enterShelf(event.touch);continue;
        }
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
            if(!setupMode){
                if(music_content::touch(event.x,event.y,heartTop)==music_content::Like){contentPressed=0;contentPressedUntil=millis()+120;}
                submit(music::touch(event.x,event.y,heartTop));
            }dirty=true;
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
    bool animating=!shelfOpen.load()&&!setupMode&&(recordMoving||lyricMoving||lyricFade.active(now));
    // Battery/command acknowledgements must not insert a slow full-page render
    // halfway through the 1.05 s record motion. Keep dirty pending until it ends.
    // A real track/cover/background replacement still needs a complete frame.
    bool recordOnly=false;
    if(recordMoving&&!shelfOpen.load()&&!setupMode&&stageOK){
        xSemaphoreTake(stateLock,portMAX_DELAY);
        recordOnly=!strcmp(drawnTrack,state.track)&&!strcmp(drawnArtId,state.artId)
            &&drawnSceneVersion==sceneVersion&&cachedCoverVersion==coverVersion;
        xSemaphoreGive(stateLock);
    }
    now=millis();
    if(shelfOpen.load()){
        if(dirty.load()&&now-lastDraw>=33){lastDraw=now;draw();}
    }else if((!recordOnly&&dirty.load())||now-lastDraw>(animating?33:200)){
        lastDraw=now;draw(recordOnly||(animating&&!dirty.load()&&now-lastFullDraw<500));
    }
    faces_screen::render();delay(2);
}

#ifdef FACES_SUITE
} // namespace cloud_app
#endif

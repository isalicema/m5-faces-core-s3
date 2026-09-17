#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <atomic>
#include <memory>
#include <new>
#include "DirectInput.h"
#include "InputDiagnostics.h"
#include "KeyboardIndicators.h"
#include "FacesScreenPower.h"
#include "RadioModel.h"
#include "RadioSpeaker.h"
#include "MusicLight.h"
#include <utility/led/LED_Strip_Class.hpp>

#include "Launcher.h"
#ifdef FACES_SUITE
#include "FacesNetwork.h"
#include "RadioLayout.h"
#include "SmoothMusicFonts.h"
#include "FacesPower.h"
#include "FacesBatteryIcon.h"
#include "MusicIcons.h"
#endif
#ifdef FACES_SUITE
namespace radio_app {
extern const uint8_t radio0Start[] asm("_binary_assets_radio_stage_0_png_start");
extern const uint8_t radio0End[] asm("_binary_assets_radio_stage_0_png_end");
extern const uint8_t radio1Start[] asm("_binary_assets_radio_stage_1_png_start");
extern const uint8_t radio1End[] asm("_binary_assets_radio_stage_1_png_end");
extern const uint8_t radio2Start[] asm("_binary_assets_radio_stage_2_png_start");
extern const uint8_t radio2End[] asm("_binary_assets_radio_stage_2_png_end");
#endif

namespace {
M5Faces_Keyboard3 keyboard;
DirectInput input;
KeyboardIndicators indicators;
void syncIndicators(){indicators.sync(input,[](uint8_t mode){return keyboard.setLED(mode)==M5FACES_OK;});}
RadioModel model;
M5Canvas canvas(&M5.Display);
#ifdef FACES_SUITE
M5Canvas radioStage(&M5.Display);
bool radioStageOK=false;
int radioStageIndex=-1;
faces_power::State radioPower;
uint32_t lastPowerRead=0,needleAt=0;
float needleFrom=255,needlePosition=255;
int needleTarget=255;
#endif
#ifndef FACES_SUITE
m5::LED_Strip_Class sideLights;
MusicLight musicLight;
#endif
bool lightsReady = false;
uint8_t lightMode = 1;
uint32_t lastLight = 0;
const char* lightNames[] = {"L:OFF", "L:DIM", "L:SOFT"};
Preferences prefs;
WebServer server(80);
bool keyboardReady = false, dirty = true, portal = false, settings = false;
bool connecting = false, wasConnected = false;
uint32_t lastPoll = 0, lastHealth = 0, lastDraw = 0, connectAt = 0, restartAt = 0, saveAt = 0;
String apName, apPassword;
std::atomic<int> wanted{-1}, volume{48};
std::atomic<bool> speakerOK{false};
// Physical-USB diagnostics only; no persistent preference or network command.
std::atomic<int> diagnosticMirror{-1};
std::atomic<bool> receiveProbe{false};
std::atomic<unsigned> streamRevision{0};
enum class Playback { Idle, Connecting, Playing, Retry, NoWiFi, AudioError };
constexpr uint32_t inputBufferBytes = 64 * 1024;
constexpr uint32_t prefillBytes = 48 * 1024; // Three seconds at the stations' 128 kbps.
constexpr uint32_t lowWaterBytes = 8 * 1024;
struct Snapshot { Playback state = Playback::Idle; int station = -1; char title[128] = {}; uint32_t frames = 0, buffered = 0, underflows = 0, maxLoopMs = 0; char failure[48] = {}; };
Snapshot status;
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
constexpr uint16_t paper = 0xF79D, ink = 0x18E4, muted = 0x738E, blue = 0x329F;

Snapshot snapshot() { portENTER_CRITICAL(&stateLock); Snapshot s = status; portEXIT_CRITICAL(&stateLock); return s; }
void publish(Playback state, int station) {
    portENTER_CRITICAL(&stateLock);
    if (station != status.station) { status.title[0] = 0; status.frames = 0; }
    status.state = state; status.station = station;
    portEXIT_CRITICAL(&stateLock);
}
void metadata(void*, const char* type, bool, const char* value) {
    if (!type || !value || strcmp(type, "StreamTitle")) return;
    portENTER_CRITICAL(&stateLock);
    snprintf(status.title, sizeof(status.title), "%s", value);
    portEXIT_CRITICAL(&stateLock);
}
void audioStatus(void* context, int code, const char* message) {
    const char* component = static_cast<const char*>(context);
    Serial.printf("AUDIO %s code=%d %s\n", component, code, message ? message : "");
    portENTER_CRITICAL(&stateLock);
    if (!strcmp(component, "buffer") && code == AudioFileSourceBuffer::STATUS_UNDERFLOW) ++status.underflows;
    if (!strcmp(component, "stream")) snprintf(status.failure, sizeof(status.failure), "%s", message ? message : "Stream error");
    portEXIT_CRITICAL(&stateLock);
}
using RadioStream = AudioFileSourceICYStream;

void audioTask(void*) {
    RadioSpeaker output;
    RadioStream* source = nullptr;
    AudioFileSourceBuffer* buffer = nullptr;
    AudioGeneratorMP3* decoder = nullptr;
    int active = -1, previousVolume = -1;
    unsigned mirror = 0;
    unsigned revision = 0;
    uint32_t retryAt = 0;
    auto prefill = [&]() {
        publish(Playback::Connecting, active);
        uint32_t start = millis();
        while (buffer->getFillLevel() < prefillBytes && millis() - start < 10000) {
            if (wanted.load() != active || WiFi.status() != WL_CONNECTED || !source->isOpen()) return false;
            buffer->loop();
            portENTER_CRITICAL(&stateLock); status.buffered = buffer->getFillLevel(); portEXIT_CRITICAL(&stateLock);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        Serial.printf("PREFILL bytes=%lu elapsed=%lu\n", (unsigned long)buffer->getFillLevel(), (unsigned long)(millis() - start));
        return buffer->getFillLevel() >= prefillBytes;
    };
    auto cleanup = [&]() {
        if (decoder) { decoder->stop(); delete decoder; decoder = nullptr; }
        if (buffer) { buffer->close(); delete buffer; buffer = nullptr; }
        if (source) { source->close(); delete source; source = nullptr; }
        output.stop();
    };
    for (;;) {
        const int request = wanted.load();
        if (previousVolume != volume.load()) { previousVolume = volume.load(); M5.Speaker.setVolume(previousVolume); }
        if (request != active || revision != streamRevision.load()) { cleanup(); active = request; revision = streamRevision.load(); retryAt = 0; mirror = 0; publish(Playback::Idle, active); }
        if (active < 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (!speakerOK.load()) { publish(Playback::AudioError, active); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (WiFi.status() != WL_CONNECTED) {
            cleanup(); publish(Playback::NoWiFi, active); vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (!decoder && int32_t(millis() - retryAt) >= 0) {
            publish(Playback::Connecting, active);
            source = new RadioStream();
            source->RegisterMetadataCB(metadata, nullptr);
            source->RegisterStatusCB(audioStatus, (void*)"stream");
            String url = radioStations[active].url;
            // Both hosts are listed in the station's official playlist.
            unsigned selectedMirror = diagnosticMirror.load() < 0 ? mirror++ % 2 : unsigned(diagnosticMirror.load());
            if (selectedMirror == 0) url.replace("ice2.", "ice5.");
            Serial.printf("CONNECT station=%d rssi=%d mirror=%u\n", active, WiFi.RSSI(), selectedMirror);
            bool opened = source->open(url.c_str());
            Serial.printf("OPEN result=%d\n", opened);
            // A key press during the blocking connection must supersede this request.
            if (wanted.load() != active) { cleanup(); continue; }
            if (opened && receiveProbe.exchange(false)) {
                // Keep the probe buffer off the audio task's small stack.
                std::unique_ptr<uint8_t[]> chunk(new(std::nothrow) uint8_t[4096]);
                if (!chunk) { cleanup(); wanted.store(-1); continue; }
                uint32_t started = millis(), reported = started, total = 0, previous = 0, maxRead = 0;
                while (millis() - started < 25000 && wanted.load() == active && revision == streamRevision.load() && WiFi.status() == WL_CONNECTED && source->isOpen()) {
                    auto before = millis();
                    total += source->readNonBlock(chunk.get(), 4096);
                    maxRead = std::max(maxRead, millis() - before);
                    auto now = millis();
                    if (now - reported >= 5000) {
                        Serial.printf("RX_PROBE mirror=%u elapsed=%lu bytes=%lu bytes_per_sec=%lu maxread=%lu rssi=%d\n", selectedMirror, (unsigned long)(now-started), (unsigned long)total, (unsigned long)((total-previous)*1000/(now-reported)), (unsigned long)maxRead, WiFi.RSSI());
                        reported=now;previous=total;
                    }
                    vTaskDelay(1);
                }
                Serial.printf("RX_DONE mirror=%u elapsed=%lu bytes=%lu maxread=%lu\n", selectedMirror, (unsigned long)(millis()-started), (unsigned long)total, (unsigned long)maxRead);
                cleanup();
                if (revision == streamRevision.load()) wanted.store(-1);
                continue;
            }
            if (opened) {
                buffer = new AudioFileSourceBuffer(source, inputBufferBytes);
                buffer->RegisterStatusCB(audioStatus, (void*)"buffer");
                // Initialize the library's buffer without consuming any audio bytes.
                uint8_t unused = 0;
                buffer->read(&unused, 0);
                opened = prefill();
                if (opened && wanted.load() == active) {
                    decoder = new AudioGeneratorMP3();
                    decoder->RegisterStatusCB(audioStatus, (void*)"mp3");
                    opened = decoder->begin(buffer, &output);
                    Serial.printf("DECODER begin=%d\n", opened);
                } else opened = false;
            }
            if (!opened) { cleanup(); retryAt = millis() + 8000; publish(Playback::Retry, active); }
        }
        if (decoder) {
            if (buffer->getFillLevel() < lowWaterBytes) {
                output.stop();
                if (!prefill()) { cleanup(); retryAt = millis() + 8000; publish(Playback::Retry, active); }
            }
        }
        if (decoder) {
            const auto loopAt = millis();
            if (!decoder->isRunning() || !decoder->loop()) {
                cleanup(); retryAt = millis() + 8000; publish(Playback::Retry, active);
            } else if (output.submitted) {
                publish(Playback::Playing, active);
                portENTER_CRITICAL(&stateLock); status.frames = output.submitted; portEXIT_CRITICAL(&stateLock);
            }
            portENTER_CRITICAL(&stateLock);
            status.buffered = buffer ? buffer->getFillLevel() : 0;
            status.maxLoopMs = std::max(status.maxLoopMs, millis() - loopAt);
            portEXIT_CRITICAL(&stateLock);
        }
        vTaskDelay(1);
    }
}

void label(int x, int y, const String& text, int size = 1, uint16_t color = ink) {
    canvas.setTextColor(color); canvas.setTextSize(size); canvas.setCursor(x, y); canvas.print(text);
}
const char* stateName(Playback state) {
    switch (state) {
        case Playback::Playing: return "LIVE";
        case Playback::Connecting: return "BUFFERING";
        case Playback::Retry: return "RETRYING";
        case Playback::NoWiFi: return "WAITING FOR WI-FI";
        case Playback::AudioError: return "SPEAKER UNAVAILABLE";
        default: return "STOPPED";
    }
}
void button(int x, int width, const char* text) {
    canvas.fillRoundRect(x, 207, width, 26, 5, 0xE71C);
    label(x + 8, 216, text);
}
#ifdef FACES_SUITE
void paperText(int x,int y,const String& value,uint16_t color=0x1082){
    canvas.setFont(music_fonts::tiny());canvas.setTextSize(.9f);canvas.setTextColor(color);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);canvas.setCursor(x,y);canvas.print(value);
}
void drawPaper(){
    const auto s=snapshot();const uint32_t now=millis();
    const uint8_t* starts[]={radio0Start,radio1Start,radio2Start};
    const uint8_t* ends[]={radio0End,radio1End,radio2End};
    if(radioStageOK){
        if(radioStageIndex!=model.selected){
            radioStage.drawPng(starts[model.selected],ends[model.selected]-starts[model.selected],0,0);
            radioStageIndex=model.selected;
        }
        radioStage.pushSprite(&canvas,0,0);
    }else canvas.drawPng(starts[model.selected],ends[model.selected]-starts[model.selected],0,0);
    if(!lastPowerRead||now-lastPowerRead>=5000){radioPower=faces_power::read();lastPowerRead=now;}
    canvas.setFont(music_fonts::small());canvas.setTextSize(10.f/12);
    const int statusRight=faces_power::drawBattery(canvas,radioPower,radio_layout::powerRight,radio_layout::powerY,true)-4;
    const char* state="READY";
    if(wanted.load()<0)state=model.nowPlaying?"PAUSED":"READY";
    else if(!speakerOK.load())state="AUDIO ERR";
    else if(WiFi.status()!=WL_CONNECTED)state="WI-FI";
    else if(s.station!=model.selected)state="TUNING";
    else if(s.state==Playback::Playing)state="LIVE";
    else if(s.state==Playback::Retry)state="RETRYING";
    else if(s.state==Playback::AudioError)state="AUDIO ERR";
    else state="BUFFERING";
    canvas.setFont(music_fonts::small());canvas.setTextSize(10.f/12);
    const int statusWidth=canvas.textWidth(state)+10;
    canvas.fillSmoothRoundRect(statusRight-statusWidth,10,statusWidth,18,9,canvas.color888(0xff,0x65,0x5f));
    canvas.setTextColor(0x1082);canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.drawString(state,statusRight-statusWidth/2,19);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    // Native font at 1x keeps the battery and small text free of scale clipping.
    if(needleTarget!=radio_layout::needleX[model.selected]){
        needleFrom=needlePosition;needleTarget=radio_layout::needleX[model.selected];needleAt=now;
    }
    float t=std::min(1.f,float(now-needleAt)/260.f);t=1-(1-t)*(1-t)*(1-t);
    needlePosition=needleFrom+(needleTarget-needleFrom)*t;
    canvas.fillSmoothRoundRect(needlePosition-1.5f,radio_layout::needleY,3,radio_layout::needleHeight,1.5f,canvas.color888(255,107,67));
    // Reuse Music's antialiased Phosphor masks, avoiding jagged tiny triangles.
    const uint8_t* icon=wanted.load()>=0?music_icons::pause:music_icons::play;
    for(int iy=0;iy<13;++iy)for(int ix=0;ix<13;++ix){
        int alpha=icon[iy*13+ix];if(!alpha)continue;
        auto bg=canvas.readPixelRGB(18+ix,129+iy);
        auto blend=[&](int c){return (c*(255-alpha)+17*alpha+127)/255;};
        canvas.drawPixel(18+ix,129+iy,canvas.color888(blend(bg.R8()),blend(bg.G8()),blend(bg.B8())));
    }
    String title;
    if(wanted.load()<0)title=model.nowPlaying?"Paused · SPACE to listen":"Choose a station to listen";
    else if(s.station==model.selected&&s.title[0])title=s.title;
    else if(!strcmp(state,"BUFFERING"))title="Buffering "+String(std::min<uint32_t>(100,s.buffered*100/prefillBytes))+"% · device audio";
    else if(!strcmp(state,"WI-FI"))title="Connecting Wi-Fi · S settings";
    else title="SomaFM · listener-supported radio";
    canvas.setClipRect(10,147,300,14);paperText(10,148,title,canvas.color565(85,81,75));canvas.clearClipRect();
    canvas.setFont(music_fonts::small());canvas.setTextSize(10.f/12);canvas.setTextColor(0x1082);canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.drawString("-",181,228);canvas.drawString("VOL "+String(model.volume*100/160)+"%",240,228);canvas.drawString("+",301,228);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);canvas.setTextSize(1);
    canvas.pushSprite(0,0);dirty=false;
}
#endif
void draw() {
#ifdef FACES_SUITE
    drawPaper();return;
#endif
    const auto s = snapshot();
    canvas.fillSprite(paper);
    label(12, 10, "FACES / radio", 2);
#ifdef FACES_SUITE
    label(238,13,"< HOME",1,muted);
#else
    label(242, 13, lightNames[lightMode], 1, muted);
#endif
    canvas.fillCircle(308, 16, 3, WiFi.status() == WL_CONNECTED ? 0x33C9 : muted);
    canvas.drawFastHLine(0, 34, 320, 0xCE79);
    if (settings) {
        label(14, 48, "Wi-Fi setup", 2);
        if (portal) {
            label(14, 79, "1. Join this network on your phone:", 1, muted);
            label(14, 98, apName, 2);
            label(14, 125, "Password: " + apPassword);
            label(14, 149, "2. Open http://192.168.4.1");
            label(14, 175, "Enter your 2.4 GHz Wi-Fi details.", 1, muted);
        } else {
            label(14, 90, WiFi.status() == WL_CONNECTED ? "Connected" : "Connecting...", 2);
            label(14, 123, "IP: " + WiFi.localIP().toString());
            label(14, 158, "Tap SETUP to change the network.", 1, muted);
        }
        button(12, 140, "BACK"); button(164, 144, portal ? "CLOSE SETUP" : "SETUP");
    } else if (!model.nowPlaying) {
        label(14, 46, model.query.empty() ? "Stations" : String("Find: ") + model.query.c_str(), 2);
        if (model.matches.empty()) label(14, 92, "No stations. DEL clears search.", 1, muted);
        for (size_t i = 0; i < model.matches.size(); ++i) {
            const auto& station = radioStations[model.matches[i]];
            const int y = 75 + int(i) * 38;
            const bool selected = int(i) == model.cursor;
            if (selected) canvas.fillRoundRect(8, y, 304, 36, 5, blue);
            label(16, y + 5, station.name, 2, selected ? TFT_WHITE : ink);
            label(17, y + 24, station.genre, 1, selected ? TFT_WHITE : muted);
        }
        label(14, 193, "Type to find  /  ENTER to listen", 1, muted);
        button(12, 96, "PLAY"); button(116, 94, "NOW"); button(218, 90, "WI-FI");
    } else {
        const auto& station = radioStations[model.selected];
        uint16_t cover = canvas.color888(station.color >> 16, station.color >> 8, station.color);
        canvas.fillRoundRect(12, 49, 90, 104, 6, cover);
        canvas.drawCircle(57, 93, 29, TFT_WHITE);
        canvas.drawCircle(57, 93, 11, TFT_WHITE);
        label(20, 134, "SOMA FM", 1, TFT_WHITE);
        label(114, 52, stateName(s.state), 1, s.state == Playback::Playing ? 0x33C9 : muted);
        if (s.state == Playback::Connecting) label(238, 52, String(std::min<uint32_t>(100, s.buffered * 100 / prefillBytes)) + "%", 1, muted);
        String name = station.name;
        int split = name.indexOf(' ');
        label(114, 74, split < 0 ? name : name.substring(0, split), 2);
        if (split >= 0) label(114, 96, name.substring(split + 1), 2);
        label(114, 128, station.genre, 1, muted);
        String title = s.station == model.selected && s.title[0] ? String(s.title) : "Listener-supported internet radio";
        label(14, 163, title.substring(0, 48));
        label(14, 177, title.substring(48, 96));
        label(14, 193, "VOL " + String(model.volume * 100 / 160) + "%", 1, muted);
        canvas.fillRoundRect(70, 194, 190, 4, 2, 0xD69A);
        if (model.volume) canvas.fillRoundRect(70, 194, model.volume * 190 / 160, 4, 2, blue);
        button(12, 78, "LIST"); button(98, 124, wanted.load() < 0 ? "PLAY" : "PAUSE"); button(230, 78, "NEXT");
    }
    canvas.pushSprite(0, 0); dirty = false;
}

void closePortal() { server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); portal = false; dirty = true; }
void startPortal() {
#ifdef FACES_SUITE
    wanted.store(-1);suite::open(suite::App::Connection);
#else

    if (portal) return;
    wanted.store(-1);
    apName = "Faces-Radio-" + WiFi.macAddress().substring(12);
    apName.replace(":", "");
    char pass[16]; snprintf(pass, sizeof(pass), "%08lx", (unsigned long)esp_random());
    apPassword = pass;
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(apName.c_str(), apPassword.c_str())) return;
    portal = true; settings = true;
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Faces Radio</title><style>body{font:18px system-ui;background:#f4f3eb;color:#223;margin:40px auto;padding:20px;max-width:400px}input,button{font:inherit;box-sizing:border-box;padding:12px;width:100%;margin:8px 0 20px;border:1px solid #ccc;border-radius:8px}button{background:#3684e8;color:white}small{color:#667}</style><h1>Faces Radio</h1><p>Connect your player to 2.4 GHz Wi-Fi.</p><form method="post" action="/wifi"><label>Network name<input name="ssid" maxlength="32" required autocomplete="off"></label><label>Password<input name="password" type="password" maxlength="63" autocomplete="new-password"></label><button>Save &amp; connect</button></form><small>Saved on your player. This setup network closes after restart.</small>)HTML");
    });
    server.on("/wifi", HTTP_POST, []() {
        String ssid = server.arg("ssid"), password = server.arg("password");
        if (ssid.isEmpty() || ssid.length() > 32 || (password.length() && (password.length() < 8 || password.length() > 63))) {
            server.send(400, "text/plain", "Check the network name and password length."); return;
        }
        prefs.putString("ssid", ssid); prefs.putString("pass", password);
        server.send(200, "text/html", "<h1>Saved</h1><p>Your player is restarting. You can return to your normal Wi-Fi.</p>");
        restartAt = millis() + 1500;
    });
    server.begin(); dirty = true;
#endif
}

bool connectKeyboard() {
    if (keyboard.begin(&M5.In_I2C, M5FACES_BOTTOM3_ADDR, 100000) != M5FACES_OK) return false;
    m5faces_mode_t mode;
    if (keyboard.getMode(&mode) != M5FACES_OK) return false;
    if (mode != M5FACES_MODE_DIRECT && keyboard.setMode(M5FACES_MODE_DIRECT) != M5FACES_OK) return false;
    input = DirectInput{}; indicators.reset(); syncIndicators(); return true;
}
void playSelected() { wanted.store(model.selected); saveAt = millis() + 1500; dirty = true; }
void togglePlayback() { if (wanted.load() >= 0) wanted.store(-1); else playSelected(); dirty = true; }
void changeVolume(int delta) { model.changeVolume(delta); volume.store(model.volume); saveAt = millis() + 1500; dirty = true; }
void leaveHome(){
#ifdef FACES_SUITE
    wanted.store(-1);M5.Speaker.setVolume(0);prefs.putUChar("volume",model.volume);prefs.putUChar("station",model.selected);
    suite::open(suite::App::Home);
#endif
}
void onKey(uint8_t key) {
    input_diagnostics::key();
#ifdef FACES_SUITE
    if(!suite::inputReady())return;
    if(key==KEYBOARD3_KEY_ESC||key=='q'||key=='Q'){leaveHome();return;}
    if(key=='s'||key=='S'){startPortal();return;}
    if(key==' '||key==KEYBOARD3_KEY_ENTER||key=='\n'){model.nowPlaying=true;togglePlayback();}
    else if(key=='j'||key=='J'||key=='k'||key=='K'||key=='r'||key=='R'){
        model.tune(key=='j'||key=='J'?-1:1);model.nowPlaying=true;playSelected();
    }else if(key>='1'&&key<='3'){model.selected=key-'1';model.nowPlaying=true;playSelected();}
    else if(key=='+'||key=='=')changeVolume(8);
    else if(key=='-')changeVolume(-8);
    dirty=true;return;
#endif
#ifndef FACES_SUITE
    if (key == KEYBOARD3_KEY_ESC) {
        if (settings) settings = false;
        else if (!model.query.empty()) { model.query.clear(); model.filter(); }
        else model.nowPlaying = !model.nowPlaying;
    } else
#endif
    if (settings) {
        if (key == KEYBOARD3_KEY_ENTER) { if (portal) closePortal(); else startPortal(); }
    } else if (key == ' ') togglePlayback();
    else if (key == KEYBOARD3_KEY_ENTER || key == '\n') {
        if (model.nowPlaying) togglePlayback(); else if (model.choose()) playSelected();
    } else if (key == KEYBOARD3_FN_UP || key == KEYBOARD3_FN_DOWN) {
        const int delta = key == KEYBOARD3_FN_UP ? -1 : 1;
        if (model.nowPlaying) changeVolume(-delta * 8); else model.move(delta);
    } else if (key == KEYBOARD3_FN_LEFT || key == KEYBOARD3_FN_RIGHT) {
        model.tune(key == KEYBOARD3_FN_LEFT ? -1 : 1); model.nowPlaying = true; playSelected();
    } else if (key == '+' || key == '-') changeVolume(key == '+' ? 8 : -8);
    else if (key == KEYBOARD3_KEY_BS || key == KEYBOARD3_KEY_DEL) {
        if (!model.query.empty()) { model.query.pop_back(); model.filter(); }
    } else if (key >= 33 && key <= 126 && model.query.size() < 20) {
        model.nowPlaying = false; model.query += char(key); model.filter();
    }
    dirty = true;
}
}

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk = true; cfg.internal_mic = false; cfg.internal_imu = false;
    M5.begin(cfg);
    #ifndef FACES_SUITE
    // Faces Bottom3 S1 must select legacy G25 -> CoreS3 GPIO5.
    // Legacy G15 selects GPIO13, which is the speaker's I2S data output.
    auto ledBus = std::make_shared<m5::LedBus_RMT>();
    auto busConfig = ledBus->config(); busConfig.pin_data = 5;
    busConfig.t0h_ns = 300; busConfig.t0l_ns = 900;
    busConfig.t1h_ns = 600; busConfig.t1l_ns = 600;
    ledBus->config(busConfig);
    auto ledConfig = sideLights.config(); ledConfig.led_count = 10;
    sideLights.config(ledConfig); sideLights.setBus(ledBus);
    sideLights.setBrightness(255); // Output bytes are capped at 8/16 in MusicLight.
    lightsReady = sideLights.begin();
    if (lightsReady) { RGBColor black[10] = {}; sideLights.setColors(black, 0, 10); sideLights.display(); }
    #endif
    M5.Display.setRotation(1); M5.Display.setBrightness(85);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(320, 240)) { M5.Display.print("Display allocation failed"); while (true) delay(1000); }
    canvas.setTextWrap(false);
#ifdef FACES_SUITE
    music_fonts::begin();radioStage.setColorDepth(16);radioStage.setPsram(true);
    radioStageOK=radioStage.createSprite(320,240)!=nullptr;
#endif
    prefs.begin("faces-radio", false);
    lightMode = std::min<uint8_t>(2, prefs.getUChar("light", 1));
    model.volume = std::clamp(int(prefs.getUChar("volume", 48)), 0, 160);
    model.selected = std::clamp(int(prefs.getUChar("station", 0)), 0, radioStationCount - 1);
    model.cursor = model.selected;
#ifdef FACES_SUITE
    needlePosition=needleFrom=needleTarget=radio_layout::needleX[model.selected];
#endif
    volume.store(model.volume); M5.Speaker.setVolume(model.volume);
    speakerOK.store(M5.Speaker.begin());
    if (!M5.In_I2C.isEnabled()) M5.In_I2C.begin(I2C_NUM_1, 12, 11);
    keyboardReady = connectKeyboard();
#ifdef FACES_SUITE
    faces_network::begin(false);
    if(!faces_network::hasWifi())startPortal();
#else
    WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
    String ssid = prefs.getString("ssid"), password = prefs.getString("pass");
#ifdef FACES_SUITE
    if(ssid.isEmpty()){Preferences music;music.begin("faces-music",true);ssid=music.getString("ssid");password=music.getString("pass");music.end();}
#endif
    if (!ssid.isEmpty()) { WiFi.begin(ssid.c_str(), password.c_str()); connecting = true; connectAt = millis(); }
    else startPortal();
#endif
    if (xTaskCreatePinnedToCore(audioTask, "radio-audio", 8192, nullptr, 2, nullptr, 0) != pdPASS) speakerOK.store(false);
    Serial.printf("BOOT faces-radio 0.3 board=%d keyboard=%d speaker=%d lights=%d pin=5 heap=%u\n", int(M5.getBoard()), keyboardReady, speakerOK.load(), lightsReady, ESP.getFreeHeap());
    Serial.println("UI radio-paper-v1");
    draw();
}
void loop() {
    M5.update();if(faces_screen::tick()){dirty=true;lastDraw=0;}
    input_diagnostics::sample("radio",faces_screen::touchEnabled());
    uint32_t now = millis();
    #ifndef FACES_SUITE
    if (lightsReady && now - lastLight >= 40) {
        uint32_t elapsed = now - lastLight; lastLight = now;
        uint32_t pcmAt = radioPcmAt.load();
        bool fresh = wanted.load() >= 0 && pcmAt && now - pcmAt < 200 && model.volume > 0;
        auto pixels = musicLight.frame(radioAmplitude.load(), fresh, lightMode, elapsed / 1000.f);
        RGBColor colors[10];
        for (int i = 0; i < 10; ++i) colors[i] = RGBColor(pixels[i].r, pixels[i].g, pixels[i].b);
        sideLights.setColors(colors, 0, 10); sideLights.display();
    }
    #endif
#ifdef FACES_SUITE
    if(!portal)faces_network::tick();
#endif
    if (portal) server.handleClient();
    if (restartAt && int32_t(now - restartAt) >= 0){
#ifdef FACES_SUITE
        suite::open(suite::App::Radio);
#else
        ESP.restart();
#endif
    }
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) {
        wasConnected = connected; dirty = true;
        if (connected) { connecting = false; if (portal) closePortal(); settings = false; }
    }
    if (connecting && now - connectAt > 15000) { connecting = false; startPortal(); }
    if (keyboardReady && now - lastPoll >= 5) {
        lastPoll = now; uint8_t raw[10] = {};
        if (keyboard.readReg(M5FACES_REG_KEY, raw, sizeof(raw)) == M5FACES_OK) input.feed(raw, now);
        input.tick(now, [](uint8_t index, uint8_t layer) { onKey(M5Faces_Keyboard3::KEYMAP[index][layer]); });
        syncIndicators();
    }
    if (now - lastHealth > 5000) {
        lastHealth = now; uint8_t id = 0;
        keyboardReady = keyboardReady ? keyboard.getModelID(&id) == M5FACES_OK && id == M5Faces_Keyboard3::MODEL_ID : connectKeyboard();
        const auto s = snapshot();
        Serial.printf("HEALTH wifi=%d rssi=%d keyboard=%d state=%s station=%d frames=%lu buffer=%lu underflow=%lu maxloop=%lu heap=%u light=%u amplitude=%u\n", connected, WiFi.RSSI(), keyboardReady, stateName(s.state), s.station, (unsigned long)s.frames, (unsigned long)s.buffered, (unsigned long)s.underflows, (unsigned long)s.maxLoopMs, ESP.getFreeHeap(), lightMode, radioAmplitude.load());
    }
    auto touch = M5.Touch.getDetail();
    if (faces_screen::touchEnabled() && touch.wasPressed()) {
#ifdef FACES_SUITE
        if(!suite::inputReady())return;
#endif
#ifdef FACES_SUITE
        if(radio_layout::home.contains(touch.x,touch.y)){leaveHome();return;}
        else if(radio_layout::connection.contains(touch.x,touch.y)){startPortal();return;}
        else if(radio_layout::play.contains(touch.x,touch.y)){model.nowPlaying=true;togglePlayback();}
        else if(radio_layout::volumeDown.contains(touch.x,touch.y))changeVolume(-8);
        else if(radio_layout::volumeUp.contains(touch.x,touch.y))changeVolume(8);
        else if(int station=radio_layout::stationAt(touch.x,touch.y);station>=0){model.selected=station;model.nowPlaying=true;playSelected();}
#else
        if (touch.y < 34 && touch.x >= 232) {
#ifdef FACES_SUITE
            leaveHome();
#else
            lightMode = (lightMode + 1) % 3;
            prefs.putUChar("light", lightMode);
#endif
        } else if (touch.y >= 207) {
            if (settings) { if (touch.x < 160) settings = false; else if (portal) closePortal(); else startPortal(); }
            else if (model.nowPlaying) {
                if (touch.x < 94) model.nowPlaying = false;
                else if (touch.x < 226) togglePlayback();
                else { model.tune(1); playSelected(); }
            } else {
                if (touch.x < 112) { if (model.choose()) playSelected(); }
                else if (touch.x < 214) model.nowPlaying = true;
                else {
#ifdef FACES_SUITE
                    startPortal();
#else
                    settings=true;
#endif
                }
            }
        } else if (!settings && !model.nowPlaying && touch.y >= 75 && touch.y < 189) {
            int row = (touch.y - 75) / 38;
            if (row < int(model.matches.size())) { model.cursor = row; if (model.choose()) playSelected(); }
        } else if (!settings && model.nowPlaying && touch.y >= 187 && touch.y < 207) {
            model.volume = std::clamp((int(touch.x) - 70) * 160 / 190, 0, 160); changeVolume(0);
        }
#endif
        dirty = true;
    }
    if (saveAt && int32_t(now - saveAt) >= 0) {
        prefs.putUChar("volume", model.volume); prefs.putUChar("station", model.selected); saveAt = 0;
    }
    // Exact, bounded diagnostic commands over physical USB; ignore Bridge JSON.
    static String command;
    static bool overflow = false;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            if (!overflow && command == "RADIO PLAY") { model.nowPlaying = true; playSelected(); }
            if (!overflow && command == "RADIO STOP") { wanted.store(-1); dirty = true; }
            if (!overflow && (command == "RADIO MIRROR 0" || command == "RADIO MIRROR 1")) { diagnosticMirror.store(command.endsWith("0") ? 0 : 1); streamRevision.fetch_add(1); }
            if (!overflow && command == "RADIO PROBE") { receiveProbe.store(true); wanted.store(model.selected); streamRevision.fetch_add(1); }
            command = ""; overflow = false;
        } else if (c != '\r') {
            if (command.length() < 32 && !overflow) command += c;
            else overflow = true;
        }
    }
#ifdef FACES_SUITE
    // Animate only the short tuning transition; do not steal time from decoding.
    if(needleTarget!=radio_layout::needleX[model.selected]||now-needleAt<260)dirty=true;
#endif
    if (faces_screen::appVisible()&&((dirty && now - lastDraw > 40) || now - lastDraw > 1000)) { lastDraw = now; draw(); }
    faces_screen::render();
    delay(1);
}

#ifdef FACES_SUITE
} // namespace radio_app
#endif

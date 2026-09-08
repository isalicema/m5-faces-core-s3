#include "FacesOta.h"
#include "FacesOtaPolicy.h"
#include "SmoothMusicFonts.h"
#include "FacesPower.h"
#include <M5Unified.h>
#include <WiFi.h>
#include "FacesNetwork.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <cJSON.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <mbedtls/sha256.h>
#include <memory>
namespace faces_ota {
namespace {
String host,token,lastSha,rejected;
uint16_t port=8766;
uint32_t lastCheck=0;
bool configured=false;
bool reported=false;
uint32_t lastReport=0;
M5Canvas screen(&M5.Display);
String url(const String& path){return "http://"+host+":"+String(port)+path;}
void display(const char* label,int percent=-1){
    if(!screen.getBuffer()){screen.setColorDepth(16);if(!screen.createSprite(320,240))return;}
    screen.fillSprite(0x18E4);screen.setTextColor(0xF79C);screen.setTextWrap(false);
    screen.setFont(music_fonts::large());screen.setCursor(24,45);screen.print("无线更新");
    screen.setFont(music_fonts::small());screen.setCursor(24,85);screen.print(label);
    if(percent>=0){screen.fillRoundRect(24,133,272,6,3,0x324B);if(percent)screen.fillRect(24,133,272*percent/100,6,0xE6D3);screen.setCursor(24,156);screen.printf("%d%%",percent);}
    screen.pushSprite(0,0);
}
bool saveSha(const String& sha){Preferences p;if(!p.begin("faces-ota",false))return false;bool ok=p.putString("last_sha",sha)==sha.length();p.end();if(ok)lastSha=sha;return ok;}
bool manifest(String& sha,size_t& size){
    WiFiClient client;HTTPClient http;http.setConnectTimeout(1200);http.setTimeout(3000);
    if(!http.begin(client,url("/api/ota/manifest")))return false;
    http.addHeader("Authorization","Bearer "+token);int code=http.GET();int n=http.getSize();
    if(code!=200||n<=0||n>1024){http.end();return false;}
    String body=http.getString();http.end();if(body.length()!=size_t(n))return false;
    cJSON* j=cJSON_Parse(body.c_str());if(!j)return false;
    auto str=[&](const char* key){auto v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:"";};
    auto number=cJSON_GetObjectItemCaseSensitive(j,"size"),schema=cJSON_GetObjectItemCaseSensitive(j,"schema");
    sha=str("sha256");double rawSize=cJSON_IsNumber(number)?number->valuedouble:0;
    bool ok=cJSON_IsNumber(schema)&&schema->valuedouble==1 && !strcmp(str("target"),targetId)
        && WiFi.macAddress().equalsIgnoreCase(str("device_mac")) && validSha(sha.c_str())
        && sha==str("release_id") && rawSize>=288 && rawSize<=slotSize && rawSize==double(size_t(rawSize));
    size=ok?size_t(rawSize):0;cJSON_Delete(j);return ok;
}
String hex(const uint8_t* bytes){char out[65];for(int i=0;i<32;++i)snprintf(out+2*i,3,"%02x",bytes[i]);return String(out);}
bool report(const char* event){
    auto p=esp_ota_get_running_partition();if(!p)return false;
    cJSON* j=cJSON_CreateObject();cJSON_AddStringToObject(j,"target",targetId);cJSON_AddStringToObject(j,"device_mac",WiFi.macAddress().c_str());
    cJSON_AddStringToObject(j,"elf_sha256",hex(esp_app_get_description()->app_elf_sha256).c_str());
    cJSON_AddNumberToObject(j,"partition",p->address);cJSON_AddStringToObject(j,"event",event);
    faces_power::addJson(j,faces_power::read());
    char* body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return false;
    WiFiClient client;HTTPClient http;http.setConnectTimeout(1200);http.setTimeout(3000);int code=0;
    if(http.begin(client,url("/api/ota/report"))){http.addHeader("Authorization","Bearer "+token);http.addHeader("Content-Type","application/json");code=http.POST(String(body));}
    cJSON_free(body);http.end();return code==200;
}
// Also skip a candidate identical to a USB-installed build, without rewriting flash.
bool alreadyRunning(const String& sha,size_t size){
    auto p=esp_ota_get_running_partition();if(!p||!validSize(size,p->size))return false;
    std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[8192]);if(!buffer)return false;
    mbedtls_sha256_context digest;mbedtls_sha256_init(&digest);bool ok=mbedtls_sha256_starts(&digest,0)==0;
    for(size_t offset=0;ok&&offset<size;){size_t n=std::min(size_t(8192),size-offset);ok=esp_partition_read(p,offset,buffer.get(),n)==ESP_OK&&mbedtls_sha256_update(&digest,buffer.get(),n)==0;offset+=n;delay(1);}
    uint8_t sum[32];ok=ok&&mbedtls_sha256_finish(&digest,sum)==0;mbedtls_sha256_free(&digest);return ok&&hex(sum)==sha;
}
bool install(const String& sha,size_t size){
    auto running=esp_ota_get_running_partition(),target=esp_ota_get_next_update_partition(nullptr);
    if(!target||target==running||!validSize(size,target->size))return false;
    WiFiClient client;HTTPClient http;const char* headers[]={"X-Firmware-SHA256","X-Firmware-Release","X-Faces-Target"};
    http.collectHeaders(headers,3);http.setConnectTimeout(1200);http.setTimeout(10000);
    if(!http.begin(client,url("/api/ota/firmware/"+sha)))return false;
    http.addHeader("Authorization","Bearer "+token);
    if(http.GET()!=200||http.getSize()!=int(size)||http.header(headers[0])!=sha||http.header(headers[1])!=sha||http.header(headers[2])!=targetId){http.end();return false;}
    esp_ota_handle_t handle=0;
    std::unique_ptr<uint8_t[]> buffer(new(std::nothrow) uint8_t[8192]);
    if(!buffer||esp_ota_begin(target,size,&handle)!=ESP_OK){http.end();return false;}
    mbedtls_sha256_context digest;mbedtls_sha256_init(&digest);bool ok=mbedtls_sha256_starts(&digest,0)==0;
    size_t received=0;uint32_t started=millis(),lastData=started;int lastPercent=-1;auto stream=http.getStreamPtr();
    while(ok&&received<size){
        if(millis()-started>300000){ok=false;break;}
        size_t available=stream->available();
        if(!available){if(!http.connected()||millis()-lastData>10000){ok=false;break;}delay(2);continue;}
        size_t n=stream->readBytes(buffer.get(),std::min(std::min(available,size_t(8192)),size-received));
        if(!n)continue;lastData=millis();
        ok=mbedtls_sha256_update(&digest,buffer.get(),n)==0&&esp_ota_write(handle,buffer.get(),n)==ESP_OK;
        received+=n;int percent=received*100/size;
        if(percent/5!=lastPercent/5){display("正在安装，请保持供电",percent);lastPercent=percent;}
        delay(1);
    }
    uint8_t sum[32];ok=ok&&received==size&&mbedtls_sha256_finish(&digest,sum)==0;
    mbedtls_sha256_free(&digest);http.end();
    if(!ok||hex(sum)!=sha){esp_ota_abort(handle);return false;}
    if(esp_ota_end(handle)!=ESP_OK)return false;
    // Stop a rollback survivor from repeatedly installing the same bad release.
    if(!saveSha(sha))return false;
    return esp_ota_set_boot_partition(target)==ESP_OK;
}
}
void acceptBoot(bool healthy){
    auto running=esp_ota_get_running_partition();esp_ota_img_states_t state;
    if(!running||esp_ota_get_state_partition(running,&state)!=ESP_OK||state!=ESP_OTA_IMG_PENDING_VERIFY)return;
    if(healthy){esp_ota_mark_app_valid_cancel_rollback();Serial.println("OTA boot accepted");}
    else esp_ota_mark_app_invalid_rollback_and_reboot();
}
void begin(){
    Serial.println(marker);
    faces_network::begin();configured=faces_network::hasWifi()&&faces_network::hasBridge();
    Preferences p;
    if(p.begin("faces-ota",true)){lastSha=p.getString("last_sha");p.end();}
    lastCheck=millis()-25000; // First idle check after about five seconds.
}
bool tick(bool idle){
    faces_network::tick();host=faces_network::host();token=faces_network::token();port=faces_network::port();
    if(host.isEmpty()||token.isEmpty())return false;
    if(!idle||!configured||millis()-lastCheck<30000||WiFi.status()!=WL_CONNECTED)return false;
    lastCheck=millis();
    // Restore the boot receipt after a Bridge restart, without a device reboot.
    // Low battery blocks installation, not read-only power telemetry.
    if(!reported||millis()-lastReport>=30000){reported=report("ready");if(reported)lastReport=millis();}
    if(!canStart(idle,true,configured,M5.Power.getBatteryLevel(),M5.Power.getVBUSVoltage()))return false;
    String sha;size_t size=0;if(!manifest(sha,size)||sha==lastSha||sha==rejected)return false;
    if(alreadyRunning(sha,size)){saveSha(sha);return false;}
    display("正在校验更新",0);
    if(install(sha,size)){report("installed");display("更新完成，正在重启",100);delay(350);ESP.restart();}
    rejected=sha;report("failed");Serial.println("OTA failed; current app retained");display("更新未完成，保留当前版本");delay(1800);
    screen.deleteSprite();
    return true;
}
}

// Shared Faces infrastructure, adapted from StopWatch's two-profile connector.
#include "FacesNetwork.h"
#include "ConnectionPolicy.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cJSON.h>
#include <mbedtls/md.h>
namespace faces_network {
namespace {
Settings cfg;WiFiUDP udp;bool udpReady=false,started=false,attempting=false;
connection_policy::RetryWait retry;
int attempt=-1,connected=-1;uint32_t searchAt=0,attemptAt=0,discoverAt=0;
String bridgeHost,bridgeToken,nonce;uint16_t bridgePort=8766;
String str(cJSON* j,const char* k){auto v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";}
String digest(const String& key,const String& body){
 unsigned char out[32];mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const unsigned char*)key.c_str(),key.length(),(const unsigned char*)body.c_str(),body.length(),out);
 char hex[65];for(int i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",out[i]);return hex;
}
void resetBridge(){udp.stop();udpReady=false;nonce="";discoverAt=0;bridgeHost="";bridgeToken="";bridgePort=8766;}
}
Settings load(){
 Settings s;Preferences p;
 if(p.begin("faces-connect",true)){
  String raw=p.getString("config");p.end();auto j=cJSON_Parse(raw.c_str());
  if(j){auto v=cJSON_GetObjectItem(j,"version");auto a=cJSON_GetObjectItem(j,"profiles");
   if(cJSON_IsNumber(v)&&v->valueint==1&&cJSON_GetArraySize(a)==2){
    for(int i=0;i<2;++i){auto q=cJSON_GetArrayItem(a,i);auto& f=s.profiles[i];f.ssid=str(q,"ssid");f.password=str(q,"password");f.host=str(q,"host");f.token=str(q,"token");auto port=cJSON_GetObjectItem(q,"port");if(cJSON_IsNumber(port)&&connection_policy::validPort(port->valueint))f.port=port->valueint;}
    auto mode=cJSON_GetObjectItem(j,"selected");if(cJSON_IsNumber(mode)&&mode->valueint>=-1&&mode->valueint<=1)s.selected=mode->valueint;
    cJSON_Delete(j);return s;
   }cJSON_Delete(j);
  }
 }
 // Read the old namespaces without rewriting them. Old firmware remains usable.
 if(p.begin("faces-music",true)){auto& f=s.profiles[0];f.ssid=p.getString("ssid");f.password=p.getString("pass");f.host=p.getString("host");f.token=p.getString("token");f.port=p.getUShort("port",8766);p.end();}
 if(p.begin("faces-radio",true)){String ssid=p.getString("ssid"),pass=p.getString("pass");p.end();
  if(s.profiles[0].ssid.isEmpty()){s.profiles[0].ssid=ssid;s.profiles[0].password=pass;}
  else if(!ssid.isEmpty()&&ssid!=s.profiles[0].ssid){s.profiles[1].ssid=ssid;s.profiles[1].password=pass;s.profiles[1].host=s.profiles[0].host;s.profiles[1].token=s.profiles[0].token;}
 }return s;
}
bool save(const Settings& s){
 auto j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"version",1);cJSON_AddNumberToObject(j,"selected",s.selected);auto a=cJSON_AddArrayToObject(j,"profiles");
 for(const auto& f:s.profiles){auto q=cJSON_CreateObject();cJSON_AddItemToArray(a,q);cJSON_AddStringToObject(q,"ssid",f.ssid.c_str());cJSON_AddStringToObject(q,"password",f.password.c_str());cJSON_AddStringToObject(q,"host",f.host.c_str());cJSON_AddStringToObject(q,"token",f.token.c_str());cJSON_AddNumberToObject(q,"port",f.port);}
 char* raw=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!raw)return false;String json(raw);cJSON_free(raw);
 Preferences p;if(!p.begin("faces-connect",false))return false;p.putString("config",json);bool ok=p.getString("config")==json;p.end();return ok;
}
void begin(bool powerSave){cfg=load();resetBridge();started=true;retry.reset();attempting=false;attempt=-1;connected=-1;searchAt=millis();WiFi.mode(WIFI_STA);WiFi.setSleep(powerSave);WiFi.setAutoReconnect(false);tick();}
bool choose(int selected){if(selected<-1||selected>1||(selected>=0&&cfg.profiles[selected].ssid.isEmpty()))return false;auto next=cfg;next.selected=selected;if(!save(next))return false;cfg=next;WiFi.disconnect(false,false);resetBridge();connected=-1;attempt=-1;retry.reset();attempting=false;searchAt=millis();return true;}
bool hasWifi(){return !cfg.profiles[0].ssid.isEmpty()||!cfg.profiles[1].ssid.isEmpty();}
bool hasBridge(){return !cfg.profiles[0].token.isEmpty()||!cfg.profiles[1].token.isEmpty();}
void tick(){
 if(!started)return;uint32_t now=millis();
 if(WiFi.status()!=WL_CONNECTED){
  if(connected>=0){connected=-1;resetBridge();attempting=false;retry.reset();searchAt=now;}
  if(!hasWifi())return;
  if(retry.waiting()){
   if(!retry.resume(now))return;
   searchAt=now;attempt=-1;attempting=false;
   Serial.println("NETWORK retrying saved Wi-Fi profiles");
  }
  if(now-searchAt>=connection_policy::searchMs){retry.start(now);attempting=false;WiFi.disconnect(false,false);return;}
  if(attempting){if(now-attemptAt<connection_policy::attemptMs)return;WiFi.disconnect(false,false);attempting=false;if(cfg.selected>=0){retry.start(now);return;}}
  attempt=connection_policy::next(attempt,!cfg.profiles[0].ssid.isEmpty(),!cfg.profiles[1].ssid.isEmpty(),cfg.selected);
  if(attempt<0){retry.start(now);return;}auto& f=cfg.profiles[attempt];WiFi.begin(f.ssid.c_str(),f.password.c_str());attempting=true;attemptAt=now;return;
 }
 if(connected<0){connected=attempt>=0?attempt:0;attempting=false;retry.reset();auto& f=cfg.profiles[connected];bridgeHost=f.host;bridgeToken=f.token;bridgePort=f.port;}
 if(!hasBridge())return;
 if(!udpReady)udpReady=udp.begin(0)==1;
 if(!udpReady)return;
 int length=udp.parsePacket();if(length>0){char raw[512];int n=udp.read(raw,sizeof(raw)-1);if(n>0){raw[n]=0;String packet(raw);int last=packet.lastIndexOf('|');
  if(last>0&&!nonce.isEmpty()&&now-discoverAt<5000){String body=packet.substring(0,last),signature=packet.substring(last+1);String prefix="FACES_BRIDGE_V1|"+nonce+"|";
   if(body.startsWith(prefix)){String portText=body.substring(prefix.length());int p=portText.toInt();if(connection_policy::validPort(p)&&String(p)==portText){
    // Discovery proves possession of an existing pairing token before any HTTP token is sent.
    const auto& f=cfg.profiles[connected];if(!f.token.isEmpty()&&signature==digest(f.token,body)){bridgeHost=udp.remoteIP().toString();bridgeToken=f.token;bridgePort=p;}
   }}
  }
 }}
 if(discoverAt==0||now-discoverAt>=5000){discoverAt=now;char value[33];snprintf(value,sizeof(value),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());nonce=value;
  IPAddress ip=WiFi.localIP(),mask=WiFi.subnetMask(),broadcast;for(int i=0;i<4;++i)broadcast[i]=ip[i]|~mask[i];String q="FACES_DISCOVER_V1|"+nonce;
  udp.beginPacket(broadcast,42116);udp.write((const uint8_t*)q.c_str(),q.length());udp.endPacket();
 }
}
const Settings& settings(){return cfg;}bool failed(){return retry.waiting();}int activeProfile(){return connected;}
String status(){if(WiFi.status()==WL_CONNECTED)return WiFi.SSID();if(!hasWifi())return "尚未保存网络";if(retry.waiting())return "连接未成功，稍后自动重试";return "正在连接 Wi-Fi";}
String host(){return bridgeHost;}String token(){return bridgeToken;}uint16_t port(){return bridgePort;}
}

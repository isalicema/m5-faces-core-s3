// StopWatch connection-center flow adapted to Faces' 320x240 touch/keyboard UI.
#include <M5Unified.h>
#include <M5Faces.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Launcher.h"
#include "FacesNetwork.h"
#include "ConnectionPolicy.h"
#include "DirectInput.h"
#include "KeyboardIndicators.h"
#include "SmoothMusicFonts.h"
#include "FacesScreenPower.h"
#include "FacesSideLights.h"
namespace connection_app {
namespace {
M5Canvas canvas(&M5.Display);M5Faces_Keyboard3 keyboard;DirectInput input;KeyboardIndicators indicators;
WebServer web(80);DNSServer dns;bool portal=false,scanning=false,keyboardOK=false;uint32_t lastDraw=0,lastPoll=0,lastHealth=0,restartAt=0;
String apName,apPassword,options,notice,csrf;
String escape(const String& value){String out;for(char c:value){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=c;}}return out;}
String page(){
 String h=R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Faces 连接中心</title><style>*{box-sizing:border-box}body{margin:0;background:#141b1b;color:#f6f1e5;font:16px -apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif}main{max-width:520px;margin:auto;padding:28px 20px}h1{font-size:25px}p,small{color:#b7c4bc;line-height:1.6}section{padding:18px;margin:20px 0;background:#23332f;border:1px solid #4d6459;border-radius:16px}h2{margin:0}label{display:block;margin:16px 0 6px}input,select,button{width:100%;padding:12px;border:1px solid #698073;border-radius:10px;background:#182420;color:#f6f1e5;font:inherit}input[type=checkbox]{width:auto}button{background:#ead5a8;color:#1c2924;font-weight:600}details{margin-top:20px}</style><main><h1>Faces 连接中心</h1><p>音乐、收音机与无线更新共用网络。保存家庭和公司两套配置，或只填一套。</p><form method="post" action="/save">)HTML";
 h+="<input type=hidden name=csrf value='"+csrf+"'>";
 auto cfg=faces_network::settings();
 for(int i=0;i<2;++i){auto& f=cfg.profiles[i];String id=String(i);h+="<section><h2>"+String(i==0?"家庭":"公司")+"</h2><label>附近的 2.4GHz Wi-Fi</label><select onchange=\"if(this.value)document.getElementById('ssid"+id+"').value=this.value\"><option value=''>选择附近网络</option>"+options+"</select><label>Wi-Fi 名称</label><input id='ssid"+id+"' name='ssid"+id+"' maxlength=32 value='"+escape(f.ssid)+"'><label>Wi-Fi 密码</label><input type=password name='pass"+id+"' maxlength=63 placeholder='同一网络留空保留原密码' autocomplete=new-password><label><input type=checkbox name='open"+id+"' value=1> 这是无密码网络</label><details><summary>Mac 连接（只听收音机可不填）</summary><label>Faces 配对密钥</label><input type=password name='token"+id+"' maxlength=128 autocomplete=off placeholder='留空保留原密钥'><label>Mac IPv4 地址（可选）</label><input name='host"+id+"' value='"+escape(f.host)+"' placeholder='留空自动寻找已配对 Mac'><label>Bridge 端口</label><input name='port"+id+"' type=number min=1 max=65535 value='"+String(f.port)+"'></details></section>";}
 h+="<p>配对密钥在 Mac 的 Faces 预览页 → 连接 Faces 中查看。两处使用同一台 Mac 时填写同一个密钥。密码和密钥不会回显或广播。</p><button>保存并返回首页</button></form><p>支持普通 2.4GHz 网络；需要网页认证的 Wi-Fi 不能直接使用。清空第二套网络名称可移除该配置。</p></main></html>";return h;
}
void saveForm(){
 if(web.arg("csrf")!=csrf){web.send(403,"text/plain; charset=utf-8","请刷新配网页再提交");return;}
 auto next=faces_network::settings();
 for(int i=0;i<2;++i){String id=String(i);auto& f=next.profiles[i];String ssid=web.arg("ssid"+id),pass=web.arg("pass"+id),host=web.arg("host"+id),token=web.arg("token"+id);host.trim();token.trim();String portText=web.arg("port"+id);long port=portText.toInt();
  if(ssid.length()>32||pass.length()>63||!connection_policy::validPort(port)||String(port)!=portText){web.send(400,"text/plain; charset=utf-8","请检查网络名称、密码或端口");return;}
  if(ssid.isEmpty()){f=faces_network::Profile{};continue;}
  bool open=web.arg("open"+id)=="1";
  if(!connection_policy::validPassword(f.ssid.c_str(),f.password.c_str(),ssid.c_str(),pass.c_str(),open)){web.send(400,"text/plain; charset=utf-8","请填写至少八位密码，或勾选无密码网络");return;}
  f.password=connection_policy::passwordFor(f.ssid.c_str(),f.password.c_str(),ssid.c_str(),pass.c_str(),open).c_str();
  IPAddress ip;if(!host.isEmpty()&&!ip.fromString(host)){web.send(400,"text/plain; charset=utf-8","Mac 地址需要填写 IPv4 或留空");return;}
  if(!token.isEmpty()){bool valid=token.length()>=32&&token.length()<=128;for(char c:token)if(!isalnum(c)&&c!='-'&&c!='_')valid=false;if(!valid){web.send(400,"text/plain; charset=utf-8","请检查 Faces 配对密钥");return;}f.token=token;}
  f.ssid=ssid;f.host=host;f.port=port;
 }
 if(next.profiles[0].ssid.isEmpty()&&next.profiles[1].ssid.isEmpty()){web.send(400,"text/plain; charset=utf-8","请至少填写一个网络");return;}
 next.selected=-1;
 if(!faces_network::save(next)){web.send(500,"text/plain; charset=utf-8","保存失败，原配置保留");return;}
 web.send(200,"text/html; charset=utf-8","<meta name=viewport content='width=device-width'><h2>已保存</h2><p>Faces 正在返回首页。手机可以重新连接原来的 Wi-Fi。</p>");restartAt=millis()+1500;
}
void redirect(){web.sendHeader("Location","http://192.168.4.1/",true);web.send(302,"text/plain","");}
void startPortal(){
 if(portal||scanning)return;scanning=true;notice="正在扫描附近网络";
 WiFi.disconnect(false,false);WiFi.mode(WIFI_STA);WiFi.scanNetworks(true,true);
}
void finishPortal(){
 int n=WiFi.scanComplete();if(n==WIFI_SCAN_RUNNING)return;
 options="";for(int i=0;i<n&&i<24;++i){String s=escape(WiFi.SSID(i));if(!s.isEmpty()&&options.indexOf("value='"+s+"'")<0)options+="<option value='"+s+"'>"+s+"</option>";}WiFi.scanDelete();
 char p[17];snprintf(p,sizeof(p),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());csrf=p;
 snprintf(p,sizeof(p),"%08lx",(unsigned long)esp_random());apPassword=p;apName="Faces-Setup-"+String(uint32_t(ESP.getEfuseMac()),HEX).substring(0,4);
 WiFi.mode(WIFI_AP);if(!WiFi.softAP(apName.c_str(),apPassword.c_str())){scanning=false;notice="热点启动失败，请重试";return;}
 dns.start(53,"*",WiFi.softAPIP());web.on("/",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send(200,"text/html; charset=utf-8",page());});web.on("/save",HTTP_POST,saveForm);web.onNotFound(redirect);web.begin();portal=true;scanning=false;notice="";
}
void text(int x,int y,const String& s,uint16_t color=0xF79D){canvas.setTextColor(color);canvas.setCursor(x,y);canvas.print(s);}
String fit(String s,int width){while(canvas.textWidth(s)>width&&s.length()){int i=s.length()-1;while(i>0&&(uint8_t(s[i])&0xc0)==0x80)--i;s.remove(i);}return s;}
void draw(){if(!faces_screen::appVisible())return;canvas.fillSprite(0x10E4);canvas.setFont(music_fonts::large());text(12,9,"<");text(50,9,"连接中心");canvas.setFont(music_fonts::small());
 if(portal){text(16,49,"手机连接以下 Wi-Fi");text(16,76,apName);text(16,101,"密码："+apPassword);text(16,132,"浏览器打开 192.168.4.1");text(16,162,"保存后，三个联网入口共用");text(16,213,"Q 返回首页");}
 else{ text(14,34,fit(notice.isEmpty()?faces_network::status():notice,290),0xBDF7);
 const char* labels[]={"自动选择","家庭网络","公司网络","添加或修改网络"};auto cfg=faces_network::settings();
 for(int i=0;i<4;++i){int y=57+i*37;bool current=i==0?cfg.selected<0:i<3&&faces_network::activeProfile()==i-1;
 canvas.fillSmoothRoundRect(12,y,296,32,8,current?0x29A8:0x2105);text(23,y+7,labels[i]);if(i==1||i==2){String sub=cfg.profiles[i-1].ssid;canvas.setTextColor(0xBDF7);text(127,y+7,fit(sub.isEmpty()?"未配置":sub,164),0xBDF7);}else text(265,y+7,String(i==0?'A':'E'),0xE6D3);
 }text(14,212,scanning?"正在扫描，可按 Q 返回":"A 自动 · H 家庭 · W 公司 · E 编辑",0xBDF7);}
 canvas.pushSprite(0,0);
}
void select(int row){if(portal||scanning)return;if(row==3){startPortal();return;}int profile=row-1;if(!faces_network::choose(profile))notice="该网络未配置，或保存失败";else notice="";draw();}
bool connectKeyboard(){if(keyboard.begin(&M5.In_I2C,M5FACES_BOTTOM3_ADDR,100000)!=M5FACES_OK)return false;m5faces_mode_t mode;if(keyboard.getMode(&mode)!=M5FACES_OK)return false;if(mode!=M5FACES_MODE_DIRECT&&keyboard.setMode(M5FACES_MODE_DIRECT)!=M5FACES_OK)return false;input=DirectInput{};indicators.reset();return true;}
}
void setup(){Serial.begin(115200);auto cfg=M5.config();cfg.fallback_board=m5::board_t::board_M5StackCoreS3;cfg.internal_spk=false;cfg.internal_mic=false;cfg.internal_imu=false;M5.begin(cfg);faces_lights::begin();M5.Display.setRotation(1);M5.Display.setBrightness(85);canvas.setColorDepth(16);if(!canvas.createSprite(320,240)){while(true)delay(100);}canvas.setTextWrap(false);music_fonts::begin();if(!M5.In_I2C.isEnabled())M5.In_I2C.begin(I2C_NUM_1,12,11);keyboardOK=connectKeyboard();faces_network::begin();draw();}
void loop(){M5.update();faces_screen::tick();uint32_t now=millis();if(portal){dns.processNextRequest();web.handleClient();}else if(scanning)finishPortal();else faces_network::tick();
 if(restartAt&&int32_t(now-restartAt)>=0)suite::open(suite::App::Home);
 if(keyboardOK&&now-lastPoll>=5){lastPoll=now;uint8_t raw[10]={};if(keyboard.readReg(M5FACES_REG_KEY,raw,sizeof(raw))==M5FACES_OK)input.feed(raw,now);input.tick(now,[](uint8_t index,uint8_t layer){if(!suite::inputReady())return;auto c=M5Faces_Keyboard3::KEYMAP[index][layer];if(c=='q'||c=='Q'||c==KEYBOARD3_KEY_ESC)suite::open(suite::App::Home);if(c=='a'||c=='A')select(0);if(c=='1'||c=='h'||c=='H')select(1);if(c=='2'||c=='w'||c=='W')select(2);if(c=='e'||c=='E')select(3);});indicators.sync(input,[](uint8_t m){return keyboard.setLED(m)==M5FACES_OK;});}
 if(now-lastHealth>5000){lastHealth=now;if(!keyboardOK)keyboardOK=connectKeyboard();}
 auto t=M5.Touch.getDetail();if(faces_screen::touchEnabled()&&t.wasPressed()&&suite::inputReady()){if(t.y<40&&t.x<70)suite::open(suite::App::Home);if(t.x>=12&&t.x<308&&t.y>=57&&t.y<200){int row=(t.y-57)/37;if((t.y-57)%37<32)select(row);}}
 if(now-lastDraw>=120){lastDraw=now;draw();}faces_screen::render();delay(5);
}
}

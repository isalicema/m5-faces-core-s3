#pragma once
#include <Arduino.h>
namespace faces_network {
struct Profile {String ssid,password,host,token;uint16_t port=8766;};
struct Settings {Profile profiles[2];int selected=-1;};
Settings load();
bool save(const Settings&);
void begin(bool powerSave=true);
void tick();
bool choose(int profile);
const Settings& settings();
bool hasWifi();bool hasBridge();bool failed();
String status();int activeProfile();
String host();String token();uint16_t port();
}

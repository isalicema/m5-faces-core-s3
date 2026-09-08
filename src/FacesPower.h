#pragma once
#include <M5Unified.h>
#include <cJSON.h>
#include "FacesPowerModel.h"
namespace faces_power {
// A single AXP2101 BAT input, not two independent battery gauges.
// Read-only; do not alter charging current, voltage or enable registers.
inline State read(){
    auto board=M5.getBoard();
    if(board!=m5::board_t::board_M5StackCoreS3&&board!=m5::board_t::board_M5StackCoreS3SE&&board!=m5::board_t::board_M5StackChan)return {};
    uint8_t status[2]={},level=255,bat[2]={},vbus[2]={};
    bool ok=M5.In_I2C.readRegister(0x34,0x00,status,2,100000)
        &&M5.In_I2C.readRegister(0x34,0xA4,&level,1,100000)
        &&M5.In_I2C.readRegister(0x34,0x34,bat,2,100000)
        &&M5.In_I2C.readRegister(0x34,0x38,vbus,2,100000);
    return decode(ok,status[0],status[1],level,((bat[0]&63)<<8)|bat[1],((vbus[0]&63)<<8)|vbus[1]);
}
inline void addJson(cJSON* root,const State& s){
    auto p=cJSON_AddObjectToObject(root,"power");
    cJSON_AddBoolToObject(p,"valid",s.valid);
    if(s.valid){cJSON_AddBoolToObject(p,"battery_present",s.present);cJSON_AddBoolToObject(p,"usb_present",s.usb);cJSON_AddBoolToObject(p,"charging",s.charging);}
    else{cJSON_AddNullToObject(p,"battery_present");cJSON_AddNullToObject(p,"usb_present");cJSON_AddNullToObject(p,"charging");}
    if(s.percent>=0)cJSON_AddNumberToObject(p,"percent",s.percent);else cJSON_AddNullToObject(p,"percent");
    if(s.batteryMv>=0)cJSON_AddNumberToObject(p,"battery_mv",s.batteryMv);else cJSON_AddNullToObject(p,"battery_mv");
    if(s.vbusMv>=0)cJSON_AddNumberToObject(p,"vbus_mv",s.vbusMv);else cJSON_AddNullToObject(p,"vbus_mv");
}
}

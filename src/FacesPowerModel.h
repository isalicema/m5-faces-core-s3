#pragma once
namespace faces_power {
struct State {
    bool valid=false,present=false,usb=false,charging=false;
    int percent=-1,batteryMv=-1,vbusMv=-1;
};
// Ported from StopWatch drawBatteryStatusAt: charging takes precedence.
enum class Tint {Normal,Charging,Low,Critical};
inline Tint tint(const State& s){
    if(s.valid&&s.present&&s.charging)return Tint::Charging;
    if(s.valid&&s.present&&s.percent>=0){
        if(s.percent<=15)return Tint::Critical;
        if(s.percent<=30)return Tint::Low;
    }
    return Tint::Normal;
}
inline int fillWidth(const State& s){
    if(!s.valid||!s.present)return 0;
    if(s.charging)return 20;
    return s.percent>=0&&s.percent<=100?20*s.percent/100:0;
}
inline State decode(bool ok,int status0,int status1,int level,int batteryMv,int vbusMv){
    State s;if(!ok)return s;
    s.valid=true;s.present=status0&8;s.usb=status0&32;
    s.charging=s.present&&((status1>>5)&3)==1;
    if(s.present&&batteryMv>=2500&&batteryMv<=4500){
        s.batteryMv=batteryMv;
        if(level>=0&&level<=100)s.percent=level;
    }
    if(!s.usb)s.vbusMv=0;
    else if(vbusMv>=4000&&vbusMv<=6000)s.vbusMv=vbusMv;
    return s;
}
inline const char* label(const State& s){
    if(!s.valid)return "电量未知";
    if(!s.present)return "未检测到电池";
    if(s.charging)return "充电中";
    if(s.usb)return "USB 供电";
    return "电池供电";
}
}

#include "../src/FacesPowerModel.h"
#include <cassert>
#include <cstring>
int main(){using namespace faces_power;
    auto s=decode(true,0x28,0x20,72,3980,5040);assert(s.valid&&s.present&&s.usb&&s.charging&&s.percent==72);assert(!strcmp(label(s),"充电中"));
    s=decode(true,0x08,0x40,50,3800,0);assert(!s.usb&&!s.charging&&s.percent==50);assert(!strcmp(label(s),"电池供电"));
    s=decode(true,0x20,0x20,100,4200,5050);assert(!s.present&&!s.charging&&s.percent==-1&&s.batteryMv==-1);
    s=decode(false,0x28,0x20,72,3980,5040);assert(!s.valid&&s.percent==-1);
    s=decode(true,0x28,0,255,3980,16383);assert(s.percent==-1&&s.vbusMv==-1);assert(!strcmp(label(s),"USB 供电"));
    s=decode(true,0x08,0,0,3200,0);assert(s.percent==0);
    s=decode(true,0x08,0,80,0,0);assert(s.percent==-1);
    for(int n=0;n<=100;++n){s=decode(true,8,0,n,3800,0);assert(tint(s)==(n<=15?Tint::Critical:n<=30?Tint::Low:Tint::Normal));assert(fillWidth(s)==20*n/100);}
    s=decode(true,0x28,0x20,5,3500,5050);assert(tint(s)==Tint::Charging&&fillWidth(s)==20);
    s=decode(false,0,0,0,0,0);assert(tint(s)==Tint::Normal&&fillWidth(s)==0);
}

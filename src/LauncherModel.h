#pragma once
#include <cstdint>
#include "HomeLayout.h"
namespace suite {
enum class App : uint32_t { Home=0, Music=1, Radio=2, Companion=3 };
constexpr uint32_t homeIdleMs=180000;
inline bool homeIdleDue(App app,uint32_t now,uint32_t lastInteraction,bool held=false){
    return app==App::Home&&!held&&uint32_t(now-lastInteraction)>=homeIdleMs;
}
constexpr uint32_t marker=0x46414345;
struct Ticket {uint32_t magic, app, check;};
inline Ticket ticket(App app){return {marker,uint32_t(app),marker^uint32_t(app)^0xA55A};}
inline App consume(Ticket& t,bool softwareReset){
    Ticket copy=t;t={};
    if(!softwareReset||copy.magic!=marker||copy.check!=(marker^copy.app^0xA55A)||copy.app>3)return App::Home;
    return App(copy.app);
}
inline App key(uint8_t c){return c=='m'||c=='M'?App::Music:c=='r'||c=='R'?App::Radio:c=='a'||c=='A'?App::Companion:App::Home;}
inline App touch(int x,int y){
    for(int i=0;i<3;++i){const auto& c=home_layout::cards[i];
        if(x>=c.x&&x<c.x+c.w&&y>=c.y&&y<c.y+c.h)return App(i+1);
    }
    return App::Home;
}
}

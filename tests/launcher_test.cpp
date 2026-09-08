#include <cassert>
#include <initializer_list>
#include "LauncherModel.h"
int main(){
    using namespace suite;
    assert(!homeIdleDue(App::Home,180999,1000));
    assert(homeIdleDue(App::Home,181000,1000));
    assert(!homeIdleDue(App::Home,181000,179000)); // Interaction restarts the timer.
    assert(!homeIdleDue(App::Home,181000,1000,true)); // Held input prevents entry.
    for(auto app:{App::Music,App::Radio,App::Companion})assert(!homeIdleDue(app,999999,0));
    uint32_t start=0xfffffff0;
    assert(!homeIdleDue(App::Home,uint32_t(start+179999),start));
    assert(homeIdleDue(App::Home,uint32_t(start+180000),start));
    for(auto app:{App::Home,App::Music,App::Radio,App::Companion}){
        auto t=ticket(app);assert(consume(t,true)==app);assert(consume(t,true)==App::Home);
        t=ticket(app);assert(consume(t,false)==App::Home);
    }
    auto t=ticket(App::Radio);t.check^=1;assert(consume(t,true)==App::Home);
    t=ticket(App(99));assert(consume(t,true)==App::Home);
    assert(key('m')==App::Music&&key('M')==App::Music);
    assert(key('r')==App::Radio&&key('R')==App::Radio);
    assert(key('a')==App::Companion&&key('A')==App::Companion);
    assert(touch(164,128)==App::Companion&&touch(305,211)==App::Companion);
    assert(key(27)==App::Home&&key('1')==App::Home);
    assert(touch(14,42)==App::Music&&touch(305,119)==App::Music);
    assert(touch(14,128)==App::Radio&&touch(155,211)==App::Radio);
    for(int y:{0,41,120,127,212,239})assert(touch(100,y)==App::Home);
    for(int x:{13,156,163,306})assert(touch(x,170)==App::Home);
    assert(touch(13,90)==App::Home);
}

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
    assert(touch(163,132)==App::Companion&&touch(311,213)==App::Companion);
    assert(key(27)==App::Home&&key('1')==App::Home);
    assert(key('s')==App::Connection&&key('S')==App::Connection);
    assert(touch(0,0)==App::Connection&&touch(189,33)==App::Connection);
    assert(touch(100,34)==App::Music); // Header must never steal the hero's top edge.
    assert(touch(8,34)==App::Music&&touch(311,125)==App::Music);
    assert(touch(8,132)==App::Radio&&touch(156,213)==App::Radio);
    for(int y:{126,127,131,214,239})assert(touch(100,y)==App::Home);
    for(int x:{7,157,162,312})assert(touch(x,170)==App::Home);
    assert(touch(7,90)==App::Home);
    assert(touch(240,17)==App::Home); // Battery is not a connection button.
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        int targets=(x<190&&y<34);
        for(const auto& c:home_layout::cards)
            targets+=(x>=c.x&&x<c.x+c.w&&y>=c.y&&y<c.y+c.h);
        assert(targets<=1); // Every physical pixel has at most one action.
    }
}

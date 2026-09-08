#include <cassert>
#include "ScreenPowerModel.h"
int main(){
    using namespace screen_power;
    Model s;
    assert(s.touchEnabled());
    assert(s.update(true,false,75,100));assert(s.phase()==Phase::SleepOut&&s.overlay());
    assert(s.brightness(100)==75&&s.brightness(700)<75);
    s.update(false,true,0,950);assert(s.phase()==Phase::Dark&&!s.appVisible()&&!s.touchEnabled());
    s.update(true,true,0,1000);assert(s.phase()==Phase::WakeIn&&s.brightness(1000)==0);
    s.update(false,true,0,1300);assert(s.phase()==Phase::Awake&&s.brightness(1300)==75&&!s.touchEnabled());
    s.update(false,false,75,1301);assert(s.touchEnabled());
    // A released long gesture restores the app, without claiming shutdown.
    s.update(false,true,75,2000,true);assert(s.phase()==Phase::ShutdownHold);
    s.update(false,false,60,2400,false,true);assert(s.phase()==Phase::WakeIn);
    s.update(false,false,75,2700);assert(s.phase()==Phase::Awake);
    // Long press while dark shows shutdown; cancel returns to dark.
    s.update(true,false,75,3000);s.update(false,false,0,3850);
    s.update(false,false,0,4000,true);assert(s.phase()==Phase::ShutdownHold&&s.brightness(4000)>0);
    s.update(false,false,50,4100,false,true);assert(s.phase()==Phase::Dark&&s.brightness(4100)==0);
    // Read failure cannot leave a misleading shutdown gesture onscreen.
    s.update(false,false,0,5000,true);s.update(false,false,50,5010,false,false,false);assert(s.phase()==Phase::Dark);
    // If long and release arrive together, release wins.
    s.update(false,false,0,6000,true,true);assert(s.phase()==Phase::Dark);
    // Two short clicks have no special action, including reversal mid-animation.
    s.update(true,false,0,7000);s.update(true,false,20,7100);assert(s.phase()==Phase::SleepOut);
    // A missed physical cutoff never causes software power-off or an 'off' state.
    s.update(false,false,0,8000,true);s.update(false,false,50,18000);assert(s.phase()==Phase::ShutdownHold&&s.brightness(18000)>0);
    Model wrap;uint32_t t=0xffffff00;
    wrap.update(true,false,85,t);wrap.update(false,false,0,uint32_t(t+850));assert(wrap.phase()==Phase::Dark);
}

#pragma once
#include <array>
#include <cstdint>

// Single UI-thread queue: polling may run inside a render, but actions must
// wait until the render releases its sprites and state locks.
class InputEventQueue {
public:
    struct Event { bool touch=false; uint8_t key=0; int16_t x=0,y=0; };
    bool push(Event event){
        if(count==events.size())return false;
        events[(head+count)%events.size()]=event;++count;return true;
    }
    bool pop(Event& event){
        if(!count)return false;
        event=events[head];head=(head+1)%events.size();--count;return true;
    }
private:
    std::array<Event,16> events{};
    unsigned head=0,count=0;
};

#pragma once
#include <cstdint>
#include "MusicLayout.h"
namespace music {
inline bool homeTouch(int x,int y) {
    return x>=0&&x<music_layout::homeTouchWidth&&y>=0&&y<music_layout::homeTouchHeight;
}
enum Action : uint8_t { None, Toggle, Previous, Next, Favorite, VolumeUp, VolumeDown, RepeatOne, RepeatAll };
inline const char* name(Action a) {
    constexpr const char* names[] = {"", "toggle", "previous", "next", "favorite", "volume_up", "volume_down", "repeat_one", "repeat_all"};
    return a <= RepeatAll ? names[a] : "";
}
inline Action key(uint8_t c) {
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    switch(c) {
        case ' ': return Toggle; case 'j': return Previous; case 'k': return Next;
        case 'h': return Favorite; case 'c': return RepeatOne; case 'v': return RepeatAll;
        default: return None;
    }
}
inline Action touch(int x,int y,int heartY=-100) {
    using namespace music_layout;
    if(x<0||x>=width||y<0||y>=height)return None;
    if(y>=buttonY&&y<buttonY+buttonHeight){
        const Action actions[]={Previous,Toggle,Next,Favorite,RepeatOne,RepeatAll};
        for(int i=0;i<buttonCount;++i)if(x>=buttonX[i]&&x<buttonX[i]+buttonWidth[i])return actions[i];
    }
    if(x>=metaX&&x<metaX+68&&y>=heartY&&y<heartY+heartHeight)return Favorite;
    return None;
}
}

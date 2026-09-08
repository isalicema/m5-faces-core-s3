#pragma once
#include <string>
#include <cstdint>
namespace music {
// Network refreshes can temporarily omit art or change media-session identifiers.
struct ArtworkState {
    std::string song, wanted, loaded;
    bool update(const std::string& nextSong,const std::string& art){
        bool clear=nextSong!=song && (art.empty()||art!=loaded);
        song=nextSong;wanted=art;if(clear)loaded.clear();return clear;
    }
    bool needsImage()const{return !wanted.empty()&&wanted!=loaded;}
    void accept(){loaded=wanted;}
};
struct LyricFade {
    std::string key;
    uint32_t started=0;
    void select(const std::string& next,uint32_t now){if(next!=key){key=next;started=now;}}
    float opacity(uint32_t now)const{return uint32_t(now-started)>=220?1.f:float(uint32_t(now-started))/220.f;}
    bool active(uint32_t now)const{return opacity(now)<1.f;}
};
}

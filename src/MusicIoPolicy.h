#pragma once
#include <cstdint>
namespace music_io {
inline bool cancelRead(bool readOnly,bool pending){return readOnly&&pending;}
struct ArtRetry {
    uint8_t pendingMisses=0;
    uint32_t nextDelay(int code,bool cancelled,bool success){
        if(cancelled)return 0;
        if(success){pendingMisses=0;return 0;}
        if(code!=404)return 3000;
        const uint32_t delays[]={250,500,1000,3000};
        auto delay=delays[pendingMisses];if(pendingMisses<3)++pendingMisses;return delay;
    }
};
}

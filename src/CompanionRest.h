#pragma once
#include <cstdint>

namespace companion {
constexpr uint32_t restAfterMs=180000;
inline bool restDue(uint32_t now,uint32_t lastInteraction,bool held){
    return !held&&uint32_t(now-lastInteraction)>=restAfterMs;
}
}

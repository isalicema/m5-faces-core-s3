#pragma once
#include <cstdint>
namespace music {
// A command receipt can precede the player's new metadata. Retry reads briefly;
// never replay the command, and return to the idle cadence after the burst.
struct SyncCadence {
    uint32_t actionAt = 0;
    bool triggered = false;
    void afterAction(uint32_t now) { actionAt = now; triggered = true; }
    bool burst(uint32_t now) const { return triggered && uint32_t(now-actionAt)<3000; }
    uint32_t interval(uint32_t now, bool visible=true, bool playing=true) const {
        return burst(now) ? 200 : !visible ? 5000 : playing ? 1800 : 3500;
    }
};
inline uint32_t batteryInterval(bool visible) { return visible?15000:60000; }
}

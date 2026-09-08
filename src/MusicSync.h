#pragma once
#include <cstdint>
namespace music {
// A command receipt can precede the player's new metadata. Retry reads briefly;
// never replay the command, and return to the idle cadence after the burst.
struct SyncCadence {
    uint32_t actionAt = 0;
    bool triggered = false;
    void afterAction(uint32_t now) { actionAt = now; triggered = true; }
    uint32_t interval(uint32_t now) const {
        return triggered && uint32_t(now - actionAt) < 3000 ? 200 : 1800;
    }
};
}

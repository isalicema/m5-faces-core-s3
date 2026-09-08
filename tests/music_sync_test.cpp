#include "../src/MusicSync.h"
#include <cassert>
int main() {
    music::SyncCadence s;
    assert(s.interval(0) == 1800);
    s.afterAction(100);
    assert(s.interval(100) == 200);
    assert(s.interval(3099) == 200);
    assert(s.interval(3100) == 1800);
    s.afterAction(3000); // A second real action extends the observation window.
    assert(s.interval(5999) == 200);
    assert(s.interval(6000) == 1800);
    s.afterAction(UINT32_MAX - 100);
    assert(s.interval(99) == 200);
    assert(s.interval(2999) == 1800);
}

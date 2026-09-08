#include "../src/MusicLight.h"
#include <cassert>
#include <iostream>
int main() {
    MusicLight light;
    auto black = light.frame(0, false, 1, 0.04f);
    for (auto p : black) assert(p.r == 0 && p.g == 0 && p.b == 0);
    for (uint8_t mode = 0; mode < 3; ++mode) {
        int cap = mode == 0 ? 0 : mode == 1 ? 8 : 16;
        for (int i = 0; i < 3000; ++i) {
            auto f = light.frame(i % 2 ? 32768 : 1000, true, mode, 0.04f);
            for (auto p : f) assert(p.r <= cap && p.g <= cap && p.b <= cap);
        }
    }
    auto alive = light.frame(32768, true, 2, 0.04f);
    assert(alive[0].b > 0);
    for (int i = 0; i < 150; ++i) black = light.frame(32768, false, 2, 0.04f);
    for (auto p : black) assert(p.r == 0 && p.g == 0 && p.b == 0);
    std::cout << "PASS: hard brightness caps, flowing palette, silence and stale audio fade to black\n";
}

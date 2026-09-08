#pragma once
#include "DirectInput.h"

// Keyboard3 reg 0xF1 presets; separate from the RGB side-light strip.
class KeyboardIndicators {
public:
    static uint8_t mode(const DirectInput& input) {
        constexpr uint8_t single[] = {0, 1, 7, 4, 3};
        constexpr uint8_t locked[] = {0, 2, 6, 5, 3};
        return input.locked() ? locked[input.activeLayer()] : single[input.activeLayer()];
    }
    void reset() { last_ = 255; }
    template<class Write> void sync(const DirectInput& input, Write write) {
        const auto desired = mode(input);
        if (desired != last_ && write(desired)) last_ = desired;
    }
private:
    uint8_t last_ = 255;
};

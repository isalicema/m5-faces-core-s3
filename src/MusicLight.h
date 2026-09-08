#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct LightPixel { uint8_t r, g, b; };
// Slow, bounded animation driven by decoded PCM amplitude, never microphone input.
class MusicLight {
    float energy = 0, phase = 0;
public:
    std::array<LightPixel, 10> frame(uint16_t amplitude, bool fresh, uint8_t mode, float seconds) {
        seconds = std::clamp(seconds, 0.0f, 0.1f);
        float target = fresh ? std::min(1.0f, amplitude / 6500.0f) : 0.0f;
        float rate = target > energy ? 0.25f : 0.65f;
        energy += (target - energy) * (1.0f - std::exp(-seconds / rate));
        if (energy < 0.005f) energy = 0;
        phase = std::fmod(phase + seconds * (0.035f + 0.065f * energy), 1.0f);
        constexpr float palette[4][3] = {{0.08f, 0.8f, 1.f}, {0.15f, 0.25f, 1.f}, {0.65f, 0.12f, 1.f}, {0.08f, 0.8f, 1.f}};
        const int cap = mode == 1 ? 8 : mode == 2 ? 16 : 0;
        std::array<LightPixel, 10> result{};
        for (int i = 0; i < 10; ++i) {
            const int position = i < 5 ? i : 9 - i;
            float p = std::fmod(phase + position * 0.09f, 1.f) * 3.f;
            int base = int(p); float mix = p - base;
            uint8_t values[3];
            for (int c = 0; c < 3; ++c) {
                float color = palette[base][c] * (1.f - mix) + palette[base + 1][c] * mix;
                values[c] = uint8_t(std::clamp(std::lround(color * cap * energy), 0l, long(cap)));
            }
            result[i] = {values[0], values[1], values[2]};
        }
        return result;
    }
};

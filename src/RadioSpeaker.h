#pragma once
#include <AudioOutput.h>
#include <M5Unified.h>
#include <atomic>

inline std::atomic<uint16_t> radioAmplitude{0};
inline std::atomic<uint32_t> radioPcmAt{0};

// Triple-buffered PCM adapter based on M5Unified's ESP8266Audio example.
class RadioSpeaker : public AudioOutput {
    int16_t buffers[3][640] = {};
    size_t index = 0, used = 0;
public:
    uint32_t submitted = 0;
    bool begin() override { submitted = 0; return true; }
    bool ConsumeSample(int16_t sample[2]) override {
        if (used == 640) { flush(); return false; }
        buffers[index][used++] = sample[0];
        buffers[index][used++] = sample[1];
        return true;
    }
    void flush() override {
        if (used && M5.Speaker.playRaw(buffers[index], used, hertz, true, 1, 0)) {
            uint32_t sum = 0;
            for (size_t i = 0; i < used; ++i) {
                int32_t value = buffers[index][i];
                sum += value < 0 ? -value : value;
            }
            radioAmplitude.store(sum / used);
            radioPcmAt.store(millis());
            ++submitted;
            used = 0;
            index = (index + 1) % 3;
        }
    }
    bool stop() override {
        M5.Speaker.stop(0);
        radioAmplitude.store(0);
        radioPcmAt.store(0);
        used = 0;
        return true;
    }
};

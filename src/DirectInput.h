#pragma once
#include <cstdint>

// Host interpretation of Keyboard3's active-low Direct matrix frames.
// Indices and layer columns match the official M5Faces_Keyboard3::KEYMAP.
class DirectInput {
public:
    static constexpr uint64_t keyMask(unsigned index) { return uint64_t(1) << index; }
    static constexpr uint64_t modifiers = (uint64_t(1) << 20) | (uint64_t(1) << 30) |
                                         (uint64_t(1) << 33) | (uint64_t(1) << 34);

    bool feed(const uint8_t raw[10], uint32_t now) {
        uint8_t sum = 0;
        for (unsigned i = 0; i < 10; ++i) sum += raw[i];
        if (raw[0] != 10 || sum != 0) return false;
        uint64_t mask = 0;
        constexpr uint8_t thirdRow[10] = {32, 28, 27, 26, 25, 24, 23, 22, 21, 31};
        for (unsigned row = 0; row < 3; ++row) {
            const uint8_t hi = raw[1 + row * 2];
            if (((hi >> 4) & 7) != row) return false;
            const uint16_t released = ((hi & 3) << 8) | raw[2 + row * 2];
            for (unsigned b = 0; b < 10; ++b) {
                if (!(released & (1U << b))) {
                    const unsigned index = row < 2 ? row * 10 + 9 - b : thirdRow[b];
                    mask |= keyMask(index);
                }
            }
        }
        if (((raw[7] >> 4) & 7) != 3) return false;
        constexpr uint8_t modIndices[5] = {20, 30, 29, 33, 34};
        for (unsigned b = 0; b < 5; ++b) if (!(raw[8] & (1U << b))) mask |= keyMask(modIndices[b]);
        if (mask != candidate_) { candidate_ = mask; changedAt_ = now; }
        return true;
    }

    template <typename Emit>
    bool tick(uint32_t now, Emit emit) {
        if (candidate_ == stable_ || uint32_t(now - changedAt_) < 25) return false;
        const uint64_t rising = candidate_ & ~stable_;
        stable_ = candidate_;
        constexpr unsigned indices[4] = {20, 33, 34, 30};
        for (unsigned layer = 1; layer <= 4; ++layer) {
            if (!(rising & keyMask(indices[layer - 1]))) continue;
            if (latch_ == layer && !locked_ && lastTap_ == layer && uint32_t(now - tappedAt_) <= 400) {
                locked_ = true;
            } else if (latch_ == layer) {
                latch_ = 0;
                locked_ = false;
            } else {
                latch_ = layer;
                locked_ = false;
            }
            lastTap_ = layer;
            tappedAt_ = now;
        }
        const uint8_t column = activeLayer();
        const uint64_t actions = rising & ~modifiers;
        for (unsigned index = 0; index < 35; ++index) {
            if (actions & keyMask(index)) emit(uint8_t(index), column);
        }
        if (actions && !locked_) { latch_ = 0; lastTap_ = 0; }
        return true;
    }

    uint8_t activeLayer() const {
        if (stable_ & keyMask(30)) return 4;
        if (stable_ & keyMask(33)) return 2;
        if (stable_ & keyMask(34)) return 3;
        if (stable_ & keyMask(20)) return 1;
        return latch_;
    }
    bool locked() const { return locked_; }
    bool anyPressed() const { return candidate_!=0||stable_!=0; }

private:
    uint64_t candidate_ = 0, stable_ = 0;
    uint32_t changedAt_ = 0, tappedAt_ = 0;
    uint8_t latch_ = 0, lastTap_ = 0;
    bool locked_ = false;
};

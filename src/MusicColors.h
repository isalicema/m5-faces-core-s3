#pragma once
#include <cstdint>
namespace music_colors {
// sRGB values from the approved preview; convert explicitly to LCD RGB565.
constexpr uint16_t rgb565(uint32_t rgb){return ((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x001f);}
constexpr uint16_t title=rgb565(0xf6f1e5);
constexpr uint16_t muted=rgb565(0xe0e4e2);
constexpr uint16_t header=rgb565(0xe5e7dd);
constexpr uint16_t lyric=rgb565(0xf4ead8);
constexpr uint16_t button=rgb565(0xfff8ed);
constexpr uint16_t progress=rgb565(0xf4e8c8);
constexpr uint16_t favorite=rgb565(0xff3b4d);
constexpr uint16_t unliked=rgb565(0xffd0ba);
}

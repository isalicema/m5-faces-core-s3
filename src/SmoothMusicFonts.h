#pragma once
#include <M5GFX.h>
extern const uint8_t faces10_start[] asm("_binary_assets_fonts_faces10_vlw_start");
extern const uint8_t faces12_start[] asm("_binary_assets_fonts_faces12_vlw_start");
extern const uint8_t faces16_start[] asm("_binary_assets_fonts_faces16_vlw_start");
extern const uint8_t faces16regular_start[] asm("_binary_assets_fonts_faces16regular_vlw_start");
namespace music_fonts {
inline lgfx::PointerWrapper tinyData(faces10_start);
inline lgfx::PointerWrapper bodyData(faces12_start), titleData(faces16_start);
inline lgfx::PointerWrapper regularData(faces16regular_start);
inline lgfx::VLWfont body, title, tinyFont, regularTitle;
inline bool ready=false;
inline bool begin(){if(ready)return true;ready=tinyFont.loadFont(&tinyData)&&body.loadFont(&bodyData)&&title.loadFont(&titleData)&&regularTitle.loadFont(&regularData);return ready;}
inline const lgfx::IFont* tiny(){return ready?static_cast<const lgfx::IFont*>(&tinyFont):&fonts::efontCN_12;}
inline const lgfx::IFont* small(){return ready?static_cast<const lgfx::IFont*>(&body):&fonts::efontCN_12;}
inline const lgfx::IFont* homeConnection(){return ready?static_cast<const lgfx::IFont*>(&regularTitle):&fonts::efontCN_14;}
inline const lgfx::IFont* large(){return ready?static_cast<const lgfx::IFont*>(&title):&fonts::efontCN_14;}
}

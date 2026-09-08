#include <Arduino.h>
#include <M5Unified.h>
#include <M5Faces.h>
#include <esp_system.h>
#include "DirectInput.h"

namespace {
M5Faces_Keyboard3 keyboard;
DirectInput input;
const char* const layerNames[] = {"abc", "ABC", "SYM", "Fn", "ALT"};
uint8_t lastLed = 0xFF;
M5Canvas canvas(&M5.Display);
String text;
String keyName = "Waiting for a key";
bool ready = false;
uint8_t version = 0;
uint8_t lastRaw = 0;
uint32_t events = 0;
uint32_t lastHealth = 0;
uint32_t lastPoll = 0;
uint32_t lastRender = 0;
bool dirty = true;
constexpr uint16_t bg = 0x1084;
constexpr uint16_t accent = 0x8CFF;
constexpr uint16_t ink = 0xFFFF;
constexpr uint16_t muted = 0xAD75;

void draw() {
    canvas.fillSprite(bg);
    canvas.setTextSize(2);
    canvas.setTextColor(ink, bg);
    canvas.setCursor(16, 12);
    canvas.print("FACES / TYPE TEST");
    canvas.setTextSize(1);
    canvas.setCursor(16, 40);
    canvas.setTextColor(ready ? TFT_GREEN : TFT_ORANGE, bg);
    if (ready) canvas.printf("Keyboard3 | DIRECT v0.2 | FW %02X", version);
    else canvas.print("Keyboard not ready - retrying...");

    canvas.fillRoundRect(12, 60, 296, 108, 8, 0x2126);
    canvas.setTextColor(ink, 0x2126);
    canvas.setTextSize(2);
    // Fixed-width ASCII viewport: retain the last six wrapped lines.
    String lines[6];
    int row = 0;
    for (unsigned int i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c == '\n' || lines[row].length() == 24) {
            if (row < 5) ++row;
            else {
                for (int j = 0; j < 5; ++j) lines[j] = lines[j + 1];
                lines[5] = "";
            }
        }
        if (c != '\n') lines[row] += c;
    }
    for (int i = 0; i <= row; ++i) {
        canvas.setCursor(16, 66 + i * 16);
        canvas.print(lines[i]);
    }
    if (text.isEmpty()) {
        canvas.setTextColor(muted, 0x2126);
        canvas.setCursor(16, 66);
        canvas.print("Type something...");
    }
    canvas.setTextColor(accent, bg);
    canvas.setTextSize(2);
    canvas.setCursor(16, 179);
    canvas.print(keyName.substring(0, 23));
    canvas.setTextSize(1);
    canvas.setTextColor(muted, bg);
    canvas.setCursor(16, 204);
    canvas.printf("Key: 0x%02X   Events: %lu", lastRaw, (unsigned long)events);
    canvas.setCursor(244, 204);
    canvas.printf("%s%s", layerNames[input.activeLayer()], input.locked() ? " LOCK" : "");
    canvas.setCursor(16, 225);
    canvas.print("DEL erase | ENTER newline");
    canvas.fillRoundRect(242, 214, 66, 24, 5, accent);
    canvas.setTextColor(bg, accent);
    canvas.setCursor(258, 222);
    canvas.print("CLEAR");
    canvas.pushSprite(0, 0);
    dirty = false;
}

bool connectKeyboard() {
    if (keyboard.begin(&M5.In_I2C, M5FACES_BOTTOM3_ADDR, 100000) != M5FACES_OK) return false;
    m5faces_mode_t mode;
    if (keyboard.getMode(&mode) != M5FACES_OK) return false;
    // Set only the input mode; never invoke the library's firmware upgrader.
    if (mode != M5FACES_MODE_DIRECT && keyboard.setMode(M5FACES_MODE_DIRECT) != M5FACES_OK) return false;
    input = DirectInput{};
    lastLed = 0xFF;
    return keyboard.getFirmwareVersion(&version) == M5FACES_OK;
}

void onKey(uint8_t raw) {
    if (raw == 0 || raw == 0xFF) return;
    lastRaw = raw;
    ++events;
    const char* name = M5Faces_Keyboard3::keyboard3_code_parse(raw);
    if (raw >= 0x20 && raw < 0x7F) {
        text += char(raw);
        keyName = raw == ' ' ? "SPACE" : String(char(raw));
    } else {
        keyName = name ? name : "Unknown key";
        if (raw == KEYBOARD3_KEY_BS || raw == KEYBOARD3_KEY_DEL) {
            if (!text.isEmpty()) text.remove(text.length() - 1);
        } else if (raw == KEYBOARD3_KEY_ENTER || raw == '\n') text += '\n';
        else if (raw == KEYBOARD3_KEY_TAB || raw == KEYBOARD3_FN_TAB) text += "    ";
        // ESC and navigation keys are shown without destroying the typed text.
    }
    if (text.length() > 1024) text.remove(0, text.length() - 1024);
    Serial.printf("KEY raw=0x%02X event=%lu\n", raw, (unsigned long)events);
    dirty = true;
}
}

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StackCoreS3;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.internal_imu = false;
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(150);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(320, 240)) {
        M5.Display.print("Display buffer allocation failed");
        while (true) delay(1000);
    }
    canvas.setTextWrap(false);
    text.reserve(1030);
    if (!M5.In_I2C.isEnabled()) M5.In_I2C.begin(I2C_NUM_1, 12, 11);
    ready = connectKeyboard();
    Serial.printf("BOOT faces-type-test 0.2 DIRECT reset=%d board=%d sda=%d scl=%d\n",
                  int(esp_reset_reason()), int(M5.getBoard()),
                  M5.getPin(m5::pin_name_t::in_i2c_sda), M5.getPin(m5::pin_name_t::in_i2c_scl));
    draw();
}

void loop() {
    M5.update();
    const uint32_t now = millis();
    if (ready && now - lastPoll >= 5) {
        lastPoll = now;
        uint8_t raw[10] = {};
        if (keyboard.readReg(M5FACES_REG_KEY, raw, sizeof(raw)) == M5FACES_OK) input.feed(raw, now);
    }
    if (ready) {
        bool emitted = false;
        if (input.tick(now, [&](uint8_t index, uint8_t layer) {
            const uint8_t mapped = M5Faces_Keyboard3::KEYMAP[index][layer];
            Serial.printf("MAP index=%u layer=%u code=0x%02X\n", index, layer, mapped);
            onKey(mapped);
            emitted = true;
        })) {
            if (!emitted && input.activeLayer()) {
                keyName = String(layerNames[input.activeLayer()]) + (input.locked() ? ": locked" : ": next key");
            }
            dirty = true;
        }
        constexpr uint8_t singleLED[] = {0, 1, 7, 4, 3};
        constexpr uint8_t lockedLED[] = {0, 2, 6, 5, 3};
        const uint8_t led = input.locked() ? lockedLED[input.activeLayer()] : singleLED[input.activeLayer()];
        if (led != lastLed && keyboard.setLED(led) == M5FACES_OK) lastLed = led;
    }
    if (now - lastHealth >= 5000) {
        lastHealth = now;
        const bool wasReady = ready;
        uint8_t model = 0;
        ready = ready ? keyboard.getModelID(&model) == M5FACES_OK && model == M5Faces_Keyboard3::MODEL_ID
                      : connectKeyboard();
        dirty |= ready != wasReady;
        Serial.printf("HEALTH ms=%lu keyboard=%s fw=0x%02X events=%lu heap=%u\n",
                      (unsigned long)now, ready ? "READY" : "MISSING", version,
                      (unsigned long)events, ESP.getFreeHeap());
    }
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed() && touch.x >= 242 && touch.y >= 214) {
        text = "";
        keyName = "Cleared";
        dirty = true;
    }
    if (dirty && now - lastRender >= 30) { lastRender = now; draw(); }
    delay(1);
}

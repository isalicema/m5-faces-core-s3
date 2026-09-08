#include "../src/DirectInput.h"
#include "../src/KeyboardIndicators.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

using Frame = std::array<uint8_t, 10>;
Frame frame(unsigned row = 4, unsigned b = 0, uint8_t mods = 0) {
    Frame f = {10, 0x03, 0xFF, 0x13, 0xFF, 0x23, 0xFF, 0x30, uint8_t(0x1F & ~mods), 0};
    if (row < 3) { const auto bits = uint16_t(0x3FF & ~(1U << b));
        f[1 + 2 * row] = uint8_t(row << 4) | (bits >> 8); f[2 + 2 * row] = bits; }
    for (unsigned i = 0; i < 9; ++i) f[9] -= f[i];
    return f;
}
struct Fixture {
    DirectInput input;
    uint32_t now = 1000;
    std::vector<std::pair<uint8_t, uint8_t>> events;
    void send(Frame f) {
        assert(input.feed(f.data(), now));
        now += 30;
        input.tick(now, [&](uint8_t index, uint8_t layer) { events.emplace_back(index, layer); });
        now += 30;
    }
};
int main() {
    Fixture idleHeld;assert(!idleHeld.input.anyPressed());
    idleHeld.send(frame(4,0,8));assert(idleHeld.input.anyPressed()); // SYM alone counts.
    idleHeld.send(frame());assert(!idleHeld.input.anyPressed());assert(idleHeld.input.activeLayer()==2); // Latched SYM is not idleHeld.

    // Physical keyboard retains its LED register while the host changes apps.
    uint8_t hardwareLED=7;
    KeyboardIndicators radioLED;
    Fixture indicator;
    unsigned writes=0;
    auto write=[&](uint8_t value){hardwareLED=value;++writes;return true;};
    radioLED.sync(indicator.input,write);assert(hardwareLED==0);
    radioLED.sync(indicator.input,write);assert(writes==1);
    indicator.send(frame(4,0,8));indicator.send(frame());
    radioLED.sync(indicator.input,write);assert(hardwareLED==7);
    // Home / Music must overwrite a retained SYM animation on entry.
    for(int page=0;page<2;++page){
        hardwareLED=7;KeyboardIndicators nextPage;DirectInput fresh;
        nextPage.sync(fresh,write);assert(hardwareLED==0);
    }
    indicator.now+=500;indicator.send(frame(4,0,8));indicator.send(frame());
    radioLED.sync(indicator.input,write);assert(hardwareLED==0);
    // Failed I2C writes are retried; reconnect forces a write even for idle.
    radioLED.reset();unsigned failures=0;
    radioLED.sync(indicator.input,[&](uint8_t){++failures;return false;});
    radioLED.sync(indicator.input,[&](uint8_t){++failures;return false;});assert(failures==2);
    hardwareLED=7;radioLED.sync(indicator.input,write);assert(hardwareLED==0);
    const uint8_t mods[]={1,8,16,2},singleModes[]={1,7,4,3},lockModes[]={2,6,5,3};
    for(unsigned i=0;i<4;++i){
        Fixture modifier;modifier.send(frame(4,0,mods[i]));modifier.send(frame());
        assert(KeyboardIndicators::mode(modifier.input)==singleModes[i]);
        modifier.send(frame(4,0,mods[i]));modifier.send(frame());
        assert(KeyboardIndicators::mode(modifier.input)==lockModes[i]);
    }
    // A tapped/released SYM applies to exactly the next key; plain 0 remains 0.
    Fixture tap;
    tap.send(frame(4, 0, 8)); tap.send(frame()); tap.send(frame(2, 9));
    assert(tap.events.size() == 1 && tap.events[0] == std::make_pair(uint8_t(31), uint8_t(2)));
    assert(KeyboardIndicators::mode(tap.input)==0);
    tap.send(frame()); tap.send(frame(2, 9));
    assert(tap.events.back().second == 0);
    // Simultaneous SYM+0 produces the same ESC mapping column.
    Fixture held; held.send(frame(2, 9, 8));
    assert(held.events[0] == tap.events[0]);
    assert(KeyboardIndicators::mode(held.input)==7);held.send(frame());assert(KeyboardIndicators::mode(held.input)==0);
    // Fn navigation and ALT use their own columns; rollover emits only new keys.
    Fixture fn; fn.send(frame(4, 0, 16)); fn.send(frame()); fn.send(frame(1, 2));
    assert(fn.events[0] == std::make_pair(uint8_t(17), uint8_t(3)));
    Fixture alt; alt.send(frame(0, 9, 2));
    assert(alt.events[0] == std::make_pair(uint8_t(0), uint8_t(4)));
    Fixture rollover; auto h = frame(1, 4); rollover.send(h);
    auto he = h; he[2] &= ~uint8_t(1 << 7); he[9] += uint8_t(1 << 7); rollover.send(he);
    assert(rollover.events.size() == 2 && rollover.events.back().first == 2);
    // Empty polls do not release keys, held frames do not repeat, release/repress does.
    Fixture repeat; repeat.send(frame(1, 1)); repeat.send(frame(1, 1));
    Frame empty{}; assert(!repeat.input.feed(empty.data(), repeat.now));
    repeat.send(frame(1, 1)); assert(repeat.events.size() == 1);
    repeat.send(frame()); repeat.send(frame(1, 1)); assert(repeat.events.size() == 2);
    // Tap aA then A selects uppercase, followed by ordinary lowercase.
    Fixture aa; aa.send(frame(4, 0, 1)); aa.send(frame()); aa.send(frame(1, 9));
    assert(aa.events[0] == std::make_pair(uint8_t(10), uint8_t(1)));
    aa.send(frame()); aa.send(frame(1, 9)); assert(aa.events.back().second == 0);
    // Double SYM locks symbols across keys. Tap once again to unlock.
    Fixture lock; lock.send(frame(4, 0, 8)); lock.send(frame()); lock.send(frame(4, 0, 8)); lock.send(frame());
    assert(lock.input.locked());assert(KeyboardIndicators::mode(lock.input)==6); lock.send(frame(0, 8)); lock.send(frame()); lock.send(frame(2, 9));
    assert(lock.events[0].second == 2 && lock.events[1].second == 2);
    lock.send(frame()); lock.send(frame(4, 0, 8)); lock.send(frame());
    assert(!lock.input.locked() && lock.input.activeLayer() == 0);
    assert(KeyboardIndicators::mode(lock.input)==0);
    // Enter is a physical modifier-row bit but must emit exactly one action.
    Fixture enter; enter.send(frame(4, 0, 4)); enter.send(frame(4, 0, 4));
    assert(enter.events.size() == 1 && enter.events[0].first == 29);
    // Bad checksum and bad row tags must not create phantom events.
    auto bad = frame(0, 9); bad[9] ^= 1; assert(!enter.input.feed(bad.data(), enter.now));
    bad = frame(0, 9); bad[1] ^= 0x10; bad[9] -= 0x10; assert(!enter.input.feed(bad.data(), enter.now));
    // Contact bounce shorter than the settle window emits nothing.
    Fixture bounce; auto down = frame(0, 9), up = frame();
    bounce.input.feed(down.data(), 1000); bounce.input.feed(up.data(), 1010);
    bounce.input.tick(1050, [&](uint8_t, uint8_t) { assert(false); });
    std::puts("PASS: app-entry LED reset/retry, all modifier indicators, ESC tap/hold, plain zero, repeated keys, uppercase, layer lock, Enter, corrupt frames, debounce");
}

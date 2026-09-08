#include <AudioFileSourceBuffer.h>
#include <algorithm>
#include <cassert>
#include <iostream>
DevNullOut silencedLogger;
Print* audioLogger = &silencedLogger;
class BurstySource : public AudioFileSource {
public:
    uint32_t offset = 0;
    bool available = true;
    bool isOpen() override { return true; }
    bool close() override { return true; }
    uint32_t read(void* dst, uint32_t count) override {
        count = available ? std::min(count, 4096u) : 0;
        for (uint32_t i = 0; i < count; ++i) static_cast<uint8_t*>(dst)[i] = (offset + i) % 251;
        offset += count;
        return count;
    }
};
int main() {
    BurstySource source;
    AudioFileSourceBuffer buffer(&source, 65536);
    uint8_t unused = 0;
    assert(buffer.read(&unused, 0) == 0);
    while (buffer.getFillLevel() < 49152) buffer.loop();
    uint8_t block[3000];
    uint32_t consumed = 0;
    for (int pass = 0; pass < 200; ++pass) {
        assert(buffer.read(block, sizeof(block)) == sizeof(block));
        for (size_t i = 0; i < sizeof(block); ++i) assert(block[i] == (consumed + i) % 251);
        consumed += sizeof(block);
    }
    source.available = false;
    while (buffer.getFillLevel() >= 8192) {
        assert(buffer.read(block, sizeof(block)) == sizeof(block));
        for (size_t i = 0; i < sizeof(block); ++i) assert(block[i] == (consumed + i) % 251);
        consumed += sizeof(block);
    }
    source.available = true;
    while (buffer.getFillLevel() < 49152) buffer.loop();
    assert(buffer.read(block, sizeof(block)) == sizeof(block));
    for (size_t i = 0; i < sizeof(block); ++i) assert(block[i] == (consumed + i) % 251);
    std::cout << "PASS: actual library prefill preserves byte zero, ring wrap, and refill after stall\n";
}

#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#define PSTR(x) x
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    template <class... Args> int printf_P(const char*, Args...) { return 0; }
};

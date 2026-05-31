#pragma once
#include <cstdint>

class Sleep{
public:
    void sleepMs(std::uint32_t ms);
    void sleepUs(std::uint64_t us);
    void sleepSec(std::uint32_t sec);
};

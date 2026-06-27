#pragma once
#include <cstdint>
#include <thread>
#include <chrono>

class Sleep{
public:
    static void sleepMs(std::uint32_t ms){
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    static void sleepUs(std::uint64_t us){
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }

    static void sleepSec(std::uint32_t sec){
        std::this_thread::sleep_for(std::chrono::seconds(sec));
    }
};

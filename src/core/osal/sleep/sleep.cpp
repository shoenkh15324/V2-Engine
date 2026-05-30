#include "sleep.hpp"
#include <thread>
#include <chrono>

namespace core::osal{

void Sleep::sleepMs(std::uint32_t ms){
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void Sleep::sleepUs(std::uint64_t us){
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void Sleep::sleepSec(std::uint32_t sec){
    std::this_thread::sleep_for(std::chrono::seconds(sec));
}

} // namespace core::osal

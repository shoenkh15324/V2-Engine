#pragma once
#include <cstdint>
#include <chrono>
#include <string>

class Time{
public:
    using MonoClock = std::chrono::steady_clock;
    using TimeStamp = MonoClock::time_point;
    using Duration = MonoClock::duration;
    static TimeStamp now();
    static int64_t nowMs();
    static int64_t nowUs();
    static int64_t nowNs();

    using SysClock = std::chrono::system_clock;
    struct Date{
        int year, month, day, hour, min, sec;
    };
    static std::time_t nowEpoch();
    static Date nowDate();
    static std::string nowDateString();

    static int64_t toMs(Duration d);
    static int64_t toUs(Duration d);
    static int64_t toNs(Duration d);
    static Duration ms(int64_t v);
    static Duration us(int64_t v);
    static Duration ns(int64_t v);
    static TimeStamp afterMs(int64_t ms);
    static TimeStamp afterUs(int64_t us);
    static TimeStamp afterNs(int64_t ns);
};

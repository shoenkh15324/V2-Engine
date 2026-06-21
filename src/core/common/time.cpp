#include "time.hpp"
#include "core/common/config.h"
#include <ctime>
#include <cstdio>

Time::TimeStamp Time::now(){
    return MonoClock::now();
}

int64_t Time::nowMs(){
    return toMs(MonoClock::now().time_since_epoch());
}

int64_t Time::nowUs(){
    return toUs(MonoClock::now().time_since_epoch());
}

int64_t Time::nowNs(){
    return toNs(MonoClock::now().time_since_epoch());
}

std::time_t Time::nowEpoch(){
    return SysClock::to_time_t(SysClock::now());
}

Time::Date Time::nowDate(){
    std::time_t t = nowEpoch();
    std::tm tm{};
#if V2_PLATFORM_LINUX
    localtime_r(&t, &tm);
#else
    localtime_s(&tm, &t);
#endif
    return {
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    };
}

std::string Time::nowDateString(){
    auto d = nowDate();
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf),
        "%04d-%02d-%02d %02d:%02d:%02d",
        d.year, d.month, d.day,
        d.hour, d.min, d.sec
    );
    return std::string(buf);
}

int64_t Time::toMs(Duration d){
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

int64_t Time::toUs(Duration d){
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

int64_t Time::toNs(Duration d){
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

Time::Duration Time::ms(int64_t v){
    return std::chrono::milliseconds(v);
}

Time::Duration Time::us(int64_t v){
    return std::chrono::microseconds(v);
}

Time::Duration Time::ns(int64_t v){
    return std::chrono::nanoseconds(v);
}

Time::TimeStamp Time::afterMs(int64_t ms){
    return now() + std::chrono::milliseconds(ms);
}

Time::TimeStamp Time::afterUs(int64_t us){
    return now() + std::chrono::microseconds(us);
}

Time::TimeStamp Time::afterNs(int64_t ns){
    return now() + std::chrono::nanoseconds(ns);
}

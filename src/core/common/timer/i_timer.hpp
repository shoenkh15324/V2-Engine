#pragma once
#include <cstdint>

class ITimer {
public:
    using Callback = void(*)(int, void*);

    virtual ~ITimer() = default;

    virtual int add(uint64_t delayMs, bool repeating, Callback cb, void* payload) = 0;
    virtual void cancel(int id) = 0;
    virtual void clear() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void handleTimerEvent() = 0;
    virtual bool isAlive(int id) const = 0;
    virtual bool isRepeating(int id) const = 0;
};

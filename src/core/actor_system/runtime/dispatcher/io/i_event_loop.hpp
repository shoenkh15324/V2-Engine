#pragma once
#include <functional>

class IEventLoop {
public:
    using Handler = std::function<void()>;
    using WatchedFd = int;

    virtual ~IEventLoop() = default;
    
    virtual void start() = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual int subscribe(WatchedFd fd, Handler handler) = 0;
    virtual int unsubscribe(WatchedFd fd) = 0;
};

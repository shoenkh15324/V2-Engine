#pragma once
#include <functional>

class IEventLoop {
public:
    using Handler = std::function<void()>;
    using WatchedFd = int;

    virtual ~IEventLoop() = default;
    virtual int subscribe(WatchedFd fd, Handler handler) = 0;
    virtual int unsubscribe(WatchedFd fd) = 0;
};

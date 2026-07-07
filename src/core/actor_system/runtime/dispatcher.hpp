#pragma once
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include "core/common/config/platform_config.h"
#include <semaphore>

#if V2_PLATFORM_LINUX
#include "core/common/os/epoll.hpp"
#endif

class ActorContext;

class Dispatcher{
private:
    using Handler = std::function<void()>;
#if V2_PLATFORM_LINUX
    using WatchedFd = int;
#endif

public:
    explicit Dispatcher(int workerCount, int epollMaxEvents = 64, int epollWaitTimeoutMs = 1000);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;
    Dispatcher(Dispatcher&&) = delete;
    Dispatcher& operator=(Dispatcher&&) = delete;

    void start();
    void run();
    void stop();
    void dispatch(ActorContext* actorCtx);
    ActorContext* acquire();
    int subscribe(WatchedFd fd, Handler handler);
    int unsubscribe(WatchedFd fd);
    bool isRunning() const { return running_; }

private:
    int workerCount_ = 0;
    int epollMaxEvents_ = 64;
    int epollWaitTimeoutMs_ = 1000;
    std::counting_semaphore<> sema_{0};
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
    std::unordered_map<WatchedFd, Handler> handlers_;

#if V2_PLATFORM_LINUX
    Epoll epoll_;
    int stopFd_ = -1;
    std::vector<epoll_event> epollEvents_;
#endif
};

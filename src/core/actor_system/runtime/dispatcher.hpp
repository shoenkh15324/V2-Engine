#pragma once
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <functional>
#include "core/common/semaphore.hpp"

#ifdef __linux__
#include "core/common/epoll.hpp"
#endif

class ActorContext;

class Dispatcher{
private:
    using Handler = std::function<void()>;
#ifdef __linux__
    using WatchedFd = int;
#endif

public:
    explicit Dispatcher(int workerCount = 1);
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
    Semaphore sema_{0};
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
    std::unordered_map<WatchedFd, Handler> handlers_;

#ifdef __linux__
    Epoll epoll_;
    int stopFd_ = -1;
#endif
};

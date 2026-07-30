#pragma once
#include <atomic>
#include <vector>
#include <thread>
#include <functional>
#include <unordered_map>
#include "core/common/os/epoll.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

class EventLoopEpoll : public IEventLoop {
public:
    explicit EventLoopEpoll(int maxEvents = 64, int waitTimeoutMs = 1000);
    ~EventLoopEpoll();

    EventLoopEpoll(const EventLoopEpoll&) = delete;
    EventLoopEpoll& operator=(const EventLoopEpoll&) = delete;
    EventLoopEpoll(EventLoopEpoll&&) = delete;
    EventLoopEpoll& operator=(EventLoopEpoll&&) = delete;

    void start();
    void run();
    void stop();
    int subscribe(WatchedFd fd, Handler handler) override;
    int unsubscribe(WatchedFd fd) override;

private:
    void post(std::function<void()> op);
    void drainPendingOps();

    std::atomic<bool> running_{false};
    std::vector<epoll_event> epollEvents_;
    std::unordered_map<WatchedFd, Handler> handlers_;
    LockFreeMpscQueue<std::function<void()>> pendingOps_{128};
    Epoll epoll_;
    int stopFd_ = -1;
    int maxEvents_ = 64;
    int waitTimeoutMs_ = 1000;
    std::thread::id threadId_;
};

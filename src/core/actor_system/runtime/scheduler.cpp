#include "scheduler.hpp"
#include "core/actor_system/actor/actor_context.hpp"

Scheduler::Scheduler() = default;

Scheduler::~Scheduler(){
    stop();
}

bool Scheduler::Compare::operator()(const TimerPtr& lhs, const TimerPtr& rhs) const {
    return lhs->expiry > rhs->expiry;
}

void Scheduler::start(){
    bool expected = false;
    if(!running_.compare_exchange_strong(expected, true)){
        return;
    }
    thread_ = std::thread(&Scheduler::threadFunc, this);
}

void Scheduler::stop(){
    bool expected = true;
    if(!running_.compare_exchange_strong(expected, false)){
        return;
    }
    cv_.notify_all();
    if(thread_.joinable()){
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    while(!queue_.empty()){
        queue_.pop();
    }
    timers_.clear();
}

int Scheduler::addTimer(Actor* target, Message message, uint64_t delayMs, bool repeating){
    auto timer = std::make_shared<Timer>();
    timer->id = nextId_++;
    timer->target = target;
    timer->message = std::move(message);
    timer->interval = std::chrono::milliseconds(delayMs);
    timer->expiry = Clock::now() + timer->interval;
    timer->repeating = repeating;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(timer);
        timers_[timer->id] = timer;
    }
    cv_.notify_one();
    return timer->id;
}

void Scheduler::cancel(int timerId){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(timerId);
    if(it == timers_.end()){
        return;
    }
    it->second->canceled.store(true, std::memory_order_relaxed);
    timers_.erase(it);
}

void Scheduler::threadFunc(){
    std::unique_lock<std::mutex> lock(mutex_);
    while(running_){
        if(queue_.empty()){
            cv_.wait(lock, [this]{ return (!running_ || !queue_.empty()); });
            continue;
        }
        auto timer = queue_.top();
        auto now = Clock::now();
        if(timer->expiry > now){
            cv_.wait_until(lock, timer->expiry);
            continue;
        }
        queue_.pop();
        if(timer->canceled.load(std::memory_order_relaxed)){
            continue;
        }
        if(!timer->repeating){
            timers_.erase(timer->id);
        }
        lock.unlock();
        timer->target->receiveMsg(Message(timer->message));
        lock.lock();
        if(timer->repeating && !timer->canceled.load(std::memory_order_relaxed)){
            timer->expiry += timer->interval;
            queue_.push(timer);
        }
    }
}

#include "timer_base.hpp"

int TimerBase::add(uint64_t delayMs, bool repeating, Callback cb, void* payload){
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = allocNode();
    auto& node = pool_[idx];
    node.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    node.expiry = now + std::chrono::milliseconds(delayMs);
    node.interval = std::chrono::milliseconds(delayMs);
    node.repeating = repeating;
    node.cb = cb;
    node.payload = payload;

    bool needReschedule = heap_.empty() || (node.expiry < pool_[heap_.top()].expiry);
    heap_.push(idx);
    timers_[node.id] = idx;
    if(needReschedule) scheduleNextTimer(now);
    return node.id;
}

void TimerBase::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return;
    pool_[it->second].alive = false;
    timers_.erase(it);
    purgeDeadEntries();
}

void TimerBase::clear(){
    std::lock_guard<std::mutex> lock(mutex_);
    heap_ = decltype(heap_){Compare{&pool_}};
    pool_.clear();
    freeList_.clear();
    timers_.clear();
    scheduleNextTimer(Clock::now());
}

bool TimerBase::isAlive(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    return (it != timers_.end()) && pool_[it->second].alive;
}

bool TimerBase::isRepeating(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return false;
    return pool_[it->second].repeating;
}

void TimerBase::handleTimerEvent(){
    onWake();
    executeExpiredTimers();
}

void TimerBase::purgeDeadEntries(){
    while(!heap_.empty() && !pool_[heap_.top()].alive){
        freeNode(heap_.top());
        heap_.pop();
    }
}

bool TimerBase::hasLiveTop() const {
    return !heap_.empty() && pool_[heap_.top()].alive;
}

TimerBase::Clock::time_point TimerBase::nextExpiry() const {
    return pool_[heap_.top()].expiry;
}

void TimerBase::executeExpiredTimers(){
    struct ReadyNode{
        int64_t id;
        Callback cb;
        void* payload;
    };
    std::vector<ReadyNode> ready;
    auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while(!heap_.empty() && (pool_[heap_.top()].expiry <= now)){
            int idx = heap_.top();
            heap_.pop();
            if(!pool_[idx].alive){
                freeNode(idx);
                continue;
            }
            ready.push_back({
                pool_[idx].id,
                pool_[idx].cb,
                pool_[idx].payload
            });
        }
    }
    for(auto& rn : ready){ // 콜백: 풀과 무관한 로컬 데이터로 호출 (락 없음)
        rn.cb(rn.id, rn. payload);
    }
    { // 재조회: id로 timers_ lookup — 콜백 중 cancel/add로 인한 풀 재할당 안전
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& rn : ready){
            auto it = timers_.find(rn.id);
            if(it == timers_.end()) continue;
            int idx = it->second;
            if(pool_[idx].repeating && pool_[idx].alive){
                pool_[idx].expiry += pool_[idx].interval;
                heap_.push(idx);
            }else{
                timers_.erase(rn.id);
                freeNode(idx);
            }
        }
        scheduleNextTimer(Clock::now());
    }
}

int TimerBase::allocNode(){
    if(!freeList_.empty()){
        int idx = freeList_.back();
        freeList_.pop_back();
        pool_[idx] = TimerNode{};
        return idx;
    }
    pool_.emplace_back();
    return static_cast<int>(pool_.size()) - 1;
}

void TimerBase::freeNode(int idx){
    pool_[idx] = TimerNode{};
    freeList_.push_back(idx);
}

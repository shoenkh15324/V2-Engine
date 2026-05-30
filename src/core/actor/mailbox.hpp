#pragma once
#include <vector>
#include <utility>
#include "../common/return.hpp"
#include "../../platform/osal/mutex/mutex.hpp"
#include "../../platform/osal/lock_guard/lock_guard.hpp"

namespace core::actor {

template <typename T>
class Mailbox {
private:
    using Result = core::common::Result;
    using Mutex = platform::osal::Mutex;
    using LockGuard = platform::osal::LockGuard;
public:
    explicit Mailbox(size_t size) : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0) {}
    
    Result push(const T& msg){
        LockGuard<Mutex> lock(mutex_);
        if (count_ == capacity_) return Result::Fail;
        buffer_[tail_] = msg;
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return Result::Ok;
    }

    Result push(T&& msg){
        LockGuard<Mutex> lock(mutex_);
        if (count_ == capacity_) return Result::Fail;
        buffer_[tail_] = std::move(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return Result::Ok;
    }

    Result pop(T& out){
        LockGuard<Mutex> lock(mutex_);
        if (count_ == 0) return Result::Fail;
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return Result::Ok;
    }

    bool empty(){
        LockGuard<Mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t capacity(){
        LockGuard<Mutex> lock(mutex_);
        return capacity_;
    }

    size_t count(){
        LockGuard<Mutex> lock(mutex_);
        return count_;
    }

private:
    std::vector<T> buffer_;
    mutable Mutex mutex_;
    size_t capacity_;
    size_t count_;
    size_t tail_;
    size_t head_;
};

} // namespace core::actor

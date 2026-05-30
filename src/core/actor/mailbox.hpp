#pragma once
#include <vector>
#include <utility>
#include "../osal/lock_guard/lock_guard.hpp"
#include "../osal/mutex/mutex.hpp"

namespace core::actor{
namespace osal = core::osal;

template <typename T>
class Mailbox {
public:
    explicit Mailbox(size_t size) : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0){}

    template <typename U>
    bool push(U&& msg){
        osal::LockGuard<osal::Mutex> lock(mutex_);
        if(count_ == capacity_){
            return false;
        }
        bool wasEmpty = (count_ == 0);
        buffer_[tail_] = std::forward<U>(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return wasEmpty;
    }

    bool pop(T& out) {
        osal::LockGuard<osal::Mutex> lock(mutex_);
        if(count_ == 0){
            return false;
        }
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    bool empty(){
        osal::LockGuard<osal::Mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t capacity(){
        osal::LockGuard<osal::Mutex> lock(mutex_);
        return capacity_;
    }

    size_t count(){
        osal::LockGuard<osal::Mutex> lock(mutex_);
        return count_;
    }

private:
    std::vector<T> buffer_;
    mutable osal::Mutex mutex_;
    size_t capacity_;
    size_t count_;
    size_t head_;
    size_t tail_;
};

} // namespace core::actor
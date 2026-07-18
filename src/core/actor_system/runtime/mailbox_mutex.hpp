#pragma once
#include <vector>
#include <utility>
#include <mutex>
#include "i_mailbox.hpp"

template <typename T>
class MutexMailbox : public IMailbox<T>{
public: 
    explicit MutexMailbox(size_t size) : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0){}

    MutexMailbox(const MutexMailbox&) = delete;
    MutexMailbox& operator=(const MutexMailbox&) = delete;
    MutexMailbox(MutexMailbox&&) = delete;
    MutexMailbox& operator=(MutexMailbox&&) = delete;

    bool push(T&& msg) override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == capacity_) return false;
        buffer_[tail_] = std::move(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool pop(T& out) override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == 0) return false;
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    size_t popBatch(T* out, size_t max) override{
        std::lock_guard<std::mutex> lock(mutex_);
        size_t n = 0;
        while(n < max && count_ > 0){
            out[n] = std::move(buffer_[head_]);
            head_ = (head_ + 1) % capacity_;
            --count_;
            ++n;
        }
        return n;
    }

    bool empty() const override{
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t capacity() const override{
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    size_t count() const override{
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    std::vector<T> buffer_;
    mutable std::mutex mutex_;
    size_t capacity_;
    size_t count_;
    size_t head_;
    size_t tail_;
};

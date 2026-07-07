#pragma once
#include <vector>
#include <utility>
#include <mutex>

template <typename T>
class Mailbox{
public:
    explicit Mailbox(size_t size) : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0){}

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    Mailbox(Mailbox&&) = delete;
    Mailbox& operator=(Mailbox&&) = delete;

    template <typename U>
    bool push(U&& msg){
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == capacity_){
            return false;
        }
        buffer_[tail_] = std::forward<U>(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool pop(T& out){
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == 0){
            return false;
        }
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    size_t count() const {
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

#pragma once
#include <atomic>
#include <vector>
#include <utility>
#include "i_mailbox.hpp"

#ifdef __cpp_lib_hardware_interference_size
    inline constexpr size_t kCacheLine = std::hardware_destructive_interference_size
#else
    inline constexpr size_t kCacheLine = 64;
#endif

template <typename T>
class LockFreeMailbox : public IMailbox<T>{
public:
    explicit LockFreeMailbox(size_t size) : buffer_(size), slot_(size), capacity_(size), head_(0), tail_(0){}

    LockFreeMailbox(const LockFreeMailbox&) = delete;
    LockFreeMailbox& operator=(const LockFreeMailbox&) = delete;
    LockFreeMailbox(LockFreeMailbox&&) = delete;
    LockFreeMailbox& operator=(LockFreeMailbox&&) = delete;

    bool push(T&& msg) override{
        size_t t = tail_.val.load(std::memory_order_relaxed);
        while(true){
            size_t h = head_.val.load(std::memory_order_acquire);
            if((t - h) >= capacity_) return false;
            if(tail_.val.compare_exchange_weak(t, t + 1, std::memory_order_relaxed)){
                buffer_[t % capacity_] = std::move(msg);
                slot_[t % capacity_].seq.store(t + 1, std::memory_order_release);
                return true;
            }
        }
    }

    bool pop(T& out) override{
        size_t h = head_.val.load(std::memory_order_relaxed);
        size_t t = tail_.val.load(std::memory_order_acquire);
        if(h >= t) return false;

        size_t seq = slot_[h % capacity_].seq.load(std::memory_order_acquire);
        if(seq != h + 1) return false;

        out = std::move(buffer_[h % capacity_]);
        head_.val.store(h + 1, std::memory_order_release);
        return true;
    }

    size_t popBatch(T* out, size_t max) override{
        size_t h = head_.val.load(std::memory_order_relaxed);
        size_t t = tail_.val.load(std::memory_order_acquire);
        size_t available = t - h;
        size_t n = (available < max) ? available : max;

        size_t count = 0;
        for(size_t i = 0; i < n; i++){
            size_t idx = h + i;
            size_t seq = slot_[idx % capacity_].seq.load(std::memory_order_acquire);
            if(seq != idx + 1) break;
            out[count] = std::move(buffer_[idx % capacity_]);
            ++count;
        }
        if(count > 0) head_.val.store(h + count, std::memory_order_release);
        return count;
    }

    bool empty() const override{
        return head_.val.load(std::memory_order_acquire) >= tail_.val.load(std::memory_order_acquire);
    }

    size_t capacity() const override { return capacity_; }

    size_t count() const override{
        size_t h = head_.val.load(std::memory_order_acquire);
        size_t t = tail_.val.load(std::memory_order_acquire);
        return (h <= t) ? (t - h) : 0;
    }

private:
    struct alignas(kCacheLine) AlignedSizeT{
        std::atomic<size_t> val{0};
    };

    struct alignas(kCacheLine) Slot{
        std::atomic<size_t> seq{0};
    };

    std::vector<T> buffer_;
    std::vector<Slot> slot_;
    const size_t capacity_;
    AlignedSizeT head_; // single consumer
    alignas(kCacheLine) AlignedSizeT tail_; // multi producer
};

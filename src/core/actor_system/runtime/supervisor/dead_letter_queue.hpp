#pragma once
#include <cstddef>
#include "core/actor_system/runtime/supervisor/i_dead_letter_queue.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"

inline constexpr size_t kDeadLetterQueueCapacity = 128;

class DeadLetterQueue : public IDeadLetterQueue{
public:
    explicit DeadLetterQueue(size_t capacity = kDeadLetterQueueCapacity) : queue_(capacity){}
    ~DeadLetterQueue() override = default;

    DeadLetterQueue(const DeadLetterQueue&) = delete;
    DeadLetterQueue& operator=(const DeadLetterQueue&) = delete;
    DeadLetterQueue(DeadLetterQueue&&) = delete;
    DeadLetterQueue& operator=(DeadLetterQueue&&) = delete;

    bool push(DeadLetter letter) override{
        return queue_.push(std::move(letter));
    }

    bool pop(DeadLetter& out) override{
        return queue_.pop(out);
    }

    size_t count() const noexcept override {
        return queue_.count();
    
    }
    size_t capacity() const noexcept override {
        return queue_.capacity();
    }

    void clear() override{
        DeadLetter letter;
        while(queue_.pop(letter)){}
    }

private:
    LockFreeMpscQueue<DeadLetter> queue_;
};

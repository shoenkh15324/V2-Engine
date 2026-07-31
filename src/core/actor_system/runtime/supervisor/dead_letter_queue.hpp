#pragma once
#include <string>
#include <cstddef>
#include "core/actor_system/messages/message.hpp"
#include "core/common/container/lock_free_mpsc_queue.hpp"

struct DeadLetter{
    uint64_t actorId = 0;
    std::string actorName = "";
    std::string reason = "";
    uint64_t timestampNs = 0;
    Message msg;
};

class DeadLetterQueue{
public:
    explicit DeadLetterQueue(size_t capacity = 128) : queue_(capacity){}
    ~DeadLetterQueue() = default;

    DeadLetterQueue(const DeadLetterQueue&) = delete;
    DeadLetterQueue& operator=(const DeadLetterQueue&) = delete;
    DeadLetterQueue(DeadLetterQueue&&) = delete;
    DeadLetterQueue& operator=(DeadLetterQueue&&) = delete;

    bool push(DeadLetter letter){
        return queue_.push(std::move(letter));
    }

    bool pop(DeadLetter& out){
        return queue_.pop(out);
    }

    size_t count() const noexcept {
        return queue_.count();
    
    }
    size_t capacity() const noexcept {
        return queue_.capacity();
    }

    void clear(){
        DeadLetter letter;
        while(queue_.pop(letter)){}
    }

private:
    LockFreeMpscQueue<DeadLetter> queue_;
};

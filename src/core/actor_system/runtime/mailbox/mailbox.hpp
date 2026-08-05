#pragma once
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/runtime/mailbox/i_mailbox.hpp"

class Mailbox : public IMailbox {
public:
    explicit Mailbox(size_t capacity) : queue_(capacity){}

    bool push(Message&& msg) override { return queue_.push(std::move(msg)); }
    bool pop(Message& out) override { return queue_.pop(out); }
    size_t count() const override { return queue_.count(); }
    size_t capacity() const override { return queue_.capacity(); }
    bool empty() const override { return queue_.empty(); }
    void clear() override { 
        Message tmp;
        while(queue_.pop(tmp)){}
    }

private:
    LockFreeMpscQueue<Message> queue_;
};
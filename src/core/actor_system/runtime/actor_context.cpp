#include "actor_context.hpp"
#include "dispatcher.hpp"

ActorContext::ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize)
: actor_(std::move(actor)), mailbox_(mailboxSize)
{
    actor_->context_ = this;
}

void ActorContext::enqueue(MessagePtr msg){
    bool wasEmpty = mailbox_.push(std::move(msg));
    if(!wasEmpty) return;
    if(dispatcher_ && !scheduled_.exchange(true)){
        dispatcher_->schedule(this);
    }
}

void ActorContext::run(int maxBatch){
    MessagePtr msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_.pop(msg)) break;
        actor_->handle(*msg);
        msg.reset();
        processed++;
    }
    scheduled_ = false;
    if(!mailbox_.empty()){
        dispatcher_->schedule(this);
    }
}

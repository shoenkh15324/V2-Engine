#include "actor.hpp"
#include "message.hpp"
#include "../runtime/dispatcher.hpp"

namespace core::actor{

Actor::Actor(size_t mailboxSize) : mailbox_(mailboxSize), dispatcher_{nullptr}{
}

void Actor::attachDispatcher(Dispatcher* dispatcher){
    dispatcher_ = dispatcher;
}

void Actor::send(MessagePtr msg){
    if(mailbox_.push(std::move(msg))){
        if(dispatcher_){
            dispatcher_->notify(this);
        }
    }
};

} // namespace core::actor

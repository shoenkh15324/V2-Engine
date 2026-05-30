#include "actor.hpp"
#include "../common/return.hpp"

namespace core::actor{

Actor::Actor(size_t mailboxSize) : mailbox_(mailboxSize), isRunning_(false){
}

void Actor::send(Message msg){
    mailbox_.push(std::move(msg));
}

void Actor::run(){
    isRunning_ = true;
    Message msg;
    while(isRunning_){
        if(mailbox_.pop(msg) == core::common::Result::Ok){
            handle(msg);
        }else{
            // 메시지 없으면 idle (busy spin 방지 여지)
            // 필요하면 sleep/yield 넣는 자리
        }
    }
}

void Actor::stop(){
    isRunning_ = false;
}

} // namespace core::actor

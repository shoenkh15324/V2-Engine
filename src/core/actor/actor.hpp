#pragma once
#include "message.hpp"
#include "mailbox.hpp"

namespace core::actor{

class Actor{
public:
    explicit Actor(size_t mailboxSize) : mailbox_(mailboxSize){}

    void send(Message msg){
        mailbox_.push(std::move(msg));
    };

    Mailbox<Message>& mailbox(){
        return mailbox_;
    };

protected:
    virtual void handle(const Message& msg) = 0;

private:
    Mailbox<Message> mailbox_;
};

} // namespace core::actor

#pragma once
#include "message.hpp"
#include "mailbox.hpp"

namespace core::actor{

class Actor{
public:
    explicit Actor(size_t mailboxSize);
    void send(Message msg);
    void run();
    void stop();

protected:
    virtual void handle(const Message& msg) = 0;

private:
    Mailbox<Message> mailbox_;
    bool isRunning_;
};

} // namespace core::actor

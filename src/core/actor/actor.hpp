#pragma once
#include "mailbox.hpp"

namespace runtime{
class Dispatcher;
} // namespace runtime

namespace core::actor{

class Message;

class Actor{
private:
    using Dispatcher = runtime::Dispatcher;

public:
    explicit Actor(size_t mailboxSize);
    void send(Message msg);
    void attachDispatcher(Dispatcher* dispatcher);

protected:
    virtual void handle(const Message& msg) = 0;

private:
    Mailbox<Message> mailbox_;
    Dispatcher* dispatcher_;
};

} // namespace core::actor

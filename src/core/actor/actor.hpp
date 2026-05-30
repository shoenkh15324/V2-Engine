#pragma once
#include "mailbox.hpp"
#include "message.hpp"

namespace core::runtime{
class Dispatcher;
} // namespace core::runtime

namespace core::actor{

class Actor{
private:
    using Dispatcher = runtime::Dispatcher;

public:
    explicit Actor(size_t mailboxSize);
    void send(MessagePtr msg);
    void attachDispatcher(Dispatcher* dispatcher);

protected:
    virtual void handle(const Message& msg) = 0;

private:
    Mailbox<MessagePtr> mailbox_;
    Dispatcher* dispatcher_;
};

} // namespace core::actor

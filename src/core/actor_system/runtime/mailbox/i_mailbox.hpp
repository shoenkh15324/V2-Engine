#pragma once
#include "core/actor_system/messages/message.hpp"

class IMailbox {
public:
    virtual ~IMailbox() = default;
    
    virtual bool push(Message&& msg) = 0;
    virtual bool pop(Message& out) = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
    virtual bool empty() const = 0;
    virtual void clear() = 0;
};

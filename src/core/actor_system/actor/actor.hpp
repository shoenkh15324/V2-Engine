#pragma once
#include <cstdint>
#include "message.hpp"

namespace core::runtime{
class ActorContext;
}

namespace core::actor{

class Actor{
    friend class runtime::ActorContext;
public:
    virtual void handle(const Message& msg) = 0;
    virtual ~Actor() = default;
    void sendMsg(Actor* target, MessagePtr msg);
    uint64_t id() const { return id_; }

private:
    runtime::ActorContext* context_ = nullptr;
    uint64_t id_ = 0;
};

} // namespace core::actor

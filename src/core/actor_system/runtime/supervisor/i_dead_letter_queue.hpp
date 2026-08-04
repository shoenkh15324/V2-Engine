#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "core/actor_system/messages/message.hpp"

struct DeadLetter {
    uint64_t actorId = 0;
    std::string actorName = "";
    std::string reason = "";
    uint64_t timestampNs = 0;
    Message msg;
};

class IDeadLetterQueue {
public:
    virtual ~IDeadLetterQueue() = default;

    virtual bool push(DeadLetter letter) = 0;
    virtual bool pop(DeadLetter& out) = 0;
    virtual size_t count() const noexcept = 0;
    virtual size_t capacity() const noexcept = 0;
    virtual void clear() = 0;
};

#pragma once
#include <memory>

namespace core::actor{

class Message{
public:
    virtual ~Message() = default;
};

using MessagePtr = std::unique_ptr<Message>;

} // namespace core::actor

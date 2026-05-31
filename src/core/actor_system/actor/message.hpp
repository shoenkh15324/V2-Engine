#pragma once
#include <memory>

namespace core::actor{

class Message{
public:
    virtual ~Message() = default;
    virtual const void* type() const noexcept = 0;

    template <typename T>
    static const void* typeId() noexcept {
        static const char id = 0;
        return &id;
    }
};

using MessagePtr = std::unique_ptr<Message>;

} // namespace core::actor

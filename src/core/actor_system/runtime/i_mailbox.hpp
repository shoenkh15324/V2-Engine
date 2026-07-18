#pragma once
#include <cstddef>
#include <memory>

template <typename T>
class IMailbox{
public:
    virtual ~IMailbox() = default;

    virtual bool push(T&& msg) = 0;
    virtual bool pop(T& out) = 0;
    virtual size_t popBatch(T* out, size_t max) = 0;
    virtual bool empty() const = 0;
    virtual size_t capacity() const = 0;
    virtual size_t count() const = 0;
};

enum class MailboxType{
    Mutex,
    LockFree
};

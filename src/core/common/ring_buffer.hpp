#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "return.hpp"

namespace core::common{

class RingBuffer{
public:
    explicit RingBuffer(size_t size);
    ~RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = default;
    RingBuffer& operator=(RingBuffer&&) = default;
    Result push(const uint8_t* data, size_t size);
    Result pop(uint8_t* out, size_t size);
    void reset();
    size_t size() const;
    size_t capacity() const;
    size_t freeSpace() const;
    bool empty() const;
    bool full() const;
private:
    std::vector<uint8_t> buffer_;
    size_t head_ = 0, tail_ = 0, size_ = 0;
};

} // namespace core::common

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

class RingBuffer{
public:
    explicit RingBuffer(size_t size);
    ~RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = default;
    RingBuffer& operator=(RingBuffer&&) = default;
    int push(const uint8_t* data, size_t size);
    int pop(uint8_t* out, size_t size);
    void reset();
    size_t count() const;
    size_t capacity() const;
    size_t freeSpace() const;
    bool empty() const;
    bool full() const;
    
private:
    std::vector<uint8_t> buffer_;
    size_t head_ = 0, tail_ = 0, count_ = 0;
};

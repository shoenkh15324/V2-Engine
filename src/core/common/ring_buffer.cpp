#include "ring_buffer.hpp"
#include "debug.hpp"
#include <cstring>
#include <algorithm>

namespace core::common{

RingBuffer::RingBuffer(size_t size) : buffer_(size){
    V2_ASSERT(size > 0);
}

Result RingBuffer::push(const uint8_t* data, size_t size){
    if((data == nullptr) || (size == 0)){
        return Result::InvalidArg;
    }
    if(size > freeSpace()){
        return Result::Fail;
    }
    const size_t firstChunkSize = std::min(size, capacity() - head_);
    std::memcpy(&buffer_[head_], data, firstChunkSize);
    const size_t remainChunkSize = size - firstChunkSize;
    if(remainChunkSize > 0){
        std::memcpy(&buffer_[0], data + firstChunkSize, remainChunkSize);
    }
    head_ = (head_ + size) % capacity();
    count_ += size;
    return Result::Ok;
}

Result RingBuffer::pop(uint8_t* out, size_t size){
    if((out == nullptr) || (size == 0)){
        return Result::InvalidArg;
    }
    if(size > count_){
        return Result::Fail;
    }
    const size_t firstChunkSize = std::min(size, capacity() - tail_);
    std::memcpy(out, &buffer_[tail_], firstChunkSize);
    const size_t remainChunkSize = size - firstChunkSize;
    if(remainChunkSize > 0){
        std::memcpy(out + firstChunkSize, &buffer_[0], remainChunkSize);
    }
    tail_ = (tail_ + size) % capacity();
    count_ -= size;
    return Result::Ok;
}

void RingBuffer::reset(){
    head_ = tail_ = count_ = 0;
}

size_t RingBuffer::count() const {
    return count_;
}

size_t RingBuffer::capacity() const {
    return buffer_.size();
}

size_t RingBuffer::freeSpace() const {
    return capacity() - count_;
}

bool RingBuffer::empty() const {
    return count_ == 0;
}

bool RingBuffer::full() const {
    return count_ == capacity();
}

} // namespace core::common

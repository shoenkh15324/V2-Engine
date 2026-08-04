#pragma once
#include <cstdint>
#include <cstddef>

class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;
    
    virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    virtual void deallocate(void* ptr, size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    virtual size_t allocatedBytes() const noexcept = 0;
    virtual size_t allocatedBlocks() const noexcept = 0;
};

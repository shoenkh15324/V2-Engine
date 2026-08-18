#pragma once
#include <new>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <stdexcept>
#include "core/common/memory/free_list.hpp"

enum class SlabState{
    Empty,
    Partial,
    Full
};

class Slab{
public:
    static constexpr std::size_t kDefaultSlabSize = 4096;

    explicit Slab(std::size_t blockSize, std::size_t slabSize = kDefaultSlabSize)
        : slabSize_(slabSize), blockSize_(blockSize), totalBlocks_(slabSize / blockSize){
        assert(blockSize >= FreeList::kNodeSize);
        assert(totalBlocks_ > 0);
        if((slabSize & (slabSize - 1)) != 0) throw std::invalid_argument("slabSize must be power of two");

        memory_ = static_cast<uint8_t*>(::operator new(slabSize_, std::align_val_t{slabSize_}));
        assert((reinterpret_cast<std::uintptr_t>(memory_) % slabSize_) == 0);

        for(std::size_t i = 0; i < totalBlocks_; ++i){
            freeList_.push(memory_ + i * blockSize_);
        }
    }

    ~Slab(){ ::operator delete(memory_, std::align_val_t{slabSize_}); }

    Slab(const Slab&) = delete;
    Slab& operator=(const Slab&) = delete;
    Slab(Slab&&) = delete;
    Slab& operator=(Slab&&) = delete;

    void* allocate() noexcept {
        if(freeList_.empty()) return nullptr;
        ++usedCount_;
        return freeList_.pop();
    }

    std::size_t allocateBatch(void** out, std::size_t maxCount) noexcept {
        std::size_t count = 0;
        while((count < maxCount) && !freeList_.empty()){
            out[count] = freeList_.pop();
            ++count;
        }
        usedCount_ += count;
        return count;
    }

    void deallocate(void* ptr) noexcept {
        freeList_.push(ptr);
        --usedCount_;
    }

    std::size_t deallocateBatch(void** blocks, std::size_t count) noexcept {
        assert(count <= usedCount_);
        for(std::size_t i = 0; i < count; ++i){
            freeList_.push(blocks[i]);
        }
        usedCount_ -= count;
        return count;
    }

    bool owns(void* ptr) const noexcept {
        auto* p = static_cast<uint8_t*>(ptr);
        return (p >= memory_) && (p < (memory_ + slabSize_));
    }

    bool contains(void* ptr) const noexcept {
        if(!owns(ptr)) return false;
        auto* p = static_cast<uint8_t*>(ptr);
        std::uintptr_t offset = static_cast<std::uintptr_t>(p - memory_);
        return (offset % blockSize_) == 0;
    }

    SlabState state() const noexcept{
        if(usedCount_ == 0) return SlabState::Empty;
        if(usedCount_ == totalBlocks_) return SlabState::Full;
        return SlabState::Partial;
    }

    uint8_t* begin() const noexcept { return memory_; }
    uint8_t* end() const noexcept { return memory_ + slabSize_; }
    bool full() const noexcept { return freeList_.empty(); }
    bool empty() const noexcept { return freeList_.count() == totalBlocks_; }
    std::size_t usedBlocks() const noexcept { return usedCount_; }
    std::size_t totalBlocks() const noexcept { return totalBlocks_; }
    std::size_t blockSize() const noexcept { return blockSize_; }
    std::size_t slabSize() const noexcept { return slabSize_; }

private:
    std::size_t slabSize_;
    std::size_t blockSize_;
    std::size_t totalBlocks_;
    uint8_t* memory_;
    std::size_t usedCount_ = 0;
    FreeList freeList_;
};

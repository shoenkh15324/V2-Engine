#pragma once
#include <new>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include "core/common/memory/free_list.hpp"

enum class SlabState{
    Empty,
    Partial,
    Full
};

class Slab{
public:
    explicit Slab(std::size_t blockSize) : blockSize_(blockSize), totalBlocks_(kSlabSize / blockSize){
        assert(blockSize >= FreeList::kNodeSize);
        assert(totalBlocks_ > 0);

        memory_ = static_cast<uint8_t*>(::operator new(kSlabSize, std::align_val_t{kSlabSize}));
        assert((reinterpret_cast<std::uintptr_t>(memory_) % kSlabSize) == 0);

        for(std::size_t i = 0; i < totalBlocks_; ++i){
            freeList_.push(memory_ + i * blockSize_);
        }
    }

    ~Slab(){ ::operator delete(memory_, std::align_val_t{kSlabSize}); }

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

    // ptr이 이 슬랩의 메모리 범위 내에 있으면 true를 반환
    bool owns(void* ptr) const noexcept {
        auto* p = static_cast<uint8_t*>(ptr);
        return (p >= memory_) && (p < (memory_ + kSlabSize));
    }

    // ptr이 이 슬랩 내에서 유효하고 블록 정렬된 주소이면 true를 반환
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
    uint8_t* end() const noexcept { return memory_ + kSlabSize; }
    bool full() const noexcept { return freeList_.empty(); }
    bool empty() const noexcept { return freeList_.count() == totalBlocks_; }
    std::size_t usedBlocks() const noexcept { return usedCount_; }
    std::size_t totalBlocks() const noexcept { return totalBlocks_; }
    std::size_t blockSize() const noexcept { return blockSize_; }

private:
    static constexpr std::size_t kSlabSize = 4096;

    uint8_t* memory_;
    std::size_t blockSize_;
    std::size_t totalBlocks_;
    std::size_t usedCount_ = 0;
    FreeList freeList_;
};
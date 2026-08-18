#pragma once
#include <list>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <unordered_map>
#include "core/common/memory/slab.hpp"

class CentralCache{
public:
    CentralCache() = default;
    ~CentralCache() = default;

    CentralCache(const CentralCache&) = delete;
    CentralCache& operator=(const CentralCache&) = delete;
    CentralCache(CentralCache&&) = delete;
    CentralCache& operator=(CentralCache&&) = delete;

    void init(std::size_t blockSize, std::size_t slabSize = Slab::kDefaultSlabSize){
        assert(blockSize != 0);
        if((slabSize & (slabSize - 1)) != 0) throw std::invalid_argument("slabSize must be power of two");
        blockSize_ = blockSize;
        slabSize_ = slabSize;
        slabMask_ = ~(slabSize_ - 1);
    }

    void* allocate(){
        std::lock_guard<std::mutex> guard(mutex_);
        Slab* slab = partialSlabs_.empty() ? addSlab() : partialSlabs_.front();
        SlabState before = slab->state();
        void* ptr = slab->allocate();
        assert(ptr != nullptr);
        ++allocatedBlocks_;
        transitionState(before, slab);
        return ptr;
    }

    void deallocate(void* ptr){
        assert(ptr != nullptr);
        std::lock_guard<std::mutex> guard(mutex_);
        Slab* slab = findSlab(ptr);
        if(!slab) return;
        SlabState before = slab->state();
        slab->deallocate(ptr);
        --allocatedBlocks_;
        transitionState(before, slab);
    }

    std::size_t fetchBatch(void** out, std::size_t batchSize){
        assert(out != nullptr);
        assert(batchSize > 0);
        std::lock_guard<std::mutex> guard(mutex_);
        std::size_t fetched = 0;
        for(auto it = partialSlabs_.begin(); it != partialSlabs_.end() && (fetched < batchSize);){
            Slab* slab = *it++;
            SlabState before = slab->state();
            fetched += slab->allocateBatch(out + fetched, batchSize - fetched);
            transitionState(before, slab);
        }
        while(fetched < batchSize){
            Slab* slab = addSlab();
            SlabState before = slab->state();
            fetched += slab->allocateBatch(out + fetched, batchSize - fetched);
            transitionState(before, slab);
            if(slab->full()) break;
        }
        allocatedBlocks_ += fetched;
        return fetched;
    }

    std::size_t returnBatch(void** in, std::size_t count){
        assert(in != nullptr);
        std::lock_guard<std::mutex> guard(mutex_);
        std::size_t returned = 0;
        for(std::size_t i = 0; i < count; ++i){
            Slab* slab = findSlab(in[i]);
            if(!slab) continue;
            SlabState before = slab->state();
            slab->deallocate(in[i]);
            transitionState(before, slab);
            ++returned;
        }
        allocatedBlocks_ -= returned;
        return returned;
    }

    std::size_t blockSize() const noexcept{ return blockSize_; }
    std::size_t allocatedBlocks() const noexcept{ return allocatedBlocks_.load(std::memory_order_relaxed); }

private:
    Slab* addSlab(){
        auto slab = std::make_unique<Slab>(blockSize_, slabSize_);
        Slab* raw = slab.get();
        slabMap_.emplace(reinterpret_cast<std::uintptr_t>(raw->begin()), raw);
        slabs_.push_back(std::move(slab));
        return raw;
    }

    Slab* findSlab(void* ptr){
        auto it = slabMap_.find(reinterpret_cast<std::uintptr_t>(ptr) & slabMask_);
        return (it != slabMap_.end()) ? it->second : nullptr;
    }

    void transitionState(SlabState before, Slab* slab){
        SlabState after = slab->state();
        if(before == after) return;
        if(before == SlabState::Partial) partialSlabs_.remove(slab);
        if(after == SlabState::Partial) partialSlabs_.push_back(slab);
    }

    std::mutex mutex_;
    std::size_t blockSize_ = 0;
    std::size_t slabSize_ = Slab::kDefaultSlabSize;
    std::uintptr_t slabMask_ = ~(Slab::kDefaultSlabSize - 1);
    std::list<Slab*> partialSlabs_;
    std::vector<std::unique_ptr<Slab>> slabs_;
    std::atomic<std::size_t> allocatedBlocks_ = 0;
    std::unordered_map<std::uintptr_t, Slab*> slabMap_;
};

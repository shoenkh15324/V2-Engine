#pragma once
#include <array>
#include <cstddef>
#include <cassert>
#include "core/common/memory/free_list.hpp"
#include "core/common/memory/slab.hpp"
#include "core/common/memory/size_class.hpp"

class ThreadLocalCache{
public:
    void init(const std::array<Slab*, SizeClass::kNumSizeClasses>& slabs){
        slabs_ = slabs;
        initialized_ = true;
    }

    bool initialized() const noexcept { return initialized_; }

    void* allocate(std::size_t size){
        assert(initialized());
        std::size_t idx = SizeClass::index(size);

        auto& cache = caches_[idx];
        if(cache.freeList.count() > 0) return cache.freeList.pop();
        return fetchFromCentral(idx);
    }

    void deallocate(void* ptr, std::size_t size){
        assert(ptr != nullptr);
        assert(initialized());
        std::size_t idx = SizeClass::index(size);

        auto& cache = caches_[idx];
        cache.freeList.push(ptr);
        if(cache.freeList.count() > SizeClass::batchSize(idx)){
            returnToCentral(idx);
        }
    }

    void drain(){
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            if(caches_[i].freeList.count() > 0){
                returnToCentral(i);
            }
        }
    }

private:
    static constexpr size_t kMaxBatchSize = 64;

    struct CacheLine{
        FreeList freeList;
    };

    void* fetchFromCentral(std::size_t idx){
        assert(slabs_[idx] != nullptr);
        std::size_t batch = SizeClass::batchSize(idx);

        void* buf[kMaxBatchSize];
        std::size_t fetched = slabs_[idx]->fetchBatch(buf, batch);

        if(fetched == 0) return nullptr;

        for(std::size_t i = 1; i < fetched; ++i){
            caches_[idx].freeList.push(buf[i]);
        }

        return buf[0];
    }

    void returnToCentral(std::size_t idx){
        assert(slabs_[idx] != nullptr);
        auto& cache = caches_[idx];
        std::size_t batch = SizeClass::batchSize(idx);
        std::size_t returnCount = cache.freeList.count() < batch ? cache.freeList.count() : batch;

        void* buf[kMaxBatchSize];
        for(std::size_t i = 0; i < returnCount; ++i){ 
            buf[i] = cache.freeList.pop();
        }

        std::size_t returned = slabs_[idx]->returnBatch(buf, returnCount);
        assert(returned == returnCount);
    }

    bool initialized_ = false;
    std::array<Slab*, SizeClass::kNumSizeClasses> slabs_{};
    std::array<CacheLine, SizeClass::kNumSizeClasses> caches_;
};

inline thread_local ThreadLocalCache tlCache_;

#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cassert>
#include "core/common/memory/free_list.hpp"
#include "core/common/memory/central_cache.hpp"
#include "core/common/memory/size_class.hpp"

inline constexpr std::size_t kMaxPools = 16;

class ThreadLocalCache{
public:
    ~ThreadLocalCache(){
        drainAll();
        for(auto& ptr : central_){
            ptr = nullptr;
        }
        initialized_ = false;
    }

    void init(const std::array<CentralCache*, SizeClass::kNumSizeClasses>& centralCaches){
        central_ = centralCaches;
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
        assert(central_[idx] != nullptr);
        std::size_t batch = SizeClass::batchSize(idx);
        void* buf[kMaxBatchSize];
        std::size_t fetched = central_[idx]->fetchBatch(buf, batch);

        if(fetched == 0) return nullptr;
        for(std::size_t i = 1; i < fetched; ++i){
            caches_[idx].freeList.push(buf[i]);
        }
        return buf[0];
    }

    void returnToCentral(std::size_t idx){
        assert(central_[idx] != nullptr);
        auto& cache = caches_[idx];
        std::size_t batch = SizeClass::batchSize(idx);
        std::size_t returnCount = (cache.freeList.count() < batch) ? cache.freeList.count() : batch;

        void* buf[kMaxBatchSize];
        for(std::size_t i = 0; i < returnCount; ++i){ 
            buf[i] = cache.freeList.pop();
        }

        std::size_t returned = central_[idx]->returnBatch(buf, returnCount);
        assert(returned == returnCount);
        (void)returned;
    }

    void drainAll(){
        for(size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            if(!central_[i]) continue;
            void* ptrs[kMaxBatchSize];
            while(size_t n = caches_[i].freeList.popBatch(ptrs, kMaxBatchSize)){
                central_[i]->returnBatch(ptrs, n);
            }
        }
    }

    bool initialized_ = false;
    std::array<CentralCache*, SizeClass::kNumSizeClasses> central_{};
    std::array<CacheLine, SizeClass::kNumSizeClasses> caches_;
};

inline std::atomic<std::size_t> nextPoolId{0};
inline thread_local std::array<ThreadLocalCache, kMaxPools> poolCaches;

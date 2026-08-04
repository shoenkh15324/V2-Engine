#pragma once
#include <new>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <utility>
#include "core/common/memory/size_class.hpp"
#include "core/common/memory/central_cache.hpp"
#include "core/common/memory/thread_local_cache.hpp"
#include "core/common/memory/i_memory_allocator.hpp"

// Debug Policy
struct NoDebugPolicy{
    static void onAllocate(void*, std::size_t) noexcept {}
    static void onDeallocate(void*, std::size_t) noexcept {}
};

struct PoisonDebugPolicy{
    static void onAllocate(void*, std::size_t) noexcept {}
    static void onDeallocate(void* ptr, std::size_t size) noexcept {
        std::memset(ptr, 0xCD, size);
    }
};

// Allocation Failure Policy
struct ThrowAllocPolicy{
    [[noreturn]]
    static void onAllocFailed(){
        throw std::bad_alloc{};
    }
};

struct NoExceptAllocPolicy{
    [[noreturn]]
    static void onAllocFailed() noexcept {
        std::abort();
    }
};

// Memory Pool
template<
    typename DebugPolicy = NoDebugPolicy,
    typename AllocPolicy = ThrowAllocPolicy
>
class MemoryPoolT : public IMemoryAllocator {
public:
    static MemoryPoolT& instance(){
        static MemoryPoolT pool;
        return pool;
    }

    MemoryPoolT(){
        poolId_ = nextPoolId.fetch_add(1, std::memory_order_relaxed);
        assert(poolId_ < kMaxPools);
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            central_[i].init(SizeClass::blockSize(i));
        }
    }

    ~MemoryPoolT() override = default;

    MemoryPoolT(const MemoryPoolT&) = delete;
    MemoryPoolT& operator=(const MemoryPoolT&) = delete;
    MemoryPoolT(MemoryPoolT&&) = delete;
    MemoryPoolT& operator=(MemoryPoolT&&) = delete;

    template<typename T, typename... Args>
    T* allocate(Args&&... args){
        void* mem = allocate(sizeof(T), alignof(T));
        if(!mem) AllocPolicy::onAllocFailed();
        DebugPolicy::onAllocate(mem, sizeof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    void* allocate(std::size_t size, std::size_t alignment) override {
        if(size == 0) size = 1; // SizeClass::index(0) assert 방지
        if((size <= SizeClass::kMaxAllocSize) && (alignment <= alignof(std::max_align_t))){
            auto& cache = poolCaches[poolId_];
            if(!cache.initialized()) cache.init(centralPointers());
            return cache.allocate(size);
        }
        return allocateLarge(size, alignment);
    }
        
    template<typename T>
    void deallocate(T* ptr){
        if(!ptr) return;
        ptr->~T();
        DebugPolicy::onDeallocate(ptr, sizeof(T));
        deallocate(ptr, sizeof(T), alignof(T));
    }

    void deallocate(void* ptr, std::size_t size, std::size_t alignment) override {
        if(!ptr) return;
        if(size == 0) size = 1;
        if((size <= SizeClass::kMaxAllocSize) && (alignment <= alignof(std::max_align_t))){
            auto& cache = poolCaches[poolId_];
            if(!cache.initialized()) cache.init(centralPointers());
            cache.deallocate(ptr, size);
        }else{
            deallocateLarge(ptr, alignment);
        }
    }

    std::size_t allocatedBlocks() const noexcept override {
        std::size_t total = 0;
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            total += central_[i].allocatedBlocks();
        }
        return total;
    }

    std::size_t allocatedBytes() const noexcept override {
        std::size_t total = 0;
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            total += central_[i].allocatedBlocks() * SizeClass::blockSize(i);
        }
        return total;
    }

    std::array<CentralCache*, SizeClass::kNumSizeClasses> centralCaches() noexcept {
        return centralPointers();
    }

private:

#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
    static constexpr std::size_t kDefaultAlign = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
    static constexpr std::size_t kDefaultAlign = alignof(std::max_align_t);
#endif

    std::array<CentralCache*, SizeClass::kNumSizeClasses> centralPointers() noexcept {
        std::array<CentralCache*, SizeClass::kNumSizeClasses> ptrs{};
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            ptrs[i] = &central_[i];
        }
        return ptrs;
    }

    static void* allocateLarge(std::size_t size, std::size_t alignment){
        if(alignment > kDefaultAlign){
            return ::operator new(size, std::align_val_t(alignment));
        }
        return ::operator new(size);
    }

    static void deallocateLarge(void* ptr, std::size_t alignment){
        if(alignment > kDefaultAlign){
            ::operator delete(ptr, std::align_val_t(alignment));
            return;
        }
        ::operator delete(ptr);
    }

    std::size_t poolId_ = 0;
    std::array<CentralCache, SizeClass::kNumSizeClasses> central_;
};

using MemoryPool = MemoryPoolT<NoDebugPolicy, ThrowAllocPolicy>;
using DebugMemoryPool = MemoryPoolT<PoisonDebugPolicy, ThrowAllocPolicy>;

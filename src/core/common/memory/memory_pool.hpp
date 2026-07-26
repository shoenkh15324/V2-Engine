#pragma once
#include <new>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <utility>
#include "core/common/memory/slab.hpp"
#include "core/common/memory/size_class.hpp"
#include "core/common/memory/thread_local_cache.hpp"

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
class MemoryPoolT{
public:
    static MemoryPoolT& instance(){
        static MemoryPoolT pool;
        return pool;
    }

    MemoryPoolT(const MemoryPoolT&) = delete;
    MemoryPoolT& operator=(const MemoryPoolT&) = delete;
    MemoryPoolT(MemoryPoolT&&) = delete;
    MemoryPoolT& operator=(MemoryPoolT&&) = delete;

    ~MemoryPoolT() = default;

    template<typename T, typename... Args>
    T* allocate(Args&&... args){
        ensureThreadCacheInit();

        void* mem = nullptr;
        if constexpr ((sizeof(T) <= SizeClass::kMaxAllocSize) && (alignof(T) <= alignof(std::max_align_t))){
            mem = tlCache_.allocate(sizeof(T));
        }else{
            mem = allocateLarge(sizeof(T), alignof(T));
        }
        
        if(!mem){
            AllocPolicy::onAllocFailed();
        }
        DebugPolicy::onAllocate(mem, sizeof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    void deallocate(T* ptr){
        if(!ptr) return;
        ptr->~T();
        DebugPolicy::onDeallocate(ptr, sizeof(T));

        if constexpr ((sizeof(T) <= SizeClass::kMaxAllocSize) && (alignof(T) <= alignof(std::max_align_t))){
            tlCache_.deallocate(ptr, sizeof(T));
        }else{
            deallocateLarge(ptr, alignof(T));
        }
    }

    std::size_t allocatedBlocks() const noexcept {
        std::size_t total = 0;
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            total += slabs_[i].allocatedBlocks();
        }
        return total;
    }

    std::size_t allocatedBytes() const noexcept {
        std::size_t total = 0;
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            total += slabs_[i].allocatedBlocks() * SizeClass::blockSize(i);
        }
        return total;
    }

    std::array<Slab*, SizeClass::kNumSizeClasses> slabPtrs() noexcept {
        return slabPointers();
    }

private:

#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
    static constexpr std::size_t kDefaultAlign = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
    static constexpr std::size_t kDefaultAlign = alignof(std::max_align_t);
#endif

    MemoryPoolT(){
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            slabs_[i].init(SizeClass::blockSize(i));
        }
    }

    void ensureThreadCacheInit(){
        if(!tlCache_.initialized()){
            tlCache_.init(slabPointers());
        }
    }

    std::array<Slab*, SizeClass::kNumSizeClasses> slabPointers() noexcept {
        std::array<Slab*, SizeClass::kNumSizeClasses> ptrs{};
        for(std::size_t i = 0; i < SizeClass::kNumSizeClasses; ++i){
            ptrs[i] = &slabs_[i];
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

    std::array<Slab, SizeClass::kNumSizeClasses> slabs_;
};

using MemoryPool = MemoryPoolT<NoDebugPolicy, ThrowAllocPolicy>;
using DebugMemoryPool = MemoryPoolT<PoisonDebugPolicy, ThrowAllocPolicy>;

#include <gtest/gtest.h>
#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <cstring>
#include "core/common/memory/size_class.hpp"
#include "core/common/memory/memory_pool.hpp"
#include "core/common/memory/i_memory_allocator.hpp"

namespace {

// 크기 클래스에 매핑되는 소형 페이로드 (sizeof=4, alignof=4 → class 0, batch 64)
struct SmallPayload {
    explicit SmallPayload(int v = 0) : value(v){}
    int value;
};
static_assert(sizeof(SmallPayload) <= SizeClass::kMaxAllocSize);

// ctor/dtor 카운트 검증용
std::atomic<int> g_ctors{0}, g_dtors{0};

struct Tracked {
    explicit Tracked(int v) : v_(v){ g_ctors.fetch_add(1, std::memory_order_relaxed); }
    ~Tracked(){ g_dtors.fetch_add(1, std::memory_order_relaxed); }
    int v_;
};

// 대형 경로 (size > kMaxAllocSize && align > max_align_t)
struct alignas(64) BigPayload {
    char data[8192];
};

}; // namespace

TEST(MemoryPool, RawAllocDeallocRoundtrip){
    MemoryPool pool;
    IMemoryAllocator* alloc = &pool;

    std::vector<void*> ptrs;
    constexpr size_t kCount = 128; // batch(64)를 넘겨 returnToCentral → findSlab 경로 강제
    ptrs.reserve(kCount);
    for(size_t i = 0; i < kCount; ++i){
        void* p = alloc->allocate(sizeof(SmallPayload), alignof(SmallPayload));
        ASSERT_NE(p, nullptr);
        ASSERT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(SmallPayload), 0u);
        std::memset(p, 0xAB, sizeof(SmallPayload));
        EXPECT_EQ(static_cast<unsigned char*>(p)[0], 0xAB);
        ptrs.push_back(p);
    }
    for(void* p : ptrs){
        alloc->deallocate(p, sizeof(SmallPayload), alignof(SmallPayload));
    }
}

TEST(MemoryPool, TypedAllocDealloc){
    g_ctors.store(0);
    g_dtors.store(0);
    {
        MemoryPool pool;
        Tracked* p = pool.allocate<Tracked>(42);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->v_, 42);
        pool.deallocate(p);
    }
    EXPECT_EQ(g_ctors.load(), 1);
    EXPECT_EQ(g_dtors.load(), 1);
}

TEST(MemoryPool, MultipleInstancesIsolated){
    MemoryPool poolA, poolB;
    IMemoryAllocator* allocA = &poolA;

    const size_t bytesBeforeA = poolA.allocatedBytes();
    const size_t bytesBeforeB = poolB.allocatedBytes();

    for(size_t i = 0; i < 1000; ++i){
        allocA->allocate(sizeof(SmallPayload), alignof(SmallPayload));
    }
    
    EXPECT_GT(poolA.allocatedBytes(), bytesBeforeA); // A는 증가
    EXPECT_EQ(poolB.allocatedBytes(), bytesBeforeB); // B는 불변 (per-pool TLS 격리)
}

TEST(MemoryPool, LargeAllocation){
    MemoryPool pool;
    IMemoryAllocator* alloc = &pool;

    // raw 대형 경로
    void* p = alloc->allocate(sizeof(BigPayload), alignof(BigPayload));
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64, 0u);
    std::memset(p, 0x11, sizeof(BigPayload));
    alloc->deallocate(p, sizeof(BigPayload), alignof(BigPayload));

    // typed 대형 겅로 (placement-new + align>max_align → allocateLarge)
    BigPayload* b = pool.allocate<BigPayload>();
    b->data[0] = 5;
    pool.deallocate(b);
}

TEST(MemoryPool, CrossThreadDealloc){
    MemoryPool pool;
    std::vector<void*> ptrs;
    for(size_t i = 0; i < 32; ++i){
        ptrs.push_back(pool.allocate(sizeof(SmallPayload), alignof(SmallPayload)));
    }
    // 스레드 B가 스레드 A(메인)가 할당한 블록을 해제 - deallocate lazy-init 경로 검증
    std::thread t([&](){
        for(void* p : ptrs){
            pool.deallocate(p, sizeof(SmallPayload), alignof(SmallPayload));
        }
    });
    t.join();
}

TEST(MemoryPool, CounterConsistency){
    MemoryPool pool;
    constexpr size_t kSize = sizeof(SmallPayload);
    for(size_t i = 0; i < 200; ++i){
        pool.allocate(kSize, alignof(SmallPayload));
    }
    // 단일 타입 워크로드 -> bytes == blocks * sizeClass(해당 타입)
    const size_t classBlockSize = SizeClass::blockSize(SizeClass::index(kSize));
    EXPECT_EQ(pool.allocatedBytes(), pool.allocatedBlocks() * classBlockSize);
    EXPECT_GT(pool.allocatedBlocks(), 0u);
}

TEST(MemoryPool, ConcurrentStress){
    MemoryPool pool;
    constexpr int kThreads = 4;
    constexpr int kIters = 2000;
    std::vector<std::thread> threads;
    for(int t = 0; t < kThreads; ++t){
        threads.emplace_back([&](){
            for(int i = 0; i < kIters; ++i){
                void* p = pool.allocate(32, alignof(std::max_align_t));
                pool.deallocate(p, 32, alignof(std::max_align_t));
            }
        });
    }
    for(auto& th : threads) th.join();
    EXPECT_LE(pool.allocatedBlocks(), 512u);
}

TEST(MemoryPool, ZeroSizeRoundtrip){
    MemoryPool pool;
    void* p = pool.allocate(0, alignof(std::max_align_t));
    ASSERT_NE(p, nullptr);
    // allocate(0)의 클램프(1)과 대칭이 되도록 deallocate(0)도 1로 클램프 필요
    pool.deallocate(p, 0, alignof(std::max_align_t)); 
}


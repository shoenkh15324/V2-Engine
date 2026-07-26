#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>

struct SizeClass{
    static constexpr std::size_t kNumSizeClasses = 9;
    static constexpr std::size_t kMaxAllocSize = 2048;

    struct Entry{
        std::size_t blockSize;
        std::size_t batchSize;
    };

    static constexpr Entry kTable[kNumSizeClasses] = {
        {8, 64}, // 512B
        {16, 64}, // 1KB
        {32, 32}, // 1KB
        {64, 32}, // 2KB
        {128, 16}, // 2KB
        {256, 16}, // 4KB
        {512, 8}, // 4KB
        {1024, 4}, // 4KB
        {2048, 2}, // 4KB
    };

    static std::size_t index(std::size_t size){
        assert(size > 0 && size <= kMaxAllocSize);
        for(std::size_t i = 0; i < kNumSizeClasses; ++i){
            if(size <= kTable[i].blockSize) return i;
        }
        assert(false);
        return 0;
    }

    static std::size_t blockSize(std::size_t idx){
        assert(idx < kNumSizeClasses);
        return kTable[idx].blockSize;
    }

    static std::size_t batchSize(std::size_t idx){
        assert(idx < kNumSizeClasses);
        return kTable[idx].batchSize;
    }
};

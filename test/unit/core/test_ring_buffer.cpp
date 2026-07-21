#include <gtest/gtest.h>
#include "core/common/container/ring_buffer.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <cstring>

// Construction

TEST(RingBuffer, Create){
    RingBuffer buf(64);
    EXPECT_EQ(buf.capacity(), 64);
    EXPECT_EQ(buf.count(), 0);
    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(buf.full());
    EXPECT_EQ(buf.freeSpace(), 64);
}

TEST(RingBuffer, CreateMinSize){
    RingBuffer buf(1);
    EXPECT_EQ(buf.capacity(), 1);
    EXPECT_TRUE(buf.empty());
}

#ifndef NDEBUG
TEST(RingBufferDeathTest, CreateZeroSize){
    EXPECT_DEATH({ RingBuffer buf(0); }, ".*");
}
#endif

// Basic Push / Pop

TEST(RingBuffer, PushPopSequential){
    RingBuffer buf(64);
    uint8_t src[32];
    for(size_t i = 0; i < 32; i++){
        src[i] = static_cast<uint8_t>(i);
    }

    EXPECT_EQ(buf.push(src, 32), Ok);
    EXPECT_EQ(buf.count(), 32);
    EXPECT_FALSE(buf.empty());

    uint8_t dst[32] = {};
    EXPECT_EQ(buf.pop(dst, 32), Ok);
    EXPECT_EQ(buf.count(), 0);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(std::memcmp(src, dst, 32), 0);
}

// Wrap-Around

TEST(RingBuffer, PushPopWrapAround){
    RingBuffer buf(8);
    uint8_t src[8];
    for(size_t i = 0; i < 8; i++){
        src[i] = static_cast<uint8_t>(i);
    }

    // fill buffer to the brim (head at 0 -> 8, wraps to 0)
    EXPECT_EQ(buf.push(src, 8), Ok);
    EXPECT_TRUE(buf.full());

    // drain 3 bytes (tail moves to 3)
    uint8_t tmp[3];
    EXPECT_EQ(buf.pop(tmp, 3), Ok);

    // push 3 new bytes over the wrap boundary
    uint8_t src2[3] = {100, 101, 102};
    EXPECT_EQ(buf.push(src2, 3), Ok);

    // read remaining 8 bytes in order
    uint8_t dst[8];
    EXPECT_EQ(buf.pop(dst, 8), Ok);
    EXPECT_EQ(dst[0], 3); // old data tail
    EXPECT_EQ(dst[1], 4);
    EXPECT_EQ(dst[2], 5);
    EXPECT_EQ(dst[3], 6); // new data after wrap
    EXPECT_EQ(dst[4], 7);
    EXPECT_EQ(dst[5], 100);
    EXPECT_EQ(dst[6], 101); // old data after the 3 popped
    EXPECT_EQ(dst[7], 102);
}

// Boundary

TEST(RingBuffer, PushExactCapacity){
    RingBuffer buf(4);
    uint8_t src[4] = {1, 2, 3, 4};

    EXPECT_EQ(buf.push(src, 4), Ok);
    EXPECT_TRUE(buf.full());
    EXPECT_EQ(buf.freeSpace(), 0);

    uint8_t dst[4];
    EXPECT_EQ(buf.pop(dst, 4), Ok);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(std::memcmp(src, dst, 4), 0);
}

TEST(RingBuffer, PushOverflow){
    RingBuffer buf(4);
    uint8_t src[4] = {1, 2, 3, 4};

    EXPECT_EQ(buf.push(src, 4), Ok);
    EXPECT_EQ(buf.push(src, 1), Fail); // overflow
    EXPECT_EQ(buf.count(), 4); // data preserved
    EXPECT_TRUE(buf.full());
}

TEST(RingBuffer, PopUnderflow){
    RingBuffer buf(4);
    uint8_t dst[4];
    EXPECT_EQ(buf.pop(dst, 4), Fail);
    EXPECT_EQ(buf.count(), 0);
}

// Invalid Arguments

TEST(RingBuffer, PushPopNullptr){
    RingBuffer buf(4);
    EXPECT_EQ(buf.push(nullptr, 4), InvalidArg);
    EXPECT_EQ(buf.pop(nullptr, 4), InvalidArg);
}

TEST(RingBuffer, PushPopZeroSize){
    RingBuffer buf(4);
    uint8_t data[4] = {};
    EXPECT_EQ(buf.push(data, 0), InvalidArg);
    EXPECT_EQ(buf.pop(data, 0), InvalidArg);
}

// Reset

TEST(RingBuffer, Reset){
    RingBuffer buf(64);
    uint8_t src[32] = {};
    std::memset(src, 0xAB, 32);

    EXPECT_EQ(buf.push(src, 32), Ok);
    EXPECT_FALSE(buf.empty());

    buf.reset();
    EXPECT_EQ(buf.count(), 0);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.freeSpace(), buf.capacity());
}

// Repeated Push / Pop

TEST(RingBuffer, RepeatedPushPop){
    RingBuffer buf(64);
    for(int round = 0; round < 50; round++){
        uint8_t src[8];
        for(size_t i = 0; i < 8; i++){
            src[i] = static_cast<uint8_t>(round * 8 + i);
        }
        EXPECT_EQ(buf.push(src, 8), Ok);
        uint8_t dst[8];
        EXPECT_EQ(buf.pop(dst, 8), Ok);
        EXPECT_EQ(std::memcmp(src, dst, 8), 0);
    }
    EXPECT_TRUE(buf.empty());
}

// Partial Pop

TEST(RingBuffer, PartialPop){
    RingBuffer buf(64);
    uint8_t src[32];
    for(size_t i = 0; i < 32; i++){
        src[i] = static_cast<uint8_t>(i);
    }
    EXPECT_EQ(buf.push(src, 32), Ok);

    uint8_t first[12];
    EXPECT_EQ(buf.pop(first, 12), Ok);
    EXPECT_EQ(buf.count(), 20);
    EXPECT_EQ(std::memcmp(src, first, 12), 0);

    uint8_t second[20];
    EXPECT_EQ(buf.pop(second, 20), Ok);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(std::memcmp(src + 12, second, 20), 0);
}

// FreeSpace

TEST(RingBuffer, FreeSpace){
    RingBuffer buf(16);
    EXPECT_EQ(buf.freeSpace(), 16);

    uint8_t data[6] = {};
    EXPECT_EQ(buf.push(data, 6), Ok);
    EXPECT_EQ(buf.freeSpace(), 10);

    uint8_t out[4];
    EXPECT_EQ(buf.pop(out, 4), Ok);
    EXPECT_EQ(buf.freeSpace(), 14);

    buf.reset();
    EXPECT_EQ(buf.freeSpace(), 16);
}

// Move Semantics

TEST(RingBuffer, MoveSemantics){
    RingBuffer a(16);
    uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    a.push(src, 8);

    RingBuffer b = std::move(a);
    EXPECT_EQ(b. capacity(), 16);
    EXPECT_EQ(b.count(), 8);

    uint8_t dst[8];
    b.pop(dst, 8);
    EXPECT_EQ(std::memcmp(src, dst, 8), 0);
}


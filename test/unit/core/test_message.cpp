#include <gtest/gtest.h>
#include "core/actor_system/messages/message.hpp"
#include <type_traits>

namespace{

struct InlineCopyable{
    static constexpr MessageId kId = MessageId::Tick;
    int value;
};

struct InlineNonCopyable{
    static constexpr MessageId kId = MessageId::IpcNewConnection;
    int value;
    InlineNonCopyable() = default;
    explicit InlineNonCopyable(int v) : value(v){}
    InlineNonCopyable(const InlineNonCopyable&) = delete;
    InlineNonCopyable& operator=(const InlineNonCopyable&) = delete;
    InlineNonCopyable(InlineNonCopyable&&) = default;
    InlineNonCopyable& operator=(InlineNonCopyable&&) = default;
};

struct PoolCopyable{
    static constexpr MessageId kId = MessageId::MonitorPoll;
    int values[32];
};

struct PoolNonCopyable{
    static constexpr MessageId kId = MessageId::MonitorClientDisconnected;
    int values[32];
    PoolNonCopyable() = default;
    explicit PoolNonCopyable(int seed){
        for(int i = 0; i < 32; ++i) values[i] = seed + i;
    }
    PoolNonCopyable(const PoolNonCopyable&) = delete;
    PoolNonCopyable& operator=(const PoolNonCopyable&) = delete;
    PoolNonCopyable(PoolNonCopyable&&) = default;
    PoolNonCopyable& operator=(PoolNonCopyable&&) = default;
};

static_assert(sizeof(PoolCopyable) > Message::kInlineSize, "PoolCopyable must use pool storage");
static_assert(sizeof(InlineCopyable) <= Message::kInlineSize, "InlineCopyable must use inline storage");

} // namespace

TEST(Message, CloneInlineCopyable){
    auto msg = Message::make(InlineCopyable{42});
    ASSERT_TRUE(msg);
    EXPECT_EQ(msg.id(), MessageId::Tick);
    EXPECT_EQ(msg.as<InlineCopyable>().value, 42);

    auto copy = msg.clone();
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.id(), MessageId::Tick);
    EXPECT_EQ(copy.as<InlineCopyable>().value, 42);
    EXPECT_EQ(msg.as<InlineCopyable>().value, 42); // 원본 불변
}

TEST(Message, CloneInlineNonCopyable){
    auto msg = Message::make(InlineNonCopyable{7});
    ASSERT_TRUE(msg);
    auto copy = msg.clone();
    EXPECT_FALSE(copy); // 복제 불가 → 빈 메시지
}

TEST(Message, ClonePoolCopyable){
    PoolCopyable payload;
    for(int i = 0; i < 32; ++i) payload.values[i] = i * 3;
    auto msg = Message::make(payload);
    ASSERT_TRUE(msg);
    EXPECT_EQ(msg.id(), MessageId::MonitorPoll);
    EXPECT_EQ(msg.as<PoolCopyable>().values[5], 15);

    auto copy = msg.clone();
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy.id(), MessageId::MonitorPoll);
    for(int i = 0; i < 32; ++i) EXPECT_EQ(copy.as<PoolCopyable>().values[i], i * 3);
    EXPECT_EQ(msg.as<PoolCopyable>().values[5], 15); // 원본 불변
}

TEST(Message, ClonePoolNonCopyable){
    auto msg = Message::make(PoolNonCopyable{10});
    ASSERT_TRUE(msg);
    EXPECT_EQ(msg.as<PoolNonCopyable>().values[0], 10);
    auto copy = msg.clone();
    EXPECT_FALSE(copy); // 복제 불가 → 빈 메시지
}

TEST(Message, CloneEmpty){
    Message empty;
    auto copy = empty.clone();
    EXPECT_FALSE(copy);
}

TEST(Message, MovePreservesInlinePayload){
    auto msg = Message::make(InlineCopyable{99});
    auto moved = std::move(msg);
    EXPECT_TRUE(moved);
    EXPECT_EQ(moved.as<InlineCopyable>().value, 99);
    EXPECT_FALSE(msg);
}

TEST(Message, MovePreservesPoolPayload){
    PoolCopyable payload;
    for(int i = 0; i < 32; ++i) payload.values[i] = i + 1;
    auto msg = Message::make(payload);
    auto moved = std::move(msg);
    EXPECT_TRUE(moved);
    EXPECT_EQ(moved.as<PoolCopyable>().values[31], 32);
    EXPECT_FALSE(msg);
}

#include <gtest/gtest.h>
#include <tuple>
#include <utility>
#include <cstddef>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/core_messages.hpp"

namespace {

using TestTuple = std::tuple<SignalNotify, ActorEnableRequest>;

template<class ActorT, typename Tuple>
consteval bool allHandled(){
    return []<std::size_t... I>(std::index_sequence<I...>){
        return (requires(ActorT& a, const std::tuple_element_t<I, Tuple>& m){
            a.handle(m);
        } && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

class DispatchActor : public Actor {
public:
    DispatchActor() : Actor("dispatch", 1) {}

    int open() override { return 0; }
    int close() override { return 0; }

    void handle(const Message& msg) override {
        dispatch(*this, msg, TestTuple{});
    }

    void handle(const SignalNotify& msg){ sig_ = msg.signum; }
    void handle(const ActorEnableRequest&){ enableCount_++; }

    void handleUnknown(const Message&) override { unknownCount_++; }

    int sig_ = -1;
    int enableCount_ = 0;
    int unknownCount_ = 0;
};

static_assert(allHandled<DispatchActor, TestTuple>());
static_assert(!allHandled<DispatchActor, std::tuple<ActorDisableRequest>>());
static_assert(!allHandled<DispatchActor, std::tuple<ActorRestartRequest>>());

}; // namespace

TEST(MessageTypedDispatch, RoutesByType){
    DispatchActor actor;
    actor.handle(Message::make(SignalNotify{123}));
    actor.handle(Message::make(ActorEnableRequest{}));
    EXPECT_EQ(actor.sig_, 123);
    EXPECT_EQ(actor.enableCount_, 1);
    EXPECT_EQ(actor.unknownCount_, 0);
}

TEST(MessageTypedDispatch, IdOutsideTupleIsDeadLetter){
    DispatchActor actor;
    actor.handle(Message::make(ActorDisableRequest{}));
    actor.handle(Message::make(ActorRestartRequest{"test"}));
    EXPECT_EQ(actor.unknownCount_, 2);
    EXPECT_EQ(actor.sig_, -1);
    EXPECT_EQ(actor.enableCount_, 0);
}

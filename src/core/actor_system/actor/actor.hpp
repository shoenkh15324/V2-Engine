#pragma once
#include <string>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include "core/actor_system/messages/message.hpp"

enum ActorState : uint8_t{
    Closed = 0,
    Closing,
    Opening,
    Opened,
    Inherited
};

class IActorRuntime;
class ActorRuntime;

class Actor{
    friend class ActorRuntime;
    friend class ActorHandle;
public:
    explicit Actor(std::string name = "unknown", uint64_t id = -1);
    virtual ~Actor();

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;
    Actor(Actor&&) = delete;
    Actor& operator=(Actor&&) = delete;

    virtual int open() = 0;
    virtual int close() = 0;
    virtual void handle(const Message& msg) = 0;

    template<class ActorT, typename Tuple>
    void dispatch(ActorT& self, const Message& msg, Tuple){
        constexpr bool hasAllHandlers = []<std::size_t... I>(std::index_sequence<I...>){
            return (requires(ActorT& a, const std::tuple_element_t<I, Tuple>& m){
                a.handle(m);
            } && ...);
        }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        static_assert(hasAllHandlers, "dispatch tuple: handle(T) no overload");
        bool matched = msg.visit<Tuple>([&](const auto& m){
            self.handle(m);
        });
        if(!matched) self.handleUnknown(msg);
    }
    
    void sendMsg(const std::string& name, Message msg);
    void sendMsg(uint64_t id, Message msg);
    int sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs);
    int sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs);
    void receiveMsg(Message msg);

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    void sendMsg(const std::string& name, M&& m){ sendMsg(name, Message::make(std::forward<M>(m))); }

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    void sendMsg(uint64_t id, M&& m){ sendMsg(id, Message::make(std::forward<M>(m))); }

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    int sendMsgAfter(const std::string& targetName, M&& m, uint64_t delayMs){ return sendMsgAfter(targetName, Message::make(std::forward<M>(m)), delayMs); }

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    int sendMsgAfter(uint64_t targetId, M&& m, uint64_t delayMs){ return sendMsgAfter(targetId, Message::make(std::forward<M>(m)), delayMs); }

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    void receiveMsg(M&& m){ receiveMsg(Message::make(std::forward<M>(m))); }

    template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
    int startTimer(M&& m, uint64_t delayMs, bool repeating){ return startTimer(Message::make(std::forward<M>(m)), delayMs, repeating); }

    int startTimer(Message msg, uint64_t delayMs, bool repeating);
    void cancelTimer(int timerId);
    void cancelAllTimers();
    size_t timerCount() const;

    size_t mailboxCount() const;
    size_t mailboxCapacity() const;

    const std::string& name() const { return name_; }
    uint64_t id() const { return id_; }
    uint64_t generation() const { return generation_; }
    void setGeneration(uint64_t gen){ generation_ = gen; }
    ActorState getState() const { return state_.load(std::memory_order_relaxed); }
    bool isEssential() const { return essential_; }
    void setEssential(bool v){ essential_ = v; }

    IActorRuntime* runtime() const { return runtime_; }

protected:
    virtual void handleUnknown(const Message& msg);

    void setRuntime(IActorRuntime* r) { runtime_ = r; }

    std::atomic<ActorState> state_{Closed};

private:
    IActorRuntime* runtime_ = nullptr;
    std::string name_;
    uint64_t id_;
    uint64_t generation_{0};
    bool essential_{false};
};

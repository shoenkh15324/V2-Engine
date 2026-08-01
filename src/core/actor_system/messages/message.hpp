#pragma once
#include <new>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <type_traits>
#include "message_traits.hpp"
#include "core/common/memory/memory_pool.hpp"

class Message{
public:
    static constexpr size_t kInlineSize = 64;

    Message() = default;
    ~Message(){ destroy(); }

    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    Message(Message&& other) noexcept : id_(other.id_), mode_(other.mode_), ops_(other.ops_){
        if(mode_ == StorageMode::Inline){
            ops_->move(storage_.inlineData, other.storage_.inlineData);
        }else if(mode_ == StorageMode::Pool){
            storage_.ptr = other.storage_.ptr;
        }
        other.id_ = static_cast<MessageId>(0);
        other.mode_ = StorageMode::Empty;
        other.ops_ = nullptr;
    }

    Message& operator=(Message&& other) noexcept {
        if(this != &other){
            destroy();
            id_ = other.id_;
            mode_ = other.mode_;
            ops_ = other.ops_;
            if(mode_ == StorageMode::Inline){
                ops_->move(storage_.inlineData, other.storage_.inlineData);
            }else if(mode_ == StorageMode::Pool){
                storage_.ptr = other.storage_.ptr;
            }
            other.id_ = static_cast<MessageId>(0);
            other.mode_ = StorageMode::Empty;
            other.ops_ = nullptr;
        }
        return *this;
    }

    explicit operator bool() const { return mode_ != StorageMode::Empty; }

    template<typename T>
    static Message make(T&& value){
        using DT = std::decay_t<T>;
        Message msg;
        msg.id_ = DT::kId;
        msg.ops_ = opsFor<DT>();

        if constexpr ((sizeof(DT) <= kInlineSize) && (alignof(DT) <= alignof(std::max_align_t)) && std::is_nothrow_move_constructible_v<DT>){
            msg.mode_ = StorageMode::Inline;
            ::new (msg.storage_.inlineData) DT(std::forward<T>(value));
        }else{
            msg.mode_ = StorageMode::Pool;
            msg.storage_.ptr = MemoryPool::instance().allocate<DT>(std::forward<T>(value));
        }
        return msg;
    }

    Message clone() const {
        if(mode_ == StorageMode::Empty) return {};
        Message result;

        if(mode_ == StorageMode::Inline){
            if(!ops_->cloneConstruct) return {};
            ops_->cloneConstruct(result.storage_.inlineData, data());
            result.id_ = id_;
            result.ops_ = ops_;
            result.mode_ = StorageMode::Inline;
            return result;
        }
        
        if(!ops_->cloneAllocate) return {};
        result.storage_.ptr = ops_->cloneAllocate(data());
        result.id_ = id_;
        result.ops_ = ops_;
        result.mode_ = StorageMode::Pool;
        return result;
    }

    template<typename T>
    T& as(){ return *static_cast<T*>(data()); }

    template<typename T>
    const T& as() const { return *static_cast<const T*>(data()); }

    MessageId id() const { return id_; }

private:
    enum class StorageMode : uint8_t {
        Empty,
        Inline,
        Pool
    };

    union Storage{
        alignas(std::max_align_t) std::byte inlineData[kInlineSize];
        void* ptr;
    };

    struct MessageOps{
        void (*destroy)(void*);
        void (*move)(void* dst, void* src);
        void (*cloneConstruct)(void* dst, const void* src);
        void* (*cloneAllocate)(const void* src);
    };

    template<typename T>
    static const MessageOps* opsFor(){
        static constexpr bool isPool = (sizeof(T) > kInlineSize) || (alignof(T) > alignof(std::max_align_t)) || !std::is_nothrow_move_constructible_v<T>;
        static constexpr bool isCopyable = std::is_copy_constructible_v<T>;
        if constexpr (isPool){
            static constexpr MessageOps ops = []{
                MessageOps ops{
                    .destroy = [](void* p){ MemoryPool::instance().deallocate(static_cast<T*>(p)); },
                    .move = [](void* dst, void* src){
                        ::new(dst) T(std::move(*static_cast<T*>(src)));
                        static_cast<T*>(src)->~T();
                    },
                    .cloneConstruct = nullptr,
                    .cloneAllocate = nullptr,
                };
                if constexpr (isCopyable){
                    ops.cloneAllocate = [](const void* src) -> void*{
                        return MemoryPool::instance().allocate<T>(*static_cast<const T*>(src));
                    };
                }
                return ops;
            }();
            return &ops;
        }else{
            static constexpr MessageOps ops = []{
                MessageOps ops{
                    .destroy = [](void* p){ static_cast<T*>(p)->~T(); },
                    .move = [](void* dst, void* src){
                        ::new(dst) T(std::move(*static_cast<T*>(src)));
                        static_cast<T*>(src)->~T();
                    },
                    .cloneConstruct = nullptr,
                    .cloneAllocate = nullptr,
                };
                if constexpr (isCopyable){
                    ops.cloneConstruct = [](void* dst, const void* src){
                        ::new(dst) T(*static_cast<const T*>(src));
                    };
                }
                return ops;
            }();
            return &ops;
        }
    }
    
    void* data(){
        return (mode_ == StorageMode::Inline) ? storage_.inlineData : storage_.ptr;
    }

    const void* data() const {
        return (mode_ == StorageMode::Inline) ? storage_.inlineData : storage_.ptr;
    }

    void destroy(){
        if((mode_ == StorageMode::Empty) || !ops_) return;
        ops_->destroy(data());
    }

    MessageId id_{static_cast<MessageId>(0)};
    StorageMode mode_{StorageMode::Empty};
    const MessageOps* ops_{nullptr};
    Storage storage_;
};

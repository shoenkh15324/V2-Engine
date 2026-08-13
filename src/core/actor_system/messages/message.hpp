#pragma once
#include <new>
#include <tuple>
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

    Message(Message&& other) noexcept : id_(other.id_), mode_(other.mode_), ops_(other.ops_), allocator_(other.allocator_){
        if(mode_ == StorageMode::Inline){
            ops_->move(storage_.inlineData, other.storage_.inlineData);
        }else if(mode_ == StorageMode::Pool){
            storage_.ptr = other.storage_.ptr;
        }
        other.id_ = static_cast<MessageId>(0);
        other.mode_ = StorageMode::Empty;
        other.ops_ = nullptr;
        other.allocator_ = nullptr;
    }

    Message& operator=(Message&& other) noexcept {
        if(this != &other){
            destroy();
            id_ = other.id_;
            mode_ = other.mode_;
            ops_ = other.ops_;
            allocator_ = other.allocator_;
            if(mode_ == StorageMode::Inline){
                ops_->move(storage_.inlineData, other.storage_.inlineData);
            }else if(mode_ == StorageMode::Pool){
                storage_.ptr = other.storage_.ptr;
            }
            other.id_ = static_cast<MessageId>(0);
            other.mode_ = StorageMode::Empty;
            other.ops_ = nullptr;
            other.allocator_ = nullptr;
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
            IMemoryAllocator* alloc = &defaultMemoryPool();
            void* mem = alloc->allocate(sizeof(DT), alignof(DT));
            if(!mem) throw std::bad_alloc{};
            msg.storage_.ptr = ::new(mem) DT(std::forward<T>(value));
            msg.allocator_ = alloc;
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
        IMemoryAllocator* alloc = allocator_ ? allocator_ : &defaultMemoryPool();
        result.storage_.ptr = ops_->cloneAllocate(data(), alloc);
        result.id_ = id_;
        result.ops_ = ops_;
        result.mode_ = StorageMode::Pool;
        result.allocator_ = alloc;
        return result;
    }

    template<typename Tuple, typename Visitor>
    bool visit(Visitor&& v) const {
        return std::apply([&](auto... t){
            auto tryVisit = [&](auto x){
                using T = std::decay_t<decltype(x)>;
                if(id_ != T::kId) return false;
                v(as<T>());
                return true;
            };
            return (tryVisit(t) || ...);
        }, Tuple{});
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
        void (*destroy)(void*, IMemoryAllocator*);
        void (*move)(void* dst, void* src);
        void (*cloneConstruct)(void* dst, const void* src);
        void* (*cloneAllocate)(const void* src, IMemoryAllocator* alloc);
    };

    template<typename T>
    static const MessageOps* opsFor(){
        static constexpr bool isPool = (sizeof(T) > kInlineSize) || (alignof(T) > alignof(std::max_align_t)) || !std::is_nothrow_move_constructible_v<T>;
        static constexpr bool isCopyable = std::is_copy_constructible_v<T>;
        if constexpr (isPool){
            static constexpr MessageOps ops = []{
                MessageOps ops{
                    .destroy = [](void* p, IMemoryAllocator* alloc){
                        static_cast<T*>(p)->~T();
                        alloc->deallocate(p, sizeof(T), alignof(T));
                    },
                    .move = [](void* dst, void* src){
                        ::new(dst) T(std::move(*static_cast<T*>(src)));
                        static_cast<T*>(src)->~T();
                    },
                    .cloneConstruct = nullptr,
                    .cloneAllocate = nullptr,
                };
                if constexpr (isCopyable){
                    ops.cloneAllocate = [](const void* src, IMemoryAllocator* alloc) -> void*{
                        void* mem = alloc->allocate(sizeof(T), alignof(T));
                        if(!mem) throw std::bad_alloc{};
                        return ::new(mem) T(*static_cast<const T*>(src));
                    };
                }
                return ops;
            }();
            return &ops;
        }else{
            static constexpr MessageOps ops = []{
                MessageOps ops{
                    .destroy = [](void* p, IMemoryAllocator*){ static_cast<T*>(p)->~T(); },
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
        ops_->destroy(data(), allocator_ ? allocator_ : &defaultMemoryPool());
    }

    MessageId id_{static_cast<MessageId>(0)};
    StorageMode mode_{StorageMode::Empty};
    const MessageOps* ops_{nullptr};
    IMemoryAllocator* allocator_ = nullptr;
    Storage storage_;
};

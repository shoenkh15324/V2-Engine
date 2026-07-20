#pragma once
#include <new>
#include <atomic>
#include <memory>
#include <utility>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#if defined(__APPLE__) && defined(__arm64__)
    inline constexpr size_t kCacheLine = 128;
#elif defined(__aarch64__)
    inline constexpr size_t kCacheLine = 64;
#else
    inline constexpr size_t kCacheLine = 64;
#endif

template<typename T>
class LockFreeMpscQueue{
    static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move assignable");
    static_assert(std::is_nothrow_destructible_v<T>,"T must be nothrow destructible");

private:
    using SignedSize = std::make_signed_t<size_t>;

public:
    explicit LockFreeMpscQueue(size_t capacity) : slots_(std::make_unique<Slot[]>(capacity)), capacity_(capacity){
        if(capacity_ == 0) throw std::invalid_argument("LockFreeMpscQueue capacity must be greater than zero");
        for(size_t i = 0; i < capacity_; ++i){
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    ~LockFreeMpscQueue(){ // 모든 producer와 consumer가 종료된 뒤 파괴해야 한다.
        size_t begin = head_.value.load(std::memory_order_relaxed);
        size_t end = tail_.value.load(std::memory_order_relaxed);
        for(size_t pos = begin; pos != end; ++pos){
            Slot& slot = slots_[pos % capacity_];
            if(slot.sequence.load(std::memory_order_relaxed) == pos + 1){
                slot.element()->~T();
            }
        }
    } 

    LockFreeMpscQueue(const LockFreeMpscQueue&) = delete;
    LockFreeMpscQueue& operator=(const LockFreeMpscQueue&) = delete;
    LockFreeMpscQueue(LockFreeMpscQueue&&) = delete;
    LockFreeMpscQueue& operator=(LockFreeMpscQueue&&) = delete;

    bool push(T&& msg) noexcept {
        size_t pos = tail_.value.load(std::memory_order_relaxed);
        for(;;){
            Slot& slot = slots_[pos % capacity_];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos);

            if(diff == 0){
                if(tail_.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)){
                    ::new (static_cast<void*>(slot.storage)) T(std::move(msg));
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            }else if(diff < 0){
                return false;
            }else{
                pos = tail_.value.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& out) noexcept {
        size_t pos = head_.value.load(std::memory_order_relaxed);
        Slot& slot = slots_[pos % capacity_];
        size_t seq = slot.sequence.load(std::memory_order_acquire);

        auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos + 1);
        if(diff != 0) return false;

        T* element = slot.element();
        out = std::move(*element);
        element->~T();

        head_.value.store(pos + 1, std::memory_order_relaxed);
        slot.sequence.store(pos + capacity_, std::memory_order_release);
        return true;
    }

    size_t popBatch(T* out, size_t maxCount) noexcept {
        if(out == nullptr || maxCount == 0) return 0;
        size_t count = 0;
        while((count < maxCount) && pop(out[count])){
            ++count;
        }
        return count;
    }

    bool empty() const noexcept {
        size_t pos = head_.value.load(std::memory_order_relaxed);
        const Slot& slot = slots_[pos % capacity_];
        return (slot.sequence.load(std::memory_order_acquire) != pos + 1);
    }

    size_t capacity() const noexcept { return capacity_; }

    size_t count() const noexcept { // approximate count
        size_t h = head_.value.load(std::memory_order_relaxed);
        size_t t = tail_.value.load(std::memory_order_relaxed);
        size_t reserved = t - h;
        return reserved < capacity_ ? reserved : capacity_;
    }

private:
    struct Slot{
        std::atomic<size_t> sequence{0};
        alignas(T) std::byte storage[sizeof(T)];

        T* element() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
        const T* element() const noexcept { return std::launder(reinterpret_cast<const T*>(storage)); }
    };

    struct alignas(kCacheLine) AlignedAtomic{
        std::atomic<size_t> value{0};
    };

    std::unique_ptr<Slot[]> slots_;
    const size_t capacity_;
    AlignedAtomic head_, tail_;
};

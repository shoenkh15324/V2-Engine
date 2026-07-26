#pragma once
#include <cstdint>
#include <atomic>

using TypeId = uint16_t;

namespace detail{
    inline TypeId nextTypeId(){
        static std::atomic<TypeId> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }
} // namespace detail

template<typename T>
TypeId typeId(){
    static const TypeId id = detail::nextTypeId();
    return id;
}

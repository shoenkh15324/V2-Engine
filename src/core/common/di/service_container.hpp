#pragma once
#include <memory>
#include <string>
#include <utility>
#include <typeinfo>
#include <typeindex>
#include <stdexcept>
#include <functional>
#include <type_traits>
#include <unordered_map>

enum class Lifetime {
    Transient,
    Singleton
};

class ServiceContainer {
public:
    // bind<IPmu, PmuRsp5>()  — 의존성 없는 구현체 등록 (같은 키는 덮어씀)
    template <typename T, typename Impl>
    void bind(Lifetime lifetime = Lifetime::Transient){
        static_assert(std::is_base_of_v<T, Impl>, "Impl must derive from T");
        static_assert(!std::is_abstract_v<Impl>, "Impl must not be abstract");
        static_assert(std::is_default_constructible_v<Impl>, "Impl must be default-constructible, use bindFactory() instead");
        registerFactory<T>([]() -> std::shared_ptr<void>{
            return std::make_shared<Impl>();
        }, lifetime);
    }

    // bindFactory<CmdActor>([](ServiceContainer& c, ...){ ... })  — 생성자 의존성 배선
    template <typename T, typename F>
    void bindFactory(F&& f, Lifetime lifetime = Lifetime::Transient){
        static_assert(std::is_invocable_r_v<std::shared_ptr<T>, F, ServiceContainer&>, "factory must be callable as shared_ptr<T>(ServiceContainer&)");
        std::function<std::shared_ptr<T>(ServiceContainer&)> fn = std::forward<F>(f);
        registerFactory<T>([this, fn = std::move(fn)]() -> std::shared_ptr<void>{
            return fn(*this);
        }, lifetime);
    }

    // 조회 — 미등록 시 등록된 타입명을 포함한 예외
    template <typename T>
    std::shared_ptr<T> resolve() const {
        auto it = factories_.find(std::type_index(typeid(T)));
        if(it == factories_.end()){
            throw std::runtime_error(std::string("DI: type not registered: ") + typeid(T).name());
        }
        return std::static_pointer_cast<T>(it->second.create());
    }

    template <typename T>
    bool contains() const { return factories_.contains(std::type_index(typeid(T))); }

    template <typename T>
    bool remove(){ return factories_.erase(std::type_index(typeid(T))) > 0; }

private:
    using FactoryFunc = std::function<std::shared_ptr<void>()>;

    struct Factory {
        FactoryFunc create;
    };

    template <typename T, typename F>
    void registerFactory(F&& f, Lifetime lifetime){
        factories_[std::type_index(typeid(T))] = Factory{applyLifetime(std::forward<F>(f), lifetime)};
    }

    // Singleton이면 최초 생성 결과를 클로저 캐시에 보관.
    // (스레드 안전성 계약: bind/resolve는 시작 전 단일 스레드에서만. Singleton lazy 캐시도 마찬가지)
    template <typename F>
    static auto applyLifetime(F&& f, Lifetime lifetime){
        if(lifetime == Lifetime::Transient){
            return FactoryFunc{std::forward<F>(f)};
        }
        auto cache = std::make_shared<std::shared_ptr<void>>();
        return FactoryFunc{
            [cache, factory = std::forward<F>(f)]() -> std::shared_ptr<void>{
                if(!*cache) *cache = factory();
                return *cache;
            }
        };
    }

    std::unordered_map<std::type_index, Factory> factories_;
};

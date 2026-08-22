#include "bench/event_loop_factory.hpp"

#if defined(_WIN32)
    // TODO(windows): IOCP 기반 IEventLoop 구현 예정 — 그 전까지 nullptr 반환
#else
    #include "infra/platform/linux/event_loop_epoll.hpp"
#endif

namespace bench{

std::unique_ptr<IEventLoop> makeDefaultEventLoop(int maxEvents, int waitTimeoutMs){
#if defined(_WIN32)
    (void)maxEvents;
    (void)waitTimeoutMs;
    return nullptr;
#else
    return std::make_unique<EventLoopEpoll>(maxEvents, waitTimeoutMs);
#endif
}

} // namespace bench

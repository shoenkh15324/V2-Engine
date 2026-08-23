#pragma once
#include <memory>
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

namespace bench{

// 플랫폼 기본 IEventLoop 생성. 미지원 플랫폼은 nullptr 반환
// (벤치는 fd I/O를 쓰지 않으므로 nullptr 이벤트 루프로도 동작 가능)
std::unique_ptr<IEventLoop> makeDefaultEventLoop(int maxEvents = 64, int waitTimeoutMs = 1000);

} // namespace bench

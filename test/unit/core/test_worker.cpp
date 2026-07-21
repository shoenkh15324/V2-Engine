#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher/worker.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"

TEST(Worker, Create){
    WorkDispatcher d(1);
    Worker w(&d, 0, 32);
}

TEST(Worker, StartStop){
    WorkDispatcher d(1);
    Worker w(&d, 0, 32);
    w.start();
    w.stop(); // thread join까지 완료
}

TEST(Worker, StartStopRepeated){
    WorkDispatcher d(1);
    Worker w(&d, 0, 32);
    w.start();
    w.stop();
    w.start();
    w.stop();
}

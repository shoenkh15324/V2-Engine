#include <gtest/gtest.h>
#include "core/actor_system/runtime/dispatcher/worker.hpp"
#include "core/actor_system/runtime/dispatcher/dispatcher.hpp"

TEST(Worker, Create){
    Dispatcher d(1);
    Worker w(&d, 0, 32);
}

TEST(Worker, StartStop){
    Dispatcher d(1);
    Worker w(&d, 0, 32);
    w.start();
    w.stop(); // thread join까지 완료
}

TEST(Worker, StartStopRepeated){
    Dispatcher d(1);
    Worker w(&d, 0, 32);
    w.start();
    w.stop();
    w.start();
    w.stop();
}

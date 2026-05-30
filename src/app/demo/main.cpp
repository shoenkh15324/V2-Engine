#include <iostream>
#include "core/common/time.hpp"
#include "core/common/log.hpp"

int main(int, char**){
    V2_LOG_INFO("V2_Engine Open");
    V2_LOG_INFO("Build Data: %s", core::common::Time::nowDateString().c_str());
}

#include <iostream>
#include "../../core/common/time.hpp"

int main(int, char**){
    std::cout << "Demo App\n";
    std::cout << "Build Date: " << core::common::Time::nowDateString() << "\n";
}

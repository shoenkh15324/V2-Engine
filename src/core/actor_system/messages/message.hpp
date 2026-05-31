#pragma once
#include <variant>

struct DummyMsg {};

using Message = std::variant<
    DummyMsg
>;

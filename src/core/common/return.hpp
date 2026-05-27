#pragma once

namespace core::common{

enum class Result{
    Ok = 0,
    Fail = -1,
    InvalidArg = -2,
    InvalidState = -3,
    Timeout = -4,
};

} // namespace core::common

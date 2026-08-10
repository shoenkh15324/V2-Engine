#pragma once
#include "system_data.hpp"

class ISys{
public:
    virtual ~ISys() = default;
    virtual int open() = 0;
    virtual int close() = 0;
    virtual int collect(SystemResources& data) = 0;
};

#pragma once
#include "service/monitor/monitor_data.hpp"

class IPmu{
public:
    virtual ~IPmu() = default;
    virtual int open() = 0;
    virtual int close() = 0;
    virtual bool isOpen() const = 0;
    virtual int readPmuData(PmuData& data) = 0;
};

#pragma once
#include <cstdint>
#include <cstddef>

class II2c{
public:
    virtual ~II2c() = default;

    virtual int open(int busNumber) = 0;
    virtual int close() = 0;

    virtual bool isOpen() const = 0;
    virtual int read(uint16_t addr, void* data, size_t len) = 0;
    virtual int write(uint16_t addr, const void* data, size_t len) = 0;
    virtual int transfer(uint16_t addr, const void* wbuf, size_t wlen, void* rbuf, size_t rlen);
    virtual int busNumber() const = 0;
};

#pragma once
#include "i_i2c.hpp"

class I2c : public II2c{
public:
    I2c() = default;
    ~I2c() override;

    I2c(const I2c&) = delete;
    I2c& operator=(const I2c&) = delete;
    I2c(I2c&&) noexcept;
    I2c& operator=(I2c&&) noexcept;

    int open(int busNumber) override;
    int close() override;

    bool isOpen() const override;
    int read(uint16_t addr, void* data, size_t len) override;
    int write(uint16_t addr, const void* data, size_t len) override;
    int transfer(uint16_t addr, const void* wbuf, size_t wlen, void* rbuf, size_t rlen) override;
    int busNumber() const override;

private:
    int fd_ = -1;
    int busNum_ = -1;
};

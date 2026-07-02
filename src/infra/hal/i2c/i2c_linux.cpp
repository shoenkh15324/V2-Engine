#include "i2c_linux.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <cerrno>
#include <cstring>

I2c::~I2c(){
    close();
}

I2c::I2c(I2c&& other) noexcept : fd_(other.fd_), busNum_(other.busNum_){
    other.fd_ = -1;
    other.busNum_ = -1;
}

I2c& I2c::operator=(I2c&& other) noexcept {
    if(this != &other){
        close();
        fd_ = other.fd_;
        busNum_ = other.busNum_;
        other.fd_ = -1;
        other.busNum_ = -1;
    }
    return *this;
}

int I2c::open(int busNumber){
    if(fd_ >= 0) return InvalidState;
    char path[32];
    int n = snprintf(path, sizeof(path), "/dev/i2c-%d", busNumber);
    if((n < 0) || (static_cast<size_t>(n) >= sizeof(path))) return InvalidArg;

    fd_ = ::open(path, O_RDWR);
    if(fd_ < 0){ V2_LOG_ERROR("Failed to open %s: %s", path, strerror(errno));
        return Fail;
    }
    unsigned long funcs = 0;
    if(ioctl(fd_, I2C_FUNCS, &funcs) < 0){ V2_LOG_ERROR("I2C_FUNCS failed on %s: %s", path, strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return Fail;
    }
    if(!(funcs & I2C_FUNC_I2C)){ V2_LOG_ERROR("%s does not support I2C protocol", path);
        ::close(fd_);
        fd_ = -1;
        return Fail;
    }
    busNum_ = busNumber;
    V2_LOG_INFO("I2C bus %d opened (fd=%d)", busNum_, fd_);
    return Ok;
}

int I2c::close(){
    if(fd_ >= 0){
        ::close(fd_);
        V2_LOG_INFO("I2C bus %d closed", busNum_);
        fd_ = -1;
        busNum_ = -1;
    }
    return Ok;
}

bool I2c::isOpen() const {
    return fd_ >= 0;
}

int I2c::read(uint16_t addr, void* data, size_t len){
    return transfer(addr, nullptr, 0, data, len);
}

int I2c::write(uint16_t addr, const void* data, size_t len){
    return transfer(addr, data, len, nullptr, 0);
}

int I2c::transfer(uint16_t addr, const void* wbuf, size_t wlen, void* rbuf, size_t rlen){
    if(fd_ < 0) return InvalidState;
    if(wlen == 0 && rlen == 0) return InvalidArg;
    struct i2c_msg msgs[2];
    int count = 0;

    if(wlen > 0){
        msgs[count].addr = addr;
        msgs[count].flags = 0;
        msgs[count].len = static_cast<uint16_t>(wlen);
        msgs[count].buf = const_cast<uint8_t*>(static_cast<const uint8_t*>(wbuf));
        count++;
    }
    if(rlen > 0){
        msgs[count].addr = addr;
        msgs[count].flags = I2C_M_RD;
        msgs[count].len = static_cast<uint16_t>(rlen);
        msgs[count].buf = static_cast<uint8_t*>(rbuf);
        count++;
    }
    struct i2c_rdwr_ioctl_data data;
    data.msgs = msgs;
    data.nmsgs = static_cast<uint32_t>(count);
    if(ioctl(fd_, I2C_RDWR, &data) < 0){ V2_LOG_ERROR("I2C_RDWR failed (addr=0x%02x): %s", addr, strerror(errno));
        return Fail;
    }
    return static_cast<int>(wlen + rlen);
}

int I2c::busNumber() const {
    return busNum_;
}

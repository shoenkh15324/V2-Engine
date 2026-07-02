#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class HalType : uint8_t {
    I2c = 0,
    Spi,
    Gpio,
    Uart,
};

struct DeviceRegister{
    std::string name;
    HalType type;
    int bus;
};

struct DeviceUnregister{
    std::string name;
};

struct DeviceEnumerate{
    std::string requesterName;
};

struct DeviceList{
    std::vector<std::string> names;
    std::vector<uint8_t> types;
    std::vector<int> buses;
};

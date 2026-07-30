#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "message_traits.hpp"

enum class HalType : uint8_t {
    I2c = 0,
    Spi,
    Gpio,
    Uart,
};

struct DeviceRegister{
    static constexpr MessageId kId = MessageId::DeviceRegister;
    std::string name;
    HalType type;
    int bus;
};

struct DeviceUnregister{
    static constexpr MessageId kId = MessageId::DeviceUnregister;
    std::string name;
};

struct DeviceEnumerate{
    static constexpr MessageId kId = MessageId::DeviceEnumerate;
    std::string requesterName;
};

struct DeviceList{
    static constexpr MessageId kId = MessageId::DeviceList;
    std::vector<std::string> names;
    std::vector<uint8_t> types;
    std::vector<int> buses;
};

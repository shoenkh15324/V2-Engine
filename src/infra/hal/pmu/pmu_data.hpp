#pragma once
#include <cstdint>

struct PmuData{
    uint64_t clockArmHz = 0; // vcgencmd measure_clock arm (Hz)
    uint64_t clockCoreHz = 0; // vcgencmd measure_clock core (Hz)
    uint64_t clockV3dHz = 0; // vcgencmd measure_clock v3d (Hz)
    uint64_t memArmMb = 0; // vcgencmd get_mem arm (MB)
    uint64_t memGpuMb = 0; // vcgencmd get_mem gpu (MB)
    uint64_t throttled = 0; // vcgencmd get_throttled (bitmask)
    float tempCelsius = 0.0f; // vcgencmd measure_temp (°C)
    float voltCore = 0.0f; // vcgencmd measure_volts core (V)
    float currentVddCoreA = 0.0f; // vcgencmd pmic_read_adc VDD_CORE_A (A)
};

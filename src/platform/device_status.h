/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace platform {

struct DeviceStatus {
    bool wifi_present{false};
    bool wifi_connected{false};
    int32_t wifi_strength_percent{-1};
    bool ethernet_present{false};
    bool ethernet_connected{false};
    bool battery_present{false};
    bool battery_charging{false};
    int32_t battery_percent{-1};
};

DeviceStatus read_device_status();

// This device's CPU temperature in degrees Celsius, or a negative value when unknown
// (e.g. the desktop simulator).
double read_cpu_temperature();

} // namespace platform

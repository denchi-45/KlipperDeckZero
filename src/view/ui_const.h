/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */



#pragma once

#include <cstdint>

namespace view {

// some font constants
constexpr const char* ICON_SIGN_OUT           = "\uE42A";
constexpr const char* ICON_TEXT_BOLD          = "\uE5BE";
constexpr const char* ICON_MOON               = "\uE330";
constexpr const char* ICON_SUN                = "\uE474";
constexpr const char* ICON_SQUARE_ARROW_LEFT  = "\uE074";
constexpr const char* ICON_SQUARE_ARROW_RIGHT = "\uE076";
constexpr const char* ICON_MINUS              = "\uE32A";
constexpr const char* ICON_PLUS               = "\uE3D4";
constexpr const char* ICON_INFO               = "\uE2CE";
constexpr const char* ICON_WIFI_NONE          = "\uE4F0";
constexpr const char* ICON_WIFI_LOW           = "\uE4EC";
constexpr const char* ICON_WIFI_MEDIUM        = "\uE4EE";
constexpr const char* ICON_WIFI_HIGH          = "\uE4EA";
constexpr const char* ICON_ETHERNET           = "\uEDDE";
constexpr const char* ICON_BAT_FULL           = "\uE7C4";
constexpr const char* ICON_BAT_HIGH           = "\uE7C2";
constexpr const char* ICON_BAT_MEDIUM         = "\uE7C0";
constexpr const char* ICON_BAT_LOW            = "\uE7BE";
constexpr const char* ICON_BAT_EMPTY          = "\uE7C6";
constexpr const char* ICON_BAT_CHARGING       = "\uE0BC";

// Phosphor 2.1 codepoints used by the printer pages.
constexpr const char* ICON_GAUGE              = "\uE628";
constexpr const char* ICON_THERMOMETER        = "\uE5C6";
constexpr const char* ICON_DROP               = "\uE210";
constexpr const char* ICON_FILES              = "\uE710";
constexpr const char* ICON_PLAY_PAUSE         = "\uE8BE";
constexpr const char* ICON_ARROWS_OUT_CARDINAL = "\uE0A4";
constexpr const char* ICON_FIRE               = "\uE242";
constexpr const char* ICON_CLOCK              = "\uE19A";
constexpr const char* ICON_HOURGLASS          = "\uE2B8";
constexpr const char* ICON_STACK              = "\uE466";
constexpr const char* ICON_PLUGS              = "\uEB56";
constexpr const char* ICON_WARNING            = "\uE4E0";
constexpr const char* ICON_WEBCAM             = "\uE9B2";

namespace color {

// Naive UI common color tokens, flattened to solid RGB values for LVGL.
namespace light {
constexpr uint32_t kPrimary      = 0x18a058;
constexpr uint32_t kInfo         = 0x2080f0;
constexpr uint32_t kSuccess      = 0x18a058;
constexpr uint32_t kWarning      = 0xf0a020;
constexpr uint32_t kError        = 0xd03050;
constexpr uint32_t kBody         = 0xffffff;
constexpr uint32_t kCard         = 0xffffff;
constexpr uint32_t kAction       = 0xfafafc;
constexpr uint32_t kButton       = 0xf5f5f5;
constexpr uint32_t kBorder       = 0xe0e0e6;
constexpr uint32_t kTextPrimary  = 0x1f2225;
constexpr uint32_t kTextDisabled = 0xc2c2c2;
} // namespace light

namespace dark {
constexpr uint32_t kPrimary      = 0x63e2b7;
constexpr uint32_t kInfo         = 0x70c0e8;
constexpr uint32_t kSuccess      = 0x63e2b7;
constexpr uint32_t kWarning      = 0xf2c97d;
constexpr uint32_t kError        = 0xe88080;
constexpr uint32_t kBody         = 0x101014;
constexpr uint32_t kCard         = 0x18181c;
constexpr uint32_t kAction       = 0x0f0f0f;
constexpr uint32_t kButton       = 0x141414;
constexpr uint32_t kBorder       = 0x3d3d3d;
constexpr uint32_t kTextPrimary  = 0xe6e6e6;
constexpr uint32_t kTextDisabled = 0x616161;
} // namespace dark

} // namespace color

} // namespace view

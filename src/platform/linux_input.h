#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace platform {

// Keys 4..8 click the nav bar slot of the same position (see register_nav_button).
constexpr size_t kNavKeyCount = 5;
constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U; // Fn+J
constexpr uint32_t kKeyHelp = kSpecialKeyBase | 19U;        // Fn+H
constexpr uint32_t kKeyPrevLayer = kSpecialKeyBase | 20U;   // Sym+H
constexpr uint32_t kKeyNextLayer = kSpecialKeyBase | 21U;   // Sym+J

using KeyReleaseListener = void (*)(uint32_t key, const char* key_name, void* user_data);
using GlobalKeyListener = bool (*)(uint32_t key,
                                   const char* key_name,
                                   bool long_pressed,
                                   void* user_data);

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);
void set_key_release_listener(KeyReleaseListener listener, void* user_data);
void clear_key_release_listener(KeyReleaseListener listener, void* user_data);
void set_global_key_listener(GlobalKeyListener listener, void* user_data);
void clear_global_key_listener(GlobalKeyListener listener, void* user_data);

// The active screen's own key handling (e.g. a text field). The global key listener offers keys
// to it before treating them as shortcuts; returning true consumes the key.
using ScreenKeyHandler = bool (*)(uint32_t key, bool long_pressed, void* user_data);
void set_screen_key_handler(ScreenKeyHandler handler, void* user_data);
void clear_screen_key_handler(ScreenKeyHandler handler, void* user_data);
bool dispatch_screen_key(uint32_t key, bool long_pressed);
const char* describe_key(uint32_t key);

} // namespace platform

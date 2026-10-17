/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "control_panel.h"

#include <array>
#include <cstdint>
#include <string>

namespace app {
class PrinterCommands;
}

namespace screen {

// The running print: pause / resume, cancel (Enter twice), and the emergency stop (hold Enter).
class ControlScreen : public BaseScreen {
public:
    ControlScreen(viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets,
                  app::PrinterCommands& commands);
    ~ControlScreen() override;

private:
    void build_content(lv_obj_t* content) override;
    void build_stat(lv_obj_t* parent, const char* icon, lv_subject_t* subject);
    bool handle_key(uint32_t key, bool long_pressed);
    model::PrintState print_state() const;
    void start_confirm_timer();
    void refresh();

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void confirm_timer_cb(lv_timer_t* timer);

    app::PrinterCommands& commands_;
    app::ControlPanel panel_;
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    std::array<lv_obj_t*, 3> buttons_{};
    std::array<lv_obj_t*, 3> button_labels_{};
    lv_obj_t* hint_{nullptr};
    // Redraws once the cancel confirmation window has passed.
    lv_timer_t* confirm_timer_{nullptr};
    // Feedback for the last key, e.g. "No active print"; cleared by the next key.
    std::string message_;
};

} // namespace screen

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "move_panel.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace app {
class PrinterCommands;
}

namespace screen {

// Axis moves: pick an axis and a step, then jog, home, or turn the motors off.
class MoveScreen : public BaseScreen {
public:
    MoveScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               app::PrinterCommands& commands);
    ~MoveScreen() override;

private:
    struct ChoiceCell {
        lv_obj_t* cell{nullptr};
        lv_obj_t* label{nullptr};
        std::vector<lv_obj_t*> chips;
    };

    struct ActionCell {
        lv_obj_t* cell{nullptr};
        lv_obj_t* label{nullptr};
    };

    // Rows of the panel from MoveRow::JogMinus on, in order.
    static constexpr std::size_t kActionCount = 5;

    void build_content(lv_obj_t* content) override;
    lv_obj_t* build_line(lv_obj_t* parent);
    ChoiceCell build_choice_cell(lv_obj_t* parent, const char* title, const std::vector<std::string>& choices);
    ActionCell build_action_cell(lv_obj_t* parent);
    bool handle_key(uint32_t key, bool long_pressed);
    void refresh();

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::PrinterCommands& commands_;
    app::MovePanel panel_;
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    lv_obj_t* homed_{nullptr};
    ChoiceCell axis_;
    ChoiceCell step_;
    std::array<ActionCell, kActionCount> actions_{};
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

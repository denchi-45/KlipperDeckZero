/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "extruder_panel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace app {
class PrinterCommands;
}

namespace screen {

// Filament moves: pick a length, a speed and a direction, then extrude or retract.
class ExtruderScreen : public BaseScreen {
public:
    ExtruderScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   app::PrinterCommands& commands);
    ~ExtruderScreen() override;

private:
    struct ChoiceRow {
        lv_obj_t* row{nullptr};
        lv_obj_t* label{nullptr};
        std::vector<lv_obj_t*> chips;
    };

    void build_content(lv_obj_t* content) override;
    ChoiceRow build_choice_row(lv_obj_t* parent,
                               const char* title,
                               const std::vector<std::string>& choices,
                               const char* unit);
    bool handle_key(uint32_t key, bool long_pressed);
    void refresh();

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::PrinterCommands& commands_;
    app::ExtruderPanel panel_;
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    lv_obj_t* readiness_{nullptr};
    ChoiceRow length_;
    ChoiceRow speed_;
    ChoiceRow direction_;
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

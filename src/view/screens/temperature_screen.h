/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "temperature_panel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace app {
class PrinterCommands;
}

namespace screen {

// Heater targets: step or type the nozzle / bed target, or apply a material preset.
class TemperatureScreen : public BaseScreen {
public:
    TemperatureScreen(viewmodel::BaseViewModel& view_model,
                      app::AssetManager& assets,
                      app::PrinterCommands& commands);
    ~TemperatureScreen() override;

private:
    struct HeaterWidgets {
        lv_obj_t* row{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* target{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    HeaterWidgets build_heater_row(lv_obj_t* parent,
                                   const char* title,
                                   lv_subject_t* temperature,
                                   lv_subject_t* power);
    bool handle_key(uint32_t key, bool long_pressed);
    app::HeaterTargets current_targets() const;
    void refresh();
    void style_row(lv_obj_t* row, bool focused, bool editing);

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void refresh_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::PrinterCommands& commands_;
    app::TemperaturePanel panel_;
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    const lv_font_t* large_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    HeaterWidgets nozzle_;
    HeaterWidgets bed_;
    lv_obj_t* presets_row_{nullptr};
    lv_obj_t* presets_label_{nullptr};
    std::vector<lv_obj_t*> preset_chips_;
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

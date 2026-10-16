/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// Home page: print job, progress, times and heater temperatures.
// A connection overlay covers it until Moonraker/Klippy is ready.
class StatusScreen : public BaseScreen {
public:
    StatusScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    void build_heater_card(lv_obj_t* parent,
                           const char* title,
                           lv_subject_t* temperature,
                           lv_subject_t* target,
                           lv_subject_t* power,
                           lv_obj_t** card_out);
    void build_overlay(lv_obj_t* content);

    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    const lv_font_t* large_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    const lv_font_t* overlay_icon_font_{nullptr};
};

} // namespace screen

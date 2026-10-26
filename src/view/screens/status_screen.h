/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "status_thumbnail.h"

#include <memory>
#include <string>

namespace app {
class PrinterCommands;
}

namespace screen {

// Home page: print job, progress, times and heater temperatures, with the thumbnail of the
// file while it is printing or paused.
// A connection overlay covers it until Moonraker/Klippy is ready.
class StatusScreen : public BaseScreen {
public:
    StatusScreen(viewmodel::BaseViewModel& view_model,
                 app::AssetManager& assets,
                 app::PrinterCommands& commands);
    ~StatusScreen() override;

private:
    void build_content(lv_obj_t* content) override;
    void build_heater_card(lv_obj_t* parent,
                           const char* title,
                           lv_subject_t* temperature,
                           lv_subject_t* target,
                           lv_subject_t* power,
                           lv_obj_t** card_out);
    void build_overlay(lv_obj_t* content);
    // Follows the print state: fetches the thumbnail of a new print, drops it when it ends.
    void update_thumbnail();
    void load_thumbnail();
    // Shows thumbnail_state_.thumbnail() when it changed.
    void refresh_thumbnail();

    static void print_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::PrinterCommands& commands_;
    app::StatusThumbnail thumbnail_state_;
    // Expires with the screen, so responses arriving later are dropped.
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    const lv_font_t* large_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};
    const lv_font_t* overlay_icon_font_{nullptr};
    lv_obj_t* thumbnail_frame_{nullptr};
    lv_obj_t* thumbnail_{nullptr};
    // The PNG the image shows; png_dsc_ points into it, so both change together.
    std::string png_;
    lv_image_dsc_t png_dsc_{};
};

} // namespace screen

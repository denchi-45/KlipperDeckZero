/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "file_browser.h"

#include <array>
#include <cstdint>
#include <memory>

namespace app {
class PrinterCommands;
}

namespace screen {

// G-code files on the printer, newest first. Enter shows a file's details, and Enter again
// starts printing it.
class FilesScreen : public BaseScreen {
public:
    FilesScreen(viewmodel::BaseViewModel& view_model,
                app::AssetManager& assets,
                app::PrinterCommands& commands);
    ~FilesScreen() override;

private:
    struct RowWidgets {
        lv_obj_t* row{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* size{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void build_details(lv_obj_t* parent);
    bool handle_key(uint32_t key, bool long_pressed);
    void load_files();
    void load_metadata();
    void refresh();
    void refresh_list();
    void refresh_details();

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::PrinterCommands& commands_;
    app::FileBrowser browser_;
    // Expires with the screen, so responses arriving later are dropped.
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* body_font_{nullptr};
    lv_obj_t* list_box_{nullptr};
    std::array<RowWidgets, app::FileBrowser::kVisibleRows> rows_{};
    lv_obj_t* message_{nullptr};
    lv_obj_t* details_box_{nullptr};
    lv_obj_t* details_title_{nullptr};
    lv_obj_t* details_keys_{nullptr};
    lv_obj_t* details_body_{nullptr};
    lv_obj_t* hint_{nullptr};
};

} // namespace screen

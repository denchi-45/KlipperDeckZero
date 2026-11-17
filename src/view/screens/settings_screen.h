/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "settings_form.h"

#include <cstdint>
#include <string>
#include <vector>

namespace app {
class SettingsStore;
}

namespace screen {

// Connection settings: where to reach Moonraker. Rows come from app::connection_settings_fields(),
// so new settings appear here without changing the screen.
class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   app::SettingsStore& store);
    ~SettingsScreen() override;

private:
    struct RowWidgets {
        lv_obj_t* row{nullptr};
        lv_obj_t* label{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    bool handle_key(uint32_t key, bool long_pressed);
    void submit();
    void refresh();
    void set_message(std::string message, bool error);

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    app::SettingsStore& store_;
    app::SettingsForm form_;
    std::vector<RowWidgets> rows_;
    lv_obj_t* list_{nullptr};
    lv_obj_t* connection_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
    std::string message_;
    bool message_is_error_{false};
};

} // namespace screen

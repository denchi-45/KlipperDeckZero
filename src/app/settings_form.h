/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "application_config.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {

enum class SettingsFieldKind {
    Choice, // cycles through fixed values
    Text,   // free text typed on the keyboard
    Action, // the "save" row
};

struct SettingsChoice {
    const char* label;
    const char* value; // as written in the config file
};

// One row of a settings screen. Values are read and validated through the setting's
// SettingDescriptor, so a row only names the setting and how to show it.
struct SettingsField {
    const char* label;
    SettingsFieldKind kind;
    Setting setting;
    std::vector<SettingsChoice> choices; // Choice only
    std::size_t max_length;              // Text only
    bool secret;                         // Text only: masked unless being edited
    // Hidden when it returns false; nullptr means always shown.
    bool (*visible)(const ApplicationConfig& config);
};

// Rows of the connection settings screen. Add a SettingsField here to show another setting.
const std::vector<SettingsField>& connection_settings_fields();

enum class SettingsInput {
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Character,
};

// Keyboard-driven editing of a draft config, independent of LVGL.
class SettingsForm {
public:
    enum class Result {
        Ignored, // not used by the form; the caller may treat it as a shortcut
        Handled,
        Submit,  // the save row was activated; draft() holds the values to apply
    };

    SettingsForm(const ApplicationConfig& config, const std::vector<SettingsField>& fields);

    Result handle(SettingsInput input, char character = 0);

    const std::vector<SettingsField>& fields() const;
    std::vector<std::size_t> visible_fields() const;
    std::size_t focused() const;
    bool editing() const;
    // The text shown for a field: the edit buffer while editing it, the choice label, or the value.
    std::string display_value(std::size_t field) const;
    // Validation error of the last edit, empty when none.
    const std::string& error() const;
    const ApplicationConfig& draft() const;

private:
    void move_focus(int step);
    void cycle_choice(int step);
    bool commit_edit();

    const std::vector<SettingsField>& fields_;
    ApplicationConfig draft_;
    std::size_t focused_{0};
    bool editing_{false};
    std::string buffer_;
    std::string error_;
};

// Settings whose value differs between the two configs, in table order.
std::vector<Setting> changed_settings(const ApplicationConfig& before, const ApplicationConfig& after);

} // namespace app

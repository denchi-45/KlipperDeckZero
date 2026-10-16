/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_form.h"

#include <algorithm>

namespace app {
namespace {

bool uses_unix_socket(const ApplicationConfig& config) {
    return config.moonraker.transport == MoonrakerTransportKind::UnixSocket;
}

bool uses_websocket(const ApplicationConfig& config) {
    return config.moonraker.transport == MoonrakerTransportKind::WebSocket;
}

const std::vector<SettingsField> kConnectionFields = {
    {"Connect to", SettingsFieldKind::Choice, Setting::MoonrakerTransport,
     {{"This device", "unix"}, {"Network", "websocket"}}, 0, false, nullptr},
    // sun_path is 108 bytes including the terminating NUL.
    {"Socket", SettingsFieldKind::Text, Setting::MoonrakerSocketPath, {}, 107, false,
     uses_unix_socket},
    {"Host", SettingsFieldKind::Text, Setting::MoonrakerHost, {}, 253, false, uses_websocket},
    {"Port", SettingsFieldKind::Text, Setting::MoonrakerPort, {}, 5, false, uses_websocket},
    {"API key", SettingsFieldKind::Text, Setting::MoonrakerApiKey, {}, 64, true, uses_websocket},
    {"Save & connect", SettingsFieldKind::Action, Setting::MoonrakerTransport, {}, 0, false,
     nullptr},
};

std::string current_value(const SettingsField& field, const ApplicationConfig& config) {
    return setting_descriptor(field.setting).format(config);
}

} // namespace

const std::vector<SettingsField>& connection_settings_fields() {
    return kConnectionFields;
}

SettingsForm::SettingsForm(const ApplicationConfig& config, const std::vector<SettingsField>& fields)
    : fields_(fields), draft_(config) {}

SettingsForm::Result SettingsForm::handle(SettingsInput input, char character) {
    if (fields_.empty()) {
        return Result::Ignored;
    }

    if (editing_) {
        switch (input) {
            case SettingsInput::Character:
                if (character >= 32 && character <= 126 &&
                    buffer_.size() < fields_[focused_].max_length) {
                    buffer_.push_back(character);
                    error_.clear();
                }
                return Result::Handled;
            case SettingsInput::Backspace:
                if (!buffer_.empty()) {
                    buffer_.pop_back();
                }
                error_.clear();
                return Result::Handled;
            case SettingsInput::Enter:
                commit_edit();
                return Result::Handled;
            case SettingsInput::Escape:
                editing_ = false;
                error_.clear();
                return Result::Handled;
            default:
                // Every key belongs to the text field while editing, so no shortcut fires.
                return Result::Handled;
        }
    }

    const auto& field = fields_[focused_];
    switch (input) {
        case SettingsInput::Up:
            move_focus(-1);
            return Result::Handled;
        case SettingsInput::Down:
            move_focus(1);
            return Result::Handled;
        case SettingsInput::Left:
        case SettingsInput::Right:
            if (field.kind != SettingsFieldKind::Choice) {
                return Result::Ignored;
            }
            cycle_choice(input == SettingsInput::Left ? -1 : 1);
            return Result::Handled;
        case SettingsInput::Enter:
            error_.clear();
            if (field.kind == SettingsFieldKind::Choice) {
                cycle_choice(1);
                return Result::Handled;
            }
            if (field.kind == SettingsFieldKind::Text) {
                buffer_ = current_value(field, draft_);
                editing_ = true;
                return Result::Handled;
            }
            return Result::Submit;
        default:
            return Result::Ignored;
    }
}

const std::vector<SettingsField>& SettingsForm::fields() const {
    return fields_;
}

std::vector<std::size_t> SettingsForm::visible_fields() const {
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (!fields_[i].visible || fields_[i].visible(draft_)) {
            visible.push_back(i);
        }
    }
    return visible;
}

std::size_t SettingsForm::focused() const {
    return focused_;
}

bool SettingsForm::editing() const {
    return editing_;
}

std::string SettingsForm::display_value(std::size_t index) const {
    if (index >= fields_.size()) {
        return {};
    }
    const auto& field = fields_[index];
    if (editing_ && index == focused_) {
        return field.secret ? std::string(buffer_.size(), '*') : buffer_;
    }
    switch (field.kind) {
        case SettingsFieldKind::Choice: {
            const auto value = current_value(field, draft_);
            for (const auto& choice : field.choices) {
                if (value == choice.value) {
                    return choice.label;
                }
            }
            return value;
        }
        case SettingsFieldKind::Text: {
            const auto value = current_value(field, draft_);
            return field.secret ? std::string(value.size(), '*') : value;
        }
        case SettingsFieldKind::Action:
            break;
    }
    return {};
}

const std::string& SettingsForm::error() const {
    return error_;
}

const ApplicationConfig& SettingsForm::draft() const {
    return draft_;
}

ApplicationConfig SettingsForm::apply_to(const ApplicationConfig& current) const {
    auto updated = current;
    for (const auto& field : fields_) {
        if (field.kind == SettingsFieldKind::Action) {
            continue;
        }
        // The draft only ever holds values that passed parse, so this cannot fail.
        const auto& descriptor = setting_descriptor(field.setting);
        std::string error;
        descriptor.parse(descriptor.format(draft_), updated, error);
    }
    return updated;
}

void SettingsForm::move_focus(int step) {
    const auto visible = visible_fields();
    if (visible.empty()) {
        return;
    }
    // When the focused row was just hidden, start from the first row after it.
    auto position = std::lower_bound(visible.begin(), visible.end(), focused_) - visible.begin();
    const bool focused_visible =
        position < static_cast<std::ptrdiff_t>(visible.size()) && visible[position] == focused_;
    if (!focused_visible && step > 0) {
        --position;
    }
    position = std::clamp<std::ptrdiff_t>(position + step, 0,
                                          static_cast<std::ptrdiff_t>(visible.size()) - 1);
    focused_ = visible[static_cast<std::size_t>(position)];
}

void SettingsForm::cycle_choice(int step) {
    const auto& field = fields_[focused_];
    if (field.choices.empty()) {
        return;
    }
    const auto value = current_value(field, draft_);
    const auto count = static_cast<int>(field.choices.size());
    int index = 0;
    for (int i = 0; i < count; ++i) {
        if (value == field.choices[static_cast<std::size_t>(i)].value) {
            index = i;
            break;
        }
    }
    index = (index + step + count) % count;
    std::string ignored;
    setting_descriptor(field.setting)
        .parse(field.choices[static_cast<std::size_t>(index)].value, draft_, ignored);
}

bool SettingsForm::commit_edit() {
    // Same as loading from the file, which trims values.
    const auto first = buffer_.find_first_not_of(' ');
    const auto value = first == std::string::npos
                           ? std::string()
                           : buffer_.substr(first, buffer_.find_last_not_of(' ') - first + 1);
    auto updated = draft_;
    std::string reason;
    if (!setting_descriptor(fields_[focused_].setting).parse(value, updated, reason)) {
        error_ = std::string(fields_[focused_].label) + ": " + reason;
        return false;
    }
    draft_ = updated;
    editing_ = false;
    error_.clear();
    return true;
}

std::vector<Setting> changed_settings(const ApplicationConfig& before, const ApplicationConfig& after) {
    std::vector<Setting> changed;
    for (const auto& descriptor : setting_descriptors()) {
        if (descriptor.format(before) != descriptor.format(after)) {
            changed.push_back(descriptor.id);
        }
    }
    return changed;
}

} // namespace app

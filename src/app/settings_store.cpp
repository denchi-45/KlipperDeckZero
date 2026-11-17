/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_store.h"

#include "settings_form.h"

#include <algorithm>
#include <utility>

namespace app {

SettingsStore::SettingsStore(ApplicationConfig config, std::string user_config_path)
    : config_(std::move(config)), user_config_path_(std::move(user_config_path)) {}

const ApplicationConfig& SettingsStore::config() const {
    return config_;
}

const std::string& SettingsStore::user_config_path() const {
    return user_config_path_;
}

void SettingsStore::add_change_listener(ChangeListener listener) {
    listeners_.push_back(std::move(listener));
}

bool SettingsStore::has_unsaved() const {
    return !unsaved_.empty();
}

bool SettingsStore::apply(const ApplicationConfig& updated, std::string& error) {
    error.clear();
    const auto changed = changed_settings(config_, updated);
    auto to_save = changed;
    for (const auto id : unsaved_) {
        if (std::find(to_save.begin(), to_save.end(), id) == to_save.end()) {
            to_save.push_back(id);
        }
    }
    if (to_save.empty()) {
        return true;
    }

    const bool saved = save_application_config(user_config_path_, updated, to_save, error);
    unsaved_ = saved ? std::vector<Setting>{} : to_save;
    config_ = updated;
    if (!changed.empty()) {
        for (const auto& listener : listeners_) {
            listener(config_, changed);
        }
    }
    return saved;
}

} // namespace app

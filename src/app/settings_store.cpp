/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_store.h"

#include "settings_form.h"

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

bool SettingsStore::apply(const ApplicationConfig& updated, std::string& error) {
    error.clear();
    const auto changed = changed_settings(config_, updated);
    if (changed.empty()) {
        return true;
    }

    const bool saved = save_application_config(user_config_path_, updated, changed, error);
    config_ = updated;
    for (const auto& listener : listeners_) {
        listener(config_, changed);
    }
    return saved;
}

} // namespace app

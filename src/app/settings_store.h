/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "application_config.h"

#include <functional>
#include <string>
#include <vector>

namespace app {

// Holds the running config and writes changes to the user config file. Screens change settings
// through apply(); listeners react to the settings that changed (e.g. reconnecting Moonraker).
class SettingsStore {
public:
    using ChangeListener =
        std::function<void(const ApplicationConfig& config, const std::vector<Setting>& changed)>;

    SettingsStore(ApplicationConfig config, std::string user_config_path);

    const ApplicationConfig& config() const;
    const std::string& user_config_path() const;

    void add_change_listener(ChangeListener listener);

    // Saves the settings that differ from the current config, then notifies listeners. The new
    // values take effect even when saving fails; error then says why they were not saved.
    bool apply(const ApplicationConfig& updated, std::string& error);

private:
    ApplicationConfig config_;
    std::string user_config_path_;
    std::vector<ChangeListener> listeners_;
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_form.h"
#include "settings_store.h"
#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using app::SettingsForm;
using app::SettingsInput;

app::ApplicationConfig local_config() {
    app::ApplicationConfig config;
    config.moonraker.transport = app::MoonrakerTransportKind::UnixSocket;
    return config;
}

std::string field_label(const SettingsForm& form) {
    return form.fields()[form.focused()].label;
}

void type(SettingsForm& form, const std::string& text) {
    for (const char character : text) {
        form.handle(SettingsInput::Character, character);
    }
}

void clear_buffer(SettingsForm& form) {
    for (int i = 0; i < 300; ++i) {
        form.handle(SettingsInput::Backspace);
    }
}

} // namespace

TEST_CASE("settings form: fields follow the connection kind") {
    SettingsForm form(local_config(), app::connection_settings_fields());
    CHECK_EQ(form.display_value(0), std::string("This device"));
    CHECK_EQ(form.visible_fields().size(), static_cast<std::size_t>(3)); // kind, socket, save

    CHECK(form.handle(SettingsInput::Right) == SettingsForm::Result::Handled);
    CHECK(form.draft().moonraker.transport == app::MoonrakerTransportKind::WebSocket);
    CHECK_EQ(form.display_value(0), std::string("Network"));
    CHECK_EQ(form.visible_fields().size(), static_cast<std::size_t>(5)); // kind, host, port, key, save

    CHECK(form.handle(SettingsInput::Enter) == SettingsForm::Result::Handled);
    CHECK(form.draft().moonraker.transport == app::MoonrakerTransportKind::UnixSocket);
}

TEST_CASE("settings form: focus skips hidden rows and stops at the ends") {
    SettingsForm form(local_config(), app::connection_settings_fields());
    form.handle(SettingsInput::Up);
    CHECK_EQ(field_label(form), std::string("Connect to"));
    form.handle(SettingsInput::Down);
    CHECK_EQ(field_label(form), std::string("Socket"));
    form.handle(SettingsInput::Down);
    CHECK_EQ(field_label(form), std::string("Save & connect"));
    form.handle(SettingsInput::Down);
    CHECK_EQ(field_label(form), std::string("Save & connect"));
}

TEST_CASE("settings form: editing a host and submitting") {
    SettingsForm form(local_config(), app::connection_settings_fields());
    form.handle(SettingsInput::Right); // Network
    form.handle(SettingsInput::Down);
    CHECK_EQ(field_label(form), std::string("Host"));

    CHECK(form.handle(SettingsInput::Enter) == SettingsForm::Result::Handled);
    CHECK(form.editing());
    clear_buffer(form);
    type(form, "192.168.1.50");
    // Shortcut keys are text while editing.
    CHECK(form.handle(SettingsInput::Character, 'd') == SettingsForm::Result::Handled);
    CHECK(form.handle(SettingsInput::Up) == SettingsForm::Result::Handled);
    form.handle(SettingsInput::Backspace);
    CHECK_EQ(form.display_value(form.focused()), std::string("192.168.1.50"));
    form.handle(SettingsInput::Enter);
    CHECK(!form.editing());
    CHECK_EQ(form.draft().moonraker.host, std::string("192.168.1.50"));

    form.handle(SettingsInput::Down); // Port
    form.handle(SettingsInput::Down); // API key
    form.handle(SettingsInput::Down); // Save
    CHECK(form.handle(SettingsInput::Enter) == SettingsForm::Result::Submit);
}

TEST_CASE("settings form: invalid values stay in the editor") {
    SettingsForm form(local_config(), app::connection_settings_fields());
    form.handle(SettingsInput::Right);
    form.handle(SettingsInput::Down);
    form.handle(SettingsInput::Down);
    CHECK_EQ(field_label(form), std::string("Port"));

    form.handle(SettingsInput::Enter);
    clear_buffer(form);
    type(form, "99999");
    form.handle(SettingsInput::Enter);
    CHECK(form.editing());
    CHECK(form.error().find("Port") != std::string::npos);
    CHECK_EQ(form.draft().moonraker.port, 7125);

    form.handle(SettingsInput::Escape);
    CHECK(!form.editing());
    CHECK(form.error().empty());
    CHECK_EQ(form.display_value(form.focused()), std::string("7125"));
}

TEST_CASE("settings form: keys outside editing are left to shortcuts") {
    SettingsForm form(local_config(), app::connection_settings_fields());
    CHECK(form.handle(SettingsInput::Character, 'd') == SettingsForm::Result::Ignored);
    CHECK(form.handle(SettingsInput::Escape) == SettingsForm::Result::Ignored);
}

TEST_CASE("settings form: secret values are masked") {
    auto config = local_config();
    config.moonraker.transport = app::MoonrakerTransportKind::WebSocket;
    config.moonraker.api_key = "abc";
    SettingsForm form(config, app::connection_settings_fields());
    const auto& fields = form.fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].setting == app::Setting::MoonrakerApiKey) {
            CHECK_EQ(form.display_value(i), std::string("***"));
        }
    }

    // Typing a key must not show it either.
    while (field_label(form) != "API key") {
        form.handle(SettingsInput::Down);
    }
    form.handle(SettingsInput::Enter);
    CHECK(form.editing());
    type(form, "de");
    CHECK_EQ(form.display_value(form.focused()), std::string("*****"));
    form.handle(SettingsInput::Enter);
    CHECK_EQ(form.draft().moonraker.api_key, std::string("abcde"));
}

TEST_CASE("settings store: saves only changed settings and notifies") {
    const auto path =
        (std::filesystem::temp_directory_path() / "kdz_test_store.conf").string();
    std::remove(path.c_str());

    app::SettingsStore store(local_config(), path);
    std::vector<app::Setting> notified;
    store.add_change_listener(
        [&notified](const app::ApplicationConfig&, const std::vector<app::Setting>& changed) {
            notified = changed;
        });

    auto updated = store.config();
    updated.moonraker.transport = app::MoonrakerTransportKind::WebSocket;
    updated.moonraker.host = "printer.local";
    std::string error;
    CHECK(store.apply(updated, error));
    CHECK_EQ(notified.size(), static_cast<std::size_t>(2));
    CHECK_EQ(store.config().moonraker.host, std::string("printer.local"));

    app::ApplicationConfig loaded;
    CHECK(app::load_application_config(path, loaded, error));
    CHECK(loaded.moonraker.transport == app::MoonrakerTransportKind::WebSocket);
    CHECK_EQ(loaded.moonraker.host, std::string("printer.local"));

    notified.clear();
    CHECK(store.apply(updated, error));
    CHECK(notified.empty());
    std::remove(path.c_str());
}

TEST_CASE("settings store: a failed save is retried with the same values") {
    // A regular file where the config's directory should be makes saving fail.
    const auto blocker = std::filesystem::temp_directory_path() / "kdz_test_store_blocker";
    std::filesystem::remove_all(blocker);
    std::ofstream(blocker) << "not a directory";
    const auto path = (blocker / "user.conf").string();

    app::SettingsStore store(local_config(), path);
    int notifications = 0;
    store.add_change_listener(
        [&notifications](const app::ApplicationConfig&, const std::vector<app::Setting>&) {
            ++notifications;
        });

    auto updated = store.config();
    updated.moonraker.socket_path = "/run/moonraker.sock";
    std::string error;
    CHECK(!store.apply(updated, error));
    CHECK(!error.empty());
    CHECK(store.has_unsaved());
    CHECK_EQ(notifications, 1);

    // Same values again: still retried, but nothing changed so listeners stay quiet.
    CHECK(!store.apply(updated, error));
    CHECK(store.has_unsaved());
    CHECK_EQ(notifications, 1);

    std::filesystem::remove(blocker);
    CHECK(store.apply(updated, error));
    CHECK(!store.has_unsaved());
    CHECK_EQ(notifications, 1);

    app::ApplicationConfig loaded;
    CHECK(app::load_application_config(path, loaded, error));
    CHECK_EQ(loaded.moonraker.socket_path, std::string("/run/moonraker.sock"));
    std::filesystem::remove_all(blocker);
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "help_popup.h"
#include "logger.h"
#include "printer_commands.h"
#include "linux_input.h"
#include "printer_service.h"
#include "restart_confirm.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "settings_store.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "toast.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#include <unistd.h>

#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "klipper-deck-zero.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "klipper-deck-zero" / "klipper-deck-zero.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "klipper-deck-zero" / "klipper-deck-zero.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* store = static_cast<SettingsStore*>(lv_observer_get_user_data(observer));
    if (!store) {
        return;
    }
    auto config = store->config();
    config.dark_mode = lv_subject_get_int(subject) != 0;
    std::string error;
    if (!store->apply(config, error)) {
        LOG_WARN("failed to save config {}: {}", store->user_config_path(), error);
    }
}

bool has_moonraker_setting(const std::vector<Setting>& settings) {
    return std::any_of(settings.begin(), settings.end(), [](Setting setting) {
        return std::string(setting_descriptor(setting).section) == "moonraker";
    });
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model,
                       AssetManager& assets,
                       view::widgets::Toast& toast)
        : view_model_(view_model),
          toast_(&toast),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

    // The session the restart shortcuts act on; nullptr while it is being replaced.
    void set_session(moonraker::MoonrakerSession* session) {
        session_ = session;
        restart_confirm_.cancel();
    }

private:
    static bool restart_kind_for(uint32_t key, RestartKind& kind) {
        if (key == 'r' || key == 'R') {
            kind = RestartKind::Klipper;
            return true;
        }
        if (key == 'f' || key == 'F') {
            kind = RestartKind::Firmware;
            return true;
        }
        return false;
    }

    void request_restart(RestartKind kind) {
        if (!session_) {
            return;
        }

        const bool klipper = kind == RestartKind::Klipper;
        const bool printing =
            restart_would_abort_print(session_->session_state(), session_->printer().print_state);
        switch (restart_confirm_.press(kind, printing, lv_tick_get())) {
            case RestartConfirm::Result::Blocked:
                toast_->show("Can't restart while printing", view::widgets::ToastTone::Warning);
                return;
            case RestartConfirm::Result::Armed:
                toast_->show_highlighted("Press ",
                                         klipper ? "R" : "F",
                                         klipper ? " again to restart Klipper"
                                                 : " again to restart the MCU",
                                         view::widgets::ToastTone::Warning,
                                         RestartConfirm::kWindowMs);
                return;
            case RestartConfirm::Result::Confirmed:
                break;
        }

        LOG_INFO("{} requested", klipper ? "RESTART" : "FIRMWARE_RESTART");
        toast_->show(klipper ? "Restarting Klipper..." : "Restarting MCU firmware...",
                     view::widgets::ToastTone::Success);
        auto on_result = [this](const moonraker::RpcResponse& response) {
            if (!response.ok) {
                LOG_WARN("restart failed: {}", response.error.message);
                toast_->show("Restart failed: " + response.error.message,
                             view::widgets::ToastTone::Error,
                             4000);
            }
        };
        if (klipper) {
            session_->restart(std::move(on_result));
        }
        else {
            session_->firmware_restart(std::move(on_result));
        }
    }

    bool handle_key(uint32_t key, bool long_pressed) {
        RestartKind restart_kind{};
        const bool is_restart_key = restart_kind_for(key, restart_kind);
        if (!is_restart_key) {
            // Any other key abandons a pending confirmation.
            restart_confirm_.cancel();
        }

        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                }
                else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        if (platform::dispatch_screen_key(key, long_pressed)) {
            return true;
        }

        if ((key == 'd' || key == 'D') && !long_pressed) {
            view_model_.toggle_dark_mode();
            return true;
        }

        if (key == platform::kKeyPrevLayer || key == platform::kKeyNextLayer) {
            if (!long_pressed) {
                if (key == platform::kKeyPrevLayer) {
                    view_model_.previous_nav_layer();
                }
                else {
                    view_model_.next_nav_layer();
                }
            }
            return true;
        }

        // Settings sits in nav layer 2; jumping there switches the layer too.
        if ((key == 's' || key == 'S') && !long_pressed) {
            view_model_.show_page(model::AppPage::Settings);
            return true;
        }

        if (is_restart_key && view_model_.current_page() == model::AppPage::Status) {
            if (!long_pressed) {
                request_restart(restart_kind);
            }
            return true;
        }

        const bool is_escape = key == LV_KEY_ESC;
        // On Status (layer 1, slot 1) its own key 4 doubles as the exit key; elsewhere 4 picks
        // slot 1 of the shown layer through the nav bar.
        const bool is_home_exit_key =
            key == '4' &&
            ((!long_pressed && view_model_.current_page() == model::AppPage::Status) ||
             (long_pressed && exit_key_pressed_ == key));
        if (!is_escape && !is_home_exit_key) {
            return false;
        }

        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
            }
        }
        else {
            exit_key_pressed_ = key;
            exit_armed_ = view_model_.current_page() == model::AppPage::Status;
            if (exit_armed_) {
                toast_->show_persistent_highlighted("Hold ",
                                                    "ESC",
                                                    " to exit",
                                                    view::widgets::ToastTone::Warning);
            }
        }
        return true;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC &&
                   view_model_.current_page() != model::AppPage::Status) {
            view_model_.show_page(model::AppPage::Status);
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    view::widgets::Toast* toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    moonraker::MoonrakerSession* session_{nullptr};
    RestartConfirm restart_confirm_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
void sleep_ms(uint32_t ms) {
    usleep(ms * 1000U);
}

lv_display_t* init_device_display() {
    // The fbdev/DRM drivers only install a tick callback; without a delay callback
    // lv_delay_ms() busy-waits and the main loop keeps a CPU core at 100%.
    lv_delay_set_cb(sleep_ms);

#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("klipperdeck");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();

    // The system config is the base; the user config only overrides the keys it contains, so
    // persisting dark_mode there never hides the [moonraker] settings in the system config.
    std::vector<std::string> config_paths{APP_CONFIG_FILE};
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            config_paths.push_back(user_config_path);
        }
    }

    ApplicationConfig config;
    for (const auto& config_path : config_paths) {
        std::string config_error;
        if (load_application_config(config_path, config, config_error)) {
            LOG_INFO("loaded config: {}", config_path);
        } else {
            LOG_WARN("failed to load config {}: {}", config_path, config_error);
        }
    }
    view_model.set_dark_mode(config.dark_mode);
    LOG_INFO("dark_mode={}", config.dark_mode ? "yes" : "no");

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    SettingsStore settings(config, user_config_path);

    // Shared by the shortcuts and the page commands; created first so it outlives both.
    view::widgets::Toast toast(lv_layer_top(), view_model, assets);
    toast.build();
    PrinterCommands commands(toast, view_model);

    ScreenManager screen_manager(view_model, assets, settings, commands);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets, toast);

    // Recreated whenever the connection settings change, so new settings apply without a restart.
    // Declared after the shortcuts and commands so it is destroyed first: pending RPC handlers
    // point at them.
    std::unique_ptr<PrinterService> printer_service;
    const auto connect = [&view_model, &printer_service, &shortcuts, &commands](
                             const MoonrakerConfig& moonraker) {
        shortcuts.set_session(nullptr);
        commands.set_session(nullptr);
        commands.set_thumbnail_source(nullptr);
        printer_service.reset();
        printer_service = std::make_unique<PrinterService>(moonraker);
        shortcuts.set_session(&printer_service->session());
        commands.set_session(&printer_service->session());
        commands.set_thumbnail_source(&printer_service->thumbnails());
        printer_service->set_change_listener([&view_model, &commands](const moonraker::MoonrakerSession& session) {
            commands.on_session_changed();
            view_model.update_printer(session);
        });
        printer_service->start();
        view_model.update_printer(printer_service->session());
    };
    connect(settings.config().moonraker);
    settings.add_change_listener(
        [&connect](const ApplicationConfig& updated, const std::vector<Setting>& changed) {
            LOG_INFO("saved {} setting(s)", changed.size());
            if (has_moonraker_setting(changed)) {
                connect(updated.moonraker);
            }
        });

    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &settings);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

} // namespace app

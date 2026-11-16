/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "control_screen.h"
#include "extruder_screen.h"
#include "files_screen.h"
#include "move_screen.h"
#include "placeholder_screen.h"
#include "settings_screen.h"
#include "status_screen.h"
#include "temperature_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }

    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model,
                             AssetManager& assets,
                             SettingsStore& settings,
                             PrinterCommands& commands)
    : view_model_(view_model), assets_(assets), settings_(settings), commands_(commands) {}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }

    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer,
                                                         this,
                                                         nullptr);
}

void ScreenManager::show_screen(model::AppPage page) {
    if (page == model::AppPage::Status) {
        load_screen(std::make_unique<screen::StatusScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Temperature) {
        load_screen(std::make_unique<screen::TemperatureScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Extruder) {
        load_screen(std::make_unique<screen::ExtruderScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Files) {
        load_screen(std::make_unique<screen::FilesScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Control) {
        load_screen(std::make_unique<screen::ControlScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Move) {
        load_screen(std::make_unique<screen::MoveScreen>(view_model_, assets_, commands_));
    }
    else if (page == model::AppPage::Settings) {
        load_screen(std::make_unique<screen::SettingsScreen>(view_model_, assets_, settings_));
    }
    else {
        load_screen(std::make_unique<screen::PlaceholderScreen>(view_model_, assets_, page));
    }
    loaded_page_ = page;
    has_loaded_page_ = true;
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;

    if (page_switch_scheduled_) {
        return;
    }

    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;

    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }

    show_screen(requested_page_);
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

void ScreenManager::load_screen(std::unique_ptr<screen::BaseScreen> next_screen) {
    if (!next_screen || !next_screen->root()) {
        return;
    }

    lv_screen_load(next_screen->root());
    current_screen_ = std::move(next_screen);
}

} // namespace app

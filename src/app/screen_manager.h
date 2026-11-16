/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_screen.h"
#include "base_viewmodel.h"
#include "lvgl.h"

#include <memory>

namespace app {

class PrinterCommands;
class SettingsStore;

class ScreenManager {
public:
    ScreenManager(viewmodel::BaseViewModel& view_model,
                  AssetManager& assets,
                  SettingsStore& settings,
                  PrinterCommands& commands);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    void start();
    void show_screen(model::AppPage page);
    void request_page(model::AppPage page);
    void flush_requested_page();
    lv_obj_t* current_screen() const;

private:
    void load_screen(std::unique_ptr<screen::BaseScreen> next_screen);

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    SettingsStore& settings_;
    PrinterCommands& commands_;
    std::unique_ptr<screen::BaseScreen> current_screen_;
    lv_observer_t* page_observer_ = nullptr;
    model::AppPage requested_page_ = model::AppPage::Status;
    model::AppPage loaded_page_ = model::AppPage::Status;
    bool has_loaded_page_ = false;
    bool page_switch_scheduled_ = false;
};

} // namespace app

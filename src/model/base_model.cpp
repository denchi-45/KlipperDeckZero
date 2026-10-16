/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

namespace model {

const char* page_title(AppPage page) {
    switch (page) {
        case AppPage::Status:
            return "Status";
        case AppPage::Temperature:
            return "Temperature";
        case AppPage::Extruder:
            return "Extruder";
        case AppPage::Files:
            return "Files";
        case AppPage::Control:
            return "Print Control";
    }
    return "";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

} // namespace model

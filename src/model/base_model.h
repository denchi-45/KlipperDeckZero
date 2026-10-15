/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

namespace model {

// One page per navigation key (4..8). Status is the home page.
enum class AppPage {
    Status      = 0,
    Temperature = 1,
    Extruder    = 2,
    Files       = 3,
    Control     = 4,
};

constexpr std::size_t kAppPageCount = 5;

const char* page_title(AppPage page);

class BaseModel {
public:
    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

private:
    bool dark_mode_ = false;
    AppPage current_page_ = AppPage::Status;
};

} // namespace model

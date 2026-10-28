/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_page.h"
#include "nav_layers.h"

#include <cstddef>

namespace model {

class BaseModel {
public:
    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    std::size_t current_layer() const;
    const NavLayers& nav() const;
    NavLayers& nav();

private:
    bool dark_mode_ = false;
    NavLayers nav_;
};

} // namespace model

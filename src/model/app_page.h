/*
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

// The app's pages; nav_layers.h places them in the nav bar layers. Status is the home page.
enum class AppPage {
    Status      = 0,
    Temperature = 1,
    Extruder    = 2,
    Files       = 3,
    Control     = 4,
    Move        = 5,
    Settings    = 6, // also on the S key
    Camera      = 7,
};

const char* page_title(AppPage page);

} // namespace model

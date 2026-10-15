/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// Stand-in for pages that are not implemented yet.
class PlaceholderScreen : public BaseScreen {
public:
    PlaceholderScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;

    model::AppPage page_;
};

} // namespace screen

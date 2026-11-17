/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_page.h"

#include <cstddef>
#include <optional>

namespace model {

// The nav bar shows one layer of kNavSlotCount slots; digit keys 4..8 pick a slot of the shown
// layer by position, and Sym+H / Sym+J switch layers like keymap layers on a keyboard. The shown
// layer and the shown page are independent: switching layers only changes what the keys pick.
constexpr std::size_t kNavSlotCount = 5;
constexpr std::size_t kNavLayerCount = 2;

// The page of a slot, or nullopt for an empty slot or an index out of range.
std::optional<AppPage> nav_slot_page(std::size_t layer, std::size_t slot);

// Where a page sits in the layers; false if it is in none.
bool find_nav_slot(AppPage page, std::size_t& layer, std::size_t& slot);

// Shown layer and shown page. Independent of LVGL.
class NavLayers {
public:
    std::size_t layer() const;
    AppPage page() const;

    // Shows the page; the layer stays. False, with nothing changed, if it is in no layer.
    bool show_page(AppPage page);
    // Shows the page of a slot of the current layer. False, with nothing changed, on an empty slot.
    bool select_slot(std::size_t slot);
    // Switch layer (wrapping around); the page stays.
    void next_layer();
    void previous_layer();

private:
    std::size_t layer_{0};
    AppPage page_{AppPage::Status};
};

} // namespace model

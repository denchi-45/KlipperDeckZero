/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_page.h"

#include <array>
#include <cstddef>
#include <optional>

namespace model {

// The nav bar shows one layer of kNavSlotCount slots; digit keys 4..8 pick a slot of the shown
// layer by position, and Sym+H / Sym+J switch layers like keymap layers on a keyboard.
constexpr std::size_t kNavSlotCount = 5;
constexpr std::size_t kNavLayerCount = 2;

// The page of a slot, or nullopt for an empty slot or an index out of range.
std::optional<AppPage> nav_slot_page(std::size_t layer, std::size_t slot);

// Where a page sits in the layers; false if it is in none.
bool find_nav_slot(AppPage page, std::size_t& layer, std::size_t& slot);

// Current layer and page, and the page each layer last showed. Independent of LVGL.
class NavLayers {
public:
    NavLayers();

    std::size_t layer() const;
    AppPage page() const;
    // The page the layer shows when switched to: the last one visited there.
    AppPage last_page(std::size_t layer) const;

    // Shows the page and switches to its layer. False, with nothing changed, if it is in no layer.
    bool show_page(AppPage page);
    // Shows the page of a slot of the current layer. False, with nothing changed, on an empty slot.
    bool select_slot(std::size_t slot);
    // Switch layer (wrapping around) and show the page it last showed.
    void next_layer();
    void previous_layer();

private:
    std::size_t layer_{0};
    std::array<AppPage, kNavLayerCount> last_pages_;
};

} // namespace model

/*
 * SPDX-License-Identifier: MIT
 */

#include "nav_layers.h"

namespace model {
namespace {

using Slot = std::optional<AppPage>;

const std::array<std::array<Slot, kNavSlotCount>, kNavLayerCount> kLayers = {{
    {AppPage::Status, AppPage::Temperature, AppPage::Extruder, AppPage::Files, AppPage::Control},
    {AppPage::Move, AppPage::Settings, std::nullopt, std::nullopt, std::nullopt},
}};

} // namespace

std::optional<AppPage> nav_slot_page(std::size_t layer, std::size_t slot) {
    if (layer >= kNavLayerCount || slot >= kNavSlotCount) {
        return std::nullopt;
    }
    return kLayers[layer][slot];
}

bool find_nav_slot(AppPage page, std::size_t& layer, std::size_t& slot) {
    for (std::size_t l = 0; l < kNavLayerCount; ++l) {
        for (std::size_t s = 0; s < kNavSlotCount; ++s) {
            if (kLayers[l][s] == page) {
                layer = l;
                slot = s;
                return true;
            }
        }
    }
    return false;
}

NavLayers::NavLayers() {
    // Each layer starts on its first slot.
    for (std::size_t l = 0; l < kNavLayerCount; ++l) {
        last_pages_[l] = *kLayers[l][0];
    }
}

std::size_t NavLayers::layer() const {
    return layer_;
}

AppPage NavLayers::page() const {
    return last_pages_[layer_];
}

AppPage NavLayers::last_page(std::size_t layer) const {
    return layer < kNavLayerCount ? last_pages_[layer] : page();
}

bool NavLayers::show_page(AppPage page) {
    std::size_t layer = 0;
    std::size_t slot = 0;
    if (!find_nav_slot(page, layer, slot)) {
        return false;
    }
    layer_ = layer;
    last_pages_[layer] = page;
    return true;
}

bool NavLayers::select_slot(std::size_t slot) {
    const auto page = nav_slot_page(layer_, slot);
    if (!page) {
        return false;
    }
    last_pages_[layer_] = *page;
    return true;
}

void NavLayers::next_layer() {
    layer_ = (layer_ + 1) % kNavLayerCount;
}

void NavLayers::previous_layer() {
    layer_ = (layer_ + kNavLayerCount - 1) % kNavLayerCount;
}

} // namespace model

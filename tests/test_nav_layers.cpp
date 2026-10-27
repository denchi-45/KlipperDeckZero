/*
 * SPDX-License-Identifier: MIT
 */

#include "nav_layers.h"
#include "test_framework.h"

#include <ostream>

namespace model {

std::ostream& operator<<(std::ostream& out, AppPage page) {
    return out << "AppPage(" << static_cast<int>(page) << ")";
}

} // namespace model

namespace {

using model::AppPage;
using model::NavLayers;

} // namespace

TEST_CASE("nav layers: slots map to pages by layer and position") {
    CHECK_EQ(model::kNavSlotCount, std::size_t{5});
    CHECK_EQ(model::kNavLayerCount, std::size_t{2});

    CHECK_EQ(*model::nav_slot_page(0, 0), AppPage::Status);
    CHECK_EQ(*model::nav_slot_page(0, 1), AppPage::Temperature);
    CHECK_EQ(*model::nav_slot_page(0, 2), AppPage::Extruder);
    CHECK_EQ(*model::nav_slot_page(0, 3), AppPage::Files);
    CHECK_EQ(*model::nav_slot_page(0, 4), AppPage::Control);

    CHECK_EQ(*model::nav_slot_page(1, 0), AppPage::Move);
    CHECK_EQ(*model::nav_slot_page(1, 1), AppPage::Settings);
    CHECK(!model::nav_slot_page(1, 2));
    CHECK(!model::nav_slot_page(1, 3));
    CHECK(!model::nav_slot_page(1, 4));
}

TEST_CASE("nav layers: out-of-range slots and layers are empty") {
    CHECK(!model::nav_slot_page(0, 5));
    CHECK(!model::nav_slot_page(2, 0));
}

TEST_CASE("nav layers: every page has exactly one slot") {
    const AppPage pages[] = {AppPage::Status, AppPage::Temperature, AppPage::Extruder, AppPage::Files,
                             AppPage::Control, AppPage::Move, AppPage::Settings};
    for (const auto page : pages) {
        std::size_t layer = 99;
        std::size_t slot = 99;
        CHECK(model::find_nav_slot(page, layer, slot));
        CHECK_EQ(*model::nav_slot_page(layer, slot), page);
    }

    std::size_t layer = 0;
    std::size_t slot = 0;
    CHECK(model::find_nav_slot(AppPage::Settings, layer, slot));
    CHECK_EQ(layer, std::size_t{1});
    CHECK_EQ(slot, std::size_t{1});
}

TEST_CASE("nav layers: start on layer 1 showing Status") {
    NavLayers nav;
    CHECK_EQ(nav.layer(), std::size_t{0});
    CHECK_EQ(nav.page(), AppPage::Status);
    CHECK_EQ(nav.last_page(1), AppPage::Move);
}

TEST_CASE("nav layers: a slot shows its page in the current layer") {
    NavLayers nav;
    CHECK(nav.select_slot(3));
    CHECK_EQ(nav.page(), AppPage::Files);
    CHECK_EQ(nav.layer(), std::size_t{0});

    nav.next_layer();
    CHECK(nav.select_slot(1));
    CHECK_EQ(nav.page(), AppPage::Settings);
    CHECK(nav.select_slot(0));
    CHECK_EQ(nav.page(), AppPage::Move);
}

TEST_CASE("nav layers: empty slots change nothing") {
    NavLayers nav;
    nav.next_layer();
    CHECK(nav.select_slot(1));
    for (std::size_t slot = 2; slot < model::kNavSlotCount; ++slot) {
        CHECK(!nav.select_slot(slot));
        CHECK_EQ(nav.page(), AppPage::Settings);
        CHECK_EQ(nav.layer(), std::size_t{1});
    }
    CHECK(!nav.select_slot(model::kNavSlotCount));
    CHECK_EQ(nav.page(), AppPage::Settings);
}

TEST_CASE("nav layers: next and previous wrap around") {
    NavLayers nav;
    nav.next_layer();
    CHECK_EQ(nav.layer(), std::size_t{1});
    nav.next_layer();
    CHECK_EQ(nav.layer(), std::size_t{0});
    nav.previous_layer();
    CHECK_EQ(nav.layer(), std::size_t{1});
    nav.previous_layer();
    CHECK_EQ(nav.layer(), std::size_t{0});
}

TEST_CASE("nav layers: each layer remembers its last page") {
    NavLayers nav;
    CHECK(nav.select_slot(2)); // Extruder
    nav.next_layer();
    CHECK_EQ(nav.page(), AppPage::Move);
    CHECK(nav.select_slot(1)); // Settings
    nav.previous_layer();
    CHECK_EQ(nav.page(), AppPage::Extruder);
    nav.next_layer();
    CHECK_EQ(nav.page(), AppPage::Settings);
    CHECK_EQ(nav.last_page(0), AppPage::Extruder);
    CHECK_EQ(nav.last_page(1), AppPage::Settings);
}

TEST_CASE("nav layers: showing a page switches to its layer") {
    NavLayers nav;
    CHECK(nav.select_slot(1)); // Temperature
    // The S shortcut: Settings from layer 1 switches to layer 2 and makes it its last page.
    CHECK(nav.show_page(AppPage::Settings));
    CHECK_EQ(nav.layer(), std::size_t{1});
    CHECK_EQ(nav.page(), AppPage::Settings);
    CHECK_EQ(nav.last_page(1), AppPage::Settings);
    CHECK_EQ(nav.last_page(0), AppPage::Temperature);

    // ESC: back to Status on layer 1; layer 2 still remembers Settings.
    CHECK(nav.show_page(AppPage::Status));
    CHECK_EQ(nav.layer(), std::size_t{0});
    CHECK_EQ(nav.page(), AppPage::Status);
    nav.next_layer();
    CHECK_EQ(nav.page(), AppPage::Settings);
}

TEST_CASE("nav layers: showing a page outside every layer changes nothing") {
    NavLayers nav;
    CHECK(!nav.show_page(static_cast<AppPage>(42)));
    CHECK_EQ(nav.layer(), std::size_t{0});
    CHECK_EQ(nav.page(), AppPage::Status);
}

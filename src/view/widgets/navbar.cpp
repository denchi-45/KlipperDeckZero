/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace view::widgets {
namespace {

static_assert(platform::kNavKeyCount == model::kNavSlotCount, "one nav key per slot");

// Each slot sits right above its key: the screen x of the centers of keys 4..8, measured on the
// device image (assets/images/cp0-light.png) at screen scale. The key pitch is 56.25 px; the
// screen is 320 px wide.
constexpr std::array<int32_t, model::kNavSlotCount> kSlotCenterX = {47, 104, 160, 216, 272};
constexpr int32_t kSlotWidth = 32;
constexpr int32_t kSlotHeight = 24;
constexpr int32_t kLayerLabelInset = 8;

const char* page_icon(model::AppPage page) {
    switch (page) {
        case model::AppPage::Status:
            return ICON_GAUGE;
        case model::AppPage::Temperature:
            return ICON_THERMOMETER;
        case model::AppPage::Extruder:
            return ICON_DROP;
        case model::AppPage::Files:
            return ICON_FILES;
        case model::AppPage::Control:
            return ICON_PLAY_PAUSE;
        case model::AppPage::Move:
            return ICON_ARROWS_OUT_CARDINAL;
        case model::AppPage::Settings:
            return ICON_PLUGS;
    }
    return "";
}

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (layer_observer_) {
        lv_observer_remove(layer_observer_);
        layer_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();

    // "1/2" at the right end, right of the last slot.
    layer_label_ = lv_label_create(core_obj_);
    auto* layer_font = assets_.load_standard_font(11);
    lv_obj_set_style_text_font(layer_label_, layer_font ? layer_font : &lv_font_montserrat_12, 0);
    lv_obj_align(layer_label_, LV_ALIGN_RIGHT_MID, -kLayerLabelInset, 0);

    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    layer_observer_ = reactive::observe_obj(core_obj_, view_model_.current_layer_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::create_icon_buttons() {
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 22);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        kSlotWidth,
                                                        kSlotHeight,
                                                        "",
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        view::palette(false).text,
                                                        view::palette(true).text,
                                                        select_slot_cb,
                                                        &view_model_,
                                                        true);
        icon_buttons_[i]->build();
        lv_obj_align(icon_buttons_[i]->root(), LV_ALIGN_LEFT_MID, kSlotCenterX[i] - kSlotWidth / 2, 0);
        lv_obj_set_user_data(icon_buttons_[i]->root(), reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const auto layer = view_model_.current_layer();
    const auto current = view_model_.current_page();
    const auto light = view::palette(false);
    const auto dark = view::palette(true);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (!icon_buttons_[i]) {
            continue;
        }
        const auto page = model::nav_slot_page(layer, i);
        // An empty slot stays in place, blank and unclickable, so the slots keep their keys.
        icon_buttons_[i]->set_text(page ? page_icon(*page) : "");
        icon_buttons_[i]->set_enabled(page.has_value());
        if (page && *page == current) {
            icon_buttons_[i]->set_colors(light.primary, dark.primary);
        }
        else {
            icon_buttons_[i]->set_colors(light.text_disabled, dark.text_disabled);
        }
    }

    if (layer_label_) {
        char text[8];
        std::snprintf(text, sizeof(text), "%zu/%zu", layer + 1, model::kNavLayerCount);
        lv_label_set_text(layer_label_, text);
        lv_obj_set_style_text_color(layer_label_, view::palette(view_model_.is_dark_mode()).text, 0);
    }
}

void NavBar::select_slot_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!view_model || !button) {
        return;
    }
    view_model->select_nav_slot(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets

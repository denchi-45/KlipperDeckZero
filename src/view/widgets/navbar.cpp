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

#include <cstdint>

namespace view::widgets {
namespace {

// Same order as model::AppPage and the physical keys 4..8.
constexpr std::array<const char*, model::kNavPageCount> kPageIcons = {
    ICON_GAUGE,
    ICON_THERMOMETER,
    ICON_DROP,
    ICON_FILES,
    ICON_PLAY_PAUSE,
};

constexpr std::array<int32_t, model::kNavPageCount> kNavButtonXOffsets = {30, 17, 2, -15, -30};

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
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(core_obj_, 8, 0);
    lv_obj_set_style_pad_right(core_obj_, 8, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::create_icon_buttons() {
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 22);
    platform::set_nav_shortcut_mode(false);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        32,
                                                        24,
                                                        kPageIcons[i],
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        view::palette(false).text,
                                                        view::palette(true).text,
                                                        show_page_cb,
                                                        &view_model_,
                                                        true);
        icon_buttons_[i]->build();
        lv_obj_set_user_data(icon_buttons_[i]->root(), reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_set_style_translate_x(icon_buttons_[i]->root(), kNavButtonXOffsets[i], 0);
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const auto current = static_cast<size_t>(view_model_.current_page());
    const auto light = view::palette(false);
    const auto dark = view::palette(true);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (!icon_buttons_[i]) {
            continue;
        }
        if (i == current) {
            icon_buttons_[i]->set_colors(light.primary, dark.primary);
        }
        else {
            icon_buttons_[i]->set_colors(light.text_disabled, dark.text_disabled);
        }
    }
}

void NavBar::show_page_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!view_model || !button) {
        return;
    }
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
    if (index < model::kNavPageCount) {
        view_model->show_page(static_cast<model::AppPage>(index));
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets

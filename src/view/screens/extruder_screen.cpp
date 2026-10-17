/*
 * SPDX-License-Identifier: MIT
 */

#include "extruder_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "panel_keys.h"
#include "panel_style.h"
#include "printer_commands.h"
#include "style_bindings.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdlib>
#include <string>

namespace screen {
namespace {

constexpr int32_t kStatusRowHeight = 18;
constexpr int32_t kChoiceRowHeight = 22;
constexpr int32_t kRowLabelWidth = 48;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

std::vector<std::string> numbers(const std::vector<int>& values) {
    std::vector<std::string> texts;
    for (const int value : values) {
        texts.push_back(std::to_string(value));
    }
    return texts;
}

} // namespace

ExtruderScreen::ExtruderScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
}

ExtruderScreen::~ExtruderScreen() {
    platform::clear_screen_key_handler(key_handler, this);
}

void ExtruderScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);
    body_font_ = font_or(assets().load_standard_font(16), &lv_font_montserrat_16);
    icon_font_ = font_or(assets().load_font("Phosphor-Fill.ttf", 14), &lv_font_montserrat_14);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 8, 0);
    lv_obj_set_style_pad_top(column, 3, 0);
    lv_obj_set_style_pad_row(column, 2, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    // Nozzle temperature and whether the filament can move now.
    auto* status = lv_obj_create(column);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, LV_PCT(100), kStatusRowHeight);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(status, 6, 0);
    lv_obj_set_style_pad_column(status, 4, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon = lv_label_create(status);
    lv_label_set_text(icon, view::ICON_FIRE);
    lv_obj_set_style_text_font(icon, icon_font_, 0);
    view::widgets::bind_muted_text(icon, dark);

    auto* temperature = lv_label_create(status);
    lv_label_bind_text(temperature, vm.nozzle_temperature_subject(), nullptr);
    lv_obj_set_style_text_font(temperature, body_font_, 0);
    reactive::bind_theme(temperature, dark, reactive::ThemeRole::Text);

    auto* target = lv_label_create(status);
    lv_label_bind_text(target, vm.nozzle_target_subject(), nullptr);
    lv_obj_set_style_text_font(target, small_font_, 0);
    view::widgets::bind_muted_text(target, dark);

    readiness_ = lv_label_create(status);
    lv_label_set_long_mode(readiness_, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(readiness_, 1);
    lv_obj_set_style_text_font(readiness_, small_font_, 0);
    lv_obj_set_style_text_align(readiness_, LV_TEXT_ALIGN_RIGHT, 0);

    length_ = build_choice_row(column, "Length", numbers(app::ExtruderPanel::lengths()), "mm");
    speed_ = build_choice_row(column, "Speed", numbers(app::ExtruderPanel::speeds()), "mm/s");
    direction_ = build_choice_row(column, "Move", {"Retract", "Extrude"}, nullptr);

    hint_ = lv_label_create(column);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, small_font_, 0);

    // Redraw when the temperature, the print state, or the theme change.
    reactive::observe_obj(column, vm.nozzle_temperature_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, vm.printer_badge_text_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, dark, refresh_observer_cb, this);
    refresh();
}

ExtruderScreen::ChoiceRow ExtruderScreen::build_choice_row(lv_obj_t* parent,
                                                           const char* title,
                                                           const std::vector<std::string>& choices,
                                                           const char* unit) {
    ChoiceRow widgets;
    widgets.row = lv_obj_create(parent);
    lv_obj_remove_style_all(widgets.row);
    lv_obj_set_size(widgets.row, LV_PCT(100), kChoiceRowHeight);
    lv_obj_set_flex_flow(widgets.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widgets.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(widgets.row, 6, 0);
    lv_obj_set_style_pad_column(widgets.row, 4, 0);
    lv_obj_set_style_radius(widgets.row, 4, 0);
    lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

    widgets.label = lv_label_create(widgets.row);
    lv_label_set_text(widgets.label, title);
    lv_obj_set_style_text_font(widgets.label, small_font_, 0);
    lv_obj_set_width(widgets.label, kRowLabelWidth);

    for (const auto& choice : choices) {
        auto* chip = lv_label_create(widgets.row);
        lv_label_set_text(chip, choice.c_str());
        view::widgets::init_chip(chip, small_font_);
        widgets.chips.push_back(chip);
    }

    if (unit) {
        auto* unit_label = lv_label_create(widgets.row);
        lv_label_set_text(unit_label, unit);
        lv_obj_set_style_text_font(unit_label, small_font_, 0);
        view::widgets::bind_muted_text(unit_label, view_model().dark_mode_subject());
    }
    return widgets;
}

bool ExtruderScreen::handle_key(uint32_t key, bool long_pressed) {
    app::PanelInput input{};
    if (long_pressed || !to_panel_input(key, input)) {
        return false;
    }

    const auto result = panel_.handle(input);
    if (result == app::ExtruderPanel::Result::Ignored) {
        return false;
    }
    if (result == app::ExtruderPanel::Result::Apply) {
        commands_.move_extruder(panel_.request());
    }
    refresh();
    return true;
}

void ExtruderScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* printer = commands_.printer();

    const char* readiness = "Not connected";
    lv_color_t readiness_color = colors.text_disabled;
    if (printer) {
        if (const char* reason = app::extrude_block_reason(*printer)) {
            readiness = reason;
            readiness_color = colors.warning;
        }
        else {
            readiness = "Ready to extrude";
            readiness_color = colors.success;
        }
    }
    lv_label_set_text(readiness_, readiness);
    lv_obj_set_style_text_color(readiness_, readiness_color, 0);

    const auto focused = panel_.focused();
    const auto update_row = [&](ChoiceRow& widgets, app::ExtruderRow row, std::size_t selected) {
        const bool row_focused = focused == row;
        view::widgets::style_panel_row(widgets.row, colors, row_focused);
        lv_obj_set_style_text_color(widgets.label, colors.text, 0);
        for (std::size_t i = 0; i < widgets.chips.size(); ++i) {
            view::widgets::style_chip(widgets.chips[i], colors, i == selected, row_focused);
        }
    };
    update_row(length_, app::ExtruderRow::Length, panel_.length_index());
    update_row(speed_, app::ExtruderRow::Speed, panel_.speed_index());
    update_row(direction_,
               app::ExtruderRow::Direction,
               panel_.direction() == app::ExtruderDirection::Extrude ? 1 : 0);

    const auto request = panel_.request();
    const std::string hint = std::string("Enter: ") + (request.length_mm < 0 ? "retract " : "extrude ") +
                             std::to_string(std::abs(request.length_mm)) + " mm at " +
                             std::to_string(request.speed_mm_s) + " mm/s";
    lv_label_set_text(hint_, hint.c_str());
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

bool ExtruderScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<ExtruderScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void ExtruderScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ExtruderScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

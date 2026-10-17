/*
 * SPDX-License-Identifier: MIT
 */

#include "temperature_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "printer_commands.h"
#include "style_bindings.h"
#include "theme.h"
#include "ui_const.h"

#include <cmath>
#include <string>

namespace screen {
namespace {

constexpr int32_t kHeaterRowHeight = 30;
constexpr int32_t kPresetRowHeight = 22;
constexpr int32_t kTargetX = 196;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

bool to_panel_input(uint32_t key, app::PanelInput& input) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_PREV:
            input = app::PanelInput::Up;
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_NEXT:
            input = app::PanelInput::Down;
            return true;
        case LV_KEY_LEFT:
            input = app::PanelInput::Left;
            return true;
        case LV_KEY_RIGHT:
            input = app::PanelInput::Right;
            return true;
        case LV_KEY_ENTER:
            input = app::PanelInput::Enter;
            return true;
        case LV_KEY_ESC:
            input = app::PanelInput::Escape;
            return true;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            input = app::PanelInput::Backspace;
            return true;
        default:
            if (key >= '0' && key <= '9') {
                input = app::PanelInput::Digit;
                return true;
            }
            return false;
    }
}

int rounded_target(double celsius) {
    return celsius > 0.0 ? static_cast<int>(std::lround(celsius)) : 0;
}

std::string target_text(const model::PrinterState* printer, double target) {
    if (!printer) {
        return "--";
    }
    const int value = rounded_target(target);
    return value > 0 ? std::to_string(value) : std::string("off");
}

std::string preset_text(const app::TemperaturePreset& preset, bool has_bed) {
    if (preset.nozzle <= 0 && preset.bed <= 0) {
        return preset.label;
    }
    std::string text = std::string(preset.label) + " " + std::to_string(preset.nozzle);
    if (has_bed) {
        text += "/" + std::to_string(preset.bed);
    }
    return text;
}

} // namespace

TemperatureScreen::TemperatureScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
}

TemperatureScreen::~TemperatureScreen() {
    platform::clear_screen_key_handler(key_handler, this);
}

void TemperatureScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);
    body_font_ = font_or(assets().load_standard_font(16), &lv_font_montserrat_16);
    large_font_ = font_or(assets().load_standard_font(20), &lv_font_montserrat_20);
    icon_font_ = font_or(assets().load_font("Phosphor-Fill.ttf", 14), &lv_font_montserrat_14);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 8, 0);
    lv_obj_set_style_pad_top(column, 4, 0);
    lv_obj_set_style_pad_row(column, 3, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    nozzle_ = build_heater_row(column, "Nozzle", vm.nozzle_temperature_subject(), vm.nozzle_power_subject());
    bed_ = build_heater_row(column, "Bed", vm.bed_temperature_subject(), vm.bed_power_subject());

    presets_row_ = lv_obj_create(column);
    lv_obj_remove_style_all(presets_row_);
    lv_obj_set_size(presets_row_, LV_PCT(100), kPresetRowHeight);
    lv_obj_set_flex_flow(presets_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(presets_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(presets_row_, 6, 0);
    lv_obj_set_style_pad_column(presets_row_, 4, 0);
    lv_obj_set_style_radius(presets_row_, 4, 0);
    lv_obj_clear_flag(presets_row_, LV_OBJ_FLAG_SCROLLABLE);

    presets_label_ = lv_label_create(presets_row_);
    lv_label_set_text(presets_label_, "Preset");
    lv_obj_set_style_text_font(presets_label_, small_font_, 0);
    lv_obj_set_width(presets_label_, 44);

    for (std::size_t i = 0; i < app::temperature_presets().size(); ++i) {
        auto* chip = lv_label_create(presets_row_);
        lv_obj_set_style_text_font(chip, small_font_, 0);
        lv_obj_set_style_radius(chip, 3, 0);
        lv_obj_set_style_pad_hor(chip, 5, 0);
        lv_obj_set_style_pad_ver(chip, 1, 0);
        preset_chips_.push_back(chip);
    }

    hint_ = lv_label_create(column);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, small_font_, 0);

    // Redraw when the printer's targets, the bed, or the theme change.
    reactive::observe_obj(column, vm.nozzle_target_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, vm.bed_target_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, vm.bed_available_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, vm.printer_badge_text_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, dark, refresh_observer_cb, this);
    refresh();
}

TemperatureScreen::HeaterWidgets TemperatureScreen::build_heater_row(lv_obj_t* parent,
                                                                     const char* title,
                                                                     lv_subject_t* temperature,
                                                                     lv_subject_t* power) {
    auto* dark = view_model().dark_mode_subject();
    HeaterWidgets widgets;

    widgets.row = lv_obj_create(parent);
    lv_obj_remove_style_all(widgets.row);
    lv_obj_set_size(widgets.row, LV_PCT(100), kHeaterRowHeight);
    lv_obj_set_style_radius(widgets.row, 4, 0);
    lv_obj_set_style_pad_hor(widgets.row, 6, 0);
    lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon = lv_label_create(widgets.row);
    lv_label_set_text(icon, view::ICON_FIRE);
    lv_obj_set_style_text_font(icon, icon_font_, 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    view::widgets::bind_muted_text(icon, dark);

    widgets.name = lv_label_create(widgets.row);
    lv_label_set_text(widgets.name, title);
    lv_obj_set_style_text_font(widgets.name, small_font_, 0);
    lv_obj_align(widgets.name, LV_ALIGN_LEFT_MID, 18, 0);

    auto* value = lv_label_create(widgets.row);
    lv_label_bind_text(value, temperature, nullptr);
    lv_obj_set_style_text_font(value, large_font_, 0);
    lv_obj_align(value, LV_ALIGN_LEFT_MID, 70, 0);
    reactive::bind_theme(value, dark, reactive::ThemeRole::Text);

    auto* arrow = lv_label_create(widgets.row);
    lv_label_set_text(arrow, "Target");
    lv_obj_set_style_text_font(arrow, small_font_, 0);
    lv_obj_align(arrow, LV_ALIGN_LEFT_MID, kTargetX - 46, 0);
    view::widgets::bind_muted_text(arrow, dark);

    widgets.target = lv_label_create(widgets.row);
    lv_obj_set_style_text_font(widgets.target, body_font_, 0);
    lv_obj_set_style_radius(widgets.target, 3, 0);
    lv_obj_set_style_pad_hor(widgets.target, 4, 0);
    lv_obj_set_width(widgets.target, 50);
    lv_obj_align(widgets.target, LV_ALIGN_LEFT_MID, kTargetX, 0);

    auto* power_bar = lv_bar_create(widgets.row);
    lv_obj_set_size(power_bar, 36, 4);
    lv_bar_set_range(power_bar, 0, 100);
    lv_obj_align(power_bar, LV_ALIGN_RIGHT_MID, 0, 0);
    reactive::bind_bar_value(power_bar, power);
    view::widgets::bind_bar(power_bar, dark);

    return widgets;
}

bool TemperatureScreen::handle_key(uint32_t key, bool long_pressed) {
    if (long_pressed) {
        // Holding a key while typing must not trigger the long-press shortcuts (e.g. exit).
        return panel_.editing();
    }

    app::PanelInput input{};
    if (!to_panel_input(key, input)) {
        // While typing a target, letters must not reach the shortcuts (e.g. D, S).
        return panel_.editing();
    }

    const auto result = panel_.handle(input, static_cast<char>(key), current_targets());
    if (result == app::TemperaturePanel::Result::Ignored) {
        return false;
    }
    if (result == app::TemperaturePanel::Result::Apply) {
        commands_.set_temperatures(panel_.request());
    }
    refresh();
    return true;
}

app::HeaterTargets TemperatureScreen::current_targets() const {
    const auto* printer = commands_.printer();
    if (!printer) {
        return {};
    }
    return {rounded_target(printer->extruder.target), rounded_target(printer->heater_bed.target)};
}

void TemperatureScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* printer = commands_.printer();
    const bool has_bed = !printer || printer->has_heater_bed;
    panel_.set_has_bed(has_bed);

    const auto focused = panel_.focused();
    const bool editing = panel_.editing();

    const auto update_heater = [&](HeaterWidgets& widgets, app::TemperatureRow row, double target) {
        const bool is_focused = focused == row;
        const bool is_editing = is_focused && editing;
        style_row(widgets.row, is_focused, is_editing);
        lv_obj_set_style_text_color(widgets.name, colors.text, 0);

        const auto text = is_editing ? panel_.draft_text() + "_" : target_text(printer, target);
        lv_label_set_text(widgets.target, text.c_str());
        lv_obj_set_style_text_color(widgets.target, is_editing ? colors.primary : colors.text, 0);
        lv_obj_set_style_bg_color(widgets.target, colors.primary, 0);
        lv_obj_set_style_bg_opa(widgets.target, is_editing ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    };
    update_heater(nozzle_, app::TemperatureRow::Nozzle, printer ? printer->extruder.target : 0.0);
    update_heater(bed_, app::TemperatureRow::Bed, printer ? printer->heater_bed.target : 0.0);
    if (has_bed) {
        lv_obj_remove_flag(bed_.row, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(bed_.row, LV_OBJ_FLAG_HIDDEN);
    }

    const bool presets_focused = focused == app::TemperatureRow::Presets;
    style_row(presets_row_, presets_focused, false);
    lv_obj_set_style_text_color(presets_label_, colors.text, 0);
    const auto& presets = app::temperature_presets();
    for (std::size_t i = 0; i < preset_chips_.size(); ++i) {
        auto* chip = preset_chips_[i];
        const bool selected = i == panel_.preset();
        lv_label_set_text(chip, preset_text(presets[i], has_bed).c_str());
        lv_obj_set_style_bg_color(chip, colors.primary, 0);
        lv_obj_set_style_bg_opa(chip, selected && presets_focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(chip, selected ? colors.primary : colors.border, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_text_color(chip,
                                    selected && presets_focused ? colors.surface : colors.text,
                                    0);
    }

    const char* hint = nullptr;
    if (!panel_.error().empty()) {
        hint = panel_.error().c_str();
    }
    else if (editing) {
        hint = "0-9: type   Left/Right: -/+5   Enter: set   Esc: cancel";
    }
    else if (presets_focused) {
        hint = "Left/Right: choose   Enter: apply   Up/Down: select";
    }
    else {
        hint = "Left/Right: -/+5   Enter: type   Up/Down: select";
    }
    lv_label_set_text(hint_, hint);
    lv_obj_set_style_text_color(hint_, panel_.error().empty() ? colors.text_disabled : colors.error, 0);
}

void TemperatureScreen::style_row(lv_obj_t* row, bool focused, bool editing) {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, focused ? lv_color_mix(colors.primary, colors.button, LV_OPA_20) : colors.button, 0);
    lv_obj_set_style_border_color(row, focused ? colors.primary : colors.border, 0);
    lv_obj_set_style_border_width(row, editing ? 2 : 1, 0);
}

bool TemperatureScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<TemperatureScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void TemperatureScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<TemperatureScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

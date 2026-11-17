/*
 * SPDX-License-Identifier: MIT
 */

#include "move_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "gcode.h"
#include "linux_input.h"
#include "panel_keys.h"
#include "panel_style.h"
#include "printer_commands.h"
#include "style_bindings.h"
#include "theme.h"

#include <string>

namespace screen {
namespace {

// The content area is 110px tall: the header, three lines of cells and the hint.
constexpr int32_t kHeaderHeight = 16;
constexpr int32_t kLineHeight = 22;
constexpr int32_t kCellLabelGap = 4;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

std::vector<std::string> distances(const std::vector<double>& values) {
    std::vector<std::string> texts;
    for (const double value : values) {
        texts.push_back(app::gcode::format_distance(value));
    }
    return texts;
}

} // namespace

MoveScreen::MoveScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
}

MoveScreen::~MoveScreen() {
    platform::clear_screen_key_handler(key_handler, this);
}

void MoveScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);
    body_font_ = font_or(assets().load_standard_font(14), &lv_font_montserrat_14);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 8, 0);
    lv_obj_set_style_pad_top(column, 3, 0);
    lv_obj_set_style_pad_row(column, 2, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    // Toolhead position and which axes are homed.
    auto* header = lv_obj_create(column);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(header, 6, 0);
    lv_obj_set_style_pad_column(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* position = lv_label_create(header);
    lv_label_bind_text(position, vm.toolhead_position_subject(), nullptr);
    lv_obj_set_style_text_font(position, body_font_, 0);
    reactive::bind_theme(position, dark, reactive::ThemeRole::Text);

    homed_ = lv_label_create(header);
    lv_label_set_long_mode(homed_, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(homed_, 1);
    lv_obj_set_style_text_font(homed_, small_font_, 0);
    lv_obj_set_style_text_align(homed_, LV_TEXT_ALIGN_RIGHT, 0);

    // Axis and step side by side, then jog - / jog +, then home axis / home all / motors off.
    // Up/Down walk through the cells in this order (app::MoveRow).
    auto* choices = build_line(column);
    axis_ = build_choice_cell(choices, "Axis", {"X", "Y", "Z"});
    step_ = build_choice_cell(choices, "Step", distances(app::MovePanel::steps()));

    auto* jogs = build_line(column);
    auto* homes = build_line(column);
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        actions_[i] = build_action_cell(i < 2 ? jogs : homes);
    }

    hint_ = lv_label_create(column);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, small_font_, 0);

    // Redraw when the homed axes, the print state, or the theme change.
    reactive::observe_obj(column, vm.homed_axes_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, vm.printer_badge_text_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, dark, refresh_observer_cb, this);
    refresh();
}

lv_obj_t* MoveScreen::build_line(lv_obj_t* parent) {
    auto* line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, LV_PCT(100), kLineHeight);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 4, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

MoveScreen::ChoiceCell MoveScreen::build_choice_cell(lv_obj_t* parent,
                                                     const char* title,
                                                     const std::vector<std::string>& choices) {
    ChoiceCell widgets;
    widgets.cell = lv_obj_create(parent);
    lv_obj_remove_style_all(widgets.cell);
    lv_obj_set_height(widgets.cell, LV_PCT(100));
    lv_obj_set_flex_grow(widgets.cell, 1);
    lv_obj_set_flex_flow(widgets.cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widgets.cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(widgets.cell, 6, 0);
    lv_obj_set_style_pad_column(widgets.cell, kCellLabelGap, 0);
    lv_obj_set_style_radius(widgets.cell, 4, 0);
    lv_obj_clear_flag(widgets.cell, LV_OBJ_FLAG_SCROLLABLE);

    widgets.label = lv_label_create(widgets.cell);
    lv_label_set_text(widgets.label, title);
    lv_obj_set_style_text_font(widgets.label, small_font_, 0);

    for (const auto& choice : choices) {
        auto* chip = lv_label_create(widgets.cell);
        lv_label_set_text(chip, choice.c_str());
        view::widgets::init_chip(chip, small_font_);
        widgets.chips.push_back(chip);
    }
    return widgets;
}

MoveScreen::ActionCell MoveScreen::build_action_cell(lv_obj_t* parent) {
    ActionCell widgets;
    widgets.cell = lv_obj_create(parent);
    lv_obj_remove_style_all(widgets.cell);
    lv_obj_set_height(widgets.cell, LV_PCT(100));
    lv_obj_set_flex_grow(widgets.cell, 1);
    lv_obj_set_style_radius(widgets.cell, 4, 0);
    lv_obj_clear_flag(widgets.cell, LV_OBJ_FLAG_SCROLLABLE);

    widgets.label = lv_label_create(widgets.cell);
    lv_obj_set_style_text_font(widgets.label, small_font_, 0);
    lv_obj_center(widgets.label);
    return widgets;
}

bool MoveScreen::handle_key(uint32_t key, bool long_pressed) {
    app::PanelInput input{};
    if (long_pressed || !to_panel_input(key, input)) {
        return false;
    }

    const auto result = panel_.handle(input, commands_.printer());
    switch (result) {
        case app::MovePanel::Result::Ignored:
            return false;
        case app::MovePanel::Result::Jog:
            commands_.jog(panel_.request());
            break;
        case app::MovePanel::Result::HomeAxis:
            commands_.home_axis(panel_.axis());
            break;
        case app::MovePanel::Result::HomeAll:
            commands_.home_all();
            break;
        case app::MovePanel::Result::MotorsOff:
            commands_.motors_off();
            break;
        case app::MovePanel::Result::Blocked:
            commands_.show_warning(app::move_block_reason(panel_.block()));
            break;
        case app::MovePanel::Result::Handled:
            break;
    }
    refresh();
    return true;
}

void MoveScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* printer = commands_.printer();

    // Which axes are homed, or why nothing can move.
    std::string homed = "Not connected";
    lv_color_t homed_color = colors.text_disabled;
    if (printer) {
        const std::string axes = lv_subject_get_string(view_model().homed_axes_subject());
        if (printer->print_state == model::PrintState::Printing) {
            homed = "Printing";
            homed_color = colors.warning;
        }
        else if (axes.empty()) {
            homed = "Not homed";
            homed_color = colors.warning;
        }
        else {
            homed = "Homed " + axes;
            const bool all = app::axis_homed(axes, app::MoveAxis::X) && app::axis_homed(axes, app::MoveAxis::Y) &&
                             app::axis_homed(axes, app::MoveAxis::Z);
            homed_color = all ? colors.success : colors.warning;
        }
    }
    lv_label_set_text(homed_, homed.c_str());
    lv_obj_set_style_text_color(homed_, homed_color, 0);

    const auto focused = panel_.focused();
    const auto update_choice = [&](ChoiceCell& widgets, app::MoveRow row, std::size_t selected) {
        const bool row_focused = focused == row;
        view::widgets::style_panel_row(widgets.cell, colors, row_focused);
        lv_obj_set_style_text_color(widgets.label, colors.text, 0);
        for (std::size_t i = 0; i < widgets.chips.size(); ++i) {
            view::widgets::style_chip(widgets.chips[i], colors, i == selected, row_focused);
        }
    };
    update_choice(axis_, app::MoveRow::Axis, static_cast<std::size_t>(panel_.axis()));
    update_choice(step_, app::MoveRow::Step, panel_.step_index());

    const std::string axis(1, app::axis_letter(panel_.axis()));
    const std::string step = app::gcode::format_distance(panel_.step());
    const std::string labels[kActionCount] = {
        axis + " -" + step,
        axis + " +" + step,
        "Home " + axis,
        "Home all",
        "Motors off",
    };
    const app::MoveAction kinds[kActionCount] = {
        app::MoveAction::Jog,
        app::MoveAction::Jog,
        app::MoveAction::Home,
        app::MoveAction::Home,
        app::MoveAction::MotorsOff,
    };
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        const auto row = static_cast<app::MoveRow>(static_cast<std::size_t>(app::MoveRow::JogMinus) + i);
        const bool blocked =
            printer && app::move_block(*printer, kinds[i], panel_.axis()) != app::MoveBlock::None;
        view::widgets::style_panel_row(actions_[i].cell, colors, focused == row);
        lv_label_set_text(actions_[i].label, labels[i].c_str());
        lv_obj_set_style_text_color(actions_[i].label, blocked ? colors.text_disabled : colors.text, 0);
    }

    // The focused action, or why it is refused.
    std::string hint;
    bool warning = false;
    if (const char* reason = printer ? app::move_block_reason(panel_.focused_block(*printer)) : nullptr) {
        hint = reason;
        warning = true;
    }
    else {
        switch (focused) {
            case app::MoveRow::Axis:
                hint = "Left/Right: axis to move";
                break;
            case app::MoveRow::Step:
                hint = "Left/Right: step in mm";
                break;
            case app::MoveRow::JogMinus:
            case app::MoveRow::JogPlus: {
                const auto request = panel_.request();
                hint = "Enter: move " + axis + " " + (request.distance_mm < 0 ? "" : "+") +
                       app::gcode::format_distance(request.distance_mm) + " mm";
                break;
            }
            case app::MoveRow::HomeAxis:
                hint = "Enter: home " + axis + " (G28 " + axis + ")";
                break;
            case app::MoveRow::HomeAll:
                hint = "Enter: home all axes (G28)";
                break;
            case app::MoveRow::MotorsOff:
                hint = "Enter: turn the stepper motors off (M84)";
                break;
        }
    }
    lv_label_set_text(hint_, hint.c_str());
    lv_obj_set_style_text_color(hint_, warning ? colors.warning : colors.text_disabled, 0);
}

bool MoveScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<MoveScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void MoveScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<MoveScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

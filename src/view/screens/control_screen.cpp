/*
 * SPDX-License-Identifier: MIT
 */

#include "control_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "panel_keys.h"
#include "printer_commands.h"
#include "style_bindings.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

constexpr int32_t kJobRowHeight = 18;
constexpr int32_t kStatRowHeight = 16;
constexpr int32_t kButtonRowHeight = 30;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

lv_obj_t* create_row(lv_obj_t* parent, int32_t height, lv_flex_align_t main_align) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

} // namespace

ControlScreen::ControlScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
}

ControlScreen::~ControlScreen() {
    platform::clear_screen_key_handler(key_handler, this);
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
    }
}

void ControlScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);
    body_font_ = font_or(assets().load_standard_font(14), &lv_font_montserrat_14);
    icon_font_ = font_or(assets().load_font("Phosphor-Fill.ttf", 14), &lv_font_montserrat_14);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 8, 0);
    lv_obj_set_style_pad_top(column, 3, 0);
    lv_obj_set_style_pad_row(column, 4, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    // The job: state badge, file name, progress.
    auto* job_row = create_row(column, kJobRowHeight, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(job_row, 6, 0);

    auto* badge = lv_label_create(job_row);
    lv_label_bind_text(badge, vm.printer_badge_text_subject(), nullptr);
    lv_obj_set_style_text_font(badge, small_font_, 0);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_set_style_pad_hor(badge, 5, 0);
    lv_obj_set_style_pad_ver(badge, 1, 0);
    view::widgets::bind_tone(badge, vm.printer_badge_tone_subject(), dark, view::widgets::ToneStyle::Badge);

    auto* job = lv_label_create(job_row);
    lv_label_bind_text(job, vm.printer_job_subject(), nullptr);
    lv_label_set_long_mode(job, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(job, 1);
    lv_obj_set_height(job, lv_font_get_line_height(body_font_));
    lv_obj_set_style_text_font(job, body_font_, 0);
    reactive::bind_theme(job, dark, reactive::ThemeRole::Text);

    auto* percent = lv_label_create(job_row);
    lv_label_bind_text(percent, vm.printer_progress_text_subject(), nullptr);
    lv_obj_set_style_text_font(percent, body_font_, 0);
    reactive::bind_theme(percent, dark, reactive::ThemeRole::Text);

    auto* bar = lv_bar_create(column);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    reactive::bind_bar_value(bar, vm.printer_progress_subject());
    view::widgets::bind_bar(bar, dark, vm.printer_badge_tone_subject());

    auto* stats = create_row(column, kStatRowHeight, LV_FLEX_ALIGN_SPACE_BETWEEN);
    build_stat(stats, view::ICON_CLOCK, vm.printer_elapsed_subject());
    build_stat(stats, view::ICON_HOURGLASS, vm.printer_remaining_subject());
    build_stat(stats, view::ICON_STACK, vm.printer_layers_subject());

    // Pause / resume, cancel, emergency stop.
    auto* button_row = create_row(column, kButtonRowHeight, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_column(button_row, 6, 0);
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        auto* button = lv_obj_create(button_row);
        lv_obj_remove_style_all(button);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

        auto* label = lv_label_create(button);
        lv_obj_set_style_text_font(label, body_font_, 0);
        lv_obj_center(label);
        buttons_[i] = button;
        button_labels_[i] = label;
    }

    hint_ = lv_label_create(column);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, small_font_, 0);

    // Redraw when the print state or the theme change.
    reactive::observe_obj(column, vm.printer_badge_text_subject(), refresh_observer_cb, this);
    reactive::observe_obj(column, dark, refresh_observer_cb, this);
    refresh();
}

void ControlScreen::build_stat(lv_obj_t* parent, const char* icon_text, lv_subject_t* subject) {
    auto* dark = view_model().dark_mode_subject();
    auto* item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 3, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon = lv_label_create(item);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_style_text_font(icon, icon_font_, 0);
    view::widgets::bind_muted_text(icon, dark);

    auto* value = lv_label_create(item);
    lv_label_bind_text(value, subject, nullptr);
    lv_obj_set_style_text_font(value, body_font_, 0);
    reactive::bind_theme(value, dark, reactive::ThemeRole::Text);
}

bool ControlScreen::handle_key(uint32_t key, bool long_pressed) {
    app::PanelInput input{};
    if (!to_panel_input(key, input)) {
        return false;
    }

    const auto result = panel_.handle(input, long_pressed, print_state(), lv_tick_get());
    if (result == app::ControlPanel::Result::Ignored) {
        return false;
    }

    message_.clear();
    switch (result) {
        case app::ControlPanel::Result::Pause:
            commands_.pause_print();
            break;
        case app::ControlPanel::Result::Resume:
            commands_.resume_print();
            break;
        case app::ControlPanel::Result::Cancel:
            commands_.cancel_print();
            break;
        case app::ControlPanel::Result::EmergencyStop:
            commands_.emergency_stop();
            break;
        case app::ControlPanel::Result::ArmCancel:
            start_confirm_timer();
            break;
        case app::ControlPanel::Result::NoPrint:
            message_ = "No print is running";
            break;
        case app::ControlPanel::Result::HoldToStop:
            message_ = "Keep holding Enter to stop the printer";
            break;
        case app::ControlPanel::Result::Ignored:
        case app::ControlPanel::Result::Handled:
            break;
    }
    refresh();
    return true;
}

model::PrintState ControlScreen::print_state() const {
    const auto* printer = commands_.printer();
    return printer ? printer->print_state : model::PrintState::Unknown;
}

void ControlScreen::start_confirm_timer() {
    if (confirm_timer_) {
        lv_timer_reset(confirm_timer_);
        return;
    }
    confirm_timer_ = lv_timer_create(confirm_timer_cb, app::ControlPanel::kConfirmWindowMs + 50, this);
    lv_timer_set_repeat_count(confirm_timer_, 1);
}

void ControlScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto state = print_state();
    const bool active = app::has_active_print(state);
    const auto focused = panel_.focused();
    const bool armed = panel_.cancel_armed(lv_tick_get());

    const char* labels[] = {state == model::PrintState::Paused ? "Resume" : "Pause", "Cancel", "E-Stop"};
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const auto button = static_cast<app::ControlButton>(i);
        const bool is_focused = button == focused;
        const bool is_stop = button == app::ControlButton::EmergencyStop;
        const bool enabled = is_stop || active;
        const auto accent = is_stop || (button == app::ControlButton::Cancel && armed) ? colors.error : colors.primary;

        lv_label_set_text(button_labels_[i], labels[i]);
        lv_obj_set_style_bg_color(buttons_[i], is_focused && enabled ? accent : colors.button, 0);
        lv_obj_set_style_border_color(buttons_[i], is_focused ? accent : colors.border, 0);
        lv_obj_set_style_text_color(button_labels_[i],
                                    !enabled ? colors.text_disabled
                                             : (is_focused ? colors.surface : (is_stop ? colors.error : colors.text)),
                                    0);
    }

    std::string hint = message_;
    bool warning = !message_.empty();
    if (hint.empty()) {
        if (armed && focused == app::ControlButton::Cancel) {
            hint = "Press Enter again to cancel the print";
            warning = true;
        }
        else {
            switch (focused) {
                case app::ControlButton::PauseResume:
                    hint = !active ? "No print is running"
                                   : (state == model::PrintState::Paused ? "Enter: resume the print"
                                                                         : "Enter: pause the print");
                    break;
                case app::ControlButton::Cancel:
                    hint = active ? "Enter twice: cancel the print" : "No print is running";
                    break;
                case app::ControlButton::EmergencyStop:
                    hint = "Hold Enter: emergency stop";
                    break;
            }
        }
    }
    lv_label_set_text(hint_, hint.c_str());
    lv_obj_set_style_text_color(hint_, warning ? colors.warning : colors.text_disabled, 0);
}

bool ControlScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<ControlScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void ControlScreen::refresh_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<ControlScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void ControlScreen::confirm_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<ControlScreen*>(lv_timer_get_user_data(timer));
    if (!screen) {
        return;
    }
    // A one-shot timer deletes itself after this callback.
    screen->confirm_timer_ = nullptr;
    screen->refresh();
}

} // namespace screen

/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "settings_store.h"
#include "style_bindings.h"
#include "theme.h"

#include <algorithm>
#include <utility>

namespace screen {
namespace {

constexpr int32_t kRowHeight = 16;
constexpr int32_t kLabelWidth = 92;
// Characters of the edited value kept visible; the start is cut off so the cursor stays in view.
constexpr std::size_t kVisibleEditChars = 26;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

bool to_settings_input(uint32_t key, app::SettingsInput& input) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_PREV:
            input = app::SettingsInput::Up;
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_NEXT:
            input = app::SettingsInput::Down;
            return true;
        case LV_KEY_LEFT:
            input = app::SettingsInput::Left;
            return true;
        case LV_KEY_RIGHT:
            input = app::SettingsInput::Right;
            return true;
        case LV_KEY_ENTER:
            input = app::SettingsInput::Enter;
            return true;
        case LV_KEY_ESC:
            input = app::SettingsInput::Escape;
            return true;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            input = app::SettingsInput::Backspace;
            return true;
        default:
            if (key >= 32 && key <= 126) {
                input = app::SettingsInput::Character;
                return true;
            }
            return false;
    }
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               app::SettingsStore& store)
    : BaseScreen(view_model, assets),
      store_(store),
      form_(store.config(), app::connection_settings_fields()) {
    init();
    platform::set_screen_key_handler(key_handler, this);
}

SettingsScreen::~SettingsScreen() {
    platform::clear_screen_key_handler(key_handler, this);
}

void SettingsScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();
    const auto* body_font = font_or(assets().load_standard_font(13), &lv_font_montserrat_14);
    const auto* small_font =
        font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 6, 0);
    lv_obj_set_style_pad_ver(column, 3, 0);
    lv_obj_set_style_pad_row(column, 3, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    list_ = lv_obj_create(column);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 1, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    for (const auto& field : form_.fields()) {
        RowWidgets widgets;
        widgets.row = lv_obj_create(list_);
        lv_obj_remove_style_all(widgets.row);
        lv_obj_set_size(widgets.row, LV_PCT(100), kRowHeight);
        lv_obj_set_style_radius(widgets.row, 3, 0);
        lv_obj_set_style_pad_hor(widgets.row, 4, 0);
        lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

        widgets.label = lv_label_create(widgets.row);
        lv_label_set_text(widgets.label, field.label);
        lv_obj_set_style_text_font(widgets.label, body_font, 0);
        lv_obj_align(widgets.label, LV_ALIGN_LEFT_MID, 0, 0);

        widgets.value = lv_label_create(widgets.row);
        lv_label_set_text(widgets.value, "");
        lv_label_set_long_mode(widgets.value, LV_LABEL_LONG_DOT);
        // Screen width minus the column and row padding and the label column.
        lv_obj_set_width(widgets.value, view::kScreenWidth - 12 - 8 - kLabelWidth);
        lv_obj_set_height(widgets.value, lv_font_get_line_height(body_font));
        lv_obj_set_style_text_font(widgets.value, body_font, 0);
        lv_obj_align(widgets.value, LV_ALIGN_LEFT_MID, kLabelWidth, 0);

        if (field.kind == app::SettingsFieldKind::Action) {
            lv_obj_align(widgets.label, LV_ALIGN_CENTER, 0, 0);
        }
        rows_.push_back(widgets);
    }

    // Status line: connection badge, then the connection state or the latest message.
    auto* status = lv_obj_create(column);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, LV_PCT(100), kRowHeight);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    auto* badge = lv_label_create(status);
    lv_label_bind_text(badge, vm.printer_badge_text_subject(), nullptr);
    lv_obj_set_style_text_font(badge, small_font, 0);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_set_style_pad_hor(badge, 5, 0);
    lv_obj_set_style_pad_ver(badge, 1, 0);
    view::widgets::bind_tone(badge, vm.printer_badge_tone_subject(), dark, view::widgets::ToneStyle::Badge);

    connection_label_ = lv_label_create(status);
    lv_label_bind_text(connection_label_, vm.overlay_title_subject(), nullptr);
    lv_label_set_long_mode(connection_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(connection_label_, 1);
    lv_obj_set_style_text_font(connection_label_, small_font, 0);
    view::widgets::bind_muted_text(connection_label_, dark);

    message_label_ = lv_label_create(status);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(message_label_, 1);
    lv_obj_set_style_text_font(message_label_, small_font, 0);

    reactive::observe_obj(column, dark, theme_observer_cb, this);
    refresh();
}

bool SettingsScreen::handle_key(uint32_t key, bool long_pressed) {
    if (long_pressed) {
        // Holding a key while typing must not trigger the long-press shortcuts (e.g. exit).
        return form_.editing();
    }

    app::SettingsInput input{};
    if (!to_settings_input(key, input)) {
        return form_.editing();
    }

    const auto result = form_.handle(input, static_cast<char>(key));
    if (result == app::SettingsForm::Result::Ignored) {
        return false;
    }
    if (result == app::SettingsForm::Result::Submit) {
        submit();
    }
    else if (input != app::SettingsInput::Up && input != app::SettingsInput::Down) {
        // Any edit makes the previous save result stale.
        message_.clear();
    }
    refresh();
    return true;
}

void SettingsScreen::submit() {
    const auto updated = form_.apply_to(store_.config());
    const auto changed = app::changed_settings(store_.config(), updated);
    if (changed.empty() && !store_.has_unsaved()) {
        set_message("No changes", false);
        return;
    }
    std::string error;
    if (store_.apply(updated, error)) {
        set_message("Saved. Connecting...", false);
    }
    else {
        set_message("Not saved: " + error, true);
    }
}

void SettingsScreen::set_message(std::string message, bool error) {
    message_ = std::move(message);
    message_is_error_ = error;
}

void SettingsScreen::refresh() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto visible = form_.visible_fields();

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& widgets = rows_[i];
        const bool shown = std::find(visible.begin(), visible.end(), i) != visible.end();
        if (!shown) {
            lv_obj_add_flag(widgets.row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(widgets.row, LV_OBJ_FLAG_HIDDEN);

        const bool focused = i == form_.focused();
        const bool editing = focused && form_.editing();
        const bool action = form_.fields()[i].kind == app::SettingsFieldKind::Action;

        auto value = form_.display_value(i);
        if (editing) {
            if (value.size() > kVisibleEditChars) {
                value = "..." + value.substr(value.size() - kVisibleEditChars);
            }
            value += "_";
        }
        else if (form_.fields()[i].kind == app::SettingsFieldKind::Choice) {
            value = "< " + value + " >";
        }
        lv_label_set_text(widgets.value, value.c_str());

        lv_obj_set_style_bg_color(widgets.row, colors.primary, 0);
        lv_obj_set_style_bg_opa(widgets.row, focused ? (action ? LV_OPA_40 : LV_OPA_20) : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(widgets.row, colors.primary, 0);
        lv_obj_set_style_border_width(widgets.row, editing ? 1 : 0, 0);
        lv_obj_set_style_text_color(widgets.label, action ? colors.primary : colors.text, 0);
        lv_obj_set_style_text_color(widgets.value, focused ? colors.text : colors.text_disabled, 0);
        if (focused) {
            lv_obj_scroll_to_view(widgets.row, LV_ANIM_OFF);
        }
    }

    const auto& text = !form_.error().empty() ? form_.error() : message_;
    const bool error = !form_.error().empty() || message_is_error_;
    if (text.empty()) {
        lv_obj_add_flag(message_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(connection_label_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_label_set_text(message_label_, text.c_str());
        lv_obj_set_style_text_color(message_label_, error ? colors.error : colors.success, 0);
        lv_obj_remove_flag(message_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connection_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool SettingsScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<SettingsScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void SettingsScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

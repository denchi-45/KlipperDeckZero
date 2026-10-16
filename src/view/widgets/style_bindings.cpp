/*
 * SPDX-License-Identifier: MIT
 */

#include "style_bindings.h"

#include "theme.h"

namespace view::widgets {
namespace {

enum class Kind {
    ToneText,
    ToneBadge,
    Card,
    Bar,
    Muted,
};

struct Binding {
    Kind kind;
    lv_subject_t* tone;
    lv_subject_t* dark_mode;
};

// Mirrors viewmodel::format::Tone (Neutral, Info, Success, Warning, Error).
lv_color_t tone_color(const ThemePalette& colors, int32_t tone) {
    switch (tone) {
        case 1:
            return colors.info;
        case 2:
            return colors.success;
        case 3:
            return colors.warning;
        case 4:
            return colors.error;
        default:
            return colors.text_disabled;
    }
}

void apply(lv_obj_t* obj, const Binding& binding) {
    const auto colors = palette(lv_subject_get_int(binding.dark_mode) != 0);
    const int32_t tone = binding.tone ? lv_subject_get_int(binding.tone) : -1;

    switch (binding.kind) {
        case Kind::ToneText:
            lv_obj_set_style_text_color(obj, tone_color(colors, tone), 0);
            break;
        case Kind::ToneBadge:
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, tone_color(colors, tone), 0);
            lv_obj_set_style_text_color(obj, colors.surface, 0);
            break;
        case Kind::Card:
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, colors.button, 0);
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_color(obj, colors.border, 0);
            lv_obj_set_style_text_color(obj, colors.text, 0);
            break;
        case Kind::Bar:
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, colors.border, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(obj,
                                      binding.tone ? tone_color(colors, tone) : colors.primary,
                                      LV_PART_INDICATOR);
            break;
        case Kind::Muted:
            lv_obj_set_style_text_color(obj, colors.text_disabled, 0);
            break;
    }
}

void binding_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* obj = lv_observer_get_target_obj(observer);
    auto* binding = static_cast<Binding*>(lv_observer_get_user_data(observer));
    if (obj && binding) {
        apply(obj, *binding);
    }
}

void delete_binding(lv_event_t* event) {
    delete static_cast<Binding*>(lv_event_get_user_data(event));
}

void bind(lv_obj_t* obj, Kind kind, lv_subject_t* tone, lv_subject_t* dark_mode) {
    if (!obj || !dark_mode) {
        return;
    }
    auto* binding = new Binding{kind, tone, dark_mode};
    lv_obj_add_event_cb(obj, delete_binding, LV_EVENT_DELETE, binding);
    // Observers bound to obj are removed automatically when obj is deleted.
    lv_subject_add_observer_obj(dark_mode, binding_observer, obj, binding);
    if (tone) {
        lv_subject_add_observer_obj(tone, binding_observer, obj, binding);
    }
}

} // namespace

void bind_tone(lv_obj_t* obj, lv_subject_t* tone_subject, lv_subject_t* dark_mode_subject, ToneStyle style) {
    bind(obj, style == ToneStyle::Badge ? Kind::ToneBadge : Kind::ToneText, tone_subject, dark_mode_subject);
}

void bind_card(lv_obj_t* obj, lv_subject_t* dark_mode_subject) {
    bind(obj, Kind::Card, nullptr, dark_mode_subject);
}

void bind_bar(lv_obj_t* bar, lv_subject_t* dark_mode_subject, lv_subject_t* tone_subject) {
    bind(bar, Kind::Bar, tone_subject, dark_mode_subject);
}

void bind_muted_text(lv_obj_t* obj, lv_subject_t* dark_mode_subject) {
    bind(obj, Kind::Muted, nullptr, dark_mode_subject);
}

} // namespace view::widgets

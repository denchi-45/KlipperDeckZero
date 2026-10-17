/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_style.h"

namespace view::widgets {

void style_panel_row(lv_obj_t* row, const ThemePalette& colors, bool focused, bool editing) {
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row,
                              focused ? lv_color_mix(colors.primary, colors.button, LV_OPA_20) : colors.button,
                              0);
    lv_obj_set_style_border_color(row, focused ? colors.primary : colors.border, 0);
    lv_obj_set_style_border_width(row, editing ? 2 : 1, 0);
}

void init_chip(lv_obj_t* chip, const lv_font_t* font) {
    lv_obj_set_style_text_font(chip, font, 0);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_set_style_pad_hor(chip, 5, 0);
    lv_obj_set_style_pad_ver(chip, 1, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
}

void style_chip(lv_obj_t* chip, const ThemePalette& colors, bool selected, bool row_focused) {
    const bool filled = selected && row_focused;
    lv_obj_set_style_bg_color(chip, colors.primary, 0);
    lv_obj_set_style_bg_opa(chip, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(chip, selected ? colors.primary : colors.border, 0);
    lv_obj_set_style_text_color(chip, filled ? colors.surface : colors.text, 0);
}

} // namespace view::widgets

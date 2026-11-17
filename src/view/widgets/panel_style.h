/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include "lvgl.h"

// Looks shared by the keyboard-driven page panels (temperature, extruder, ...).
namespace view::widgets {

// A selectable row: tinted with a primary border while focused, thicker while editing.
void style_panel_row(lv_obj_t* row, const ThemePalette& colors, bool focused, bool editing = false);
// A label styled as a choice chip. The selected chip is filled while its row has focus and
// outlined otherwise.
void init_chip(lv_obj_t* chip, const lv_font_t* font);
void style_chip(lv_obj_t* chip, const ThemePalette& colors, bool selected, bool row_focused);

} // namespace view::widgets

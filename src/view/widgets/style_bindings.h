/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view::widgets {

enum class ToneStyle {
    Text,   // text colored by tone
    Badge,  // filled pill in the tone color with contrasting text
};

// Colors obj by an int subject holding viewmodel::format::Tone, following the theme.
void bind_tone(lv_obj_t* obj, lv_subject_t* tone_subject, lv_subject_t* dark_mode_subject, ToneStyle style);

// Raised card: button background, border, rounded corners.
void bind_card(lv_obj_t* obj, lv_subject_t* dark_mode_subject);

// Bar track uses the border color, indicator uses the given tone (or primary when tone_subject is null).
void bind_bar(lv_obj_t* bar, lv_subject_t* dark_mode_subject, lv_subject_t* tone_subject = nullptr);

// Secondary text (labels, units).
void bind_muted_text(lv_obj_t* obj, lv_subject_t* dark_mode_subject);

} // namespace view::widgets

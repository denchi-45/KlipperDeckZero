/*
 * SPDX-License-Identifier: MIT
 */

#include "placeholder_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "style_bindings.h"

namespace screen {

PlaceholderScreen::PlaceholderScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     model::AppPage page)
    : BaseScreen(view_model, assets), page_(page) {
    init();
}

void PlaceholderScreen::build_content(lv_obj_t* content) {
    auto* dark = view_model().dark_mode_subject();

    auto* title = lv_label_create(content);
    lv_label_set_text(title, model::page_title(page_));
    auto* title_font = assets().load_standard_font(18);
    lv_obj_set_style_text_font(title, title_font ? title_font : &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);
    reactive::bind_theme(title, dark, reactive::ThemeRole::Text);

    auto* note = lv_label_create(content);
    lv_label_set_text(note, "Coming soon - press ESC to go back");
    auto* note_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(note, note_font ? note_font : &lv_font_montserrat_12, 0);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, 14);
    view::widgets::bind_muted_text(note, dark);
}

} // namespace screen

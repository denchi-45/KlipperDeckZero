/*
 * SPDX-License-Identifier: MIT
 */

#include "status_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "printer_commands.h"
#include "style_bindings.h"
#include "ui_const.h"

// lv_image_cache_drop() is not part of lvgl.h in LVGL 9.5.
#include "src/misc/cache/lv_cache.h"

#include <string>
#include <utility>

namespace screen {
namespace {

// Left of the job, progress and time rows, as tall as the three of them.
constexpr int32_t kThumbnailSize = 46;

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

void overlay_icon_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* icon = lv_observer_get_target_obj(observer);
    if (icon) {
        // Tone 4 = Error (see viewmodel::format::Tone).
        lv_label_set_text(icon, lv_subject_get_int(subject) == 4 ? view::ICON_WARNING : view::ICON_PLUGS);
    }
}

} // namespace

StatusScreen::StatusScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
}

StatusScreen::~StatusScreen() {
    // The image and the decoded copy in the cache point at png_, which goes away with the screen.
    if (thumbnail_) {
        lv_image_set_src(thumbnail_, nullptr);
    }
    lv_image_cache_drop(&png_dsc_);
}

void StatusScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);
    body_font_ = font_or(assets().load_standard_font(14), &lv_font_montserrat_14);
    large_font_ = font_or(assets().load_standard_font(20), &lv_font_montserrat_20);
    icon_font_ = font_or(assets().load_font("Phosphor-Fill.ttf", 14), &lv_font_montserrat_14);
    overlay_icon_font_ = font_or(assets().load_font("Phosphor-Fill.ttf", 28), &lv_font_montserrat_28);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 8, 0);
    lv_obj_set_style_pad_top(column, 3, 0);
    lv_obj_set_style_pad_row(column, 3, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    // Rows 1-3 sit right of the thumbnail of the printing file. The thumbnail is hidden while
    // there is none, so the rows then take the full width.
    auto* job_block = lv_obj_create(column);
    lv_obj_remove_style_all(job_block);
    lv_obj_set_size(job_block, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(job_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(job_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(job_block, 6, 0);
    lv_obj_clear_flag(job_block, LV_OBJ_FLAG_SCROLLABLE);

    thumbnail_frame_ = lv_obj_create(job_block);
    lv_obj_remove_style_all(thumbnail_frame_);
    lv_obj_set_size(thumbnail_frame_, kThumbnailSize, kThumbnailSize);
    view::widgets::bind_card(thumbnail_frame_, dark);
    lv_obj_set_style_radius(thumbnail_frame_, 3, 0);
    lv_obj_set_style_pad_all(thumbnail_frame_, 1, 0);
    lv_obj_clear_flag(thumbnail_frame_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(thumbnail_frame_, LV_OBJ_FLAG_HIDDEN);

    thumbnail_ = lv_image_create(thumbnail_frame_);
    lv_obj_set_size(thumbnail_, LV_PCT(100), LV_PCT(100));
    // Slicers write 300x300 and smaller; scale whichever was picked to fit the frame.
    lv_image_set_inner_align(thumbnail_, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(thumbnail_, true);

    auto* job_column = lv_obj_create(job_block);
    lv_obj_remove_style_all(job_column);
    lv_obj_set_height(job_column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(job_column, 1);
    lv_obj_set_flex_flow(job_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(job_column, 3, 0);
    lv_obj_clear_flag(job_column, LV_OBJ_FLAG_SCROLLABLE);

    // Row 1: state badge, job name, progress percent.
    auto* job_row = create_row(job_column, 18, LV_FLEX_ALIGN_START);
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

    // Row 2: progress bar.
    auto* bar = lv_bar_create(job_column);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    reactive::bind_bar_value(bar, vm.printer_progress_subject());
    view::widgets::bind_bar(bar, dark, vm.printer_badge_tone_subject());

    // Row 3: elapsed / remaining / layers.
    auto* time_row = create_row(job_column, 16, LV_FLEX_ALIGN_SPACE_BETWEEN);
    const auto add_stat = [&](const char* icon_text, lv_subject_t* subject) {
        auto* item = lv_obj_create(time_row);
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
    };
    add_stat(view::ICON_CLOCK, vm.printer_elapsed_subject());
    add_stat(view::ICON_HOURGLASS, vm.printer_remaining_subject());
    add_stat(view::ICON_STACK, vm.printer_layers_subject());

    // Row 4: heater cards.
    auto* heater_row = create_row(column, 40, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_column(heater_row, 6, 0);
    lv_obj_t* nozzle_card = nullptr;
    lv_obj_t* bed_card = nullptr;
    build_heater_card(heater_row,
                      "Nozzle",
                      vm.nozzle_temperature_subject(),
                      vm.nozzle_target_subject(),
                      vm.nozzle_power_subject(),
                      &nozzle_card);
    build_heater_card(heater_row,
                      "Bed",
                      vm.bed_temperature_subject(),
                      vm.bed_target_subject(),
                      vm.bed_power_subject(),
                      &bed_card);
    reactive::bind_flag(bed_card, vm.bed_available_subject(), LV_OBJ_FLAG_HIDDEN, 0);

    // Row 5: speed / flow / fan factors.
    auto* factors = lv_label_create(column);
    lv_label_bind_text(factors, vm.printer_factors_subject(), nullptr);
    lv_label_set_long_mode(factors, LV_LABEL_LONG_DOT);
    lv_obj_set_width(factors, LV_PCT(100));
    lv_obj_set_style_text_font(factors, small_font_, 0);
    view::widgets::bind_muted_text(factors, dark);

    build_overlay(content);

    // Follow the print: the badge changes with the print state, the job with the file.
    reactive::observe_obj(column, vm.printer_badge_text_subject(), print_observer_cb, this);
    reactive::observe_obj(column, vm.printer_job_subject(), print_observer_cb, this);
    update_thumbnail();
}

void StatusScreen::build_heater_card(lv_obj_t* parent,
                                     const char* title,
                                     lv_subject_t* temperature,
                                     lv_subject_t* target,
                                     lv_subject_t* power,
                                     lv_obj_t** card_out) {
    auto* dark = view_model().dark_mode_subject();

    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_pad_hor(card, 6, 0);
    lv_obj_set_style_pad_top(card, 2, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    view::widgets::bind_card(card, dark);

    auto* icon = lv_label_create(card);
    lv_label_set_text(icon, view::ICON_FIRE);
    lv_obj_set_style_text_font(icon, icon_font_, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 1);
    view::widgets::bind_muted_text(icon, dark);

    auto* name = lv_label_create(card);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_font(name, small_font_, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 17, 0);
    view::widgets::bind_muted_text(name, dark);

    auto* target_label = lv_label_create(card);
    lv_label_bind_text(target_label, target, nullptr);
    lv_obj_set_style_text_font(target_label, small_font_, 0);
    lv_obj_align(target_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    view::widgets::bind_muted_text(target_label, dark);

    auto* value = lv_label_create(card);
    lv_label_bind_text(value, temperature, nullptr);
    lv_obj_set_style_text_font(value, large_font_, 0);
    lv_obj_align(value, LV_ALIGN_TOP_LEFT, 0, 12);
    reactive::bind_theme(value, dark, reactive::ThemeRole::Text);

    auto* power_bar = lv_bar_create(card);
    lv_obj_set_size(power_bar, 36, 4);
    lv_bar_set_range(power_bar, 0, 100);
    lv_obj_align(power_bar, LV_ALIGN_TOP_RIGHT, 0, 23);
    reactive::bind_bar_value(power_bar, power);
    view::widgets::bind_bar(power_bar, dark);

    if (card_out) {
        *card_out = card;
    }
}

void StatusScreen::build_overlay(lv_obj_t* content) {
    auto& vm = view_model();
    auto* dark = vm.dark_mode_subject();

    auto* overlay = lv_obj_create(content);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(overlay, 2, 0);
    lv_obj_set_style_pad_hor(overlay, 12, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(overlay, dark, reactive::ThemeRole::Surface);
    reactive::bind_flag(overlay, vm.overlay_visible_subject(), LV_OBJ_FLAG_HIDDEN, 0);

    auto* icon = lv_label_create(overlay);
    lv_obj_set_style_text_font(icon, overlay_icon_font_, 0);
    lv_subject_add_observer_obj(vm.overlay_tone_subject(), overlay_icon_observer, icon, nullptr);
    view::widgets::bind_tone(icon, vm.overlay_tone_subject(), dark, view::widgets::ToneStyle::Text);

    auto* title = lv_label_create(overlay);
    lv_label_bind_text(title, vm.overlay_title_subject(), nullptr);
    lv_obj_set_style_text_font(title, body_font_, 0);
    reactive::bind_theme(title, dark, reactive::ThemeRole::Text);

    auto* detail = lv_label_create(overlay);
    lv_label_bind_text(detail, vm.overlay_detail_subject(), nullptr);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail, LV_PCT(100));
    lv_obj_set_style_max_height(detail, 46, 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(detail, small_font_, 0);
    view::widgets::bind_muted_text(detail, dark);

    auto* hint = lv_label_create(overlay);
    lv_label_bind_text(hint, vm.overlay_hint_subject(), nullptr);
    lv_obj_set_style_text_font(hint, small_font_, 0);
    view::widgets::bind_muted_text(hint, dark);
}

void StatusScreen::update_thumbnail() {
    if (!thumbnail_) {
        return;
    }
    const auto* printer = commands_.printer();
    const auto action = printer ? thumbnail_state_.update(printer->print_state, printer->filename)
                                : thumbnail_state_.update(model::PrintState::Unknown, std::string());
    if (action == app::StatusThumbnail::Action::None) {
        return;
    }
    refresh_thumbnail();
    if (action == app::StatusThumbnail::Action::Fetch) {
        load_thumbnail();
    }
}

void StatusScreen::load_thumbnail() {
    std::weak_ptr<bool> alive = alive_;
    const std::string path = thumbnail_state_.file();
    commands_.file_metadata(path, [this, alive, path](const app::GcodeMetadata* metadata) {
        // The print may have ended or moved on to another file by now.
        if (alive.expired() || !metadata || !thumbnail_state_.wants(path)) {
            return;
        }
        const auto* thumbnail = app::pick_thumbnail(metadata->thumbnails, kThumbnailSize);
        if (!thumbnail) {
            return;
        }
        commands_.file_thumbnail(app::thumbnail_path(path, thumbnail->relative_path),
                                 [this, alive, path](std::string png) {
                                     if (alive.expired()) {
                                         return;
                                     }
                                     thumbnail_state_.set_thumbnail(path, std::move(png));
                                     refresh_thumbnail();
                                 });
    });
}

void StatusScreen::refresh_thumbnail() {
    const auto& png = thumbnail_state_.thumbnail();
    if (!thumbnail_ || png == png_) {
        return;
    }
    // Detach the old bytes from the image and the cache before they are replaced.
    lv_image_set_src(thumbnail_, nullptr);
    lv_image_cache_drop(&png_dsc_);
    png_ = png;
    png_dsc_ = {};
    // A truncated file would make the libpng decoder read past its end.
    if (!app::has_png_header(png_)) {
        lv_obj_add_flag(thumbnail_frame_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // A PNG file in memory: the libpng decoder reads the size from the data.
    png_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    png_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    png_dsc_.data = reinterpret_cast<const uint8_t*>(png_.data());
    png_dsc_.data_size = static_cast<uint32_t>(png_.size());
    lv_image_set_src(thumbnail_, &png_dsc_);
    lv_obj_remove_flag(thumbnail_frame_, LV_OBJ_FLAG_HIDDEN);
}

void StatusScreen::print_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<StatusScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->update_thumbnail();
    }
}

} // namespace screen

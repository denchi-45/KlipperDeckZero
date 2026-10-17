/*
 * SPDX-License-Identifier: MIT
 */

#include "files_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "panel_keys.h"
#include "panel_style.h"
#include "printer_commands.h"
#include "theme.h"

// lv_image_cache_drop() is not part of lvgl.h in LVGL 9.5.
#include "src/misc/cache/lv_cache.h"

#include <cmath>
#include <string>

namespace screen {
namespace {

constexpr int32_t kRowHeight = 17;
constexpr int32_t kSizeWidth = 58;
// Content width minus the column and row padding, the size column and a gap.
constexpr int32_t kNameWidth = view::kScreenWidth - 16 - 10 - kSizeWidth - 6;
constexpr int32_t kDetailValueX = 52;
constexpr int32_t kDetailLineSpace = -2;
// Square left of the detail lines, as tall as the four of them.
constexpr int32_t kThumbnailSize = 64;

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

std::string whole_degrees(double celsius) {
    return celsius < 0.0 ? std::string("--") : std::to_string(static_cast<int>(std::lround(celsius)));
}

// One line per detail, matching kDetailKeys, so the two labels line up. Four lines fit
// between the file name and the hint.
constexpr const char* kDetailKeys = "Time\nFilament\nLayer\nTemps";

std::string detail_values(const app::GcodeMetadata& metadata) {
    return app::format_duration(metadata.estimated_time) + "\n" +
           app::format_filament(metadata.filament_total) + "\n" +
           app::format_millimeters(metadata.layer_height) + "  (height " +
           app::format_millimeters(metadata.object_height) + ")\n" +
           whole_degrees(metadata.nozzle_temperature) + " / " + whole_degrees(metadata.bed_temperature);
}

} // namespace

FilesScreen::FilesScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
    load_files();
}

FilesScreen::~FilesScreen() {
    platform::clear_screen_key_handler(key_handler, this);
    // The image and the decoded copy in the cache point at png_, which goes away with the screen.
    if (thumbnail_) {
        lv_image_set_src(thumbnail_, nullptr);
    }
    lv_image_cache_drop(&png_dsc_);
}

void FilesScreen::build_content(lv_obj_t* content) {
    auto* dark = view_model().dark_mode_subject();
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

    auto* body = lv_obj_create(column);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    list_box_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_box_);
    lv_obj_set_size(list_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_box_, 1, 0);
    lv_obj_clear_flag(list_box_, LV_OBJ_FLAG_SCROLLABLE);

    for (auto& widgets : rows_) {
        widgets.row = lv_obj_create(list_box_);
        lv_obj_remove_style_all(widgets.row);
        lv_obj_set_size(widgets.row, LV_PCT(100), kRowHeight);
        lv_obj_set_style_radius(widgets.row, 3, 0);
        lv_obj_set_style_pad_hor(widgets.row, 5, 0);
        lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

        widgets.name = lv_label_create(widgets.row);
        lv_label_set_long_mode(widgets.name, LV_LABEL_LONG_DOT);
        // One line beside the size column; without a fixed height a long name wraps out of the row.
        lv_obj_set_size(widgets.name, kNameWidth, lv_font_get_line_height(body_font_));
        lv_obj_set_style_text_font(widgets.name, body_font_, 0);
        lv_obj_align(widgets.name, LV_ALIGN_LEFT_MID, 0, 0);

        widgets.size = lv_label_create(widgets.row);
        lv_obj_set_width(widgets.size, kSizeWidth);
        lv_obj_set_style_text_align(widgets.size, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(widgets.size, small_font_, 0);
        lv_obj_align(widgets.size, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    message_ = lv_label_create(body);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message_, LV_PCT(100));
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(message_, body_font_, 0);
    lv_obj_center(message_);

    build_details(body);

    hint_ = lv_label_create(column);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint_, LV_PCT(100));
    lv_obj_set_style_text_font(hint_, small_font_, 0);

    reactive::observe_obj(column, dark, theme_observer_cb, this);
    refresh();
}

void FilesScreen::build_details(lv_obj_t* parent) {
    details_box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(details_box_);
    lv_obj_set_size(details_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(details_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(details_box_, 6, 0);
    lv_obj_set_style_pad_row(details_box_, 2, 0);
    lv_obj_set_style_radius(details_box_, 4, 0);
    lv_obj_clear_flag(details_box_, LV_OBJ_FLAG_SCROLLABLE);

    details_title_ = lv_label_create(details_box_);
    // Long names often differ only at the end (e.g. the print time), so scroll the whole name.
    lv_label_set_long_mode(details_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(details_title_, LV_PCT(100), lv_font_get_line_height(body_font_));
    lv_obj_set_style_text_font(details_title_, body_font_, 0);

    // The thumbnail and the detail lines side by side under the name.
    auto* row = lv_obj_create(details_box_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Hidden for files without a thumbnail, so the details start at the left edge.
    thumbnail_frame_ = lv_obj_create(row);
    lv_obj_remove_style_all(thumbnail_frame_);
    lv_obj_set_size(thumbnail_frame_, kThumbnailSize, kThumbnailSize);
    lv_obj_set_style_radius(thumbnail_frame_, 3, 0);
    lv_obj_set_style_bg_opa(thumbnail_frame_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(thumbnail_frame_, 1, 0);
    lv_obj_set_style_pad_all(thumbnail_frame_, 1, 0);
    lv_obj_clear_flag(thumbnail_frame_, LV_OBJ_FLAG_SCROLLABLE);

    thumbnail_ = lv_image_create(thumbnail_frame_);
    lv_obj_set_size(thumbnail_, LV_PCT(100), LV_PCT(100));
    // Slicers write 300x300 and smaller; scale whichever was picked to fit the frame.
    lv_image_set_inner_align(thumbnail_, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(thumbnail_, true);

    auto* table = lv_obj_create(row);
    lv_obj_remove_style_all(table);
    lv_obj_set_height(table, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(table, 1);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    // Noto CJK lines are tall; without tightening, the last line falls below the box.
    details_keys_ = lv_label_create(table);
    lv_label_set_text(details_keys_, kDetailKeys);
    lv_obj_set_style_text_font(details_keys_, small_font_, 0);
    lv_obj_set_style_text_line_space(details_keys_, kDetailLineSpace, 0);

    details_body_ = lv_label_create(table);
    lv_obj_set_style_text_font(details_body_, small_font_, 0);
    lv_obj_set_style_text_line_space(details_body_, kDetailLineSpace, 0);
}

bool FilesScreen::handle_key(uint32_t key, bool long_pressed) {
    app::PanelInput input{};
    if (long_pressed || !to_panel_input(key, input)) {
        return false;
    }

    switch (browser_.handle(input)) {
        case app::FileBrowser::Result::Ignored:
            return false;
        case app::FileBrowser::Result::Handled:
            break;
        case app::FileBrowser::Result::Reload:
            load_files();
            break;
        case app::FileBrowser::Result::OpenDetails:
            load_metadata();
            break;
        case app::FileBrowser::Result::StartPrint:
            if (const auto* file = browser_.selected_file()) {
                commands_.start_print(file->path);
            }
            break;
    }
    refresh();
    return true;
}

void FilesScreen::load_files() {
    browser_.begin_loading();
    std::weak_ptr<bool> alive = alive_;
    commands_.list_files([this, alive](std::vector<app::GcodeFile> files, const std::string& error) {
        if (alive.expired()) {
            return;
        }
        if (error.empty()) {
            browser_.set_files(std::move(files));
        }
        else {
            browser_.set_failed(error);
        }
        refresh();
    });
    refresh();
}

void FilesScreen::load_metadata() {
    const auto* file = browser_.selected_file();
    if (!file) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    const std::string path = file->path;
    commands_.file_metadata(path, [this, alive, path](const app::GcodeMetadata* metadata) {
        if (alive.expired()) {
            return;
        }
        browser_.set_metadata(path, metadata);
        load_thumbnail();
        refresh();
    });
}

void FilesScreen::load_thumbnail() {
    const auto* file = browser_.selected_file();
    if (!file || browser_.details_state() != app::FileBrowser::DetailsState::Loaded) {
        return;
    }
    const auto* thumbnail = app::pick_thumbnail(browser_.metadata().thumbnails, kThumbnailSize);
    if (!thumbnail) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    const std::string path = file->path;
    commands_.file_thumbnail(app::thumbnail_path(path, thumbnail->relative_path),
                             [this, alive, path](std::string png) {
                                 if (alive.expired()) {
                                     return;
                                 }
                                 browser_.set_thumbnail(path, std::move(png));
                                 refresh();
                             });
}

void FilesScreen::refresh() {
    if (!hint_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto mode = browser_.mode();
    const bool list = mode == app::FileBrowser::Mode::List && !browser_.files().empty();
    const bool details = mode == app::FileBrowser::Mode::Details;

    const auto show = [](lv_obj_t* obj, bool visible) {
        if (visible) {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };
    show(list_box_, list);
    show(details_box_, details);
    show(message_, !list && !details);
    // Also lets go of the previous file's PNG once its details are closed.
    refresh_thumbnail();

    std::string hint;
    switch (mode) {
        case app::FileBrowser::Mode::Loading:
            lv_label_set_text(message_, "Loading files...");
            break;
        case app::FileBrowser::Mode::Failed:
            lv_label_set_text(message_, ("Could not load files\n" + browser_.error()).c_str());
            hint = "Enter: retry";
            break;
        case app::FileBrowser::Mode::List:
            if (browser_.files().empty()) {
                lv_label_set_text(message_, "No G-code files");
                break;
            }
            refresh_list();
            hint = std::to_string(browser_.selected() + 1) + "/" + std::to_string(browser_.files().size()) +
                   "   Enter: details   Left/Right: page";
            break;
        case app::FileBrowser::Mode::Details:
            refresh_details();
            hint = "Enter: start print   Esc: back to list";
            break;
    }
    lv_obj_set_style_text_color(message_, colors.text_disabled, 0);
    lv_label_set_text(hint_, hint.c_str());
    lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
}

void FilesScreen::refresh_list() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& files = browser_.files();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& widgets = rows_[i];
        const auto index = browser_.top() + i;
        if (index >= files.size()) {
            lv_obj_add_flag(widgets.row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(widgets.row, LV_OBJ_FLAG_HIDDEN);
        const bool selected = index == browser_.selected();
        if (selected) {
            view::widgets::style_panel_row(widgets.row, colors, true);
        }
        else {
            lv_obj_set_style_bg_opa(widgets.row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(widgets.row, 0, 0);
        }
        // Long names often differ only at the end (e.g. the print time): the selected row scrolls
        // through the whole name, the others are cut with an ellipsis.
        const auto long_mode = selected ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
        if (lv_label_get_long_mode(widgets.name) != long_mode) {
            lv_label_set_long_mode(widgets.name, long_mode);
        }
        if (files[index].path != lv_label_get_text(widgets.name)) {
            lv_label_set_text(widgets.name, files[index].path.c_str());
        }
        lv_obj_set_style_text_color(widgets.name, colors.text, 0);
        lv_label_set_text(widgets.size, app::format_size(files[index].size).c_str());
        lv_obj_set_style_text_color(widgets.size, colors.text_disabled, 0);
    }
}

void FilesScreen::refresh_details() {
    const auto colors = view::palette(view_model().is_dark_mode());
    view::widgets::style_panel_row(details_box_, colors, false);
    const auto* file = browser_.selected_file();
    lv_label_set_text(details_title_, file ? file->path.c_str() : "");
    lv_obj_set_style_text_color(details_title_, colors.text, 0);
    lv_obj_set_style_text_color(details_keys_, colors.text_disabled, 0);
    lv_obj_set_style_text_color(details_body_, colors.text, 0);

    const bool loaded = browser_.details_state() == app::FileBrowser::DetailsState::Loaded;
    // The frame shows as soon as the metadata names a thumbnail, so the text does not move
    // when the image arrives.
    const bool has_thumbnail =
        loaded && app::pick_thumbnail(browser_.metadata().thumbnails, kThumbnailSize) != nullptr;
    if (has_thumbnail) {
        lv_obj_remove_flag(thumbnail_frame_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(thumbnail_frame_, colors.background, 0);
        lv_obj_set_style_border_color(thumbnail_frame_, colors.border, 0);
    }
    else {
        lv_obj_add_flag(thumbnail_frame_, LV_OBJ_FLAG_HIDDEN);
    }

    if (loaded) {
        lv_obj_remove_flag(details_keys_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(details_body_, kDetailValueX);
        lv_label_set_text(details_body_, detail_values(browser_.metadata()).c_str());
    }
    else {
        lv_obj_add_flag(details_keys_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(details_body_, 0);
        lv_label_set_text(details_body_,
                          browser_.details_state() == app::FileBrowser::DetailsState::Loading
                              ? "Loading details..."
                              : "No details for this file");
    }
}

void FilesScreen::refresh_thumbnail() {
    const auto& png = browser_.thumbnail();
    if (!thumbnail_ || png == png_) {
        return;
    }
    // Detach the old bytes from the image and the cache before they are replaced.
    lv_image_set_src(thumbnail_, nullptr);
    lv_image_cache_drop(&png_dsc_);
    png_ = png;
    png_dsc_ = {};
    if (png_.empty()) {
        return;
    }
    // A PNG file in memory: the libpng decoder reads the size from the data.
    png_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    png_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    png_dsc_.data = reinterpret_cast<const uint8_t*>(png_.data());
    png_dsc_.data_size = static_cast<uint32_t>(png_.size());
    lv_image_set_src(thumbnail_, &png_dsc_);
}

bool FilesScreen::key_handler(uint32_t key, bool long_pressed, void* user_data) {
    auto* screen = static_cast<FilesScreen*>(user_data);
    return screen && screen->handle_key(key, long_pressed);
}

void FilesScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<FilesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

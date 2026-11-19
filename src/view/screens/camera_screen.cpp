/*
 * SPDX-License-Identifier: MIT
 */

#include "camera_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "logger.h"
#include "panel_keys.h"
#include "printer_commands.h"
#include "webcam_source.h"

// lv_image_cache_drop() is not part of lvgl.h in LVGL 9.5.
#include "src/misc/cache/lv_cache.h"

#include <algorithm>
#include <utility>

namespace screen {
namespace {

constexpr uint32_t kTickMs = 50;
// At most this many images a second: each one is downloaded and decoded on the CM0, which
// may also be running Klipper.
constexpr int kMaxFps = 4;
// Wait before asking again after a failed list or image.
constexpr uint32_t kRetryMs = 3000;
// A webcam list without cameras may be filled in later (crowsnest starting up).
constexpr uint32_t kEmptyListRetryMs = 10000;
// A request whose answer never came, e.g. dropped with a replaced session, is given up.
constexpr uint32_t kStaleRequestMs = 20000;

// The camera shown last; picked again when the page is opened.
std::string& last_camera_name() {
    static std::string name;
    return name;
}

bool reached(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}

const lv_font_t* font_or(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

} // namespace

CameraScreen::CameraScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           app::PrinterCommands& commands)
    : BaseScreen(view_model, assets), commands_(commands) {
    init();
    platform::set_screen_key_handler(key_handler, this);
    tick_timer_ = lv_timer_create(tick_timer_cb, kTickMs, this);
    tick();
}

CameraScreen::~CameraScreen() {
    platform::clear_screen_key_handler(key_handler, this);
    if (tick_timer_) {
        lv_timer_delete(tick_timer_);
    }
    // The image and any cached copy point at frame_, which goes away with the screen.
    if (image_) {
        lv_image_set_src(image_, nullptr);
    }
    lv_image_cache_drop(&image_dsc_);
}

void CameraScreen::build_content(lv_obj_t* content) {
    small_font_ = font_or(assets().load_standard_font(12, app::StandardFontWeight::Regular), &lv_font_montserrat_12);

    // Black behind the image in both themes, like a video player.
    frame_box_ = lv_obj_create(content);
    lv_obj_remove_style_all(frame_box_);
    lv_obj_set_size(frame_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(frame_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(frame_box_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(frame_box_, LV_OBJ_FLAG_SCROLLABLE);

    image_ = lv_image_create(frame_box_);
    lv_obj_center(image_);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);

    message_ = lv_label_create(frame_box_);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message_, LV_PCT(90));
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(message_, small_font_, 0);
    lv_obj_set_style_text_color(message_, lv_color_hex(0xC8C8C8), 0);
    lv_obj_center(message_);

    // Camera name, position in the list and frame rate, over the bottom left of the image.
    info_ = lv_label_create(frame_box_);
    lv_label_set_long_mode(info_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(info_, LV_PCT(96), 0);
    lv_obj_set_style_text_font(info_, small_font_, 0);
    lv_obj_set_style_text_color(info_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(info_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(info_, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(info_, 4, 0);
    lv_obj_set_style_radius(info_, 3, 0);
    lv_obj_align(info_, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);

    show_message("Connecting...");
}

bool CameraScreen::handle_key(uint32_t key) {
    app::PanelInput input{};
    if (!to_panel_input(key, input) || phase_ != Phase::Showing || webcams_.empty()) {
        return false;
    }
    switch (input) {
        case app::PanelInput::Left:
            if (webcams_.size() > 1) {
                select((selected_ + webcams_.size() - 1) % webcams_.size());
            }
            return true;
        case app::PanelInput::Right:
            if (webcams_.size() > 1) {
                select((selected_ + 1) % webcams_.size());
            }
            return true;
        case app::PanelInput::Enter:
            paused_ = !paused_;
            // Resume with a fresh image.
            next_image_at_ = lv_tick_get();
            refresh_info();
            return true;
        default:
            return false;
    }
}

void CameraScreen::tick() {
    auto* source = commands_.webcams();
    if (source != source_) {
        reset(source);
    }
    if (!source_) {
        show_message("Not connected");
        return;
    }

    const uint32_t now = lv_tick_get();
    switch (phase_) {
        case Phase::NeedList:
            if (reached(now, retry_at_)) {
                request_list(*source_);
            }
            break;
        case Phase::Listing:
            if (reached(now, request_started_ + kStaleRequestMs)) {
                // A late answer must not pass for the next request's.
                ++generation_;
                phase_ = Phase::NeedList;
                retry_at_ = now;
            }
            break;
        case Phase::Showing:
            if (fetching_) {
                if (reached(now, request_started_ + kStaleRequestMs)) {
                    ++generation_;
                    fetching_ = false;
                    show_message("No answer from the camera");
                }
            }
            else if (!paused_ && reached(now, next_image_at_)) {
                request_image(*source_);
            }
            break;
    }
}

void CameraScreen::reset(app::WebcamSource* source) {
    source_ = source;
    ++generation_;
    phase_ = Phase::NeedList;
    retry_at_ = lv_tick_get();
    fetching_ = false;
    webcams_.clear();
    selected_ = 0;
    refresh_info();
}

void CameraScreen::request_list(app::WebcamSource& source) {
    phase_ = Phase::Listing;
    request_started_ = lv_tick_get();
    std::weak_ptr<bool> alive = alive_;
    const auto generation = generation_;
    source.list([this, alive, generation](std::vector<app::WebcamInfo> webcams, const std::string& error) {
        if (alive.expired() || generation != generation_ || phase_ != Phase::Listing) {
            return;
        }
        const uint32_t now = lv_tick_get();
        if (!error.empty()) {
            LOG_WARN("webcam list: {}", error);
            show_message("Cannot list the webcams:\n" + error);
            phase_ = Phase::NeedList;
            retry_at_ = now + kRetryMs;
            return;
        }
        if (webcams.empty()) {
            show_message("No webcam is set up in Moonraker\n(e.g. crowsnest, or [webcam] in moonraker.conf)");
            phase_ = Phase::NeedList;
            retry_at_ = now + kEmptyListRetryMs;
            return;
        }
        webcams_ = std::move(webcams);
        phase_ = Phase::Showing;
        const auto& last = last_camera_name();
        const auto it = std::find_if(webcams_.begin(), webcams_.end(),
                                     [&last](const app::WebcamInfo& webcam) { return webcam.name == last; });
        selected_ = it != webcams_.end() ? static_cast<std::size_t>(it - webcams_.begin()) : 0;
        select(selected_);
    });
}

void CameraScreen::select(std::size_t index) {
    selected_ = index;
    last_camera_name() = webcams_[selected_].name;
    ++generation_;
    fetching_ = false;
    next_image_at_ = lv_tick_get();
    frames_ = 0;
    frames_since_ = next_image_at_;
    fps_text_.clear();
    if (image_) {
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    }
    show_message("Loading " + webcams_[selected_].name + "...");
    refresh_info();
}

void CameraScreen::request_image(app::WebcamSource& source) {
    const auto& webcam = webcams_[selected_];
    const std::string url = source.snapshot_url(webcam);
    const uint32_t now = lv_tick_get();
    if (url.empty()) {
        show_message("\"" + webcam.name + "\" has no snapshot URL\n(" +
                     (webcam.service.empty() ? std::string("unknown service") : webcam.service) + ")");
        next_image_at_ = now + kEmptyListRetryMs;
        return;
    }
    fetching_ = true;
    request_started_ = now;
    std::weak_ptr<bool> alive = alive_;
    const auto generation = generation_;
    source.fetch(url, [this, alive, generation](std::string jpeg, const std::string& error) {
        if (alive.expired()) {
            return;
        }
        on_image(generation, std::move(jpeg), error);
    });
}

void CameraScreen::on_image(std::uint32_t generation, std::string jpeg, const std::string& error) {
    if (generation != generation_ || phase_ != Phase::Showing) {
        return;
    }
    fetching_ = false;
    const uint32_t now = lv_tick_get();
    const auto& webcam = webcams_[selected_];

    std::string problem = error;
    app::WebcamFrame frame;
    if (problem.empty()) {
        app::WebcamFrameLayout layout;
        measure_box(layout.box_width, layout.box_height);
        layout.rotation = webcam.rotation;
        layout.flip_horizontal = webcam.flip_horizontal;
        layout.flip_vertical = webcam.flip_vertical;
        app::decode_webcam_jpeg(jpeg, layout, frame, problem);
    }
    if (!problem.empty()) {
        LOG_WARN("webcam {}: {}", webcam.name, problem);
        // Keep the last image visible; say why it stopped.
        show_message("Camera \"" + webcam.name + "\": " + problem);
        next_image_at_ = now + kRetryMs;
        return;
    }

    show_frame(std::move(frame));
    show_message({});

    const int fps = webcam.target_fps > 0 ? std::min(webcam.target_fps, kMaxFps) : kMaxFps;
    const uint32_t interval = 1000u / static_cast<uint32_t>(fps);
    // Keep the pace from the request's start, but leave the LVGL thread some breathing room.
    // Ticks wrap around, so they are compared with reached() rather than std::max.
    const uint32_t paced = request_started_ + interval;
    const uint32_t earliest = now + kTickMs;
    next_image_at_ = reached(earliest, paced) ? earliest : paced;

    ++frames_;
    const uint32_t span = now - frames_since_;
    if (span >= 2000) {
        const uint32_t tenths = frames_ * 10000u / span;
        fps_text_ = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " fps";
        frames_ = 0;
        frames_since_ = now;
        refresh_info();
    }
}

void CameraScreen::show_frame(app::WebcamFrame frame) {
    if (!image_) {
        return;
    }
    // Detach the old pixels from the image and the cache before they are replaced.
    lv_image_set_src(image_, nullptr);
    lv_image_cache_drop(&image_dsc_);
    frame_ = std::move(frame);
    image_dsc_ = {};
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = LV_COLOR_FORMAT_XRGB8888;
    image_dsc_.header.w = static_cast<uint32_t>(frame_.width);
    image_dsc_.header.h = static_cast<uint32_t>(frame_.height);
    image_dsc_.header.stride = static_cast<uint32_t>(frame_.width) * 4;
    image_dsc_.data = reinterpret_cast<const uint8_t*>(frame_.pixels.data());
    image_dsc_.data_size = static_cast<uint32_t>(frame_.pixels.size() * sizeof(frame_.pixels[0]));
    lv_image_set_src(image_, &image_dsc_);
    lv_obj_center(image_);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
}

void CameraScreen::show_message(const std::string& message) {
    if (!message_) {
        return;
    }
    const bool over_image = image_ && !lv_obj_has_flag(image_, LV_OBJ_FLAG_HIDDEN);
    if (message == shown_message_ && over_image == message_over_image_) {
        return;
    }
    shown_message_ = message;
    message_over_image_ = over_image;
    if (message.empty()) {
        lv_obj_add_flag(message_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(message_, message.c_str());
    lv_obj_remove_flag(message_, LV_OBJ_FLAG_HIDDEN);
    // Readable over the last image.
    lv_obj_set_style_bg_color(message_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(message_, over_image ? LV_OPA_70 : LV_OPA_TRANSP, 0);
}

void CameraScreen::refresh_info() {
    if (!info_) {
        return;
    }
    if (phase_ != Phase::Showing || webcams_.empty()) {
        lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    std::string text = webcams_[selected_].name;
    if (webcams_.size() > 1) {
        text += "  " + std::to_string(selected_ + 1) + "/" + std::to_string(webcams_.size());
    }
    if (paused_) {
        text += "  PAUSED";
    }
    else if (!fps_text_.empty()) {
        text += "  " + fps_text_;
    }
    lv_label_set_text(info_, text.c_str());
    lv_obj_remove_flag(info_, LV_OBJ_FLAG_HIDDEN);
}

void CameraScreen::measure_box(int& width, int& height) {
    lv_obj_update_layout(frame_box_);
    width = lv_obj_get_content_width(frame_box_);
    height = lv_obj_get_content_height(frame_box_);
}

bool CameraScreen::key_handler(uint32_t key, bool, void* user_data) {
    auto* screen = static_cast<CameraScreen*>(user_data);
    return screen && screen->handle_key(key);
}

void CameraScreen::tick_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<CameraScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->tick();
    }
}

} // namespace screen

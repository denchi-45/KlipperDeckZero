/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "webcam.h"
#include "webcam_frame.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app {
class PrinterCommands;
class WebcamSource;
}

namespace screen {

// The printer's webcam: still images fetched one after another from the webcam Moonraker lists,
// a few per second. Images are only fetched while this page is shown.
class CameraScreen : public BaseScreen {
public:
    CameraScreen(viewmodel::BaseViewModel& view_model,
                 app::AssetManager& assets,
                 app::PrinterCommands& commands);
    ~CameraScreen() override;

private:
    enum class Phase {
        NeedList, // ask Moonraker for the webcams at retry_at_
        Listing,  // waiting for the list
        Showing,  // fetching images of webcams_[selected_]
    };

    void build_content(lv_obj_t* content) override;
    bool handle_key(uint32_t key);
    void tick();
    void reset(app::WebcamSource* source);
    void request_list(app::WebcamSource& source);
    void request_image(app::WebcamSource& source);
    void on_image(std::uint32_t generation, std::string jpeg, const std::string& error);
    void select(std::size_t index);
    void show_frame(app::WebcamFrame frame);
    void show_message(const std::string& message);
    void refresh_info();
    // The size of the area images are fitted into.
    void measure_box(int& width, int& height);

    static bool key_handler(uint32_t key, bool long_pressed, void* user_data);
    static void tick_timer_cb(lv_timer_t* timer);

    app::PrinterCommands& commands_;
    // Expires with the screen, so responses arriving later are dropped.
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
    // The source the requests went to; the session (and with it the source) is replaced when
    // the connection settings change.
    app::WebcamSource* source_{nullptr};
    Phase phase_{Phase::NeedList};
    std::vector<app::WebcamInfo> webcams_;
    std::size_t selected_{0};
    bool paused_{false};
    // Bumped whenever a pending response stops being wanted (camera switch, new source).
    std::uint32_t generation_{0};
    bool fetching_{false};
    uint32_t request_started_{0};
    uint32_t retry_at_{0};
    uint32_t next_image_at_{0};
    // Images shown since frames_since_; gives the frame rate shown in the info line.
    uint32_t frames_{0};
    uint32_t frames_since_{0};
    std::string fps_text_;
    // What message_ shows, so the tick does not redraw an unchanged label.
    std::string shown_message_;
    bool message_over_image_{false};

    const lv_font_t* small_font_{nullptr};
    lv_obj_t* frame_box_{nullptr};
    lv_obj_t* image_{nullptr};
    lv_obj_t* message_{nullptr};
    lv_obj_t* info_{nullptr};
    lv_timer_t* tick_timer_{nullptr};
    // The image shows frame_; image_dsc_ points into it, so both change together.
    app::WebcamFrame frame_;
    lv_image_dsc_t image_dsc_{};
};

} // namespace screen

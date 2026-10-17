/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

namespace moonraker {
class MoonrakerSession;
}

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void request_quit();

    // ---- Printer state (published from the Moonraker session) ----
    void update_printer(const moonraker::MoonrakerSession& session);

    lv_subject_t* printer_badge_text_subject();
    lv_subject_t* printer_badge_tone_subject();  // format::Tone
    lv_subject_t* printer_job_subject();
    lv_subject_t* printer_progress_subject();    // permille 0..1000
    lv_subject_t* printer_progress_text_subject();
    lv_subject_t* printer_elapsed_subject();
    lv_subject_t* printer_remaining_subject();
    lv_subject_t* printer_layers_subject();
    lv_subject_t* printer_factors_subject();
    lv_subject_t* printer_message_subject();
    lv_subject_t* nozzle_temperature_subject();
    lv_subject_t* nozzle_target_subject();
    lv_subject_t* nozzle_power_subject();        // percent 0..100
    lv_subject_t* bed_available_subject();
    lv_subject_t* bed_temperature_subject();
    lv_subject_t* bed_target_subject();
    lv_subject_t* bed_power_subject();           // percent 0..100
    lv_subject_t* overlay_visible_subject();
    lv_subject_t* overlay_title_subject();
    lv_subject_t* overlay_detail_subject();
    lv_subject_t* overlay_tone_subject();        // format::Tone
    lv_subject_t* overlay_hint_subject();

private:
    void publish_all();

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;

    reactive::StringSubject<16> printer_badge_text_;
    reactive::IntSubject printer_badge_tone_;
    reactive::StringSubject<96> printer_job_;
    reactive::IntSubject printer_progress_;
    reactive::StringSubject<8> printer_progress_text_;
    reactive::StringSubject<16> printer_elapsed_;
    reactive::StringSubject<16> printer_remaining_;
    reactive::StringSubject<24> printer_layers_;
    reactive::StringSubject<48> printer_factors_;
    reactive::StringSubject<96> printer_message_;
    reactive::StringSubject<12> nozzle_temperature_;
    reactive::StringSubject<12> nozzle_target_;
    reactive::IntSubject nozzle_power_;
    reactive::BoolSubject bed_available_;
    reactive::StringSubject<12> bed_temperature_;
    reactive::StringSubject<12> bed_target_;
    reactive::IntSubject bed_power_;
    reactive::BoolSubject overlay_visible_;
    reactive::StringSubject<48> overlay_title_;
    reactive::StringSubject<256> overlay_detail_;
    reactive::IntSubject overlay_tone_;
    reactive::StringSubject<64> overlay_hint_;
};

} // namespace viewmodel

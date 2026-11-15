/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include "moonraker_session.h"
#include "printer_format.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

int tone_to_int(format::Tone tone) {
    return static_cast<int>(tone);
}

// Subjects notify on every set; only publish real changes to avoid redraws.
template <std::size_t Size>
void set_if_changed(reactive::StringSubject<Size>& subject, const std::string& value) {
    if (std::strncmp(subject.value(), value.c_str(), Size - 1) != 0) {
        subject.set(value.c_str());
    }
}

void set_if_changed(reactive::IntSubject& subject, int32_t value) {
    if (subject.value() != value) {
        subject.set(value);
    }
}

void set_if_changed(reactive::BoolSubject& subject, bool value) {
    if (subject.value() != value) {
        subject.set(value);
    }
}

int32_t to_percent(double fraction) {
    const auto value = static_cast<int32_t>(std::lround(fraction * 100.0));
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model::page_title(model_.current_page())),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      current_layer_subject_(static_cast<int32_t>(model_.current_layer())),
      quit_requested_subject_(false),
      printer_badge_text_("OFFLINE"),
      printer_badge_tone_(tone_to_int(format::Tone::Neutral)),
      printer_job_(format::job_name({}).c_str()),
      printer_file_revision_(0),
      printer_progress_(0),
      printer_progress_text_("0%"),
      printer_elapsed_("--:--"),
      printer_remaining_("--:--"),
      printer_layers_("--"),
      printer_factors_(""),
      printer_message_(""),
      nozzle_temperature_("--"),
      nozzle_target_("off"),
      nozzle_power_(0),
      nozzle_can_extrude_(false),
      bed_available_(true),
      bed_temperature_("--"),
      bed_target_("off"),
      bed_power_(0),
      toolhead_position_("X --  Y --  Z --"),
      homed_axes_(""),
      overlay_visible_(true),
      overlay_title_("Connecting to Moonraker..."),
      overlay_detail_(""),
      overlay_tone_(tone_to_int(format::Tone::Neutral)),
      overlay_hint_(format::ConnectionOverlay{}.hint.c_str()) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::current_layer_subject() {
    return current_layer_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(model::AppPage page) {
    if (model_.nav().show_page(page)) {
        publish_all();
    }
}

std::size_t BaseViewModel::current_layer() const {
    return model_.current_layer();
}

void BaseViewModel::select_nav_slot(std::size_t slot) {
    if (model_.nav().select_slot(slot)) {
        publish_all();
    }
}

void BaseViewModel::next_nav_layer() {
    model_.nav().next_layer();
    publish_all();
}

void BaseViewModel::previous_nav_layer() {
    model_.nav().previous_layer();
    publish_all();
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::update_printer(const moonraker::MoonrakerSession& session) {
    const auto& printer = session.printer();
    const auto state = session.session_state();
    const bool ready = state == moonraker::SessionState::Ready;

    const auto badge = format::status_badge(state, printer);
    set_if_changed(printer_badge_text_, badge.text);
    set_if_changed(printer_badge_tone_, tone_to_int(badge.tone));

    const bool has_job = ready && printer.print_state != model::PrintState::Standby &&
                         printer.print_state != model::PrintState::Unknown;
    set_if_changed(printer_job_, format::job_name(has_job ? printer.filename : std::string()));
    // job_name() drops the directory, so track the full path separately for the thumbnail.
    if (printer_file_.update(printer.filename)) {
        printer_file_revision_.set(printer_file_.revision());
    }

    // Moonraker can report progress outside [0, 1]; keep the subject within its 0-1000 contract.
    double progress = has_job ? printer.progress() : 0.0;
    progress = std::isfinite(progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
    set_if_changed(printer_progress_, static_cast<int32_t>(std::lround(progress * 1000.0)));
    set_if_changed(printer_progress_text_, format::percent(progress));

    const bool active = has_job && (printer.print_state == model::PrintState::Printing ||
                                    printer.print_state == model::PrintState::Paused);
    set_if_changed(printer_elapsed_, format::duration(has_job ? printer.print_duration : -1.0));
    set_if_changed(printer_remaining_, format::duration(active ? printer.estimated_remaining() : -1.0));
    set_if_changed(printer_layers_,
                   has_job ? format::layers(printer.current_layer, printer.total_layer) : std::string("--"));
    set_if_changed(printer_factors_, ready ? format::factors(printer) : std::string());

    std::string message = printer.display_message.empty() ? printer.print_message : printer.display_message;
    set_if_changed(printer_message_, ready ? message : std::string());

    set_if_changed(nozzle_temperature_, ready ? format::temperature(printer.extruder.temperature) : "--");
    set_if_changed(nozzle_target_, ready ? format::target(printer.extruder.target) : "off");
    set_if_changed(nozzle_power_, ready ? to_percent(printer.extruder.power) : 0);
    set_if_changed(nozzle_can_extrude_, ready && printer.extruder.can_extrude);
    set_if_changed(bed_available_, !ready || printer.has_heater_bed);
    set_if_changed(bed_temperature_, ready ? format::temperature(printer.heater_bed.temperature) : "--");
    set_if_changed(bed_target_, ready ? format::target(printer.heater_bed.target) : "off");
    set_if_changed(bed_power_, ready ? to_percent(printer.heater_bed.power) : 0);
    set_if_changed(toolhead_position_, ready ? format::toolhead_position(printer) : "X --  Y --  Z --");
    set_if_changed(homed_axes_, ready ? format::homed_axes(printer.homed_axes) : std::string());

    const auto overlay =
        format::connection_overlay(
            state, printer, session.endpoint(), session.last_error(), session.klippy_unresponsive(),
            !session.klipper_service().empty());
    set_if_changed(overlay_title_, overlay.title);
    set_if_changed(overlay_detail_, overlay.detail);
    set_if_changed(overlay_tone_, tone_to_int(overlay.tone));
    set_if_changed(overlay_hint_, overlay.hint);
    set_if_changed(overlay_visible_, overlay.visible);
}

lv_subject_t* BaseViewModel::printer_badge_text_subject() {
    return printer_badge_text_.native();
}

lv_subject_t* BaseViewModel::printer_badge_tone_subject() {
    return printer_badge_tone_.native();
}

lv_subject_t* BaseViewModel::printer_job_subject() {
    return printer_job_.native();
}

lv_subject_t* BaseViewModel::printer_file_revision_subject() {
    return printer_file_revision_.native();
}

lv_subject_t* BaseViewModel::printer_progress_subject() {
    return printer_progress_.native();
}

lv_subject_t* BaseViewModel::printer_progress_text_subject() {
    return printer_progress_text_.native();
}

lv_subject_t* BaseViewModel::printer_elapsed_subject() {
    return printer_elapsed_.native();
}

lv_subject_t* BaseViewModel::printer_remaining_subject() {
    return printer_remaining_.native();
}

lv_subject_t* BaseViewModel::printer_layers_subject() {
    return printer_layers_.native();
}

lv_subject_t* BaseViewModel::printer_factors_subject() {
    return printer_factors_.native();
}

lv_subject_t* BaseViewModel::printer_message_subject() {
    return printer_message_.native();
}

lv_subject_t* BaseViewModel::nozzle_temperature_subject() {
    return nozzle_temperature_.native();
}

lv_subject_t* BaseViewModel::nozzle_target_subject() {
    return nozzle_target_.native();
}

lv_subject_t* BaseViewModel::nozzle_power_subject() {
    return nozzle_power_.native();
}

lv_subject_t* BaseViewModel::nozzle_can_extrude_subject() {
    return nozzle_can_extrude_.native();
}

lv_subject_t* BaseViewModel::bed_available_subject() {
    return bed_available_.native();
}

lv_subject_t* BaseViewModel::bed_temperature_subject() {
    return bed_temperature_.native();
}

lv_subject_t* BaseViewModel::bed_target_subject() {
    return bed_target_.native();
}

lv_subject_t* BaseViewModel::bed_power_subject() {
    return bed_power_.native();
}

lv_subject_t* BaseViewModel::toolhead_position_subject() {
    return toolhead_position_.native();
}

lv_subject_t* BaseViewModel::homed_axes_subject() {
    return homed_axes_.native();
}

lv_subject_t* BaseViewModel::overlay_visible_subject() {
    return overlay_visible_.native();
}

lv_subject_t* BaseViewModel::overlay_title_subject() {
    return overlay_title_.native();
}

lv_subject_t* BaseViewModel::overlay_detail_subject() {
    return overlay_detail_.native();
}

lv_subject_t* BaseViewModel::overlay_tone_subject() {
    return overlay_tone_.native();
}

lv_subject_t* BaseViewModel::overlay_hint_subject() {
    return overlay_hint_.native();
}

void BaseViewModel::publish_all() {
    title_subject_.set(model::page_title(model_.current_page()));
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
    current_layer_subject_.set(static_cast<int32_t>(model_.current_layer()));
}

} // namespace viewmodel

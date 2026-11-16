/*
 * SPDX-License-Identifier: MIT
 */

#include "printer_format.h"
#include "test_framework.h"

using moonraker::SessionState;
namespace format = viewmodel::format;

TEST_CASE("format: durations") {
    CHECK_EQ(format::duration(-1.0), std::string("--:--"));
    CHECK_EQ(format::duration(0.0), std::string("00:00"));
    CHECK_EQ(format::duration(65.4), std::string("01:05"));
    CHECK_EQ(format::duration(3599.6), std::string("1:00:00"));
    CHECK_EQ(format::duration(3 * 3600 + 25 * 60 + 7), std::string("3:25:07"));
}

TEST_CASE("format: temperatures, percent, layers") {
    CHECK_EQ(format::temperature(209.84), std::string("209.8"));
    CHECK_EQ(format::target(0.0), std::string("off"));
    CHECK_EQ(format::target(60.0), std::string("/ 60"));
    CHECK_EQ(format::percent(0.424), std::string("42%"));
    CHECK_EQ(format::percent(1.5), std::string("100%"));
    CHECK_EQ(format::layers(-1, 10), std::string("--"));
    CHECK_EQ(format::layers(3, 0), std::string("--"));
    CHECK_EQ(format::layers(3, 120), std::string("3 / 120"));
}

TEST_CASE("format: job name and factors") {
    CHECK_EQ(format::job_name(""), std::string("No active print"));
    CHECK_EQ(format::job_name("benchy.gcode"), std::string("benchy.gcode"));
    CHECK_EQ(format::job_name("parts/arm_v2.gcode"), std::string("arm_v2.gcode"));

    model::PrinterState printer;
    printer.speed_factor = 1.0;
    printer.extrude_factor = 0.95;
    printer.fan_speed = 0.5;
    CHECK_EQ(format::factors(printer), std::string("Speed 100%  Flow 95%  Fan 50%"));
}

TEST_CASE("format: file revision follows the full path") {
    format::FileRevision file;
    CHECK_EQ(file.revision(), 0);
    CHECK(!file.update(""));
    CHECK_EQ(file.revision(), 0);

    CHECK(file.update("parts/a/benchy.gcode"));
    CHECK_EQ(file.revision(), 1);
    CHECK(!file.update("parts/a/benchy.gcode"));
    CHECK_EQ(file.revision(), 1);

    // Same job name in another directory is still a different file.
    CHECK_EQ(format::job_name("parts/b/benchy.gcode"), format::job_name("parts/a/benchy.gcode"));
    CHECK(file.update("parts/b/benchy.gcode"));
    CHECK_EQ(file.revision(), 2);
    CHECK(!file.update("parts/b/benchy.gcode"));
    CHECK_EQ(file.revision(), 2);
}

TEST_CASE("format: toolhead position and homed axes") {
    model::PrinterState printer;
    printer.position = {120.0, 80.54, 10.2, 3.0};
    CHECK_EQ(format::toolhead_position(printer), std::string("X 120.0  Y 80.5  Z 10.20"));
    CHECK_EQ(format::homed_axes("xyz"), std::string("XYZ"));
    CHECK_EQ(format::homed_axes(""), std::string(""));
}

TEST_CASE("format: status badge") {
    model::PrinterState printer;
    CHECK_EQ(format::status_badge(SessionState::Connecting, printer).text, std::string("OFFLINE"));
    CHECK(format::status_badge(SessionState::WaitingKlippy, printer).tone == format::Tone::Warning);

    printer.klippy_state = model::KlippyState::Shutdown;
    CHECK_EQ(format::status_badge(SessionState::KlippyError, printer).text, std::string("SHUTDOWN"));

    printer.klippy_state = model::KlippyState::Ready;
    printer.print_state = model::PrintState::Standby;
    CHECK_EQ(format::status_badge(SessionState::Ready, printer).text, std::string("READY"));
    printer.print_state = model::PrintState::Printing;
    const auto printing = format::status_badge(SessionState::Ready, printer);
    CHECK_EQ(printing.text, std::string("PRINTING"));
    CHECK(printing.tone == format::Tone::Info);
    printer.print_state = model::PrintState::Paused;
    CHECK_EQ(format::status_badge(SessionState::Ready, printer).text, std::string("PAUSED"));
}

TEST_CASE("format: connection overlay") {
    model::PrinterState printer;
    CHECK(!format::connection_overlay(SessionState::Ready, printer, "ws://x", "").visible);

    const auto connecting = format::connection_overlay(SessionState::Connecting, printer, "ws://x", "refused");
    CHECK(connecting.visible);
    CHECK_EQ(connecting.detail, std::string("ws://x\nrefused"));
    CHECK(connecting.tone == format::Tone::Warning);
    // Restarting Klipper needs Moonraker, so only settings are offered here.
    CHECK_EQ(connecting.hint, std::string("S: connection settings"));

    printer.klippy_state = model::KlippyState::Error;
    printer.state_message = "MCU 'mcu' shutdown: Timer too close";
    const auto error = format::connection_overlay(SessionState::KlippyError, printer, "ws://x", "");
    CHECK_EQ(error.title, std::string("Klipper error"));
    CHECK_EQ(error.detail, printer.state_message);
    CHECK(error.tone == format::Tone::Error);
    CHECK_EQ(error.hint, std::string("R: restart  F: firmware restart  S: settings"));

    printer.klippy_state = model::KlippyState::Startup;
    const auto waiting = format::connection_overlay(SessionState::WaitingKlippy, printer, "ws://x", "");
    CHECK_EQ(waiting.hint, error.hint);

    const auto stuck = format::connection_overlay(SessionState::WaitingKlippy, printer, "ws://x", "", true);
    CHECK_EQ(stuck.title, std::string("Klipper not responding"));
    CHECK_EQ(stuck.detail, std::string("Klippy state: startup"));
    CHECK(stuck.tone == format::Tone::Error);
    CHECK_EQ(stuck.hint, std::string("R: restart Klipper service  S: settings"));

    // Klippy hung while ready: the overlay covers the stale status.
    printer.klippy_state = model::KlippyState::Ready;
    const auto hung = format::connection_overlay(SessionState::Ready, printer, "ws://x", "", true);
    CHECK(hung.visible);
    CHECK_EQ(hung.title, std::string("Klipper not responding"));
    CHECK_EQ(hung.detail, std::string("No reply to the restart"));
    CHECK_EQ(hung.hint, stuck.hint);

    // Without the service name the app cannot restart it, so it does not offer to.
    const auto unknown_service =
        format::connection_overlay(SessionState::Ready, printer, "ws://x", "", true, false);
    CHECK_EQ(unknown_service.title, std::string("Klipper not responding"));
    CHECK_EQ(unknown_service.hint, std::string("Restart Klipper on its host  S: settings"));

    // Without a connection to Moonraker the flag cannot apply.
    CHECK_EQ(format::connection_overlay(SessionState::Connecting, printer, "ws://x", "", true).hint,
             std::string("S: connection settings"));
}

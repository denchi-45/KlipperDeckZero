/*
 * SPDX-License-Identifier: MIT
 */

#include "file_browser.h"
#include "file_list_parser.h"
#include "test_framework.h"

#include <string>
#include <vector>

namespace {

using app::FileBrowser;
using app::GcodeFile;
using app::PanelInput;
using Mode = FileBrowser::Mode;
using Result = FileBrowser::Result;

std::vector<GcodeFile> numbered_files(int count) {
    std::vector<GcodeFile> files;
    for (int i = 0; i < count; ++i) {
        files.push_back({"file" + std::to_string(i) + ".gcode", 1000.0 - i, 100});
    }
    return files;
}

} // namespace

TEST_CASE("files: the list is parsed newest first and skips entries without a path") {
    const auto result = moonraker::Json::parse(R"([
        {"path": "old.gcode", "modified": 100.5, "size": 2048},
        {"modified": 300.0, "size": 1},
        {"path": "parts/new.gcode", "modified": 200.0, "size": 5242880},
        "not an object"
    ])");
    const auto files = app::parse_file_list(result);
    CHECK(files.size() == 2);
    CHECK(files[0].path == "parts/new.gcode");
    CHECK(files[0].size == 5242880);
    CHECK(files[1].path == "old.gcode");
    CHECK(app::parse_file_list(moonraker::Json::object()).empty());
}

TEST_CASE("files: metadata keeps unknown fields unknown") {
    const auto metadata = app::parse_metadata(moonraker::Json::parse(R"({
        "estimated_time": 3723, "filament_total": 4520.0, "slicer": "PrusaSlicer",
        "slicer_version": "2.8.1", "layer_height": null, "first_layer_extr_temp": 215
    })"));
    CHECK(metadata.estimated_time == 3723.0);
    CHECK(metadata.filament_total == 4520.0);
    CHECK(metadata.layer_height < 0.0);
    CHECK(metadata.object_height < 0.0);
    CHECK(metadata.nozzle_temperature == 215.0);
    CHECK(metadata.bed_temperature < 0.0);
}

TEST_CASE("files: sizes and lengths are formatted for the list") {
    CHECK(app::format_size(512) == "512 B");
    CHECK(app::format_size(2048) == "2 KB");
    CHECK(app::format_size(5 * 1024 * 1024 + 300 * 1024) == "5.3 MB");
    CHECK(app::format_filament(4520.0) == "4.52 m");
    CHECK(app::format_filament(-1.0) == "--");
    CHECK(app::format_millimeters(0.2) == "0.20 mm");
}

TEST_CASE("files: selection scrolls a window of visible rows") {
    FileBrowser browser;
    CHECK(browser.mode() == Mode::Loading);
    browser.set_files(numbered_files(12));
    CHECK(browser.mode() == Mode::List);
    CHECK(browser.selected() == 0);

    for (std::size_t i = 0; i < FileBrowser::kVisibleRows; ++i) {
        browser.handle(PanelInput::Down);
    }
    CHECK(browser.selected() == FileBrowser::kVisibleRows);
    CHECK(browser.top() == 1);

    browser.handle(PanelInput::Right);
    browser.handle(PanelInput::Right);
    CHECK(browser.selected() == 11);
    CHECK(browser.top() == 12 - FileBrowser::kVisibleRows);

    browser.handle(PanelInput::Left);
    CHECK(browser.selected() == 11 - FileBrowser::kVisibleRows);
    browser.handle(PanelInput::Up);
    CHECK(browser.top() == browser.selected());
}

TEST_CASE("files: enter opens details, enter again starts the print, escape goes back") {
    FileBrowser browser;
    browser.set_files(numbered_files(3));
    browser.handle(PanelInput::Down);
    CHECK(browser.handle(PanelInput::Enter) == Result::OpenDetails);
    CHECK(browser.mode() == Mode::Details);
    CHECK(browser.details_state() == FileBrowser::DetailsState::Loading);

    // A late response for another file is ignored.
    app::GcodeMetadata other;
    other.estimated_time = 1.0;
    browser.set_metadata("file0.gcode", &other);
    CHECK(browser.details_state() == FileBrowser::DetailsState::Loading);

    app::GcodeMetadata metadata;
    metadata.estimated_time = 60.0;
    browser.set_metadata("file1.gcode", &metadata);
    CHECK(browser.details_state() == FileBrowser::DetailsState::Loaded);
    CHECK(browser.metadata().estimated_time == 60.0);

    CHECK(browser.handle(PanelInput::Enter) == Result::StartPrint);
    CHECK(browser.selected_file()->path == "file1.gcode");

    CHECK(browser.handle(PanelInput::Escape) == Result::Handled);
    CHECK(browser.mode() == Mode::List);
    CHECK(browser.handle(PanelInput::Escape) == Result::Ignored);
}

TEST_CASE("files: a file without metadata can still be printed") {
    FileBrowser browser;
    browser.set_files(numbered_files(1));
    browser.handle(PanelInput::Enter);
    browser.set_metadata("file0.gcode", nullptr);
    CHECK(browser.details_state() == FileBrowser::DetailsState::Unavailable);
    CHECK(browser.handle(PanelInput::Enter) == Result::StartPrint);
}

TEST_CASE("files: digits switch pages in every mode") {
    FileBrowser browser;
    CHECK(browser.handle(PanelInput::Digit) == Result::Ignored);
    browser.set_files(numbered_files(2));
    CHECK(browser.handle(PanelInput::Digit) == Result::Ignored);
    browser.handle(PanelInput::Enter);
    CHECK(browser.handle(PanelInput::Digit) == Result::Ignored);
}

TEST_CASE("files: a failed load is retried with enter") {
    FileBrowser browser;
    browser.set_failed("Not connected");
    CHECK(browser.mode() == Mode::Failed);
    CHECK(browser.error() == "Not connected");
    CHECK(browser.handle(PanelInput::Enter) == Result::Reload);
    CHECK(browser.mode() == Mode::Loading);
}

TEST_CASE("files: reloading keeps the selected file, and an empty list ignores enter") {
    FileBrowser browser;
    browser.set_files(numbered_files(8));
    for (int i = 0; i < 6; ++i) {
        browser.handle(PanelInput::Down);
    }
    auto files = numbered_files(8);
    files.insert(files.begin(), GcodeFile{"brand_new.gcode", 2000.0, 1});
    browser.set_files(files);
    CHECK(browser.selected_file()->path == "file6.gcode");
    CHECK(browser.selected() >= browser.top());
    CHECK(browser.selected() < browser.top() + FileBrowser::kVisibleRows);

    browser.set_files({});
    CHECK(browser.handle(PanelInput::Enter) == Result::Handled);
    CHECK(browser.mode() == Mode::List);
    CHECK(browser.selected_file() == nullptr);
}

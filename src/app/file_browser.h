/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gcode_files.h"
#include "panel_input.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {

// Keyboard browsing of the G-code files, independent of LVGL.
// The list scrolls in a window of kVisibleRows. Enter opens the details of the selected file,
// and Enter again starts printing it.
class FileBrowser {
public:
    enum class Mode {
        Loading,
        Failed,  // error() says why; Enter retries
        List,
        Details, // the selected file, with its metadata once loaded
    };

    enum class DetailsState {
        Loading,
        Loaded,
        Unavailable,
    };

    enum class Result {
        Ignored,     // not used by the browser; the caller may treat it as a shortcut
        Handled,
        Reload,      // fetch the file list again
        OpenDetails, // fetch the metadata of selected_file()
        StartPrint,  // print selected_file()
    };

    static constexpr std::size_t kVisibleRows = 5;

    void begin_loading();
    // Keeps the selected file selected when it is still in the list.
    void set_files(std::vector<GcodeFile> files);
    void set_failed(std::string error);
    // Ignored unless path is the file whose details are shown.
    void set_metadata(const std::string& path, const GcodeMetadata* metadata);

    Result handle(PanelInput input);

    Mode mode() const;
    const std::vector<GcodeFile>& files() const;
    std::size_t selected() const;
    // Index of the first visible row.
    std::size_t top() const;
    const GcodeFile* selected_file() const;
    DetailsState details_state() const;
    const GcodeMetadata& metadata() const;
    const std::string& error() const;

private:
    void select(std::size_t index);

    Mode mode_{Mode::Loading};
    std::vector<GcodeFile> files_;
    std::size_t selected_{0};
    std::size_t top_{0};
    DetailsState details_state_{DetailsState::Loading};
    GcodeMetadata metadata_;
    std::string error_;
};

} // namespace app

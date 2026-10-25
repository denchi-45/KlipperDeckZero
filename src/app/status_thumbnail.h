/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "printer_state.h"

#include <string>

namespace app {

// Which file's thumbnail the status page shows, independent of LVGL.
// A thumbnail is wanted while a file is printing or paused; it is fetched once per file and
// dropped when the print ends or the file changes.
class StatusThumbnail {
public:
    enum class Action {
        None,
        Fetch, // fetch the thumbnail of file()
        Clear, // drop the shown thumbnail
    };

    // Called with the latest print state and print_stats.filename.
    Action update(model::PrintState state, const std::string& filename);

    // The printing file whose thumbnail is wanted; empty when none is.
    const std::string& file() const;
    // True while a response for path is still wanted.
    bool wants(const std::string& path) const;
    // The PNG of the file at path (empty when it could not be read). Ignored unless path is
    // still file(), so a late response for an earlier print shows nothing.
    void set_thumbnail(const std::string& path, std::string png);
    // PNG bytes of file()'s thumbnail; empty until it is loaded.
    const std::string& thumbnail() const;

private:
    std::string file_;
    std::string thumbnail_;
};

} // namespace app

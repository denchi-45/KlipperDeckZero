/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "printer_state.h"

#include <cstdint>
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
    // Changes whenever file() does, including when the same file is printed again after the
    // print ended. A fetch keeps the generation it started in.
    std::uint32_t generation() const;
    // True while a response of a fetch started in generation is still wanted.
    bool wants(std::uint32_t generation) const;
    // The PNG fetched in generation (empty when it could not be read). Ignored unless that is
    // still generation(), so a late response for an earlier print shows nothing.
    void set_thumbnail(std::uint32_t generation, std::string png);
    // PNG bytes of file()'s thumbnail; empty until it is loaded.
    const std::string& thumbnail() const;

private:
    std::string file_;
    std::uint32_t generation_{0};
    std::string thumbnail_;
};

} // namespace app

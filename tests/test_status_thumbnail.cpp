/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_files.h"
#include "status_thumbnail.h"
#include "test_framework.h"

#include <string>

namespace {

using app::StatusThumbnail;
using Action = StatusThumbnail::Action;
using model::PrintState;

// The 8-byte signature and the IHDR chunk up to the height: what has_png_header() needs.
const std::string kPngStart = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\0\0\x0dIHDR", 8) +
                              std::string("\0\0\0\x20\0\0\0\x20", 8);

} // namespace

TEST_CASE("status thumbnail: fetched once when a print starts") {
    StatusThumbnail thumbnail;
    CHECK(thumbnail.update(PrintState::Standby, "") == Action::None);
    CHECK(thumbnail.update(PrintState::Printing, "benchy.gcode") == Action::Fetch);
    CHECK_EQ(thumbnail.file(), std::string("benchy.gcode"));
    const auto generation = thumbnail.generation();
    CHECK(thumbnail.wants(generation));
    // Later updates of the same print fetch nothing, paused or not.
    CHECK(thumbnail.update(PrintState::Printing, "benchy.gcode") == Action::None);
    CHECK(thumbnail.update(PrintState::Paused, "benchy.gcode") == Action::None);
    CHECK(thumbnail.update(PrintState::Printing, "benchy.gcode") == Action::None);
    CHECK(thumbnail.wants(generation));
}

TEST_CASE("status thumbnail: nothing is wanted while idle or without a file name") {
    StatusThumbnail thumbnail;
    CHECK(thumbnail.update(PrintState::Printing, "") == Action::None);
    CHECK(thumbnail.update(PrintState::Complete, "benchy.gcode") == Action::None);
    CHECK(thumbnail.update(PrintState::Unknown, "benchy.gcode") == Action::None);
    CHECK(thumbnail.file().empty());
    CHECK(!thumbnail.wants(thumbnail.generation()));
    thumbnail.set_thumbnail(thumbnail.generation(), kPngStart);
    CHECK(thumbnail.thumbnail().empty());
}

TEST_CASE("status thumbnail: cleared when the print ends") {
    StatusThumbnail thumbnail;
    thumbnail.update(PrintState::Printing, "benchy.gcode");
    const auto generation = thumbnail.generation();
    thumbnail.set_thumbnail(generation, kPngStart);
    CHECK_EQ(thumbnail.thumbnail(), kPngStart);

    CHECK(thumbnail.update(PrintState::Complete, "benchy.gcode") == Action::Clear);
    CHECK(thumbnail.thumbnail().empty());
    CHECK(!thumbnail.wants(generation));
    CHECK(thumbnail.update(PrintState::Standby, "") == Action::None);

    // Printing the same file again fetches it again.
    CHECK(thumbnail.update(PrintState::Printing, "benchy.gcode") == Action::Fetch);
}

TEST_CASE("status thumbnail: a new file drops the old image and ignores late responses") {
    StatusThumbnail thumbnail;
    thumbnail.update(PrintState::Printing, "a.gcode");
    const auto a = thumbnail.generation();
    thumbnail.set_thumbnail(a, kPngStart);

    CHECK(thumbnail.update(PrintState::Printing, "parts/b.gcode") == Action::Fetch);
    const auto b = thumbnail.generation();
    CHECK(thumbnail.thumbnail().empty());
    CHECK(!thumbnail.wants(a));

    // The response for a.gcode arrives after the switch.
    thumbnail.set_thumbnail(a, kPngStart);
    CHECK(thumbnail.thumbnail().empty());
    thumbnail.set_thumbnail(b, kPngStart + "rest");
    CHECK_EQ(thumbnail.thumbnail(), kPngStart + "rest");
}

TEST_CASE("status thumbnail: losing the printer clears, reconnecting fetches again") {
    StatusThumbnail thumbnail;
    thumbnail.update(PrintState::Printing, "a.gcode");
    // The status page passes Unknown while the printer is not ready.
    CHECK(thumbnail.update(PrintState::Unknown, "") == Action::Clear);
    CHECK(thumbnail.update(PrintState::Paused, "a.gcode") == Action::Fetch);
}

TEST_CASE("status thumbnail: restarting the same file ignores the previous print's responses") {
    StatusThumbnail thumbnail;
    thumbnail.update(PrintState::Printing, "benchy.gcode");
    const auto first = thumbnail.generation();

    // The print is cancelled and started again before the first fetch answered.
    CHECK(thumbnail.update(PrintState::Cancelled, "benchy.gcode") == Action::Clear);
    CHECK(thumbnail.update(PrintState::Printing, "benchy.gcode") == Action::Fetch);
    const auto second = thumbnail.generation();
    CHECK(second != first);
    CHECK_EQ(thumbnail.file(), std::string("benchy.gcode"));

    // Late metadata or image responses of the first print are no longer wanted.
    CHECK(!thumbnail.wants(first));
    thumbnail.set_thumbnail(first, kPngStart);
    CHECK(thumbnail.thumbnail().empty());

    CHECK(thumbnail.wants(second));
    thumbnail.set_thumbnail(second, kPngStart + "new");
    CHECK_EQ(thumbnail.thumbnail(), kPngStart + "new");
}

TEST_CASE("png header: signature and image size are required") {
    CHECK(app::has_png_header(kPngStart));
    CHECK(app::has_png_header(kPngStart + "IDAT..."));
    CHECK(!app::has_png_header(""));
    CHECK(!app::has_png_header(kPngStart.substr(0, 23)));
    CHECK(!app::has_png_header("GIF89a" + kPngStart));
}

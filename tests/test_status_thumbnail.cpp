/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_files.h"
#include "status_thumbnail.h"
#include "test_framework.h"

#include <cstdint>
#include <string>

namespace {

using app::StatusThumbnail;
using Action = StatusThumbnail::Action;
using model::PrintState;

// The 8-byte signature and the IHDR chunk up to the height: what has_png_header() needs.
const std::string kPngStart = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\0\0\x0dIHDR", 8) +
                              std::string("\0\0\0\x20\0\0\0\x20", 8);

void append_u32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += static_cast<char>((value >> shift) & 0xff);
    }
}

std::uint32_t crc32(const std::string& bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void append_chunk(std::string& out, const std::string& type, const std::string& data) {
    append_u32(out, static_cast<std::uint32_t>(data.size()));
    out += type + data;
    append_u32(out, crc32(type + data));
}

// A grayscale PNG file of the given IHDR. libpng's header read stops at the image data, so the
// one IDAT (a single black pixel) does not have to match the size.
std::string png_file(std::uint32_t width, std::uint32_t height, char bit_depth = 8) {
    std::string ihdr;
    append_u32(ihdr, width);
    append_u32(ihdr, height);
    ihdr += std::string{bit_depth, 0, 0, 0, 0};
    std::string png("\x89PNG\r\n\x1a\n", 8);
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", std::string("\x78\x9c\x63\x60\x00\x00\x00\x02\x00\x01", 10));
    append_chunk(png, "IEND", "");
    return png;
}

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
    CHECK(app::has_png_header(png_file(32, 32)));
    CHECK(!app::has_png_header(""));
    CHECK(!app::has_png_header(kPngStart.substr(0, 23)));
    CHECK(!app::has_png_header("GIF89a" + png_file(32, 32)));
}

TEST_CASE("png header: libpng has to accept the header") {
    // Signature and size alone pass the decoder's size check but not libpng.
    CHECK(!app::has_png_header(kPngStart + "IDAT..."));
    // IHDR with a wrong checksum, cut off before the image data, zero width, bit depth 3.
    auto bad_crc = png_file(32, 32);
    bad_crc[29] ^= 0x01;
    CHECK(!app::has_png_header(bad_crc));
    CHECK(!app::has_png_header(png_file(32, 32).substr(0, 33)));
    CHECK(!app::has_png_header(png_file(0, 32)));
    CHECK(!app::has_png_header(png_file(32, 32, 3)));
}

TEST_CASE("png header: images bigger than a thumbnail are refused") {
    CHECK(app::has_png_header(png_file(1024, 1024)));
    CHECK(!app::has_png_header(png_file(1025, 300)));
    CHECK(!app::has_png_header(png_file(300, 70000)));
}

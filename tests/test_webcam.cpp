/*
 * SPDX-License-Identifier: MIT
 */

#include "http_fetcher.h"
#include "test_framework.h"
#include "webcam.h"
#include "webcam_frame.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <jpeglib.h>

using nlohmann::json;

namespace {

// A JPEG of width x height: the left half red, the right half blue.
std::string make_jpeg(int width, int height) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* p = &rgb[(static_cast<std::size_t>(y) * width + x) * 3];
            const bool left = x < width / 2;
            p[0] = left ? 255 : 0;
            p[1] = 0;
            p[2] = left ? 0 : 255;
        }
    }
    jpeg_compress_struct cinfo;
    jpeg_error_mgr errors;
    cinfo.err = jpeg_std_error(&errors);
    jpeg_create_compress(&cinfo);
    unsigned char* buffer = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&cinfo, &buffer, &size);
    cinfo.image_width = static_cast<JDIMENSION>(width);
    cinfo.image_height = static_cast<JDIMENSION>(height);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = &rgb[static_cast<std::size_t>(cinfo.next_scanline) * width * 3];
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    std::string jpeg(reinterpret_cast<const char*>(buffer), size);
    jpeg_destroy_compress(&cinfo);
    std::free(buffer);
    return jpeg;
}

std::uint32_t pixel(const app::WebcamFrame& frame, int x, int y) {
    return frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
}

bool is_red(std::uint32_t argb) {
    return ((argb >> 16) & 0xFF) > 200 && (argb & 0xFF) < 60;
}

bool is_blue(std::uint32_t argb) {
    return (argb & 0xFF) > 200 && ((argb >> 16) & 0xFF) < 60;
}

} // namespace

TEST_CASE("webcam: the list keeps enabled webcams with their settings") {
    const auto result = json::parse(R"({"webcams": [
        {"name": "front", "enabled": true, "service": "mjpegstreamer-adaptive",
         "snapshot_url": "/webcam/?action=snapshot", "stream_url": "/webcam/?action=stream",
         "flip_horizontal": true, "flip_vertical": false, "rotation": 270, "target_fps": 15},
        {"name": "off", "enabled": false, "snapshot_url": "/webcam2/?action=snapshot"},
        {"name": "side", "rotation": -90, "target_fps": "fast"},
        "garbage"
    ]})");
    const auto webcams = app::parse_webcams(result);
    CHECK_EQ(webcams.size(), std::size_t{2});
    CHECK_EQ(webcams[0].name, std::string("front"));
    CHECK_EQ(webcams[0].service, std::string("mjpegstreamer-adaptive"));
    CHECK_EQ(webcams[0].snapshot_url, std::string("/webcam/?action=snapshot"));
    CHECK(webcams[0].flip_horizontal);
    CHECK(!webcams[0].flip_vertical);
    CHECK_EQ(webcams[0].rotation, 270);
    CHECK_EQ(webcams[0].target_fps, 15);
    // A missing "enabled" counts as enabled; odd values fall back to the defaults.
    CHECK_EQ(webcams[1].name, std::string("side"));
    CHECK_EQ(webcams[1].rotation, 270);
    CHECK_EQ(webcams[1].target_fps, 0);
}

TEST_CASE("webcam: a result without a webcam list gives none") {
    CHECK(app::parse_webcams(json::parse(R"({})")).empty());
    CHECK(app::parse_webcams(json::parse(R"({"webcams": {}})")).empty());
    CHECK(app::parse_webcams(json::parse(R"([])")).empty());
}

TEST_CASE("webcam: snapshot URLs are resolved against the printer host") {
    app::WebcamInfo webcam;
    webcam.snapshot_url = "/webcam/?action=snapshot";
    CHECK_EQ(app::webcam_snapshot_url(webcam, "192.168.1.50"),
             std::string("http://192.168.1.50/webcam/?action=snapshot"));
    CHECK_EQ(app::webcam_snapshot_url(webcam, "::1"), std::string("http://[::1]/webcam/?action=snapshot"));

    webcam.snapshot_url = "webcam/snapshot";
    CHECK_EQ(app::webcam_snapshot_url(webcam, "printer.local"), std::string("http://printer.local/webcam/snapshot"));

    webcam.snapshot_url = "http://10.0.0.5:8080/?action=snapshot";
    CHECK_EQ(app::webcam_snapshot_url(webcam, "192.168.1.50"), webcam.snapshot_url);

    webcam.snapshot_url = "//10.0.0.5/snap.jpg";
    CHECK_EQ(app::webcam_snapshot_url(webcam, "192.168.1.50"), std::string("http://10.0.0.5/snap.jpg"));
}

TEST_CASE("webcam: an mjpg-streamer stream URL gives the snapshot URL") {
    app::WebcamInfo webcam;
    webcam.stream_url = "/webcam/?action=stream";
    CHECK_EQ(app::webcam_snapshot_url(webcam, "127.0.0.1"), std::string("http://127.0.0.1/webcam/?action=snapshot"));

    webcam.stream_url = "/webcam/webrtc";
    CHECK(app::webcam_snapshot_url(webcam, "127.0.0.1").empty());
}

TEST_CASE("webcam: images are fitted into the box with their aspect ratio") {
    int width = 0;
    int height = 0;
    app::fit_webcam_frame(640, 480, 320, 120, width, height);
    CHECK_EQ(width, 160);
    CHECK_EQ(height, 120);
    app::fit_webcam_frame(1920, 1080, 320, 120, width, height);
    CHECK_EQ(width, 213);
    CHECK_EQ(height, 120);
    app::fit_webcam_frame(1000, 100, 320, 120, width, height);
    CHECK_EQ(width, 320);
    CHECK_EQ(height, 32);
    app::fit_webcam_frame(0, 480, 320, 120, width, height);
    CHECK_EQ(width, 0);
    CHECK_EQ(height, 0);
}

TEST_CASE("webcam: a JPEG is decoded and shrunk into the box") {
    const auto jpeg = make_jpeg(640, 320);
    app::WebcamFrameLayout layout;
    layout.box_width = 200;
    layout.box_height = 200;
    app::WebcamFrame frame;
    std::string error;
    CHECK(app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK(error.empty());
    CHECK_EQ(frame.width, 200);
    CHECK_EQ(frame.height, 100);
    CHECK_EQ(frame.pixels.size(), std::size_t{200 * 100});
    CHECK(is_red(pixel(frame, 10, 50)));
    CHECK(is_blue(pixel(frame, 190, 50)));
    CHECK_EQ(pixel(frame, 10, 50) >> 24, 0xFFu);
}

TEST_CASE("webcam: rotation and flips turn the image") {
    const auto jpeg = make_jpeg(64, 32);
    app::WebcamFrameLayout layout;
    layout.box_width = 64;
    layout.box_height = 64;
    app::WebcamFrame frame;
    std::string error;

    // Turned clockwise, the left (red) half ends up on top.
    layout.rotation = 90;
    CHECK(app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK_EQ(frame.width, 32);
    CHECK_EQ(frame.height, 64);
    CHECK(is_red(pixel(frame, 16, 4)));
    CHECK(is_blue(pixel(frame, 16, 60)));

    layout.rotation = 270;
    CHECK(app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK(is_blue(pixel(frame, 16, 4)));
    CHECK(is_red(pixel(frame, 16, 60)));

    layout.rotation = 180;
    CHECK(app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK_EQ(frame.width, 64);
    CHECK(is_blue(pixel(frame, 4, 16)));
    CHECK(is_red(pixel(frame, 60, 16)));

    layout.rotation = 0;
    layout.flip_horizontal = true;
    CHECK(app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK(is_blue(pixel(frame, 4, 16)));
    CHECK(is_red(pixel(frame, 60, 16)));
}

TEST_CASE("webcam: broken images are refused") {
    app::WebcamFrameLayout layout;
    layout.box_width = 100;
    layout.box_height = 100;
    app::WebcamFrame frame;
    std::string error;
    CHECK(!app::decode_webcam_jpeg("<html>502 Bad Gateway</html>", layout, frame, error));
    CHECK(!error.empty());

    const auto jpeg = make_jpeg(64, 32);
    error.clear();
    CHECK(!app::decode_webcam_jpeg(jpeg.substr(0, 40), layout, frame, error));
    CHECK(!error.empty());

    layout.box_width = 0;
    error.clear();
    CHECK(!app::decode_webcam_jpeg(jpeg, layout, frame, error));
    CHECK(!error.empty());
}

TEST_CASE("webcam: http URLs are split into host, port and target") {
    moonraker::HttpUrl url;
    std::string error;
    CHECK(moonraker::parse_http_url("http://192.168.1.50/webcam/?action=snapshot", url, error));
    CHECK_EQ(url.host, std::string("192.168.1.50"));
    CHECK_EQ(url.port, 80);
    CHECK_EQ(url.target, std::string("/webcam/?action=snapshot"));

    CHECK(moonraker::parse_http_url("HTTP://printer.local:8080?action=snapshot#x", url, error));
    CHECK_EQ(url.host, std::string("printer.local"));
    CHECK_EQ(url.port, 8080);
    CHECK_EQ(url.target, std::string("/?action=snapshot"));

    CHECK(moonraker::parse_http_url("http://[fe80::1]:81", url, error));
    CHECK_EQ(url.host, std::string("fe80::1"));
    CHECK_EQ(url.port, 81);
    CHECK_EQ(url.target, std::string("/"));

    CHECK(moonraker::parse_http_url("http://user:pw@cam/snap.jpg", url, error));
    CHECK_EQ(url.host, std::string("cam"));

    CHECK(!moonraker::parse_http_url("https://cam/snap.jpg", url, error));
    CHECK_EQ(error, std::string("https is not supported"));
    CHECK(!moonraker::parse_http_url("/webcam/?action=snapshot", url, error));
    CHECK(!moonraker::parse_http_url("http://cam:99999/", url, error));
    CHECK(!moonraker::parse_http_url("http://:80/", url, error));
}

TEST_CASE("webcam: HTTP/1.0 and 1.1 status lines are read") {
    CHECK_EQ(moonraker::parse_http_status("HTTP/1.0 200 OK\r\n"), 200);
    CHECK_EQ(moonraker::parse_http_status("HTTP/1.1 404 Not Found\r\n"), 404);
    CHECK_EQ(moonraker::parse_http_status("HTTP/1.1 302\r\n"), 302);
    CHECK_EQ(moonraker::parse_http_status("HTTP/2 200\r\n"), -1);
    CHECK_EQ(moonraker::parse_http_status("<html>"), -1);
    CHECK_EQ(moonraker::parse_http_status("HTTP/1.1 20x OK"), -1);
}

TEST_CASE("webcam: redirect locations are resolved against the URL they came from") {
    moonraker::HttpUrl from;
    from.host = "printer.local";
    from.port = 8080;
    from.target = "/webcam/dir/page?action=snapshot";
    const auto resolve = [&from](const char* location) { return moonraker::resolve_http_location(from, location); };
    CHECK_EQ(resolve("/snapshot.jpg"), std::string("http://printer.local:8080/snapshot.jpg"));
    CHECK_EQ(resolve("http://cam/a.jpg"), std::string("http://cam/a.jpg"));
    CHECK_EQ(resolve("https://cam/a.jpg"), std::string("https://cam/a.jpg"));
    CHECK_EQ(resolve("//camera.local/snapshot"), std::string("http://camera.local/snapshot"));
    CHECK_EQ(resolve("snapshot.jpg"), std::string("http://printer.local:8080/webcam/dir/snapshot.jpg"));
    CHECK_EQ(resolve("?action=snapshot&n=2"), std::string("http://printer.local:8080/webcam/dir/page?action=snapshot&n=2"));
    CHECK_EQ(resolve("../snap.jpg?x=1"), std::string("http://printer.local:8080/webcam/snap.jpg?x=1"));
    CHECK_EQ(resolve("./a/../b.jpg"), std::string("http://printer.local:8080/webcam/dir/b.jpg"));
    CHECK_EQ(resolve("../../../../top.jpg"), std::string("http://printer.local:8080/top.jpg"));
    CHECK(resolve("").empty());
    CHECK(resolve("#only-a-fragment").empty());
}

TEST_CASE("webcam: chunk sizes and Content-Length must be well formed") {
    std::uint64_t value = 99;
    CHECK(moonraker::parse_chunk_size("1a2B\r\n", value));
    CHECK_EQ(value, std::uint64_t{0x1a2b});
    CHECK(moonraker::parse_chunk_size("10;name=value\r\n", value));
    CHECK_EQ(value, std::uint64_t{16});
    CHECK(moonraker::parse_chunk_size("0 ;x\r\n", value));
    CHECK_EQ(value, std::uint64_t{0});
    CHECK(!moonraker::parse_chunk_size("\r\n", value));
    CHECK(!moonraker::parse_chunk_size("zz\r\n", value));
    CHECK(!moonraker::parse_chunk_size("12 34\r\n", value));
    CHECK(!moonraker::parse_chunk_size("ffffffffffffffffff\r\n", value));

    CHECK(moonraker::parse_content_length(" 16260 ", value));
    CHECK_EQ(value, std::uint64_t{16260});
    CHECK(!moonraker::parse_content_length("", value));
    CHECK(!moonraker::parse_content_length("-1", value));
    CHECK(!moonraker::parse_content_length("12abc", value));
}

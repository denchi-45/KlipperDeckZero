/*
 * SPDX-License-Identifier: MIT
 */

#include "webcam_frame.h"

#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstdio>

// jpeglib.h needs FILE and size_t declared first.
#include <jpeglib.h>

namespace app {
namespace {

// A webcam still is tens to a few hundred KB.
constexpr std::size_t kMaxJpegBytes = 8 * 1024 * 1024;
// Limits the memory of one decoded image after DCT scaling.
constexpr std::int64_t kMaxDecodedPixels = 4 * 1024 * 1024;
constexpr int kMaxBoxSide = 2048;

struct ErrorManager {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void on_error_exit(j_common_ptr cinfo) {
    auto* manager = reinterpret_cast<ErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, manager->message);
    std::longjmp(manager->jump, 1);
}

// libjpeg prints warnings (e.g. a truncated image) to stderr by default.
void on_output_message(j_common_ptr) {}

int ceil_div(int value, int divisor) {
    return (value + divisor - 1) / divisor;
}

// Decodes into rgb, 3 bytes per pixel. libjpeg reports errors with longjmp, so no object with a
// destructor is created in this function after setjmp(); rgb and error belong to the caller.
// swap_axes: the image is shown turned by 90 or 270 degrees, so the box is compared with its
// height for width.
bool decode_rgb(const std::string& jpeg, int box_width, int box_height, bool swap_axes,
                std::vector<unsigned char>& rgb, int& width, int& height, std::string& error) {
    jpeg_decompress_struct cinfo;
    ErrorManager errors;
    cinfo.err = jpeg_std_error(&errors.base);
    errors.base.error_exit = on_error_exit;
    errors.base.output_message = on_output_message;
    errors.message[0] = '\0';

    if (setjmp(errors.jump)) {
        jpeg_destroy_decompress(&cinfo);
        error = errors.message[0] ? errors.message : "invalid JPEG";
        return false;
    }

    jpeg_create_decompress(&cinfo);
    // Older jpeglib.h declares the buffer non-const; libjpeg only reads it.
    jpeg_mem_src(&cinfo,
                 reinterpret_cast<unsigned char*>(const_cast<char*>(jpeg.data())),
                 static_cast<unsigned long>(jpeg.size()));
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        error = "no JPEG image";
        return false;
    }

    const int source_width = static_cast<int>(cinfo.image_width);
    const int source_height = static_cast<int>(cinfo.image_height);
    const int shown_width = swap_axes ? source_height : source_width;
    const int shown_height = swap_axes ? source_width : source_height;
    int fit_width = 0;
    int fit_height = 0;
    fit_webcam_frame(shown_width, shown_height, box_width, box_height, fit_width, fit_height);

    // The largest DCT scaling that still leaves at least the fitted size to sample from.
    int denominator = 1;
    for (const int candidate : {8, 4, 2}) {
        if (ceil_div(shown_width, candidate) >= fit_width && ceil_div(shown_height, candidate) >= fit_height) {
            denominator = candidate;
            break;
        }
    }
    cinfo.scale_num = 1;
    cinfo.scale_denom = static_cast<unsigned int>(denominator);
    cinfo.out_color_space = JCS_RGB;
    // The image is shrunk afterwards anyway; trade a little quality for speed on the CM0.
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = FALSE;

    jpeg_calc_output_dimensions(&cinfo);
    if (static_cast<std::int64_t>(cinfo.output_width) * cinfo.output_height > kMaxDecodedPixels) {
        jpeg_destroy_decompress(&cinfo);
        error = "image too large";
        return false;
    }

    jpeg_start_decompress(&cinfo);
    width = static_cast<int>(cinfo.output_width);
    height = static_cast<int>(cinfo.output_height);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3;
    rgb.resize(row_bytes * static_cast<std::size_t>(height));
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb.data() + row_bytes * cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

} // namespace

void fit_webcam_frame(int source_width, int source_height, int box_width, int box_height,
                      int& width, int& height) {
    if (source_width <= 0 || source_height <= 0 || box_width <= 0 || box_height <= 0) {
        width = 0;
        height = 0;
        return;
    }
    const std::int64_t sw = source_width;
    const std::int64_t sh = source_height;
    if (sw * box_height <= static_cast<std::int64_t>(box_width) * sh) {
        // Taller than the box: the height fills it.
        height = box_height;
        width = static_cast<int>((sw * box_height + sh / 2) / sh);
    }
    else {
        width = box_width;
        height = static_cast<int>((sh * box_width + sw / 2) / sw);
    }
    width = std::clamp(width, 1, box_width);
    height = std::clamp(height, 1, box_height);
}

bool decode_webcam_jpeg(const std::string& jpeg,
                        const WebcamFrameLayout& layout,
                        WebcamFrame& frame,
                        std::string& error) {
    if (jpeg.size() < 4 || static_cast<unsigned char>(jpeg[0]) != 0xFF ||
        static_cast<unsigned char>(jpeg[1]) != 0xD8) {
        error = "not a JPEG image";
        return false;
    }
    if (jpeg.size() > kMaxJpegBytes) {
        error = "image too large";
        return false;
    }
    if (layout.box_width <= 0 || layout.box_height <= 0) {
        error = "no room to show the image";
        return false;
    }
    const int box_width = std::min(layout.box_width, kMaxBoxSide);
    const int box_height = std::min(layout.box_height, kMaxBoxSide);
    const int rotation = ((layout.rotation % 360) + 360) % 360;
    const bool swap_axes = rotation == 90 || rotation == 270;

    std::vector<unsigned char> rgb;
    int decoded_width = 0;
    int decoded_height = 0;
    if (!decode_rgb(jpeg, box_width, box_height, swap_axes, rgb, decoded_width, decoded_height, error)) {
        return false;
    }

    // The decoded image as it is shown: turned, then flipped.
    const int shown_width = swap_axes ? decoded_height : decoded_width;
    const int shown_height = swap_axes ? decoded_width : decoded_height;
    int width = 0;
    int height = 0;
    fit_webcam_frame(shown_width, shown_height, box_width, box_height, width, height);
    if (width <= 0 || height <= 0) {
        error = "empty image";
        return false;
    }

    frame.width = width;
    frame.height = height;
    frame.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        // Nearest neighbour, sampling at the centre of each output pixel.
        int sy = static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * shown_height) / (2 * height));
        if (layout.flip_vertical) {
            sy = shown_height - 1 - sy;
        }
        for (int x = 0; x < width; ++x) {
            int sx = static_cast<int>((static_cast<std::int64_t>(2 * x + 1) * shown_width) / (2 * width));
            if (layout.flip_horizontal) {
                sx = shown_width - 1 - sx;
            }
            // Turned clockwise: map the shown position back to the decoded image.
            int dx = sx;
            int dy = sy;
            switch (rotation) {
                case 90:
                    dx = sy;
                    dy = decoded_height - 1 - sx;
                    break;
                case 180:
                    dx = decoded_width - 1 - sx;
                    dy = decoded_height - 1 - sy;
                    break;
                case 270:
                    dx = decoded_width - 1 - sy;
                    dy = sx;
                    break;
                default:
                    break;
            }
            const unsigned char* p =
                rgb.data() + (static_cast<std::size_t>(dy) * static_cast<std::size_t>(decoded_width) + dx) * 3;
            frame.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] =
                0xFF000000u | (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) |
                static_cast<std::uint32_t>(p[2]);
        }
    }
    return true;
}

} // namespace app

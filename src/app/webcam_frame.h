/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// A decoded webcam image, ready to draw: 0xFFRRGGBB pixels, row by row.
struct WebcamFrame {
    int width{0};
    int height{0};
    std::vector<std::uint32_t> pixels;
};

// How the webcam image is turned and fitted into the box it is shown in.
struct WebcamFrameLayout {
    int box_width{0};
    int box_height{0};
    // Clockwise degrees: 0, 90, 180 or 270. Applied before the flips.
    int rotation{0};
    bool flip_horizontal{false};
    bool flip_vertical{false};
};

// Decodes a JPEG and scales it to the largest size that fits the box, keeping its aspect
// ratio. libjpeg's DCT scaling does most of the shrinking, so a large camera image costs about
// as much as a small one. False, with error set, for anything that is not a usable JPEG.
bool decode_webcam_jpeg(const std::string& jpeg,
                        const WebcamFrameLayout& layout,
                        WebcamFrame& frame,
                        std::string& error);

// The size that fits a source_width x source_height image into the box with its aspect ratio;
// {0, 0} when either is empty.
void fit_webcam_frame(int source_width, int source_height, int box_width, int box_height,
                      int& width, int& height);

} // namespace app

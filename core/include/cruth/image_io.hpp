// Cruth core — image output for baked maps (PRD K-02).
//
// Dependency-free writers: PNG (8/16-bit gray, gray+alpha, RGB, RGBA) and
// OpenEXR scanline files (16-bit half or 32-bit float, ZIP compressed), both
// on a small deflate encoder (LZ77 + fixed Huffman).
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cruth/result.hpp"

namespace cruth {

struct Image {
    int width = 0, height = 0, channels = 0;
    std::vector<float> pixels;  // row-major, top row first, interleaved channels

    Image() = default;
    Image(int w, int h, int c) : width(w), height(h), channels(c), pixels(std::size_t(w) * h * c, 0.0f) {}
    float& at(int x, int y, int c) { return pixels[(std::size_t(y) * width + x) * channels + c]; }
    float at(int x, int y, int c) const { return pixels[(std::size_t(y) * width + x) * channels + c]; }
};

enum class ImageFormat { Png8, Png16, ExrHalf, ExrFloat };

// zlib stream (RFC 1950) wrapping a deflate stream (RFC 1951).
std::string zlib_compress(std::span<const std::uint8_t> data);

// PNG values are clamped to [0, 1]. EXR values are written as-is.
std::string encode_png(const Image& image, int bits);
std::string encode_exr(const Image& image, bool half);
// The format follows the extension (.png / .exr) unless given.
Status write_image(const Image& image, const std::string& path, ImageFormat format);

// IEEE 754 binary16 conversion with round-to-nearest-even.
std::uint16_t float_to_half(float value);

}  // namespace cruth

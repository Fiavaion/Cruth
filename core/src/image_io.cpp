// SPDX-License-Identifier: MIT
#include "cruth/image_io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace cruth {

namespace {

// ---------------------------------------------------------------- deflate

class BitWriter {
public:
    explicit BitWriter(std::string& out) : out_(out) {}
    void bits(std::uint32_t value, int count) {  // LSB first
        acc_ |= std::uint64_t(value) << n_;
        n_ += count;
        while (n_ >= 8) {
            out_.push_back(char(acc_ & 0xFF));
            acc_ >>= 8;
            n_ -= 8;
        }
    }
    void huffman(std::uint32_t code, int length) {  // Huffman codes are MSB first
        std::uint32_t r = 0;
        for (int i = 0; i < length; ++i) r |= ((code >> i) & 1u) << (length - 1 - i);
        bits(r, length);
    }
    void flush() {
        if (n_ > 0) out_.push_back(char(acc_ & 0xFF));
        acc_ = 0;
        n_ = 0;
    }

private:
    std::string& out_;
    std::uint64_t acc_ = 0;
    int n_ = 0;
};

constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void fixed_literal(BitWriter& w, unsigned sym) {
    if (sym < 144) w.huffman(0x30 + sym, 8);
    else if (sym < 256) w.huffman(0x190 + (sym - 144), 9);
    else if (sym < 280) w.huffman(sym - 256, 7);
    else w.huffman(0xC0 + (sym - 280), 8);
}

void deflate_fixed(std::span<const std::uint8_t> in, std::string& out) {
    BitWriter w(out);
    w.bits(1, 1);  // BFINAL
    w.bits(1, 2);  // BTYPE = fixed Huffman
    constexpr int kHashBits = 15, kWindow = 32768, kMaxChain = 32;
    // prev is a ring over the window, as in zlib, so memory stays constant.
    std::vector<std::int32_t> head(1 << kHashBits, -1), prev(kWindow, -1);
    auto link = [&](std::size_t pos, std::uint32_t h) {
        prev[pos & (kWindow - 1)] = head[h];
        head[h] = std::int32_t(pos);
    };
    auto hash = [&](std::size_t i) {
        return ((std::uint32_t(in[i]) << 10) ^ (std::uint32_t(in[i + 1]) << 5) ^ in[i + 2]) & ((1u << kHashBits) - 1);
    };
    std::size_t i = 0;
    while (i < in.size()) {
        int best_len = 0, best_dist = 0;
        if (i + 2 < in.size()) {
            const std::uint32_t h = hash(i);
            std::int32_t cand = head[h];
            const std::size_t max_len = std::min<std::size_t>(258, in.size() - i);
            for (int chain = 0; cand >= 0 && chain < kMaxChain && int(i) - cand <= kWindow; ++chain) {
                std::size_t len = 0;
                while (len < max_len && in[std::size_t(cand) + len] == in[i + len]) ++len;
                if (int(len) > best_len) {
                    best_len = int(len);
                    best_dist = int(i) - cand;
                    if (len == max_len) break;
                }
                const std::int32_t next = prev[std::size_t(cand) & (kWindow - 1)];
                if (next >= cand) break;  // slot reused by a newer position
                cand = next;
            }
            link(i, h);
        }
        if (best_len >= 3) {
            int code = 0;
            while (code < 28 && kLengthBase[code + 1] <= best_len) ++code;
            fixed_literal(w, 257 + unsigned(code));
            if (kLengthExtra[code]) w.bits(std::uint32_t(best_len - kLengthBase[code]), kLengthExtra[code]);
            int dcode = 0;
            while (dcode < 29 && kDistBase[dcode + 1] <= best_dist) ++dcode;
            w.huffman(std::uint32_t(dcode), 5);
            if (kDistExtra[dcode]) w.bits(std::uint32_t(best_dist - kDistBase[dcode]), kDistExtra[dcode]);
            // Insert the skipped positions into the hash chains.
            for (std::size_t k = i + 1; k < i + std::size_t(best_len) && k + 2 < in.size(); ++k) link(k, hash(k));
            i += std::size_t(best_len);
        } else {
            fixed_literal(w, in[i]);
            ++i;
        }
    }
    fixed_literal(w, 256);  // end of block
    w.flush();
}

std::uint32_t adler32(std::span<const std::uint8_t> data) {
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

std::uint32_t crc32(const char* data, std::size_t size, std::uint32_t crc = 0) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = crc_table()[(crc ^ std::uint8_t(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put_be32(std::string& s, std::uint32_t v) {
    s.push_back(char(v >> 24));
    s.push_back(char(v >> 16));
    s.push_back(char(v >> 8));
    s.push_back(char(v));
}

template <typename T>
void put_le(std::string& s, T v) {
    char buf[sizeof(T)];
    std::memcpy(buf, &v, sizeof(T));
    s.append(buf, sizeof(T));
}

void png_chunk(std::string& out, const char* type, const std::string& data) {
    put_be32(out, std::uint32_t(data.size()));
    std::string body(type, 4);
    body += data;
    out += body;
    put_be32(out, crc32(body.data(), body.size()));
}

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return std::uint8_t(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
}

}  // namespace

std::string zlib_compress(std::span<const std::uint8_t> data) {
    std::string out;
    out.push_back(char(0x78));  // CMF: deflate, 32K window
    out.push_back(char(0x01));  // FLG: fastest, check bits make (CMF*256+FLG) % 31 == 0
    deflate_fixed(data, out);
    put_be32(out, adler32(data));
    return out;
}

std::uint16_t float_to_half(float value) {
    std::uint32_t f;
    std::memcpy(&f, &value, 4);
    const std::uint32_t sign = (f >> 16) & 0x8000u;
    const std::uint32_t exponent = (f >> 23) & 0xFF;
    std::uint32_t mantissa = f & 0x7FFFFFu;
    if (exponent == 0xFF) return std::uint16_t(sign | 0x7C00u | (mantissa ? 0x200u : 0));  // inf / nan
    int e = int(exponent) - 127 + 15;
    if (e >= 31) return std::uint16_t(sign | 0x7C00u);  // overflow -> inf
    if (e <= 0) {                                       // subnormal or zero
        if (e < -10) return std::uint16_t(sign);
        mantissa |= 0x800000u;
        const int shift = 14 - e;
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t rem = mantissa & ((1u << shift) - 1), mid = 1u << (shift - 1);
        if (rem > mid || (rem == mid && (half & 1))) ++half;
        return std::uint16_t(sign | half);
    }
    std::uint32_t half = sign | (std::uint32_t(e) << 10) | (mantissa >> 13);
    const std::uint32_t rem = mantissa & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1))) ++half;  // may carry into the exponent: correct
    return std::uint16_t(half);
}

std::string encode_png(const Image& img, int bits) {
    const int bpc = bits == 16 ? 2 : 1;
    const int bpp = img.channels * bpc;
    const std::size_t stride = std::size_t(img.width) * bpp;
    std::vector<std::uint8_t> raw(std::size_t(img.height) * stride);
    for (int y = 0; y < img.height; ++y) {
        std::uint8_t* row = raw.data() + std::size_t(y) * stride;
        for (int x = 0; x < img.width; ++x) {
            for (int c = 0; c < img.channels; ++c) {
                const float v = std::clamp(img.at(x, y, c), 0.0f, 1.0f);
                std::uint8_t* p = row + (std::size_t(x) * img.channels + c) * bpc;
                if (bpc == 2) {
                    const auto q = std::uint16_t(std::lround(v * 65535.0f));
                    p[0] = std::uint8_t(q >> 8);
                    p[1] = std::uint8_t(q);
                } else {
                    p[0] = std::uint8_t(std::lround(v * 255.0f));
                }
            }
        }
    }
    // Per-row filter chosen by the minimum sum of absolute residuals.
    std::vector<std::uint8_t> filtered;
    filtered.reserve(raw.size() + img.height);
    std::vector<std::uint8_t> candidate(stride), best(stride);
    for (int y = 0; y < img.height; ++y) {
        const std::uint8_t* cur = raw.data() + std::size_t(y) * stride;
        const std::uint8_t* up = y > 0 ? cur - stride : nullptr;
        long best_score = -1;
        int best_type = 0;
        for (int type = 0; type < 5; ++type) {
            long score = 0;
            for (std::size_t i = 0; i < stride; ++i) {
                const int a = i >= std::size_t(bpp) ? cur[i - bpp] : 0;
                const int b = up ? up[i] : 0;
                const int c = (up && i >= std::size_t(bpp)) ? up[i - bpp] : 0;
                int pred = 0;
                if (type == 1) pred = a;
                else if (type == 2) pred = b;
                else if (type == 3) pred = (a + b) / 2;
                else if (type == 4) pred = paeth(a, b, c);
                candidate[i] = std::uint8_t(cur[i] - pred);
                score += std::abs(int(std::int8_t(candidate[i])));
            }
            if (best_score < 0 || score < best_score) {
                best_score = score;
                best_type = type;
                best = candidate;
            }
        }
        filtered.push_back(std::uint8_t(best_type));
        filtered.insert(filtered.end(), best.begin(), best.end());
    }

    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    put_be32(ihdr, std::uint32_t(img.width));
    put_be32(ihdr, std::uint32_t(img.height));
    ihdr.push_back(char(bits == 16 ? 16 : 8));
    const char color_type[5] = {0, 0, 4, 2, 6};  // by channel count: gray, gray+alpha, RGB, RGBA
    ihdr.push_back(color_type[std::clamp(img.channels, 1, 4)]);
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    png_chunk(out, "IHDR", ihdr);
    png_chunk(out, "IDAT", zlib_compress(filtered));
    png_chunk(out, "IEND", "");
    return out;
}

std::string encode_exr(const Image& img, bool half) {
    // Channel names must be stored in alphabetical order.
    static const char* names[4][4] = {{"Y"}, {"A", "Y"}, {"B", "G", "R"}, {"A", "B", "G", "R"}};
    static const int source[4][4] = {{0}, {1, 0}, {2, 1, 0}, {3, 2, 1, 0}};  // channel index per name
    const int nc = std::clamp(img.channels, 1, 4);

    std::string h;
    put_le<std::uint32_t>(h, 20000630u);  // magic
    put_le<std::uint32_t>(h, 2u);         // version 2, scanline
    auto attr = [&](const char* name, const char* type, const std::string& value) {
        h += name;
        h.push_back('\0');
        h += type;
        h.push_back('\0');
        put_le<std::int32_t>(h, std::int32_t(value.size()));
        h += value;
    };
    std::string chlist;
    for (int c = 0; c < nc; ++c) {
        chlist += names[nc - 1][c];
        chlist.push_back('\0');
        put_le<std::int32_t>(chlist, half ? 1 : 2);  // HALF or FLOAT
        chlist.append(4, '\0');                      // pLinear + reserved
        put_le<std::int32_t>(chlist, 1);             // x sampling
        put_le<std::int32_t>(chlist, 1);             // y sampling
    }
    chlist.push_back('\0');
    attr("channels", "chlist", chlist);
    attr("compression", "compression", std::string(1, char(3)));  // ZIP, 16 scanlines per block
    std::string box;
    put_le<std::int32_t>(box, 0);
    put_le<std::int32_t>(box, 0);
    put_le<std::int32_t>(box, img.width - 1);
    put_le<std::int32_t>(box, img.height - 1);
    attr("dataWindow", "box2i", box);
    attr("displayWindow", "box2i", box);
    attr("lineOrder", "lineOrder", std::string(1, '\0'));
    std::string f1;
    put_le<float>(f1, 1.0f);
    attr("pixelAspectRatio", "float", f1);
    std::string v2;
    put_le<float>(v2, 0.0f);
    put_le<float>(v2, 0.0f);
    attr("screenWindowCenter", "v2f", v2);
    attr("screenWindowWidth", "float", f1);
    h.push_back('\0');

    const int lines_per_block = 16;
    const int blocks = (img.height + lines_per_block - 1) / lines_per_block;
    std::vector<std::string> chunks(static_cast<std::size_t>(blocks));
    std::vector<std::uint8_t> raw, tmp;
    for (int b = 0; b < blocks; ++b) {
        const int y0 = b * lines_per_block, y1 = std::min(img.height, y0 + lines_per_block);
        raw.clear();
        for (int y = y0; y < y1; ++y) {
            for (int c = 0; c < nc; ++c) {
                for (int x = 0; x < img.width; ++x) {
                    const float v = img.at(x, y, source[nc - 1][c]);
                    if (half) {
                        const std::uint16_t hv = float_to_half(v);
                        raw.push_back(std::uint8_t(hv));
                        raw.push_back(std::uint8_t(hv >> 8));
                    } else {
                        std::uint8_t p[4];
                        std::memcpy(p, &v, 4);
                        raw.insert(raw.end(), p, p + 4);
                    }
                }
            }
        }
        // ZIP predictor: split even/odd bytes, then byte-wise delta.
        tmp.resize(raw.size());
        std::size_t t1 = 0, t2 = (raw.size() + 1) / 2;
        for (std::size_t i = 0; i < raw.size(); ++i) tmp[(i % 2) ? t2++ : t1++] = raw[i];
        for (std::size_t i = tmp.size(); i-- > 1;) tmp[i] = std::uint8_t(int(tmp[i]) - int(tmp[i - 1]) + 128);
        std::string packed = zlib_compress(tmp);
        std::string& chunk = chunks[std::size_t(b)];
        put_le<std::int32_t>(chunk, y0);
        if (packed.size() < raw.size()) {
            put_le<std::int32_t>(chunk, std::int32_t(packed.size()));
            chunk += packed;
        } else {  // incompressible blocks are stored raw
            put_le<std::int32_t>(chunk, std::int32_t(raw.size()));
            chunk.append(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
    }

    std::string out = h;
    std::uint64_t offset = out.size() + std::uint64_t(blocks) * 8;
    for (const auto& c : chunks) {
        put_le<std::uint64_t>(out, offset);
        offset += c.size();
    }
    for (const auto& c : chunks) out += c;
    return out;
}

Status write_image(const Image& image, const std::string& path, ImageFormat format) {
    if (image.width <= 0 || image.height <= 0 || image.channels < 1 || image.channels > 4) {
        return Error{ErrorCode::InvalidArgument, "image must be non-empty with 1..4 channels"};
    }
    std::string bytes;
    switch (format) {
        case ImageFormat::Png8: bytes = encode_png(image, 8); break;
        case ImageFormat::Png16: bytes = encode_png(image, 16); break;
        case ImageFormat::ExrHalf: bytes = encode_exr(image, true); break;
        case ImageFormat::ExrFloat: bytes = encode_exr(image, false); break;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return Error{ErrorCode::Io, "cannot write " + path};
    f.write(bytes.data(), std::streamsize(bytes.size()));
    if (!f) return Error{ErrorCode::Io, "write failed: " + path};
    return Status::ok();
}

}  // namespace cruth

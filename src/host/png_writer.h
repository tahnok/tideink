// Minimal 1-bit grayscale PNG writer, so the simulator has no image
// dependencies. Uses stored (uncompressed) deflate blocks -- a 800x480 1bpp
// screen is about 48 kB, which is fine for screenshots.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

namespace png {

inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline void be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

inline void chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
    be32(out, (uint32_t)body.size());
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    be32(out, crc32(&out[start], out.size() - start) ^ 0xFFFFFFFFu);
}

// `bits` is the canvas framebuffer: 1 = black, MSB first, `stride` bytes/row.
inline bool write(const std::string& path, const uint8_t* bits, int width, int height,
                  int stride) {
    const int rowBytes = (width + 7) / 8;

    // Raw scanlines: filter byte 0, then the row with bits inverted because PNG
    // grayscale treats 0 as black and our framebuffer uses 1 for ink.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(rowBytes + 1) * height);
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        for (int x = 0; x < rowBytes; x++) raw.push_back((uint8_t)~bits[(size_t)y * stride + x]);
    }

    // zlib stream with stored deflate blocks.
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        const uint16_t len = (uint16_t)((raw.size() - pos > 65535) ? 65535 : raw.size() - pos);
        const bool last = (pos + len) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF));
        z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)width);
    be32(ihdr, (uint32_t)height);
    ihdr.push_back(1);  // bit depth
    ihdr.push_back(0);  // colour type: grayscale
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

}  // namespace png

// Unpacker for DOS32 v3.5 "Adam" executables (Adam Seychell's DOS extender), as used by JIZZ.EXE.
// Ports the DOS32 kernel's loader (stub image offsets): the header check at 0x0f2b, the decompressor 0x119f..0x12f3
// (an LZ coder with a 16-bit bit buffer in the style of LZEXE), the relocation pass 0x12f4 and the BSS fill 0x1350.
// See jizz/re/NOTES.md ("The DOS32 v3.5 loader") and jizz/re/unadam.py, which checks this against the kernel's own
// code run in an emulator.
#include <cstdio>

#include "exe.h"

namespace {
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(rd16(d, o) | uint32_t(rd16(d, o + 2)) << 16); }

struct Header {
    uint32_t mzSize = 0;  // file offset of the Adam header (end of the MZ stub image)
    uint16_t version = 0, minVersion = 0;
    uint32_t contents = 0, image = 0, headerSize = 0, eip = 0, memory = 0, esp = 0, program = 0, flags = 0;
};

bool parseHeader(const std::vector<uint8_t>& d, Header& h, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (d.size() < 0x40 || rd16(d, 0) != 0x5a4d) return fail("not an MZ executable");
    uint16_t lastPage = rd16(d, 2), pages = rd16(d, 4);
    h.mzSize = lastPage ? (pages - 1u) * 512u + lastPage : pages * 512u;
    if (size_t(h.mzSize) + 0x2c > d.size() || std::memcmp(&d[h.mzSize], "Adam", 4) != 0)
        return fail("no DOS32 'Adam' header after the MZ stub");
    h.version = rd16(d, h.mzSize + 4);
    h.minVersion = rd16(d, h.mzSize + 6);
    uint32_t* f[] = {&h.contents, &h.image, &h.headerSize, &h.eip, &h.memory, &h.esp, &h.program, &h.flags};
    for (int i = 0; i < 8; i++) *f[i] = rd32(d, h.mzSize + 8 + 4 * i);
    if (h.version != 0x350) return fail("not a DOS32 v3.5 executable");
    if (!(h.flags >> 16 & 1)) return fail("the DOS32 program is not compressed");
    if (size_t(h.mzSize) + h.headerSize + h.contents > d.size()) return fail("truncated DOS32 program");
    if (h.program > h.memory) return fail("bad DOS32 header");
    return true;
}

// The decompressor 0x119f. Returns false on malformed data.
bool decompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    size_t p = 0;
    uint32_t bits = 0;
    int left = 1;
    bool bad = false;
    auto byte = [&]() -> uint32_t {
        if (p >= n) { bad = true; return 0; }
        return src[p++];
    };
    auto bit = [&]() -> uint32_t {
        uint32_t b = bits & 1;
        bits >>= 1;
        if (--left == 0) {  // the next word is loaded as soon as the 16th bit has been taken
            uint32_t lo = byte(), hi = byte();
            bits = lo | hi << 8;
            left = 16;
        }
        return b;
    };
    bit();  // 0x11bc: loads the first word; the bit itself is discarded
    while (!bad) {
        if (bit()) {  // literal
            out.push_back(uint8_t(byte()));
            continue;
        }
        uint32_t longMatch = bit();
        uint32_t lo = byte(), hi = 0xff, len;
        if (longMatch) {  // 0x11c8
            hi = (hi << 1 | bit()) & 0xff;
            if (!bit()) {
                uint32_t dh = 2;
                for (int i = 0; i < 3; i++) {
                    if (bit()) break;
                    hi = (hi << 1 | bit()) & 0xff;
                    dh = (dh << 1) & 0xff;
                }
                hi = (hi - dh) & 0xff;
            }
            len = 0;
            for (uint32_t l = 3; l <= 6; l++)  // 0x11fe: 1 = 3, 01 = 4, 001 = 5, 0001 = 6
                if (bit()) { len = l; break; }
            if (!len) {
                if (bit()) len = 7 + bit();                  // 00001x: 7 or 8
                else if (bit()) len = byte() + 0x11;         // 000001 + byte: 17..272
                else {                                       // 000000 + 3 bits: 9..16
                    uint32_t x = 0;
                    for (int i = 0; i < 3; i++) x = x << 1 | bit();
                    len = x + 9;
                }
            }
        } else if (bit()) {  // 0x12b7: three more high bits, length 2
            for (int i = 0; i < 3; i++) hi = (hi << 1 | bit()) & 0xff;
            hi = (hi - 1) & 0xff;
            len = 2;
        } else if (lo != 0xff) {  // short match, length 2
            len = 2;
        } else if (bit()) {  // 0xff marker + 1: no-op
            continue;
        } else {  // 0xff marker + 0: end of data
            return true;
        }
        long off = long(hi << 8 | lo) - 0x10000;
        if (long(out.size()) + off < 0) return false;
        for (uint32_t i = 0; i < len; i++) {
            uint8_t c = out[out.size() + off];  // copied byte by byte: the source may overlap the output
            out.push_back(c);
        }
    }
    return !bad;
}
}  // namespace

bool unadam(const std::vector<uint8_t>& exe, std::vector<uint8_t>& image, uint32_t* entry, std::string* err) {
    Header h;
    if (!parseHeader(exe, h, err)) return false;
    std::vector<uint8_t> dec;
    dec.reserve(h.memory);
    if (!decompress(&exe[h.mzSize + h.headerSize], h.contents, dec) || dec.size() < h.program ||
        dec.size() > h.memory) {
        if (err) *err = "DOS32 decompression failed";
        return false;
    }
    // 0x12f4: relocation for base 0. The stream follows the program. Each entry is a displacement from the previous
    // site in groups of 7 bits (bit 0 of a byte = another byte follows), most significant group first; the bytes
    // are rotated left by 5 when the banner is suppressed (flags bit 2), as the kernel stores them. A zero byte
    // after an entry marks a selector fixup (the site gets the data selector, 0x18 in DOS32); otherwise the base is
    // added to the dword at the site, which for base 0 changes nothing.
    const int rot = (h.flags >> 16 & 4) ? 5 : 0;
    const size_t end = dec.size();
    size_t s = h.program;
    uint32_t site = 0;
    image.assign(h.memory, 0);
    std::memcpy(image.data(), dec.data(), h.program);
    while (s < end) {
        uint32_t v = 0;
        bool done = false;
        for (;;) {
            if (s >= end) { done = true; break; }
            uint8_t b = dec[s++];
            uint8_t al = rot ? uint8_t(b << rot | b >> (8 - rot)) : b;
            v = (v & ~0xffu) | al;
            if (!(al & 1)) break;
            v <<= 7;
        }
        if (done) break;
        site += v >> 1;
        if (s < end && dec[s] == 0) {
            if (size_t(site) + 2 > h.program) { if (err) *err = "bad DOS32 relocation"; return false; }
            image[site] = 0x18;
            image[site + 1] = 0;
            s++;
        } else if (size_t(site) + 4 > h.program) {
            if (err) *err = "bad DOS32 relocation";
            return false;
        }
    }
    if (entry) *entry = h.eip;
    return true;
}

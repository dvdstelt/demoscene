// CLONE.EXE: unpacks the release (unpack.cpp: the "VE" container behind the PMODE/W stub and the "veloader" LE
// program, its LZ77 + canonical Huffman decompressor and its fixup list, clone/re/NOTES.md "File layout" and
// clone/re/notes-core.md section 13) and gives access to the program image by linear address. All of the intro's
// static data (the sync table, the stored pictures and their grey palettes, the MXM module, the tile colours, the
// parts' parameters) is read from here at run time, at the addresses used in clone/re/notes-*.md: the image is
// relocated for linear base 0x400000, as the headless run loads it, so address A is image byte A - 0x400000.
// Nothing from the executable is copied into the remake's source.
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The unpacked program (unpack.cpp).
struct CloneUnpacked {
    std::vector<uint8_t> image;  // code and initialised data (0x2846c bytes), relocated for kBase
    uint32_t bss = 0;            // zero bytes that follow the image in the original (not stored here)
    uint32_t entry = 0;          // entry point, image offset
    uint32_t stack = 0;          // initial stack pointer, image offset
    int fixups = 0;              // dword fixups applied
};
// Unpacks CLONE.EXE and relocates the image for linear address `base`. False with a message on a malformed file.
bool unpackClone(const std::vector<uint8_t>& exe, uint32_t base, CloneUnpacked& out, std::string* err);

class CloneExe {
public:
    static constexpr uint32_t kBase = 0x400000;  // linear address of image byte 0 (as in the notes)

    // Reads and unpacks CLONE.EXE (a few ms) and checks the image against the released intro (size and hash).
    bool load(const std::string& exePath, std::string* err);

    // The image from kBase (image()[a - kBase] is address a); the BSS behind it is not included.
    std::span<const uint8_t> image() const { return {u_.image.data(), u_.image.size()}; }
    uint32_t end() const { return kBase + uint32_t(u_.image.size()); }  // 0x42846c: first BSS address
    uint32_t bssEnd() const { return end() + u_.bss; }                   // 0xf95937

    // Linear address -> data. Throws std::out_of_range when [a, a+n) is not inside the image (BSS addresses hold no
    // data in the file: the program computes them at run time).
    const uint8_t* at(uint32_t a, size_t n = 1) const;
    uint8_t u8(uint32_t a) const { return *at(a); }
    int8_t s8(uint32_t a) const { return int8_t(*at(a)); }
    uint16_t u16(uint32_t a) const { return get<uint16_t>(a); }
    int16_t s16(uint32_t a) const { return get<int16_t>(a); }
    uint32_t u32(uint32_t a) const { return get<uint32_t>(a); }
    int32_t s32(uint32_t a) const { return get<int32_t>(a); }
    float f32(uint32_t a) const { return get<float>(a); }
    double f64(uint32_t a) const { return get<double>(a); }
    std::string cstr(uint32_t a) const;  // zero-terminated string
    std::vector<uint8_t> bytes(uint32_t a, size_t n) const { const uint8_t* p = at(a, n); return {p, p + n}; }
    std::span<const uint8_t> span(uint32_t a, size_t n) const { return {at(a, n), n}; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    CloneUnpacked u_;
};

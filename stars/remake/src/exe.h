// STARS.EXE: unpacks the release (unpack.cpp: the Infiny Loader's decryption, PKLITE 1.1x and the DIET-compressed
// overlay, as stars/re/NOTES.md "File layout" and notes-core.md section 9 describe them) and gives access to the
// program image and the 58 overlay files. All of the demo's static data is read from here at run time: from the
// image by address (the addresses used in stars/re/notes-*.md) and from the overlay by file number (1..58, the
// numbers 0x4a9f3 loads, catalogued in notes-core.md section 13).
// Nothing from the executable is copied into the remake's source; it is read from the user's file at run time.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The unpacked release (unpack.cpp).
struct StarsUnpacked {
    // The program image as stars/re/unpacked/stars_img.bin holds it: the PKLITE output (0x7c0ab bytes), relocated for
    // load segment 0x1000 (only the 16-bit PMODE kernel at 0x7a210.. has relocations) and zero-filled up to the end
    // of the stack (0x840b0 bytes).
    std::vector<uint8_t> image;
    std::array<std::vector<uint8_t>, 59> files;  // [1..58]: the overlay files as 0x4a9f3 returns them; [0] empty
};
bool unpackStars(const std::vector<uint8_t>& exe, StarsUnpacked& out, std::string* err);

class StarsExe {
public:
    static constexpr int kFiles = 58;

    // Reads and unpacks STARS.EXE (about 30 ms) and checks every part against the known release.
    bool load(const std::string& exePath, std::string* err);

    const std::vector<uint8_t>& image() const { return u_.image; }
    size_t size() const { return u_.image.size(); }

    // address -> data (image offsets = the 32-bit program's near pointers). Throws std::out_of_range when [a, a+n) is
    // outside the image.
    const uint8_t* at(uint32_t a, size_t n = 1) const;
    uint8_t u8(uint32_t a) const { return *at(a); }
    int8_t s8(uint32_t a) const { return int8_t(*at(a)); }
    uint16_t u16(uint32_t a) const { return get<uint16_t>(a); }
    int16_t s16(uint32_t a) const { return get<int16_t>(a); }
    uint32_t u32(uint32_t a) const { return get<uint32_t>(a); }
    int32_t s32(uint32_t a) const { return get<int32_t>(a); }
    std::string cstr(uint32_t a) const;  // zero-terminated string
    std::vector<uint8_t> bytes(uint32_t a, size_t n) const { const uint8_t* p = at(a, n); return {p, p + n}; }

    // Overlay file n (1..58), unpacked. Throws std::out_of_range for other n.
    const std::vector<uint8_t>& file(int n) const;
    std::span<const uint8_t> fileSpan(int n) const { const auto& f = file(n); return {f.data(), f.size()}; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    bool check(std::string* err) const;
    StarsUnpacked u_;
};

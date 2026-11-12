// DOPE.EXE and DOPE.MOD: reads the release, splits DOPE.EXE into its chain of nine MZ executables and unpacks the
// eight RNC-packed ones (rnc.cpp, dope/re/NOTES.md "File layout" and "RNC ProPack method 1", notes-core.md
// section 7), checks every image against the known release, and gives the parts read access to their images with
// the addresses the notes use. All of the demo's static data is read from here at run time; nothing from the
// originals is copied into the remake's source.
//
// Addresses. The notes write `SEG:OFF` in an unpacked image (dope/re/unpacked/<name>.bin, relocated for load segment
// 0: file offset = SEG * 16 + OFF), `[xxxx]` or `cs:[xxxx]` for 0010:xxxx (all part code and variables are in
// segment 0010), "file 0x4510" for a plain file offset, and code32 offsets for the loader's 32-bit code (file offset
// 0x17e0 + offset of loader.bin). DopeImage reads all four:
//
//   const DopeImage& p2 = c.exe->part(2);
//   p2.u16(0x0451, 0x030a)          // 0451:030A
//   p2.cs16(0x1955)                 // [1955] = 0010:1955
//   p2.span(0x6507, 0x0010, 720)    // 720 bytes at 6507:0010
//   p2.file8(0x4510)                // file offset 0x4510
//   c.exe->loader().code32(0x0b37)  // the file offset of code32 0xb37
//
// Every accessor throws std::out_of_range outside the image.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// One unpacked load module (as dope/re/unpacked/<name>.bin holds it).
class DopeImage {
public:
    static constexpr uint16_t kCS = 0x0010;       // the parts' code segment: cs:[x] = 0010:x
    static constexpr uint32_t kCode32 = 0x17e0;   // loader.bin: file offset of code32 offset 0
    static constexpr uint32_t addr(uint16_t seg, uint16_t off) { return uint32_t(seg) * 16u + off; }

    std::string name;                 // loader, p1..p7, term
    std::vector<uint8_t> data;

    size_t size() const { return data.size(); }
    std::span<const uint8_t> all() const { return {data.data(), data.size()}; }

    // by file offset
    const uint8_t* at(uint32_t file, size_t n = 1) const;
    uint8_t file8(uint32_t a) const { return *at(a); }
    uint16_t file16(uint32_t a) const { return get<uint16_t>(a); }
    uint32_t file32(uint32_t a) const { return get<uint32_t>(a); }
    std::span<const uint8_t> fileSpan(uint32_t a, size_t n) const { return {at(a, n), n}; }

    // by SEG:OFF
    uint8_t u8(uint16_t seg, uint16_t off) const { return *at(addr(seg, off)); }
    int8_t s8(uint16_t seg, uint16_t off) const { return int8_t(*at(addr(seg, off))); }
    uint16_t u16(uint16_t seg, uint16_t off) const { return get<uint16_t>(addr(seg, off)); }
    int16_t s16(uint16_t seg, uint16_t off) const { return get<int16_t>(addr(seg, off)); }
    uint32_t u32(uint16_t seg, uint16_t off) const { return get<uint32_t>(addr(seg, off)); }
    int32_t s32(uint16_t seg, uint16_t off) const { return get<int32_t>(addr(seg, off)); }
    std::span<const uint8_t> span(uint16_t seg, uint16_t off, size_t n) const { return {at(addr(seg, off), n), n}; }
    std::vector<uint8_t> bytes(uint16_t seg, uint16_t off, size_t n) const {
        const uint8_t* p = at(addr(seg, off), n);
        return {p, p + n};
    }
    std::string cstr(uint16_t seg, uint16_t off) const;  // zero-terminated
    std::string dstr(uint16_t seg, uint16_t off) const;  // '$'-terminated (DOS int 21h AH=9)

    // cs-relative: [x] = 0010:x
    uint8_t cs8(uint16_t off) const { return u8(kCS, off); }
    uint16_t cs16(uint16_t off) const { return u16(kCS, off); }
    int16_t css16(uint16_t off) const { return s16(kCS, off); }
    uint32_t cs32(uint16_t off) const { return u32(kCS, off); }
    int32_t css32(uint16_t off) const { return s32(kCS, off); }
    std::span<const uint8_t> csSpan(uint16_t off, size_t n) const { return span(kCS, off, n); }

    // the loader's 32-bit code and data: code32 offset -> file offset
    static constexpr uint32_t code32(uint32_t off) { return kCode32 + off; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
};

// The chain of DOPE.EXE (NOTES.md "File layout"): 0 loader, 1..7 the parts, 8 TERM.
constexpr int kDopeImages = 9;
// Splits the chain and unpacks every RNC-1 load module (rnc.cpp); checks the RNC CRCs. TERM is not packed: its
// image is the load module after the MZ header.
bool unpackDope(const std::vector<uint8_t>& exe, std::array<std::vector<uint8_t>, kDopeImages>& out, std::string* err);

class DopeExe {
public:
    // Reads and unpacks DOPE.EXE (about 20 ms) and reads DOPE.MOD (`modPath`, or next to the exe when empty), and
    // checks both against the release (Complex, 1995: DOPE.EXE 584,133 bytes, DOPE.MOD 957,334 bytes).
    bool load(const std::string& exePath, const std::string& modPath, std::string* err);

    const DopeImage& loader() const { return img_[0]; }
    const DopeImage& part(int n) const;   // 1..7; throws std::out_of_range for other n
    const DopeImage& term() const { return img_[8]; }
    const DopeImage& image(int i) const;  // 0..8 in chain order
    std::span<const uint8_t> mod() const { return {mod_.data(), mod_.size()}; }
    const std::string& modPath() const { return modPath_; }

private:
    bool check(std::string* err) const;
    std::array<DopeImage, kDopeImages> img_;
    std::vector<uint8_t> mod_;
    std::string modPath_;
};

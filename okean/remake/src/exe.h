// KORSO2.EXE: unpacks the release and gives access to the program by code32 address. All of the intro's static data
// (the module, the player's tables, the parts' tables, palettes, meshes, the font and the texts) is read from the
// program image at the addresses used in okean/re/notes-*.md; nothing from the original is copied into the remake's
// source.
//
// KORSO2.EXE is one MZ executable packed with Rob Northen's ProPack method 1 (rnc.cpp; okean/re/NOTES.md
// "Unpacking"): the load module is the 18-byte RNC header and the bit stream, the packer's stub at its end. The
// unpacked image (= okean/re/unpacked/korso2.bin) holds Tran's PMODE 16-bit kernel and, from file offset 0xec0, the
// 32-bit segment code32 with everything of the intro. The notes give code32 offsets: file offset = 0xec0 + address.
//
//   exe.u8(0x2b65c)              // the first part's palette, byte 0
//   exe.span(0x24a3c, 4096 * 4)  // the 4096-entry sine table
//   exe.s32(0x24a3c + 4 * i)
//
// Every accessor throws std::out_of_range outside the image.
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// ProPack method 1 (rnc.cpp): the load module of a packed MZ (header, bit stream, stub) -> the unpacked bytes, both
// CRCs checked. False with a message on any inconsistency.
bool unpackRnc(const uint8_t* module, size_t size, std::vector<uint8_t>& out, std::string* err);

std::vector<uint8_t> readFile(const std::string& path, bool* ok);

class OkeanExe {
public:
    static constexpr uint32_t kCode32 = 0xec0;   // file offset of code32 offset 0

    // Reads and unpacks KORSO2.EXE and checks the image against the released intro (size and hash).
    bool load(const std::string& exePath, std::string* err);

    std::span<const uint8_t> image() const { return {img_.data(), img_.size()}; }
    uint32_t end() const { return uint32_t(img_.size()) - kCode32; }  // the first code32 address past the file

    // code32 address -> data
    const uint8_t* at(uint32_t a, size_t n = 1) const;
    uint8_t u8(uint32_t a) const { return *at(a); }
    int8_t s8(uint32_t a) const { return int8_t(*at(a)); }
    uint16_t u16(uint32_t a) const { return get<uint16_t>(a); }
    int16_t s16(uint32_t a) const { return get<int16_t>(a); }
    uint32_t u32(uint32_t a) const { return get<uint32_t>(a); }
    int32_t s32(uint32_t a) const { return get<int32_t>(a); }
    std::string cstr(uint32_t a, size_t max = 256) const;   // zero-terminated
    std::vector<uint8_t> bytes(uint32_t a, size_t n) const { const uint8_t* p = at(a, n); return {p, p + n}; }
    std::span<const uint8_t> span(uint32_t a, size_t n) const { return {at(a, n), n}; }

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    std::vector<uint8_t> img_;
};

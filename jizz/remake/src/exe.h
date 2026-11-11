// JIZZ.EXE: unpacks the DOS32 v3.5 "Adam" executable (decompression and relocation, as the DOS32 kernel's
// loader at stub offset 0x115b does it; see jizz/re/NOTES.md) and gives access to the program image, from which
// all of the intro's static data is read by its address (the addresses used in jizz/re/notes-*.md).
// Nothing from the executable is copied into the remake's source; it is read from the user's file at run time.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Unpacks a compressed DOS32 v3.5 executable (adam.cpp). `image` receives the program as the DOS32 kernel leaves
// it in memory, relocated for base 0, with the BSS zeroed up to the memory size (for JIZZ.EXE this equals
// jizz/re/jizz_img.bin, 0x33000 bytes). `entry` receives the entry point (EIP).
bool unadam(const std::vector<uint8_t>& exe, std::vector<uint8_t>& image, uint32_t* entry, std::string* err);

class JizzExe {
public:
    bool load(const std::string& exePath, std::string* err);       // JIZZ.EXE
    bool loadImage(const std::string& imgPath, std::string* err);  // already unpacked image (development)

    const std::vector<uint8_t>& image() const { return img_; }
    size_t size() const { return img_.size(); }

    // address -> data. Throws std::out_of_range when [a, a+n) is outside the image.
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

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    bool check(std::string* err) const;
    std::vector<uint8_t> img_;
};

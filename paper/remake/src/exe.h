// PAPER.EXE: unpacks the PKLITE 1.15 wrapper and gives access to the 32-bit segment ("code32"), from which all
// of the intro's static data is read by its code32 offset (the addresses used in paper/re/notes-*.md).
// Nothing from the executable is copied into the remake's source; it is read from the user's file at run time.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Unpacks a PKLITE-compressed MZ executable (implemented in pklite.cpp). `image` receives the program image as it
// sits in memory from the load segment after PKLITE's own stub has run (for PAPER.EXE this equals
// paper/re/paper_img.bin, 0x269e0 bytes).
bool unpklite(const std::vector<uint8_t>& exe, std::vector<uint8_t>& image, std::string* err);

class PaperExe {
public:
    static constexpr uint32_t kCode32 = 0xd00;  // image offset of code32 (Tran's PMODE 32-bit segment)

    bool load(const std::string& exePath, std::string* err);        // PAPER.EXE
    bool loadImage(const std::string& imgPath, std::string* err);   // already unpacked image (development)

    const std::vector<uint8_t>& image() const { return img_; }
    size_t size() const { return img_.size() > kCode32 ? img_.size() - kCode32 : 0; }  // bytes of code32

    // code32 offset -> data. Throws std::out_of_range when [a, a+n) is outside the image.
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

// 303.EXE (Watcom C, DOS/4GW): loads the LE executable's objects into one flat image with the fixups applied, so
// its static data is read by the linear addresses used in 303/re/notes-*.md (code object at 0x10000, data object
// at 0x50000). Nothing from the executable is copied into the remake's source: tables and strings (lyric words,
// jitter, glyph and icon positions, ...) are read from the user's file at run time.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Exe303 {
public:
    static constexpr uint32_t kBase = 0x10000;  // linear address of the first object (code)

    // Loads and checks 303.EXE (the released file: 414153 bytes). On failure returns false and sets *err.
    bool load(const std::string& path, std::string* err);
    bool loaded() const { return !img_.empty(); }

    // linear address -> data. Throws std::out_of_range when [a, a+n) is outside the image.
    const uint8_t* at(uint32_t a, size_t n = 1) const;
    uint8_t u8(uint32_t a) const { return *at(a); }
    int8_t s8(uint32_t a) const { return int8_t(*at(a)); }
    uint16_t u16(uint32_t a) const { return get<uint16_t>(a); }
    int16_t s16(uint32_t a) const { return get<int16_t>(a); }
    uint32_t u32(uint32_t a) const { return get<uint32_t>(a); }
    int32_t s32(uint32_t a) const { return get<int32_t>(a); }
    float f32(uint32_t a) const { return get<float>(a); }
    double f64(uint32_t a) const { return get<double>(a); }
    std::string cstr(uint32_t a) const;                            // zero-terminated string at a
    std::string str(uint32_t ptr) const { return cstr(u32(ptr)); }  // the string a pointer at ptr points to

private:
    template <class T> T get(uint32_t a) const { T v; std::memcpy(&v, at(a, sizeof(T)), sizeof(T)); return v; }
    std::vector<uint8_t> img_;  // from kBase
};

// Building blocks ported from the original executable, shared by the parts that use them:
// the palette system, Watcom rand(), a fixed-rate clock for per-frame feedback, an additive
// line layer (the exe's soft 3-pixel line stamps), the lyric renderer, and the exe tables they
// share (jitter, lyric words), read from 303.EXE at run time.
// Coordinates are in the original 320x200 screen pixels unless noted.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "fx.h"

// Watcom rand(): s = s * 0x41c64e6d + 0x3039, returns (s >> 16) & 0x7fff.
struct ExeRand {
    uint32_t s;
    explicit ExeRand(uint32_t seed = 1) : s(seed) {}
    int next() {
        s = s * 0x41c64e6du + 0x3039u;
        return int((s >> 16) & 0x7fff);
    }
};

// Palette system: 0x10124 runs every tick (70 Hz), 0x1019c uploads the palette.
// level 0x4000 = normal, lower fades to black, higher to white (or to the negative when invert is set).
// mult is a brightness multiplier (flashes) that decays towards 1.
struct ExePalette {
    int level = 0x4000, target = 0x4000, speed = 0;
    bool invert = false;
    float mult = 1.0f;
    void tick() {
        if (level == target) speed = 0;
        if (level < target) level = std::min(level + speed, target);
        if (level > target) level = std::max(level - speed, target);
        mult = (mult - 1.0f) * 0.98f + 1.0f;
    }
    void flash(int spd) {
        level = 0x8000;
        speed = spd;
    }
    void apply(const class Shader& s) const;  // sets palLevel, palMult, palInvert
};
// GLSL: uniforms palLevel, palMult, palInvert and vec3 exePalette(vec3 p) (p in 0..1).
extern const char* kExePaletteGLSL;
// GLSL: vec2 sphereRayTexel(vec2 px, vec3 R0, vec3 R1, vec3 R2, float focal, float scale, float off)
// = the 0x135dc grid, per pixel: texel coordinates (column, row) in a 256x256 texture.
extern const char* kSphereRayGLSL;
// Rotation matrix rows of 0x1cd4b for angles a, b in 1024ths of a turn.
void exeMatrix(float a, float b, Vec3& R0, Vec3& R1, Vec3& R2);

// The original renders as fast as it can and applies feedback per rendered frame. These run the
// feedback at a fixed rate instead. steps() returns how many steps to run this frame, or -1 after a seek.
struct StepClock {
    float lastT = -1e9f;
    int steps(const Ctx& c, float hz = 70.0f);
};

// Soft additive lines into an R16F intensity target, like the exe's (+1,+2,+1) stamps.
struct LineBatch {
    void add(float x0, float y0, float x1, float y1, float value, float halfWidth = 1.2f);
    void circle(float cx, float cy, float r, float value, float halfWidth = 1.2f);
    void draw();  // additive into the bound target, then clears the batch
    bool empty() const { return v_.empty(); }

private:
    std::vector<float> v_;
    unsigned vao_ = 0, vbo_ = 0;
    size_t cap_ = 0;
};

// Lyric renderer (0x2a34c frame, 0x2a0d4 tick, 0x29eac mapping grid, 0x1c058 accumulator).
// The word bitmap is stretched over a quad; outside it the letters' top and bottom rows are smeared
// to the screen edges. The result feeds a decaying accumulator (x0.7 per frame) that can be inverted,
// and the frame is lightened by accumulator / 8 fade-table rows.
struct LyricState {
    std::string word;
    int width = 0;          // word width in font pixels
    int tx = 0, ty = 0;     // position (original pixels)
    bool zoom = false;      // 3x jump
    int smear = 0;          // 0..11
    float finv = 0.0f;      // inversion amount
    int jit[5] = {0, 0, 0, 0, 0};
};
class LyricLayer {
public:
    void init(Ctx& c);
    // advance the accumulator for this frame and leave the result in acc()
    void update(Ctx& c, const LyricState& s, int steps);
    const Target& acc() const { return acc_[cur_]; }
    static int wordWidth(Ctx& c, const std::string& w);

private:
    void buildWord(Ctx& c, const std::string& w);
    Shader sh_;
    Target acc_[2];
    int cur_ = 0;
    Texture word_;
    std::string built_;
};
// Final pass shared by parts that use 0x29d1c: draws the palette-inverted 1/3-scale thumbnail of src
// in a window at (winx, winy) that opens from its centre (open 0..1, full size 106x66 + 1 px white
// border), lightens by a lyric accumulator (optional) and applies the palette. Writes the bound target.
struct ExeComposite {
    float winx = -1000, winy = 0, open = 0;   // winx < -500: no inset
    const Target* lyrics = nullptr;
    const std::vector<float>* tint = nullptr;  // fadDarkTint() of the part's palette, for the lyric lightening
};
// How the fade table lightens black, relative to a plain lerp to white (32 RGB deltas, rows 32..63).
// Palettes without dark greys pass through other hues (blobby: purple), which is where the colour of
// the lyric smears comes from.
std::vector<float> fadDarkTint(Ctx& c, const std::string& fad, const std::string& paletteRaw);
void exeComposite(Ctx& c, const Target& src, const ExeComposite& k, const ExePalette& pal);

// ---------------------------------------------------------------- tables of 303.EXE (read once from Ctx::exe)
// 0x52f10: the 16 jitter offsets (signed bytes) indexed by rand() & 15 (lyric quads, credit words, icons).
const std::array<int, 16>& exeJitter(const Ctx& c);
// 0x57dc0: the ten words (string pointers) the lyric renderer's cycle mode steps through, in order.
const std::vector<std::string>& lyricCycle(const Ctx& c);
// Song mode of 0x2a0d4: 41 entries of order (0x57e8c), row (0x57de8) and word (string pointers at 0x57f30);
// entry 40 is the init value (an empty word).
struct SongEntry { int order, row; std::string word; };
const std::vector<SongEntry>& songTable(const Ctx& c);
// The entry 0x2a0d4 picks at music position pos (orders 21/25/27 are looked up as 17, 22/26/28 as 18), 40 = none.
int songIndex(const Ctx& c, int pos);
// The music position of an entry (in its own order).
inline int songPos(const SongEntry& e) { return 2 * (e.order * 64 + e.row) - 2; }

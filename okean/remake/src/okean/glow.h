// The intro's way of drawing, shared by every part (okean/re/notes-morph-star.md section 1, notes-title-objects.md,
// notes-landscapes.md, notes-sheet.md):
//
// - the 4096-entry sine table at code32 0x24a3c (a quarter wave in the file, completed by the first part);
// - every frame starts as per-pixel noise (uniform indices 0..N-1, a new pattern per drawn frame);
// - lines are rows of small "blobs": an 8x8 kernel of brightness values added into the 8-bit buffer every ~2 pixels
//   of the line's major axis (each part has its own copy of the line drawer and its own kernel);
// - the sum of noise and blobs is an index into a black -> colour -> white palette ramp.
//
// The remake evaluates the same sums continuously: a float "index" buffer at the output resolution, the noise drawn
// fresh per refresh, every blob as a sprite at its exact (unfloored) position with the kernel as a bilinear surface
// over its 8x8 pixel centres (so its total is the same), and the sum looked up in the palette with linear
// interpolation between entries. The original's 8-bit artefacts (sums past 255 that wrap and carry into the next
// pixel, the font's grey at 254, blobs clipped whole at the screen edge, the 8-bit step counter of off-screen lines)
// are left out: every sum past the ramp's white end is white.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "demo.h"

namespace okean {

// T[i] (code32 0x24a3c, 32-bit): entries 0..1024 are in the file (a quarter wave, peak 1022); the first part's
// set-up (0x2c26f) mirrors them into 1025..5120 (notes-morph-star.md 1.1). Indices are masked with 0xfff; C(a) =
// T[a + 1025] is the cosine.
class Sine {
public:
    explicit Sine(const OkeanExe& exe);
    int32_t T(int i) const { return t_[size_t(i & 0xfff)]; }
    int32_t C(int a) const { return t_[size_t((a & 0xfff) + 1025)]; }
    int32_t raw(int i) const { return t_[size_t(i)]; }   // 0..5120, unmasked
    // The continuous curve the table samples, for any (fractional) angle a in 1/4096 turns: 1022 sin(2 pi a / 4096)
    // (the table's peak is 1022). Use it where the remake evaluates motion between callbacks.
    static double s(double a);
    static double c(double a);

private:
    std::array<int32_t, 5121> t_{};
};

// A 768-byte 6-bit palette in the exe as 256 colours (v / 63).
std::array<Vec3, 256> palette(const OkeanExe& exe, uint32_t addr);
// The palette scaled as the intro fades it: entry * num / den per component (the original truncates; the remake
// scales continuously).
std::array<Vec3, 256> scaled(const std::array<Vec3, 256>& pal, double num, double den);

// An 8x8 blob kernel as the line drawer's blob routine holds it: 16 instructions `add dword [edi + d], imm32` (81 07 /
// 81 47 d8 / 81 87 d32, from code32 `addr` on), each adding 4 pixels (the immediate's bytes) at offset d = row * 320 +
// column. Throws std::runtime_error if the code is not that.
std::array<uint8_t, 64> kernel(const OkeanExe& exe, uint32_t addr);

// One frame of a part: the index buffer, filled with noise, blobs and lines, then resolved through a palette.
// Coordinates are 320x200 pixel units ("VGA coordinates", demo.h): pixel (x, y) covers [x, x+1] x [y, y+1].
class Canvas {
public:
    explicit Canvas(Ctx& c);

    // Starts a frame: the index buffer at the frame size, cleared to 0, bound.
    void begin(Ctx& c);
    // Uniform integer noise 0..levels-1 per grain over the whole frame; `frame` picks the pattern (a new one per
    // drawn frame of the original: pass the refresh number). grain: the noise cell in output pixels (the
    // original's is one 320x200 pixel; see okean/remake/README.md "Noise").
    void noise(Ctx& c, int levels, uint32_t frame);
    // Blobs: the 8x8 kernel `k` with its top-left pixel at (x, y), added with weight w.
    void setKernel(const std::array<uint8_t, 64>& k);
    void blob(float x, float y, float w = 1.0f);
    // A line as the parts' line drawers draw it (notes-morph-star.md 1.2): n = max(|dx|, |dy|) / step blobs at
    // P1 + i (P2 - P1) / n, i = 0..n-1, each with its top-left at the point (step 2 in most parts, 4 in the title's
    // beams and the objects' first orders). Endpoints in pixel units (fractional).
    void line(float x1, float y1, float x2, float y2, float w = 1.0f, float step = 2.0f);
    // Draws the blobs collected since the last flush (additive).
    void flush(Ctx& c);
    // Adds a constant to every pixel (or a rectangle).
    void add(Ctx& c, float v, float x = 0, float y = 0, float w = 320, float h = 200);

    // The index buffer (R16F at the frame size), for parts that draw into it with their own shaders (bound by
    // begin(); additive blending is the parts' business then).
    Target& buffer() { return *buf_; }

    // Resolves the index buffer into ctx.out through the palette: colour = pal[index] with linear interpolation
    // between entries, indices clamped to 0..255. ramp = true: every entry past the ramp's first white one is white
    // (the font's grey 254 is only for the text, see notes-core.md).
    void resolve(Ctx& c, const std::array<Vec3, 256>& pal, bool ramp = true);

private:
    TargetPool::Lease buf_;
    Shader noise_, blob_, resolve_, flat_;
    Texture kernel_, pal_;
    GLuint vao_ = 0, vbo_ = 0;
    std::vector<float> blobs_;   // x, y, w per blob
};

}  // namespace okean

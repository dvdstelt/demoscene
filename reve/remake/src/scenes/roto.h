// Shared by scenes/wiz.cpp (entry 7), scenes/dots.cpp (entries 12, 13) and scenes/twirl.cpp (entries 9, 10, 15)
// (reve/re/notes-parts2d-a.md sections 0, 3, 5): the retrace-call counts of the script entries, the angle walk that
// the retrace routines 0x204c5d and 0x2056f3 step, the rotozoom layers 0x25a69c / 0x25a73c as a continuous mapping,
// and their textures.
//
// The rotozoom layers (notes 3.3). Each call of 0x25a69c / 0x25a73c writes 160 x 100 texels: texel (k, j) goes to the
// screen pixel (2k + col, 2j + row), so one texture coordinate per 2 x 2 block, read nearest. Its coordinates, in
// texels (row u, column v), are u = S (k - 80) - C (j - 50), v = C (k - 80) + S (j - 50) with (S, C) the sine and
// cosine words of the angle << 16 >> 4 (layer 1, 16 x 16 texture, 4.28 fixed point) or >> 5 (layer 2, 64 x 64, 6.26).
// The remake evaluates that per output pixel with the block's centre as the sample point, (x, y) = (2k + 1, 2j + 1):
//   u = scale (sin t (x - 161) - cos t (y - 101)),  v = scale (cos t (x - 161) + sin t (y - 101))
// scale = amp / 2^17 (layer 1) or amp / 2^16 (layer 2) texels per VGA pixel, amp = the sine table's peak (32767), and
// reads the texture filtered in colour (bicubic, tiled) instead of the nearest texel; the angle t = 2 pi (a / 2) / 4096
// is continuous instead of the table step floor(a / 2).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "music.h"
#include "planes.h"

namespace roto {

constexpr uint32_t kSin = 0x2176c8;        // 5120 signed words: 32767 sin(2 pi i / 4096); cos = kSin + 0x800
constexpr uint32_t kTextureName = 0x205ceb;  // "GFX\TEXTURE.RAW": [0x21b140], loaded by entries 0 and 12

inline int sinWord(const ReveExe& e, uint32_t byteIndex) { return e.s16(kSin + (byteIndex & 0x1ffe)); }

// The retrace calls entry e gets in the original: from its first retrace S (the cue's start) to the retrace of
// refresh P at which the timer interrupt sees the music at its end position, inclusive (timeline.cpp: P is where the
// next entry's init begins). Record S + n shows the state after n calls.
inline int callsOf(const Ctx& c, int e) {
    const ScriptEntry& s = c.script()[size_t(e)];
    double p = c.music ? std::max(s.start, c.music->refreshOfPosition(s.endPos)) : s.end - 1;
    return std::max(0, int(std::lround(p)) + 1 - int(std::lround(s.start)));
}

// The angle walk of 0x204c5d (entry 7) and 0x2056f3 (entries 12, 13): p += 10; a += SIN[p & 0x1ffe] >> shift.
// The immediates are read from the routine's code: `add dword ptr [0x2174cc], imm8` (imm8 at +6) and
// `sar eax, imm8` after `shl eax, 16` (imm8 at +2; shift = imm8 - 16).
struct AngleWalk {
    int32_t a = 0;
    uint32_t p = 0;
    int pStep = 0, shift = 0;
    void read(const ReveExe& e, uint32_t addStep, uint32_t sar) {
        pStep = e.s8(addStep + 6);
        shift = e.u8(sar + 2) - 16;
    }
    void step(const ReveExe& e) {
        p += uint32_t(pStep);
        a += sinWord(e, p) >> shift;   // arithmetic shift of the signed word: floor
    }
};

// A layer's texture: the square block of GFX\TEXTURE.RAW that an init copies (rows of `size` bytes from offset `off`
// of the 256-wide picture), through the palette of the RAW file `paletteName`; planes::loadTexture takes the upscale
// art/tex_texture.jpg when there is one.
inline void loadBlock(Ctx& c, Texture& tex, uint32_t off, int size, uint32_t paletteName) {
    planes::loadTexture(c, tex, kTextureName, paletteName, int(off & 0xff), int(off >> 8), size);
}

// GLSL: the layer's texture coordinate (for texture(), size = the block's texels) at VGA point p, angle t (radians).
inline const char* kRotoGLSL = R"(
vec2 rotoUV(vec2 p, float t, float scale, float size) {
    vec2 d = p - vec2(161.0, 101.0);
    float s = sin(t), c = cos(t);
    float u = scale * (s * d.x - c * d.y);   // texel row
    float v = scale * (c * d.x + s * d.y);   // texel column
    return vec2(v, u) / size;
}
vec3 rotoTex(sampler2D tex, vec2 uv) { return clamp(texBicubic(tex, uv).rgb, 0.0, 1.0); }
)";

}  // namespace roto

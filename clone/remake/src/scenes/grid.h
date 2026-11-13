// Craft's grid filler 0x402970 as a per-pixel shader, shared by parts 2 (tube.cpp), 5 (zoom.cpp) and 4
// (tunnels.cpp); clone/re/notes-grid.md section 3. Also the screen-ray rotation and the cylinder intersection that
// parts 2 and 4 share (0x419b05.. / 0x403b8b..), and the caption blits of the grid parts (0x4043be + 0x404002).
//
// The original evaluates each part's texture coordinates (u, v) and light at the 41 x 21 grid entries (pixel (8c,
// 20 + 8r)), interpolates them linearly over 8 x 8 blocks in 8.8 fixed point, samples the log-encoded grid texture
// [0x4207c8] with 4-bit bilinear weights, adds the light's high byte L to the level and decodes it with the exp
// table 0x420854: colour = min(255, (1 + f) e^((L - 128) / 23)). The remake evaluates the part's function at every
// pixel instead (the part's GLSL `gridMap`), with continuous u, v and L, samples the texture's levels in float
// (GridTextures below: filtered in the log domain as the filler does, mipmapped and anisotropic where the original
// shows block noise), adds L and decodes with the exp, keeping the clamp at white. Dropped: the 8 x 8 blocks and their
// wrong-way interpolation across the texture's wrap, the truncation steps of u, v, L, the level and the colour (their
// mean is kept: kLevelBias), the 4-bit weights, the one-texel seam at the wrap in v. Checked against the capture at
// its frames' drawn times: mean signed difference under 0.5 of 255 per channel, mean absolute 1..3.5.
//
// A part's fragment code defines
//   vec3 gridMap(vec2 p)   (u, v, L) at the original's pixel position p: the original's pixel (x, y) shows the
//                          grid function at p = (x, y), the top-left corner of the pixel (the grid entry (c, r) is
//                          at (8c, 20 + 8r)); u = texture row, v = texture column, in texels (unwrapped, period 256),
//                          L = the light word / 256 (128 = gain 1)
// and may use gridRotate / gridCylinder (kRayGLSL, prepended).
#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "clone/textures.h"
#include "demo.h"

namespace gridfill {

// The rows the filler writes: 20..179 (rows 0..19 and 180..199 are never written: black).
constexpr float kTop = 20, kRows = 160;

// The screen ray of parts 2 and 4 and the cylinder of radius 192 (notes-grid 5: rotate, cylinder).
inline const char* kRayGLSL = R"(
// rotate the screen ray (xs, ys, 1) by a3 in x-y, a2 in x-z, a1 in y-z; returns (x2, y3, z3)
vec3 gridRotate(vec2 s, vec3 a) {
    float x1 = sin(a.z) * s.y + cos(a.z) * s.x, y1 = cos(a.z) * s.y - sin(a.z) * s.x;
    float z2 = sin(a.y) * x1 + cos(a.y),        x2 = cos(a.y) * x1 - sin(a.y);
    return vec3(x2, sin(a.x) * z2 + cos(a.x) * y1, cos(a.x) * z2 - sin(a.x) * y1);
}
// forward distance from p along d (2D) to the circle of radius 192. The original divides by 2a + 0.001 (a guard
// against a = 0), which makes the distance collapse to a few units within a fraction of an original pixel of a
// tunnel's axis: a bright dot at the vanishing point that the original's grid of entries every 8 pixels practically
// never samples, but every pixel here would. So the guard acts as the original's down to 2a = 0.001 and then becomes
// 2a: the same distances elsewhere, growing to infinity along the axis.
float gridCylinder(vec2 d, vec2 p) {
    float a = dot(d, d), b = 2.0 * dot(p, d);
    float s = sqrt(max(b * b - a * 4.0 * (dot(p, p) - 36864.0), 0.0));
    float q = a * 2.0 + min(0.001, a * 2.0);
    float t1 = (s - b) / q, t2 = (-b - s) / q;
    return t1 > 0.0 ? t1 : t2;
}
)";

// A quad over a VGA rectangle that passes the VGA coordinates on (also used by part 11's tiles).
inline const char* kGridVS = R"(
uniform vec4 rect;   // x, y, w, h in VGA coordinates
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, 1.0 - vga.y / 100.0, 0.0, 1.0);
}
)";

inline const char* kGridFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D gridTex;   // the grid texture's levels L (GridTextures: column = s, row = t)
vec3 gridMap(vec2 p);
// The original truncates the texture's levels (0.63 levels below the formula on average, notes-gfx 4), the
// bilinear sum (floor), the light (L = word >> 8) and the decoded colour: continuous here, through the centres of
// the original's steps (half a step lower each), so that the colours stay the original's on average.
const float kLevelBias = 0.63 + 0.5 + 0.5;
void main() {
    vec3 m = gridMap(vga - 0.5);        // the pixel centre (x + 0.5, y + 0.5) shows the original's pixel (x, y)
    vec2 st = m.yx;                     // texel (row u, column v) of the grid filler = (s, t) = (v, u)
    vec2 dx = dFdx(st), dy = dFdy(st);  // the footprint, across the jumps of whole periods (part 2's atan2)
    dx -= 256.0 * round(dx / 256.0);
    dy -= 256.0 * round(dy / 256.0);
    vec3 c = textureGrad(gridTex, (st + 0.5) / 256.0, dx / 256.0, dy / 256.0).rgb;
    vec3 y = exp((c + m.z - kLevelBias - 256.0) / 23.0);                   // T[L + c] (0..255 scale)
    o = vec4(clamp(y - 0.5, 0.0, 255.0) / 255.0, 1.0);                       // trunc, clamped at white
}
)";

// The grid texture [0x4207c8] as the filler reads it (notes-gfx 2..4, textures.h): the colour table of 0x4033a7
// (table 1 until event 5, table 2 from it) as its stored levels c = 128 + 23 ln(V) on the original's 256 x 256
// texels with the integer cell radius (the texel style), in float without the truncations, filtered bilinearly in the
// log domain as the filler does (the geometric mean between texels) and decoded after the filter. The bilinear
// interpolation of these texels is what the original shows: square-ish cells with thin dark borders, where the
// continuous radius would give round spots. Mipmapped and anisotropic where the original minifies the texture (the
// far ends of the tunnels, part 5's strong zoom: block noise in the original).
class GridTextures {
public:
    explicit GridTextures(Ctx&) {}
    ~GridTextures() {
        for (Texture& t : tex_) t.destroy();
    }
    const Texture& get(const Ctx& c, double t) {
        return ensure(t < c.eventTime(5) ? 0 : 1);
    }
    // Table 1 (k = 0) or 2 (k = 1), built on first use.
    const Texture& ensure(int k) {
        if (tex_[k].id) return tex_[k];
        std::vector<float> px(256 * 256 * 4, 1.0f);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                Vec3 L = cmc::texLevel(k ? cmc::Tex::Table2 : cmc::Tex::Table1, x, y, cmc::TexStyle::Texel);
                float* p = &px[(size_t(y) * 256 + size_t(x)) * 4];
                p[0] = L.x;
                p[1] = L.y;
                p[2] = L.z;
            }
        tex_[k].createRaw(256, 256, GL_RGBA32F, GL_RGBA, GL_FLOAT, px.data(), true, true, true);
        gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        gl::GetError();  // anisotropy is optional
        return tex_[k];
    }

private:
    Texture tex_[2];
};

class Filler {
public:
    // partGLSL: uniforms and `vec3 gridMap(vec2 p)`.
    bool build(Ctx& c, const std::string& partGLSL, const char* name) {
        auto& tx = c.shared<GridTextures>();
        tx.ensure(0);  // now rather than at the first frame
        tx.ensure(1);
        return sh_.build(kGridVS, std::string(kRayGLSL) + partGLSL + kGridFS, name);
    }
    // Binds the shader and the grid texture of music time t (table 1, from event 5 on table 2) and returns the
    // shader: set the part's uniforms on it, then draw().
    const Shader& begin(Ctx& c, double t) {
        setBlend(Blend::None);
        sh_.use();
        c.shared<GridTextures>().get(c, t).bind(0);
        sh_.set("gridTex", 0);
        sh_.set("rect", 0.0f, kTop, 320.0f, kRows);
        return sh_;
    }
    void draw() const { drawQuad(); }

private:
    Shader sh_;
};

// A grid part's caption (notes-gfx 7.2): the black box 0x4043be, the text and the glyph by the blit 0x404002 (mode
// 0 or level 0: opaque), after the part's picture.
struct Caption {
    float bx, by, bw, bh;  // black box
    const char* text;
    float tx, ty;
    const char* glyph;
    float gx, gy;
};
inline void drawCaption(Ctx& c, const Caption& k) {
    setBlend(Blend::None);
    c.rect(k.bx, k.by, k.bw, k.bh, Vec3(0, 0, 0));
    c.overlay(k.text, k.tx, k.ty);
    c.overlay(k.glyph, k.gx, k.gy);
}

// An angle or phase reduced to [0, 2 pi) in double before it goes to a float uniform.
inline float phase(double a) {
    double p = std::fmod(a, 2.0 * 3.14159265358979323846);
    return float(p < 0 ? p + 2.0 * 3.14159265358979323846 : p);
}

}  // namespace gridfill

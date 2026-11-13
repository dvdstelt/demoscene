// Shared GPU code of the particle parts 1, 6 and 3 (swarm.cpp, ring.cpp, dots.cpp): the projection 0x404b84 and the
// 4 x 4 stamps 0x404772 / 0x40489c as sprites, Dune's radial blur 0x403df7 (0x40407c, 0x404137, 0x404278), and the
// 8-bit buffers as float intensity targets coloured per pixel through the palettes of 0x404a3e. The state they draw
// is in particles_sim.h (clone/re/notes-particles.md sections 3, 8).
//
// Evaluated exactly instead of the original's shortcuts:
//   - the projection without truncation to whole pixels; a stamp is a sprite of the same footprint and integral (a
//     separable cos^2 bump with the stamp's variance per axis, integrated over each target pixel: anti-aliased),
//     centred where the truncated 4 x 4 stamp is on average (1.5 pixels right of and below the projected point), and
//     clipped to x 0..320, rows 20..180 instead of dropped when not completely inside;
//   - the buffers are float intensities 0..255 (the saturation at 255 kept) at up to 1280 x 960, coloured per pixel
//     with min(255, C v / 92) (the palettes' formula; the 8 x 8 blocks of 0x404a08 were only its loop structure).
//
// The radial blur runs in place from the centre outwards and reads pixels it has already brightened: a recursion
// along every ray, weaker near the centre (where a sample's footprint includes the unprocessed pixel itself), whose
// per-step truncation keeps the streaks short of saturating at the strongest pulses (level 0.99). Order-free GPU
// versions only match it up to a level of about 0.8 (notes-particles 3.5). Here the pass is evaluated with its own
// order: every pixel of the original's 320 x 200 grid reads the processed value of a footprint pixel that the pass
// reaches before it (pass up / down, left half / right half, row, column: the order of 0x404137 / 0x404278) and the
// input value of the others; Jacobi iterations of that rule converge to exactly the sequential pass after as many
// iterations as its longest dependency chain (bit for bit with the integer arithmetic: tools/particlecheck.cpp
// checks it on the capture's frames against the sequential pass of models/particles/blur_model.py). In float, with
// the sample position continuous and the truncations of the level table and of the bilinear weights replaced by a
// loss of 0.5 level per step, 100 iterations converge in every frame, and the result is 1.5 levels from the
// original's pass on average over parts 1 and 6 (whose mean is 35..60 levels), at most 9 in a frame, also at the
// strongest pulses (where order-free versions are off by up to 40). On a grid twice as fine the recursion near the
// centre is not the original's (errors 2 to 6 times larger), so the blur stays on the original's grid: its echo
// term max(0, L v - loss) is computed there and added to the full-resolution stamps.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "render.h"
#include "scenes/particles_sim.h"

namespace particles {

// ------------------------------------------------------------------------------------------------ geometry
// M = Rz(c) Ry(b) Rx(a), 3 x 4 (0x404c9f, 0x404f0f, 0x404e36, 0x404d5c): each rotation recombines two rows.
struct Mat34 {
    double m[3][4];
};
inline Mat34 rotation(double a, double b, double c) {
    Mat34 M{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
    auto rot = [&](double ang, int r0, int r1) {
        double s = std::sin(ang), co = std::cos(ang);
        for (int k = 0; k < 4; k++) {
            double u = M.m[r0][k], v = M.m[r1][k];
            M.m[r0][k] = u * co - v * s;
            M.m[r1][k] = v * co + u * s;
        }
    };
    rot(a, 1, 2);
    rot(b, 2, 0);
    rot(c, 0, 1);
    return M;
}

struct Dot {
    float x, y;  // the stamp's centre in VGA coordinates
};

// 0x40507a + 0x404b84: camera at z = -300 looking along +z, x * 1.2 for the 4:3 pixel aspect, and the "fisheye"
// k (tx^2 + ty^2) that pulls outer points in. The stamp's top-left corner is the truncated projected point, so its
// centre lies 1.5 pixels right of and below that point on average. False behind the camera plane.
inline bool project(const Mat34& M, double x, double y, double z, double k, Dot& d) {
    double tx = M.m[0][1] * y + M.m[0][0] * x + M.m[0][2] * z + M.m[0][3];
    double ty = M.m[1][1] * y + M.m[1][0] * x + M.m[1][2] * z + M.m[1][3];
    double tz = M.m[2][1] * y + M.m[2][0] * x + M.m[2][2] * z + M.m[2][3];
    double depth = tz + 300.0;
    if (!(depth > 0)) return false;
    double q = ((tx * tx + ty * ty) * k + depth) * 0.0078125;
    d.x = float(tx / q * 1.2 + 160.0 + 1.5);
    d.y = float(ty / q + 100.0 + 1.5);
    return true;
}

// ------------------------------------------------------------------------------------------------ stamps
// The two 4 x 4 stamps (0x404772 mode 0, part 1; 0x40489c mode 1, parts 3 and 6). Both are separable (the outer
// product of their row sums / total, to within the rounding of the table).
constexpr int kStamp[2][16] = {{21, 41, 41, 21, 41, 73, 73, 41, 41, 73, 73, 41, 21, 41, 41, 21},
                               {3, 5, 5, 3, 5, 6, 6, 5, 5, 6, 6, 5, 3, 5, 5, 3}};

// A sprite: integral (level x VGA pixel^2) and the half-width of the cos^2 bump (VGA pixels).
struct Kernel {
    float integral = 0, halfWidth = 0;
};
// The stamp of `mode` as a sprite: the same integral, and per axis the variance of the stamp's 1-pixel cells
// (the marginal's variance + 1/12); `extraVariance` widens it (part 3: the blur its stamps get in their frame).
inline Kernel stampKernel(int mode, double extraVariance = 0.0) {
    double total = 0, var = 0;
    for (int i = 0; i < 16; i++) {
        double u = (i % 4) - 1.5;
        total += kStamp[mode][i];
        var += kStamp[mode][i] * u * u;
    }
    var = var / total + 1.0 / 12.0 + extraVariance;
    // variance of cos^2(pi u / 2w) on [-w, w] (normalised) = w^2 (1/3 - 2/pi^2)
    return {float(total), float(std::sqrt(var / (1.0 / 3.0 - 2.0 / (kPi * kPi))))};
}

// ------------------------------------------------------------------------------------------------ GL
inline const char* kVgaVS = R"(
uniform vec4 rect;    // x, y, w, h in VGA coordinates
uniform int flipY;    // 1: VGA y = 0 at the top of the target (ctx.out); 0: at texel row 0 (our buffers)
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = rect.xy + c * rect.zw;
    gl_Position = vec4(vga.x / 160.0 - 1.0, flipY != 0 ? 1.0 - vga.y / 100.0 : vga.y / 100.0 - 1.0, 0.0, 1.0);
}
)";

inline const char* kSpriteVS = R"(
layout(location = 0) in vec2 centre;
uniform float reach;
flat out vec2 c;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    c = centre;
    vec2 p = centre + corner * reach;
    gl_Position = vec4(p.x / 160.0 - 1.0, p.y / 100.0 - 1.0, 0.0, 1.0);
}
)";
// The bump's integral over the part of the target pixel inside the clip rectangle, per VGA pixel^2.
inline const char* kSpriteFS = R"(
flat in vec2 c;
out vec4 o;
uniform vec2 px;     // VGA units per target pixel
uniform float hw;    // half-width
uniform float amp;   // integral
uniform vec4 clip;   // x0, y0, x1, y1
float K(float u) { float s = clamp(u / hw, -1.0, 1.0); return 0.5 + 0.5 * s + sin(PI * s) / (2.0 * PI); }
void main() {
    vec2 p0 = floor(gl_FragCoord.xy) * px, p1 = p0 + px;
    vec2 a = max(p0, clip.xy), b = min(p1, clip.zw);
    if (b.x <= a.x || b.y <= a.y) discard;
    float v = (K(b.x - c.x) - K(a.x - c.x)) * (K(b.y - c.y) - K(a.y - c.y));
    o = vec4(amp * v / (px.x * px.y), 0.0, 0.0, 0.0);
}
)";

// One iteration of the in-place pass, on the 320 x 200 grid (texel row = VGA row).
inline const char* kJacobiFS = R"(
out vec4 o;
uniform sampler2D inTex;   // the stamps (saturating at 255)
uniform sampler2D xTex;    // the previous iterate
uniform float level, loss, zy;
uniform int echoOnly;
int key(ivec2 p) {         // the order of 0x404137 / 0x404278: rows up from 100 then down from 101, left half
    int pass = p.y <= 100 ? 0 : 1, side = p.x <= 159 ? 0 : 1;          // (columns 159..0) before the right half
    int ri = pass == 0 ? 100 - p.y : p.y - 101, ci = side == 0 ? 159 - p.x : p.x - 160;
    return ((pass * 2 + side) * 256 + ri) * 512 + ci;
}
float inp(ivec2 c) { return min(255.0, texelFetch(inTex, c, 0).r); }
float val(ivec2 c, int kp) {
    if (c.x < 0 || c.x > 319 || c.y < 0 || c.y > 199) return 0.0;
    return key(c) < kp ? texelFetch(xTex, c, 0).r : inp(c);
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int kp = key(p);
    vec2 q = vec2(p) + (vec2(p) - vec2(160.0, 100.0)) * vec2(-0.025, zy);   // zoom -0.025, rows * 5/6 * sigma
    vec2 fl = floor(q), f = q - fl;
    ivec2 b = ivec2(fl);
    float v = mix(mix(val(b, kp), val(b + ivec2(1, 0), kp), f.x),
                  mix(val(b + ivec2(0, 1), kp), val(b + ivec2(1, 1), kp), f.x), f.y);
    float e = max(0.0, level * v - loss);
    o = vec4(echoOnly != 0 ? e : min(255.0, inp(p) + e), 0.0, 0.0, 0.0);
}
)";

// Shared GL objects of the particle parts (c.shared<particles::Gl>()).
struct Gl {
    Shader sprite, jacobi;
    GLuint vao = 0, vbo = 0;
    size_t cap = 0;

    explicit Gl(Ctx&) {
        sprite.build(kSpriteVS, kSpriteFS, "particle sprite");
        jacobi.build(kFullscreenVS, kJacobiFS, "radial blur");
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Dot), nullptr);
        gl::VertexAttribDivisor(0, 1);
        gl::EnableVertexAttribArray(0);
        gl::BindVertexArray(0);
    }
    ~Gl() {
        if (vbo) gl::DeleteBuffers(1, &vbo);
        if (vao) gl::DeleteVertexArrays(1, &vao);
        if (sprite.id()) gl::DeleteProgram(sprite.id());
        if (jacobi.id()) gl::DeleteProgram(jacobi.id());
    }
    Gl(const Gl&) = delete;
    Gl& operator=(const Gl&) = delete;

    // Adds the sprites to target t (covering the whole VGA frame), clipped to x 0..320, rows y0..y1.
    void sprites(const Target& t, const std::vector<Dot>& dots, Kernel k, float y0 = 20, float y1 = 180) {
        if (dots.empty()) return;
        t.bind();
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        gl::Enable(GL_BLEND);
        sprite.use();
        float px = 320.0f / float(t.w), py = 200.0f / float(t.h);
        sprite.set("px", px, py);
        sprite.set("hw", k.halfWidth);
        sprite.set("amp", k.integral);
        sprite.set("reach", k.halfWidth + 2.0f * std::max(px, py));
        sprite.set("clip", 0.0f, y0, 320.0f, y1);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        size_t bytes = dots.size() * sizeof(Dot);
        if (bytes > cap) {
            cap = bytes * 2;
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(cap), nullptr, GL_STREAM_DRAW);
        }
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), dots.data());
        gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(dots.size()));
        gl::BindVertexArray(0);
        gl::Disable(GL_BLEND);
    }
};

inline void clearTarget(const Target& t) {
    t.bind();
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(GL_COLOR_BUFFER_BIT);
}

// Intensity buffers: the frame's size up to 1280 x 960 (the sprites are soft; the palette is applied at full size).
inline void bufferSize(const Ctx& c, int& w, int& h) {
    w = std::min(c.w, 1280);
    h = std::max(1, int(std::lround(double(w) * c.h / c.w)));
}

// Dune's radial blur (0x403df7) on the original's grid, see the top of this file. Draw the stamps into `in` (320 x
// 200, VGA pixel units, unclamped sums), then run(): `echo` holds max(0, L v - loss) per original pixel, the amount
// the pass adds to the pixel's own value (the result is min(255, stamps + echo)).
struct RadialBlur {
    static constexpr int kIterations = 100;  // the float rule converges within ~90 in every frame of parts 1, 6
    static constexpr float kLoss = 0.5f;     // the truncations of 0x40407c's level table and of 0x403f10, per step
    Target in, x[2], echo;

    void create() {
        in.create(320, 200, false, GL_R32F);
        x[0].create(320, 200, false, GL_R32F);
        x[1].create(320, 200, false, GL_R32F);
        echo.create(320, 200, false, GL_R32F);
    }
    void destroy() {
        in.destroy();
        x[0].destroy();
        x[1].destroy();
        echo.destroy();
    }
    // level: the blur level L; sigma: the vertical factor (mode 0: 1, mode 1: sin(phase)); zoom -0.025 and the 5/6
    // of 0x403df7.
    void run(Gl& g, float level, float sigma) {
        gl::Disable(GL_BLEND);
        clearTarget(x[0]);
        g.jacobi.use();
        g.jacobi.set("inTex", 0);
        g.jacobi.set("xTex", 1);
        g.jacobi.set("level", level);
        g.jacobi.set("loss", kLoss);
        g.jacobi.set("zy", float(-0.025 * 0.8333333333333333 * sigma));
        in.bindTex(0);
        for (int i = 0; i <= kIterations; i++) {
            bool last = i == kIterations;
            const Target& dst = last ? echo : x[(i + 1) & 1];
            dst.bind();
            x[i & 1].bindTex(1);
            g.jacobi.set("echoOnly", last ? 1 : 0);
            drawFullscreen();
        }
    }
};

// The GLSL that adds the blur's echo (bicubic from the 320 x 200 grid) to the full-resolution stamps.
inline const char* kEchoGLSL = R"(
float withEcho(float stamps, sampler2D echo, vec2 vga) {
    return min(255.0, min(255.0, stamps) + max(0.0, texBicubic(echo, vga / vec2(320.0, 200.0)).r));
}
)";

// A palette of 0x404a3e as the factors of min(255, C i / 92) / 255.
inline Vec3 paletteFactors(double r, double g, double b) {
    return Vec3(float(r / 92.0 / 255.0), float(g / 92.0 / 255.0), float(b / 92.0 / 255.0));
}

}  // namespace particles

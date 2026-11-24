// The first part, the title (0x2c26f, okean/re/notes-title-objects.md part 1): grey noise and 256 beams of light from
// the screen centre, drawn by the original as rows of 8x8 stamps that add 1 to the index, whose start points lie on an
// ellipse that pulses and slowly turns. The title lettering "the sea / ROBOT / of love", a one-bit 320x200 picture at
// 0x1502c, is never drawn: it removes light. Inside the ellipse every stamp whose top-left pixel lies on a letter is
// left out, and a beam whose start point lies on a letter loses its outer part, so the letters appear as dark shapes
// with dark radial shadows behind them. From row 0:56 the palette fades to black.
// Ports the set-up 0x2c26f (palette 0x2b65c, the radius phase [0x29a44]), the main loop 0x2c314 (the 256 points, the
// noise, the beams 0x2c0f9 with the outer drawer 0x2bc7c and the masked inner drawer 0x2bf4f, whose stamp is the 16
// adds at 0x2c054) and the callback 0x2c47b (rotation, radius, the fade).
//
// The beams are evaluated instead of stamped: a beam is the stamp's footprint integrated along the path of its
// top-left corner with the stamps' density (n = max(|dx|, |dy|) / 4 - 1 stamps over the path, as a continuous value;
// the stamp read from the exe and split into rectangles), so a pixel gets the number of stamps that cover it on
// average, as a band with exact edges. The letters: an inner beam leaves out every stamp whose top-left lies on a
// letter, so a pixel gets the share of the stamps covering it whose top-left is clear, the mask sampled along their
// part of the beam. The letters are thus soft shadows, the mask blurred by the stamp and displaced by its centre
// (+3.5 pixels right and down), as in the original, with smooth edges at any resolution. The mask is the picture
// "title_mask" (the bitmap from the exe, or a redraw if art/title_mask.png exists). Which beams keep their outer part is decided as in the original, per beam and callback.
// Left out: the full-height vertical bar of 15 records (the drawers' y "clip" adds a step to a pixel count, and the
// mask is then read outside the picture), the nearly horizontal beams that lose their outer part to the unsigned sort,
// and the stamps the drawers drop at x 311 / y 191 and above / left of the screen (the dark 8-pixel edges).
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"

namespace {

constexpr uint32_t kPalette = 0x2b65c;
constexpr uint32_t kMask = 0x1502c;      // 320 x 200 bytes, 1 = letter
constexpr uint32_t kStamp = 0x2c054;     // the stamp of the inner drawer 0x2bf4f (the outer one's is the same)
constexpr uint32_t kRadiusPhase = 0x29a44;   // q: the radius phase, 0xc00 in the file (the set-up keeps it)
constexpr int kBeams = 256;
constexpr int kMaxRects = 8;
constexpr int kFadeRow = 0x38;           // the callback fades from row 56 of order 0

// A rectangle of the stamp with its weight, in pixels relative to the stamp's top left (the original's pixel grid:
// a stamp at a continuous top left q covers floor(q) .. floor(q) + 7, on average q - 0.5 .. q + 7.5).
struct StampRect {
    float x0, y0, x1, y1, w;
};

// The 8x8 stamp as rectangles of equal value: runs of one value per row, merged with the same run in the next rows.
std::vector<StampRect> stampRects(const std::array<uint8_t, 64>& k) {
    struct Run { int c0, c1, v, r0; };
    std::vector<StampRect> out;
    std::vector<Run> open;
    for (int r = 0; r <= 8; r++) {
        std::vector<Run> row;
        for (int c = 0; r < 8 && c < 8;) {
            int v = k[size_t(r * 8 + c)], e = c + 1;
            while (e < 8 && k[size_t(r * 8 + e)] == v) e++;
            if (v) row.push_back({c, e, v, r});
            c = e;
        }
        for (const Run& o : open) {
            auto it = std::find_if(row.begin(), row.end(), [&](const Run& n) { return n.c0 == o.c0 && n.c1 == o.c1 && n.v == o.v; });
            if (it != row.end()) it->r0 = o.r0;
            else out.push_back({float(o.c0) - 0.5f, float(o.r0) - 0.5f, float(o.c1) - 0.5f, float(r) - 0.5f, float(o.v)});
        }
        open = row;
    }
    return out;
}

const char* kBeamVS = R"(
layout(location = 0) in vec4 seg;    // A, B: the path of the stamps' top-left corners (VGA units)
layout(location = 1) in vec4 info;   // S: the start point the outer beam tests; N: stamps along the path; kind
uniform vec4 kbox;                   // the stamp's footprint relative to its top left: x0, y0, x1, y1
uniform sampler2D mask;
uniform int sharp;
flat out vec4 vSeg;
flat out float vN;
flat out float vInner;
// The mask at the start point: the pixel that holds it (outside the picture: clear).
float maskAt(vec2 s) {
    if (s.x < 0.0 || s.y < 0.0 || s.x >= 320.0 || s.y >= 200.0) return 0.0;
    vec2 uv = s / vec2(320.0, 200.0);
    if (sharp != 0) return texelFetch(mask, ivec2(uv * vec2(textureSize(mask, 0))), 0).r;
    return textureLod(mask, uv, 0.0).r;
}
void main() {
    float on = info.w > 0.5 ? 1.0 : (maskAt(info.xy) < 0.5 ? 1.0 : 0.0);
    vec2 A = seg.xy, d = seg.zw - seg.xy;
    float len = length(d);
    vec2 u = len > 1e-4 ? d / len : vec2(1.0, 0.0), nv = vec2(-u.y, u.x);
    vec2 c0 = 0.5 * (kbox.xy + kbox.zw), hs = 0.5 * (kbox.zw - kbox.xy) + vec2(0.05);
    float ed = hs.x * abs(u.x) + hs.y * abs(u.y), en = hs.x * abs(nv.x) + hs.y * abs(nv.y);
    float cx = float(gl_VertexID & 1), cy = float(gl_VertexID >> 1) * 2.0 - 1.0;
    vec2 p = A + d * cx + c0 + u * ed * (2.0 * cx - 1.0) + nv * en * cy;
    vSeg = seg;
    vN = info.z * on;
    vInner = info.w;
    gl_Position = on > 0.0 ? vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)";

const char* kBeamFS = R"(
flat in vec4 vSeg;
flat in float vN;
flat in float vInner;
out vec4 o;
uniform vec4 rects[8];
uniform float rectw[8];
uniform int nrects;
uniform vec2 size;       // the index buffer in pixels
uniform sampler2D mask;
uniform int sharp;
// Narrows [t0, t1] to the t where a + t d lies in [lo, hi].
void clip1(float a, float d, float lo, float hi, inout float t0, inout float t1) {
    if (abs(d) > 1e-5) {
        float ta = (lo - a) / d, tb = (hi - a) / d;
        t0 = max(t0, min(ta, tb));
        t1 = min(t1, max(ta, tb));
    } else if (a < lo || a > hi) {
        t1 = -1.0;
    }
}
void main() {
    vec2 p = vec2(gl_FragCoord.x * 320.0 / size.x, 200.0 - gl_FragCoord.y * 200.0 / size.y);
    vec2 A = vSeg.xy, d = vSeg.zw - vSeg.xy;
    float s = 0.0;
    for (int i = 0; i < nrects; i++) {
        // the stamps whose rectangle covers p: top left in p - (x1, y1) .. p - (x0, y0)
        float t0 = 0.0, t1 = 1.0;
        clip1(A.x, d.x, p.x - rects[i].z, p.x - rects[i].x, t0, t1);
        clip1(A.y, d.y, p.y - rects[i].w, p.y - rects[i].y, t0, t1);
        if (t1 <= t0) continue;
        // inner beams (0x2bf4f): a stamp whose top left lies on a letter is left out, so p gets the share of those
        // stamps whose top left is clear (the mask sampled along their part of the path)
        float clear = 1.0;
        if (vInner > 0.5) {
            float on = 0.0;
            for (int k = 0; k < 8; k++) {
                // the pixel that holds the stamp's top left (no derivatives here: non-uniform control flow)
                vec2 q = A + d * mix(t0, t1, (float(k) + 0.5) / 8.0);
                if (sharp != 0) {
                    ivec2 ts = textureSize(mask, 0);
                    ivec2 px = clamp(ivec2(floor(q / vec2(320.0, 200.0) * vec2(ts))), ivec2(0), ts - 1);
                    on += (q.x < 0.0 || q.y < 0.0 || q.x >= 320.0 || q.y >= 200.0) ? 0.0 : texelFetch(mask, px, 0).r;
                } else {
                    on += textureLod(mask, q / vec2(320.0, 200.0), 0.0).r;
                }
            }
            clear = 1.0 - on / 8.0;
        }
        s += rectw[i] * (t1 - t0) * clear;
    }
    o = vec4(s * vN);
}
)";

class TitleScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& exe = *c.exe;
        pal_ = rampOf(okean::palette(exe, kPalette));
        q0_ = exe.s32(kRadiusPhase);
        sine_ = std::make_unique<okean::Sine>(exe);
        auto k = okean::kernel(exe, kStamp);
        rects_ = stampRects(k);
        if (rects_.size() > size_t(kMaxRects)) rects_.resize(size_t(kMaxRects));
        // the footprint of the stamp, relative to its top left
        kbox_ = {1e9f, 1e9f, -1e9f, -1e9f};
        for (const StampRect& r : rects_) {
            kbox_[0] = std::min(kbox_[0], r.x0);
            kbox_[1] = std::min(kbox_[1], r.y0);
            kbox_[2] = std::max(kbox_[2], r.x1);
            kbox_[3] = std::max(kbox_[3], r.y1);
        }

        // the mask: the picture "title_mask" if there is one, else the bitmap (1 = letter) from the exe
        if (!findPicture("title_mask")) {
            std::vector<uint8_t> m(320 * 200);
            for (uint32_t i = 0; i < 320 * 200; i++) m[i] = exe.u8(kMask + i) ? 255 : 0;
            ownMask_.createRaw(320, 200, GL_R8, GL_RED, GL_UNSIGNED_BYTE, m.data(), true, false, false);
        }

        shader_.build(kBeamVS, kBeamFS, "title beams");
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        for (GLuint a = 0; a < 2; a++) {
            gl::EnableVertexAttribArray(a);
            gl::VertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                                    reinterpret_cast<const void*>(size_t(a) * 4 * sizeof(float)));
            gl::VertexAttribDivisor(a, 1);
        }
        gl::BindVertexArray(0);
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        double cb = std::max(0.0, c.callbacks(r));
        canvas.begin(c);
        canvas.noise(c, 8, uint32_t(std::floor(r)));

        // 0x2c314: the 256 points at the callback count (p = c, q = q0 + 4 c, 1.4), as continuous curves
        double R = double(0x400) + okean::Sine::s(double(q0_) + 4.0 * cb);
        startPixels(int(std::floor(cb)));
        inst_.clear();
        const double cx = 160.0, cy = 100.0;
        for (int i = 0; i < kBeams; i++) {
            double a = cb + 16.0 * i;
            double x = 1.5 * okean::Sine::s(a) * R / 2048.0, y = okean::Sine::c(a) * R / 2048.0;
            double sx = cx + x / 8.0, sy = cy + y / 8.0;     // the start point S (0x2c0f9: z = 0x400)
            double fx = cx + 8.0 * x, fy = cy + 8.0 * y;     // the far point F (z = 0x10)
            // the pixel whose mask byte decides the outer beam (its centre), as the record shows it
            double tx = start_[size_t(2 * i)] + 0.5, ty = start_[size_t(2 * i + 1)] + 0.5;
            // outer beam 0x2bc7c: from S outwards, if the mask at S is clear (tested on the GPU)
            beam(sx, sy, fx, fy, tx, ty, false);
            // inner beam 0x2bf4f: between S and the centre, sorted by y as unsigned (S first when 0 <= S.y < 100)
            if (sy >= 0.0 && sy < cy) beam(sx, sy, cx, cy, tx, ty, true);
            else beam(cx, cy, sx, sy, tx, ty, true);
        }
        drawBeams(c, canvas);

        // the palette after n + 1 callbacks (written after the retrace), blended between the records
        Steps st = stepsOf(r);
        double f = fade(c, st.n) * (1.0 - st.frac) + fade(c, st.n + 1) * st.frac;
        canvas.resolve(c, okean::scaled(pal_, f, 256.0));
    }

private:
    // The ramp of the palette as the part reaches it: its last entries (104..) are white but for 111 (63, 34, 63)
    // and 254, which the original never reaches (the index stays under 81); a continuous sum may pass them, so the
    // ramp ends at the first entry that is much darker than the one before it.
    static std::array<Vec3, 256> rampOf(std::array<Vec3, 256> p) {
        auto luma = [](const Vec3& v) { return 0.299f * v.x + 0.587f * v.y + 0.114f * v.z; };
        for (size_t i = 1; i < 256; i++)
            if (luma(p[i]) < luma(p[i - 1]) - 0.1f) {
                for (size_t j = i; j < 256; j++) p[j] = p[i - 1];
                break;
            }
        return p;
    }

    // The start points' pixels (X1, Y1) after n callbacks, with the original's integer arithmetic and its sine table
    // (0x2c314 and 0x2c0f9). Which beams keep their outer part is a yes or no per beam and callback; around the
    // "B", whose bar and counters are a few pixels high, it depends on these truncations (records 63..71: the first
    // beams leave from the lower counter only), so the remake makes the same decisions, once per callback.
    void startPixels(int n) {
        if (n == startN_) return;
        startN_ = n;
        const okean::Sine& T = *sine_;
        int32_t R = T.T(q0_ + 4 * n) + 0x400;
        for (int i = 0; i < kBeams; i++) {
            int a = (n + 16 * i) & 0xfff;
            int32_t x = ((3 * T.T(a)) >> 1) * R >> 11, y = (T.C(a) * R) >> 11;
            start_[size_t(2 * i)] = 160 + (x * 128) / 0x400;     // idiv: truncated toward zero
            start_[size_t(2 * i + 1)] = 100 + (y * 128) / 0x400;
        }
    }

    // One beam as the drawers lay it out: n = max(|dx|, |dy|) / 4 - 1 stamps (the count's floor, continuous: its
    // average), from P1 in steps of (P2 - P1) / n; P2 is not reached. As a path of top-left corners with n stamps
    // spread evenly over it, each stamp standing for half a step either side: P1 - step / 2 .. P2 - step / 2. The
    // outer beam's path is cut where it leaves the screen (its stamps beyond cannot reach it).
    void beam(double x1, double y1, double x2, double y2, double sx, double sy, bool inner) {
        double dx = x2 - x1, dy = y2 - y1, L = std::max(std::fabs(dx), std::fabs(dy));
        double n = (L + 1.0) / 4.0 - 1.5;
        if (n <= 0.0) return;
        double h = 0.5 / n;                   // half a step as a fraction of P1 -> P2
        double ax = x1 - dx * h, ay = y1 - dy * h, bx = x2 - dx * h, by = y2 - dy * h;
        if (!inner) {
            double t = 1.0, ex = bx - ax, ey = by - ay;
            auto exitAt = [&](double a, double d, double lo, double hi) {
                if (d > 0) t = std::min(t, (hi - a) / d);
                else if (d < 0) t = std::min(t, (lo - a) / d);
            };
            exitAt(ax, ex, -9.0, 329.0);
            exitAt(ay, ey, -9.0, 209.0);
            if (t <= 0.0) return;
            bx = ax + ex * t;
            by = ay + ey * t;
            n *= t;
        }
        const float v[8] = {float(ax), float(ay), float(bx), float(by), float(sx), float(sy), float(n), inner ? 1.0f : 0.0f};
        inst_.insert(inst_.end(), v, v + 8);
    }

    void drawBeams(Ctx& c, okean::Canvas& canvas) {
        if (inst_.empty()) return;
        // the mask texture: the registered picture (redraw or original pixels) or the exe's bitmap
        const Texture* mask = &ownMask_;
        bool sharp = true;
        if (findPicture("title_mask")) {
            const Picture& pic = c.picture("title_mask");
            mask = &pic.tex;
            sharp = !pic.redraw;
        }
        Target& buf = canvas.buffer();
        buf.bind();
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        gl::Enable(GL_BLEND);
        shader_.use();
        mask->bind(0);
        shader_.set("mask", 0);
        shader_.set("sharp", sharp ? 1 : 0);
        shader_.set("size", float(buf.w), float(buf.h));
        shader_.set("kbox", kbox_[0], kbox_[1], kbox_[2], kbox_[3]);
        float rv[4 * kMaxRects] = {}, rw[kMaxRects] = {};
        for (size_t i = 0; i < rects_.size(); i++) {
            rv[4 * i] = rects_[i].x0;
            rv[4 * i + 1] = rects_[i].y0;
            rv[4 * i + 2] = rects_[i].x1;
            rv[4 * i + 3] = rects_[i].y1;
            rw[i] = rects_[i].w;
        }
        gl::Uniform4fv(shader_.loc("rects"), kMaxRects, rv);
        gl::Uniform1fv(shader_.loc("rectw"), kMaxRects, rw);
        shader_.set("nrects", int(rects_.size()));
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(inst_.size() * sizeof(float)), inst_.data(), GL_STREAM_DRAW);
        gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(inst_.size() / 8));
        gl::BindVertexArray(0);
        setBlend(Blend::None);
    }

    // The fade factor (0..256) after the callbacks of record k's refresh (n + 1 of the part, 1.4 and 1.6): every
    // callback that sees row >= 56 of order 0 takes 8 off it.
    double fade(Ctx& c, int k) {
        if (firstFadeStart_ != c.start) {
            firstFadeStart_ = c.start;
            firstFade_ = -1;
            for (int cb = 1; cb < 4096; cb++) {
                int pos = c.positionAtCallback(cb);
                if (pos >= 64) break;
                if (pos >= kFadeRow) { firstFade_ = cb; break; }
            }
        }
        int m = k - int(std::lround(c.start)) + 1;   // callbacks done when record k's palette is written
        if (firstFade_ < 0 || m < firstFade_) return 256.0;
        return std::max(0.0, 256.0 - 8.0 * double(m - firstFade_ + 1));
    }

    std::array<Vec3, 256> pal_{};
    int32_t q0_ = 0;
    std::unique_ptr<okean::Sine> sine_;
    std::array<int, 2 * kBeams> start_{};
    int startN_ = -1;
    std::vector<StampRect> rects_;
    std::array<float, 4> kbox_{};
    Texture ownMask_;
    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0;
    std::vector<float> inst_;
    double firstFadeStart_ = -1;
    int firstFade_ = -1;
};

}  // namespace

REGISTER_SCENE(TitleScene, "title");

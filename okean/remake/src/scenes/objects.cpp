// The second part, the objects (0x2e779, okean/re/notes-title-objects.md part 2): an extruded 8-tooth cog drawn in
// several copies with glowing lines over horizontal bands. Orders 1-2: eight copies 1/8 turn apart form a ribbed ball
// that the camera backs out of; they spread on a ring and fly away. Orders 3-6: five copies along their axis form a
// tube that slides in, scrolls, swings and tumbles; from order 4 the palette blends from red to green; from 6:32 the
// tube slides out.
// Ports the set-up 0x2e779 (the step shift it patches into 0x2e376, the mesh 0x2e7c2 from the sine table, palette
// 0x2d9e4), the main loop 0x2e9ca (the background rows and their noise, the copies, the blend with palette 0x2dce4),
// the transform and projection 0x2e5f5, the line drawer 0x2e304 with its stamp (the 16 adds at 0x2e490) and the
// callback 0x2eb81 (stepped per callback from the music positions it sees, interpolated between callbacks).
//
// Kept: the vertices behind the camera projected as (x (1 - z), y (1 - z)), whose lines are spokes to the screen
// edge; the stamps' spacing (4 pixels, 2 from order 3) and their sparkle; the 1.2 vertical stretch. Left out: the line
// drawer's 8-bit stamp count for lines with an end off the screen (such lines are drawn in full) and the wrap of its
// 8.8 step; lines with both ends behind the camera, which that count always stopped far outside the screen; the
// index wrap past 255 with its byte carries (white); the stamps dropped at x 311 / y 191.
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"

namespace {

constexpr uint32_t kPalette0 = 0x2d9e4;   // red
constexpr uint32_t kPalette1 = 0x2dce4;   // green
constexpr uint32_t kEdges = 0x2d0e4;      // 96 x 8 bytes: vertex a in byte 0, b in byte 4
constexpr uint32_t kStamp = 0x2e490;      // the stamp's 16 adds in 0x2e304
constexpr uint32_t kShiftSetup = 0x2e77f; // mov byte [0x2e376], 2 (the set-up)
constexpr uint32_t kShiftTube = 0x2ecb3;  // mov byte [0x2e376], 1 (the callback, order >= 3)
constexpr uint32_t kShiftByte = 0x2e376;  // the immediate of 0x2e304's shr ecx, imm8
// The part's variables (dwords; their values in the file are the start values)
constexpr uint32_t kAngA = 0x2cabc, kAngB = 0x2c9bc, kXoff = 0x2ca94, kAngC0 = 0x2cab8, kAngD = 0x2ca98;
constexpr uint32_t kZdist = 0x2ca90, kTubeX = 0x2c9c4, kPh1 = 0x2caa8, kPh2 = 0x2caac, kPh3 = 0x2cab0;
constexpr uint32_t kBg1 = 0x2cac0, kBg2 = 0x2cac4, kBgd1 = 0x2cac8, kBgd2 = 0x2cacc, kPalw0 = 0x2c9ac;
constexpr uint32_t kPalw1 = 0x2c9b0;
constexpr int kEdgeCount = 96, kVerts = 64;
constexpr double kTurn = 6.283185307179586 / 4096.0;   // one table step in radians
constexpr double kAmp = 1022.0 / 1024.0;  // a rotation: products with table values, shifted by 10

const char* kBackgroundFS = R"(
in vec2 uv;
out vec4 o;
uniform vec2 size;        // the index buffer in pixels
uniform vec4 bg;          // bg1, bgd1, bg2, bgd2: the two sines' phases and steps per row
uniform float frame;
uniform vec2 grain;       // one original pixel in output pixels
float tsin(float a) { return 1022.0 * sin(a * (6.283185307 / 4096.0)); }
void main() {
    // 0x2e9ca: per row ((T[a] + 0x400) >> 6) + ((T[b] + 0x400) >> 8), evaluated per output row (row k's centre at k;
    // each truncation's average, half a unit, taken off)
    float y = (size.y - gl_FragCoord.y) * 200.0 / size.y - 0.5;
    float v = max((tsin(bg.x + bg.y * y) + 1024.0) / 64.0 - 0.5, 0.0) + max((tsin(bg.z + bg.w * y) + 1024.0) / 256.0 - 0.5, 0.0);
    // plus 0..3 from a 4-cell pattern per row, repeated across the row, new every frame
    vec2 cell = floor(gl_FragCoord.xy / grain);
    float h = hash12(vec2(mod(cell.x, 4.0), cell.y) + vec2(frame * 7.13, frame * 3.71));
    o = vec4(v + min(floor(h * 4.0), 3.0));
}
)";

// The callback's variables (0x2eb81), angles in 1/4096 turns and not wrapped (they are only used through sines).
struct State {
    double angA = 0, angB = 0, xoff = 0, angC0 = 0, angD = 0, zdist = 0, tubeX = 0;
    double ph1 = 0, ph2 = 0, ph3 = 0, palw0 = 0, palw1 = 0;
    int shift = 2;
    bool tube = false;    // the callback has seen order 3 (it then sets angB, zdist, the shift, ...)
};

struct Vtx { double x, y, z; };

class ObjectsScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& exe = *c.exe;
        pal0_ = okean::palette(exe, kPalette0);
        pal1_ = okean::palette(exe, kPalette1);
        kern_ = okean::kernel(exe, kStamp);
        // the step shift: the immediates the set-up and the callback write into 0x2e304 (mov byte [0x2e376], imm8)
        auto shiftOf = [&](uint32_t a) {
            if (exe.u8(a) != 0xc6 || exe.u8(a + 1) != 0x05 || exe.u32(a + 2) != kShiftByte)
                throw std::runtime_error("objects: not the step shift's patch");
            return int(exe.u8(a + 6));
        };
        shiftSetup_ = shiftOf(kShiftSetup);
        shiftTube_ = shiftOf(kShiftTube);
        // start values
        start_.angA = exe.s32(kAngA);
        start_.angB = exe.s32(kAngB);
        start_.xoff = exe.s32(kXoff);
        start_.angC0 = exe.s32(kAngC0);
        start_.angD = exe.s32(kAngD);
        start_.zdist = exe.s32(kZdist);
        start_.tubeX = exe.s32(kTubeX);
        start_.ph1 = exe.s32(kPh1);
        start_.ph2 = exe.s32(kPh2);
        start_.ph3 = exe.s32(kPh3);
        start_.palw0 = exe.s32(kPalw0);
        start_.palw1 = exe.s32(kPalw1);
        start_.shift = shiftSetup_;
        bg1_ = exe.s32(kBg1);
        bg2_ = exe.s32(kBg2);
        bgd1_ = exe.s32(kBgd1);
        bgd2_ = exe.s32(kBgd2);

        // 0x2e7c2: the cog, 64 vertices from the sine table: n = 32 h + 4 j + k, radii 1 and 3 table amplitudes
        okean::Sine sine(exe);
        for (int h = 0; h < 2; h++) {
            double z = h ? -0x400 : 0x400;
            int a = 0;
            for (int j = 0; j < 8; j++) {
                double s = sine.T(a), co = sine.C(a);
                verts_.push_back({s, co, z});
                verts_.push_back({3 * s, 3 * co, z});
                a += 0x100;
                s = sine.T(a);
                co = sine.C(a);
                verts_.push_back({3 * s, 3 * co, z});
                verts_.push_back({s, co, z});
                a += 0x100;
            }
        }
        for (int e = 0; e < kEdgeCount; e++) {
            int a = exe.u8(kEdges + 8u * uint32_t(e)), b = exe.u8(kEdges + 8u * uint32_t(e) + 4u);
            if (a >= kVerts || b >= kVerts) throw std::runtime_error("objects: edge table");
            edges_.push_back({a, b});
        }
        bgShader_.build(kFullscreenVS, kBackgroundFS, "objects background");
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        double n = std::max(0.0, c.callbacks(r));
        Steps st = stepsOf(n);

        // the callback's state after N and N + 1 callbacks, interpolated (not across the switch to the tube)
        stepper_.first = 1;
        stepper_.advance(double(st.n), [&] { s_ = start_; }, [&](int k) { step(s_, c.positionAtCallback(k)); });
        State s1 = s_;
        step(s1, c.positionAtCallback(st.n + 1));
        State s2 = s1;
        step(s2, c.positionAtCallback(st.n + 2));
        double fr = s1.tube == s_.tube ? st.frac : 0.0;
        State s = lerp(s_, s1, fr);

        // 0x2e9ca: the background
        canvas.begin(c);
        Target& buf = canvas.buffer();
        setBlend(Blend::None);
        bgShader_.use();
        bgShader_.set("size", float(buf.w), float(buf.h));
        bgShader_.set("bg", float(bg1_), float(bgd1_), float(bg2_ - 16.0 * n), float(bgd2_));
        bgShader_.set("frame", float(uint32_t(std::floor(r)) % 65536u));
        bgShader_.set("grain", std::max(1.0f, float(buf.w) / 320.0f), std::max(1.0f, float(buf.h) / 200.0f));
        drawFullscreen();

        // the copies: orders 1-2 eight cogs 0x1ff apart about the vertical axis, orders 3+ five along the tube
        canvas.setKernel(kern_);
        double spacing = double(1 << s.shift);
        int order = c.positionAtDraw(st.n) / 64;
        if (order < 3) {
            for (int k = 0; k < 8; k++) drawObject(canvas, s, s.angC0 + 0x1ff * k, 0.0, spacing);
        } else {
            // angC from the phases (0x2eb81); before the callback has seen order 3 the value the loop left
            double angC = s.tube ? ((okean::Sine::s(s.ph1) / 2.0 + okean::Sine::s(s.ph2)) / 2.0 + okean::Sine::s(s.ph3))
                                 : s.angC0 + 8 * 0x1ff;
            for (int k = 0; k < 5; k++) drawObject(canvas, s, angC, s.tubeX - 0x5000 + 0x2000 * k, spacing);
        }
        canvas.flush(c);

        // the palette after n + 1 callbacks (written after the retrace from order 4 on), blended between records
        double w = (s1.palw1 * (1.0 - st.frac) + s2.palw1 * st.frac) / double(1 << 11);   // (P0 w0 + P1 w1) >> 11
        std::array<Vec3, 256> pal;
        for (size_t i = 0; i < 256; i++) pal[i] = pal0_[i] * float(1.0 - w) + pal1_[i] * float(w);
        canvas.resolve(c, pal);
    }

private:
    // 0x2eb81 with the music position the callback sees.
    void step(State& s, int pos) const {
        int order = pos / 64, row = pos % 64;
        if (order >= 4 && s.palw0 != 0) { s.palw0 -= 8; s.palw1 += 8; }
        if (order < 3) {
            s.angD += 15;
            s.angC0 += 2;
            s.zdist += 0x40;
            if (order >= 2) {
                s.xoff += 0x100;
                s.angA += 0x40;
                if (row >= 0x20) s.zdist += 0x5f4;
            }
        } else {
            s.tube = true;
            s.shift = shiftTube_;
            s.xoff = 0;
            s.angB = 0x400;
            s.zdist = 0x1555;
            s.angC0 = 0x400;
            s.ph1 += 13;
            s.ph2 += 23;
            s.ph3 -= 9;
            s.angA += 16;
            s.angD += 5;
            s.tubeX += 0x100;
            // the tube loops (the cogs are 0x2000 apart) until the first callback that sees 6:32
            if (!(order >= 6 && row >= 0x20) && s.tubeX >= 0) s.tubeX = std::fmod(s.tubeX, double(0x2000));
        }
    }

    // a + (b - a) f per variable; the tube's wrap is undone (it moves on by one cog spacing).
    static State lerp(const State& a, const State& b, double f) {
        State s = a;
        auto l = [f](double x, double y) { return x + (y - x) * f; };
        s.angA = l(a.angA, b.angA);
        s.angB = l(a.angB, b.angB);
        s.xoff = l(a.xoff, b.xoff);
        s.angC0 = l(a.angC0, b.angC0);
        s.angD = l(a.angD, b.angD);
        s.zdist = l(a.zdist, b.zdist);
        double dt = b.tubeX - a.tubeX;
        if (dt < 0) dt += 0x2000;
        s.tubeX = a.tubeX + dt * f;
        s.ph1 = l(a.ph1, b.ph1);
        s.ph2 = l(a.ph2, b.ph2);
        s.ph3 = l(a.ph3, b.ph3);
        return s;
    }

    // 0x2e5f5: rotate, translate and project the 64 vertices, then the 96 edges in their order with 0x2e304.
    void drawObject(okean::Canvas& cv, const State& s, double angC, double xoff2, double step) {
        auto rot = [](double& u, double& v, double ang) {
            double cs = std::cos(ang * kTurn) * kAmp, sn = std::sin(ang * kTurn) * kAmp;
            double nu = u * cs - v * sn, nv = v * cs + u * sn;
            u = nu;
            v = nv;
        };
        std::array<double, 2 * kVerts> P{};
        std::array<bool, kVerts> behind{};
        for (int i = 0; i < kVerts; i++) {
            double x = verts_[size_t(i)].x, y = verts_[size_t(i)].y, z = verts_[size_t(i)].z;
            rot(x, y, s.angA);
            rot(x, z, s.angB);
            x += s.xoff + xoff2;
            rot(x, z, angC);
            rot(y, z, s.angD);
            z += s.zdist;
            double X, Y;
            if (z > 0) { X = x * 128.0 / z; Y = y * 128.0 / z; }
            else { X = x * (1.0 - z); Y = y * (1.0 - z); }   // behind the camera: thrown far out (kept)
            P[size_t(2 * i)] = X + 160.0;
            P[size_t(2 * i + 1)] = Y + 100.0;
            behind[size_t(i)] = z <= 0;
        }
        for (const auto& e : edges_) {
            // a line with both ends behind the camera never reaches the screen in the original (its 8-bit stamp
            // count stops it far outside); drawn in full it would add lines that were never shown
            if (behind[size_t(e[0])] && behind[size_t(e[1])]) continue;
            line(cv, P[size_t(2 * e[0])], P[size_t(2 * e[0] + 1)], P[size_t(2 * e[1])], P[size_t(2 * e[1] + 1)], step);
        }
    }

    // 0x2e304: sorted by y as unsigned (an end above the screen counts as below it); n = (max(|dx|, |dy|) + 1) >>
    // shift stamps from P1 at P1 + k (P2 - P1) / n, k = 0..n-1, each with its top left there (the original's floor:
    // on average half a pixel up and left). Every stamp that reaches the screen is drawn (no 8-bit count, no clip
    // at x 311 / y 191).
    static void line(okean::Canvas& cv, double x1, double y1, double x2, double y2, double step) {
        auto key = [](double y) { return y < 0 ? 4294967296.0 + y : y; };
        if (!(key(y1) < key(y2))) { std::swap(x1, x2); std::swap(y1, y2); }
        double dx = x2 - x1, dy = y2 - y1;
        double n = std::floor((std::max(std::fabs(dx), std::fabs(dy)) + 1.0) / step);
        if (n < 1.0) return;
        double sx = dx / n, sy = dy / n;
        // the stamps whose footprint (top left -1 .. +8, the kernel's bilinear surface) reaches the screen
        double k0 = 0.0, k1 = n - 1.0;
        auto clip = [&](double a, double d, double lo, double hi) {
            if (d == 0.0) {
                if (a <= lo || a >= hi) k1 = -1.0;
                return;
            }
            double ta = (lo - a) / d, tb = (hi - a) / d;
            k0 = std::max(k0, std::min(ta, tb));
            k1 = std::min(k1, std::max(ta, tb));
        };
        clip(x1, sx, -8.0, 321.0);
        clip(y1, sy, -8.0, 201.0);
        if (k1 < k0) return;
        double kb = std::ceil(k0), ke = std::min(std::floor(k1), kb + 4096.0);
        for (double k = kb; k <= ke; k += 1.0)
            cv.blob(float(x1 + k * sx - 0.5), float(y1 + k * sy - 0.5));
    }

    std::array<Vec3, 256> pal0_{}, pal1_{};
    std::array<uint8_t, 64> kern_{};
    int shiftSetup_ = 2, shiftTube_ = 1;
    State start_, s_;
    TickStepper stepper_;
    double bg1_ = 0, bg2_ = 0, bgd1_ = 0, bgd2_ = 0;
    std::vector<Vtx> verts_;
    std::vector<std::array<int, 2>> edges_;
    Shader bgShader_;
};

}  // namespace

REGISTER_SCENE(ObjectsScene, "objects");

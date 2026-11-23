// Script entries 9, 10 (scene "playmo": GFX\PLAYMO.RAW, a toy figure) and 15 (scene "bouche": GFX\BOUCHE.RAW, a
// mouth): the picture turned ring by ring about the screen centre (reve/re/notes-parts2d-a.md section 4).
//
// The original: the init (0x204de0, 0x2053cc) makes 120 lists of picture pixels, ring R = 1..120 about (160, 101) in
// VGA pixel units (circles in the 320 x 200 grid, ellipses on the 4:3 screen), each pixel in the largest ring that
// reaches it; the frame routine (0x204fe8, 0x205578) moves the colours of ring R = i + 1 by
// shift = (phase + B + i twist) * n >> 12 of its n entries, clockwise on screen; the retrace routines (0x204ef8 entry
// 9, 0x204f6f entry 10, 0x2054c4 entry 15) step phase and twist (and B in entry 15) per refresh as triangle waves.
// The state carries over: entry 10 continues entry 9's values with its own bounds and speeds, entry 15 sets phase and
// twist again but keeps the steps entry 10 left (its phase step stays 8 until the first bounce).
//
// The remake turns each point continuously instead of moving whole ring entries (which tears the picture along the
// rings): a point at distance rho from the centre belongs to ring R = rho + 0.5 and is turned by
// f = (phase + B + (R - 1) twist) / 4096 of its ring. A ring that leaves rows 1..199 (R > ~99.5) only exists on its
// left and right arcs, and its list walks them without the gaps, so its colours jump from one arc to the other at the
// top and bottom of the screen instead of leaving it: the remake turns those points along the walk of the visible
// arcs. Phase, twist and B are replayed per refresh and blended between steps. Points outside the rings show the
// picture as it is (PLAYMO's black background; entry 15's buffer is black there). Not reproduced: the centre pixel,
// which the lists give to ring 120 (a read past the sine table), and entry 15's two-pixel dot over it.
#include <cmath>
#include <vector>

#include "roto.h"

namespace {

// The variables (dwords): phase [0x217598], its step [0x21758c], twist [0x2175a0], its step [0x217594], B [0x2175b0]
// (entry 15), its step [0x217590]; initial values in the image.
constexpr uint32_t kPhase = 0x217598, kPhaseStep = 0x21758c, kTwist = 0x2175a0, kTwistStep = 0x217594;
constexpr uint32_t kB = 0x2175b0, kBStep = 0x217590;

// One triangle-wave variable of a retrace routine: v += step; if (step >= 0 ? v >= hi : v <= lo) step = the other.
// The addresses are the routine's `cmp dword ptr [v], imm` (imm8 after 83 3d, imm32 after 81 3d, at +6) and
// `mov dword ptr [step], imm32` (at +6) instructions.
struct Bounce {
    uint32_t cmpHi, setDown, cmpLo, setUp;
    int32_t hi = 0, down = 0, lo = 0, up = 0;
    void read(const ReveExe& e) {
        auto cmp = [&](uint32_t a) { return e.u8(a) == 0x83 ? int32_t(e.s8(a + 6)) : e.s32(a + 6); };
        hi = cmp(cmpHi);
        lo = cmp(cmpLo);
        down = e.s32(setDown + 6);
        up = e.s32(setUp + 6);
    }
    void step(int32_t& v, int32_t& s) const {
        v += s;
        if (s >= 0 ? v >= hi : v <= lo) s = s >= 0 ? down : up;
    }
};

struct Routine {
    Bounce phase, twist, b;
    bool hasB;
};
// clang-format off
Routine kEntry9  = {{0x204f08, 0x204f14, 0x204f20, 0x204f2c}, {0x204f45, 0x204f4e, 0x204f5a, 0x204f63}, {}, false};
Routine kEntry10 = {{0x204f7f, 0x204f8b, 0x204f97, 0x204fa3}, {0x204fbc, 0x204fc5, 0x204fd1, 0x204fda}, {}, false};
Routine kEntry15 = {{0x2054d4, 0x2054e0, 0x2054ec, 0x2054f8}, {0x205511, 0x20551a, 0x205526, 0x20552f},
                    {0x205548, 0x205554, 0x205560, 0x20556c}, true};
// clang-format on

struct State {
    int32_t phase = 0, dphase = 0, twist = 0, dtwist = 0, b = 0, db = 0;
    void step(const Routine& r) {
        r.phase.step(phase, dphase);
        r.twist.step(twist, dtwist);
        if (r.hasB) r.b.step(b, db);
    }
};

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D pic;
uniform int sharp;     // the original's pixels: sharp-edged texels
uniform float turn;    // phase + B
uniform float twist;
void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates, y down
    // the redraw's mipmap level from the screen's own scale: the turned coordinate jumps at the ring edges, and its
    // derivatives there would pick a coarse level (a faint line along the edge)
    vec2 gx = dFdx(p / vec2(320.0, 200.0)), gy = dFdy(p / vec2(320.0, 200.0));
    vec2 d = p - vec2(160.0, 101.0);
    float rho = length(d);
    vec2 src = p;
    float R = rho + 0.5;
    float lim = 99.5 / max(rho, 1e-3);                   // visible rows: |rho sin(psi)| < 99.5
    float psi = mod(atan(-d.y, d.x), 2.0 * PI);          // maths angle (y up)
    bool onArc = lim >= 1.0 || abs(sin(psi)) < lim;
    if (rho <= 120.5 && onArc) {
        float f = (turn + (R - 1.0) * twist) / 4096.0;
        float src_psi;
        if (lim >= 1.0) {
            src_psi = psi + 2.0 * PI * f;                // a full ring: turned by 2 pi f
        } else {
            // the list walks the visible arcs from psi = 2 pi downwards: A (2 pi - al, 2 pi], B (pi - al, pi + al),
            // C [0, al); a colour moves f L along the walk (L = 4 al)
            float al = asin(lim), L = 4.0 * al;
            float s = psi > 2.0 * PI - al ? 2.0 * PI - psi
                    : psi < al ? 3.0 * al + (al - psi)
                    : al + (PI + al - psi);
            float ss = mod(s - f * L, L);
            src_psi = ss < al ? 2.0 * PI - ss : ss < 3.0 * al ? PI + al - (ss - al) : al - (ss - 3.0 * al);
        }
        src = vec2(160.0, 101.0) + rho * vec2(cos(src_psi), -sin(src_psi));
    }
    vec2 t = src / vec2(320.0, 200.0);
    vec4 c = sharp != 0 ? texSharp(pic, t) : textureGrad(pic, t, gx, gy);
    o = vec4(c.rgb, 1.0);   // opaque pictures (premultiplied: rgb as it is)
}
)";

struct TwirlScene : Scene {
    const char* picture;   // pictures.cpp: "playmo" (GFX\PLAYMO.RAW), "bouche" (GFX\BOUCHE.RAW); own palettes, opaque
    explicit TwirlScene(const char* p) : picture(p) {}

    Shader sh;
    // per script entry (9, 10, 15): turn (phase + B) and twist after n of its calls, n = 0 .. calls + 1
    std::vector<int32_t> turn[16], twist[16];

    void init(Ctx& c) override {
        const ReveExe& e = *c.exe;
        for (Routine* r : {&kEntry9, &kEntry10, &kEntry15}) {
            r->phase.read(e);
            r->twist.read(e);
            if (r->hasB) r->b.read(e);
        }
        State s{e.s32(kPhase), e.s32(kPhaseStep), e.s32(kTwist), e.s32(kTwistStep), e.s32(kB), e.s32(kBStep)};
        auto run = [&](int entry, const Routine& r) {
            const int n = roto::callsOf(c, entry);
            State t = s;
            for (int k = 0; k <= n + 1; k++) {
                if (k) t.step(r);
                turn[entry].push_back(t.phase + t.b);
                twist[entry].push_back(t.twist);
                if (k == n) s = t;   // the state the next entry inherits
            }
        };
        run(9, kEntry9);
        run(10, kEntry10);
        // init 0x2053cc: mov dword ptr [phase], imm32 / [twist], imm32 (imm32 at +6)
        s.phase = e.s32(0x2054aa + 6);
        s.twist = e.s32(0x2054b4 + 6);
        run(15, kEntry15);
        sh.build(kFullscreenVS, kFS, "twirl");
    }

    static double at(const std::vector<int32_t>& v, double x) {
        x = std::clamp(x, 0.0, double(v.size() - 1));
        Steps s = stepsOf(x);
        size_t n = size_t(s.n), m = std::min(n + 1, v.size() - 1);
        return v[n] + (double(v[m]) - double(v[n])) * s.frac;
    }

    void render(Ctx& c, double r) override {
        const int e = c.cue->entry;
        if (e < 0 || e >= 16 || turn[e].empty()) return;
        const double x = c.refreshes(r);
        const Picture& p = c.picture(picture);
        setBlend(Blend::None);
        sh.use();
        p.tex.bind(0);
        sh.set("pic", 0);
        sh.set("sharp", p.redraw ? 0 : 1);
        sh.set("turn", float(at(turn[e], x)));
        sh.set("twist", float(at(twist[e], x)));
        drawFullscreen();
    }
};

struct PlaymoScene : TwirlScene {
    PlaymoScene() : TwirlScene("playmo") {}
};
struct BoucheScene : TwirlScene {
    BoucheScene() : TwirlScene("bouche") {}
};
}  // namespace

REGISTER_SCENE(PlaymoScene, "playmo");
REGISTER_SCENE(BoucheScene, "bouche");

// Script entry 7 (init 0x204bed, retrace routine 0x204c5d, frame routine 0x204cb4; reve/re/notes-parts2d-a.md
// section 3): two rotozoom layers turning against each other over the whole screen, a 16 x 16 speckled block and the
// 64 x 64 burst tile of GFX\TEXTURE.RAW, and GFX\WIZ.RAW (a cartoon character) pulsing in size in the middle; all
// through WIZ's palette, which the init sets (it is TEXTURE's).
//
// The retrace routine steps per refresh: the angle a (p += 10, a += SIN[p] >> 9: a sine-shaped speed) and the zoom
// byte z (+-15 per refresh, bouncing at 245 and 50). Record start + n shows the frame drawn after n calls; the
// remake replays the integer steps and blends between n and n + 1 by the fraction.
//
// The frame routine draws, per frame, layer 1 (0x25a69c: angle a) and layer 2 (0x25a73c: angle -a) into a quarter of
// the pixels each (every other column of every other row, the column and row parity cycling over four frames), so
// layer 2 overwrites layer 1 on every other frame and the buffer keeps the other classes from earlier frames: at any
// time layer 1 shows on 25 % of the pixels and layer 2 on 75 %, a fine flickering checker of the two textures, each
// class drawn 0 to 3 frames ago (at faster turns the textures leave trails towards the screen's edges). Over two
// frames the classes are: layer 1 drawn 0 and 1 frames ago, 1/8 each; layer 2 drawn 0, 1, 2 and 3 frames ago, 1/4,
// 1/4, 1/8, 1/8 (reve/re/models/parts2d-a/layers.py). The remake evaluates the layers per pixel (scenes/roto.h) at
// those times and shows that average of the checker (closer to the capture than one time: mean absolute difference
// of 4 x 4 box-filtered frames 5.1 instead of 11.6 at record 3500). Then WIZ, scaled by z / 256 with its
// top-left corner at (160 - z / 4, 100 - z / 4) (so not centred horizontally: the original's composition), index 0
// transparent, drawn as a picture (it can have a redraw). Not reproduced: the ghosts of the shrinking sprite (the
// buffer is not cleared and its pixels are rewritten only every 2 or 4 frames) and the black pixel classes of the
// first three frames.
#include "roto.h"

namespace {
constexpr uint32_t kWizName = 0x205d64;   // "GFX\WIZ.RAW" (init 0x204bed loads it with its palette)

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex1, tex2;
uniform vec2 ang1;     // layer 1's angle (radians) now and one refresh ago
uniform vec4 ang2;     // layer 2's angle now and 1, 2, 3 refreshes ago
uniform vec2 scale;    // texels per VGA pixel
uniform vec2 size;     // texture sizes (16, 64)
vec3 l1(vec2 p, float t) { return rotoTex(tex1, rotoUV(p, t, scale.x, size.x)); }
vec3 l2(vec2 p, float t) { return rotoTex(tex2, rotoUV(p, t, scale.y, size.y)); }
void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates, y down
    vec3 c = 0.125 * (l1(p, ang1.x) + l1(p, ang1.y)) + 0.25 * (l2(p, ang2.x) + l2(p, ang2.y)) +
             0.125 * (l2(p, ang2.z) + l2(p, ang2.w));
    o = vec4(c, 1.0);
}
)";

struct WizScene : Scene {
    Shader sh;
    Texture tex1, tex2;
    int size1 = 0, size2 = 0;
    float scale1 = 0, scale2 = 0;
    std::vector<int32_t> angle;   // a after n calls
    std::vector<int> zoom;        // z after n calls

    void init(Ctx& c) override {
        const ReveExe& e = *c.exe;
        // 0x204bed: 16 x 16 from TEXTURE + 0x6010 to [0x205e90], 64 x 64 from + 0x4040 to [0x205e94] (the offsets are
        // the imm32 of `add esi, imm32`; the row lengths are 4 x the dword counts of `mov ecx, imm32`)
        size1 = 4 * int(e.u32(0x204c06));
        size2 = 4 * int(e.u32(0x204c2f));
        roto::loadBlock(c, tex1, e.u32(0x204bfc), size1, kWizName);
        roto::loadBlock(c, tex2, e.u32(0x204c25), size2, kWizName);
        // texels per written pixel: the sine word << 16 >> sh, over 2^m per texel (shr ebx, m keeps the top 32 - m
        // bits); a written pixel is 2 VGA pixels wide
        const double amp = e.s16(roto::kSin + 0x800);   // the table's peak, 32767
        auto scale = [&](uint32_t sar, uint32_t shr) {
            return float(amp * std::ldexp(1.0, 16 - e.u8(sar + 2)) / std::ldexp(1.0, e.u8(shr + 2)) / 2);
        };
        scale1 = scale(0x25a6bc, 0x25a6f6);   // sar esi, 4; shr ebx, 28
        scale2 = scale(0x25a75e, 0x25a798);   // sar esi, 5; shr ebx, 26
        sh.build(kFullscreenVS, std::string(roto::kRotoGLSL) + kFS, "wiz");

        // 0x204c5d, replayed for the entry's calls (and two more, for the blend at the end)
        roto::AngleWalk w;
        w.read(e, 0x204c5e, 0x204c79);
        w.a = e.s32(0x2174c8);
        w.p = e.u32(0x2174cc);
        int z = e.u8(0x217570), dz = e.s8(0x217571);
        const int top = e.u8(0x204c97), bottom = e.u8(0x204ca8);   // cmp byte ptr [0x217570], 0xf5 / 0x32
        const int n = roto::callsOf(c, 7) + 2;
        angle.assign(1, w.a);
        zoom.assign(1, z);
        for (int k = 1; k <= n; k++) {
            w.step(e);
            z = (z + dz) & 0xff;
            if (dz > 0 ? z == top : z == bottom) dz = -dz;
            angle.push_back(w.a);
            zoom.push_back(z);
        }
    }

    // A per-call value at x calls (fractional), blended between the steps; before the first call: the initial value.
    template <class T> static double at(const std::vector<T>& v, double x) {
        x = std::clamp(x, 0.0, double(v.size() - 1));
        Steps s = stepsOf(x);
        size_t n = size_t(s.n), m = std::min(n + 1, v.size() - 1);
        return v[n] + (double(v[m]) - double(v[n])) * s.frac;
    }

    void render(Ctx& c, double r) override {
        const double x = c.refreshes(r);
        const double z = at(zoom, x);
        constexpr double kStep = 6.283185307179586 / 4096 / 2;   // radians per unit of a (a byte offset: 2 per step)
        auto t = [&](double age) { return float(at(angle, x - age) * kStep); };

        setBlend(Blend::None);
        sh.use();
        tex1.bind(0);
        tex2.bind(1);
        sh.set("tex1", 0);
        sh.set("tex2", 1);
        sh.set("ang1", t(0), t(1));
        sh.set("ang2", -t(0), -t(1), -t(2), -t(3));   // layer 2 turns by -a
        sh.set("scale", scale1, scale2);
        sh.set("size", float(size1), float(size2));
        drawFullscreen();

        const Picture& p = c.picture("wiz");   // GFX\WIZ.RAW, its palette, index 0 transparent
        const float k = float(z / 256);
        c.drawPicture(p, {.x = float(160 - z / 4), .y = float(100 - z / 4), .w = p.srcW * k, .h = p.srcH * k});
    }
};
}  // namespace

REGISTER_SCENE(WizScene, "wiz");

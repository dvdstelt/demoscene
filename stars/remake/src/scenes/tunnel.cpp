// tunnel: a voxel landscape (height map file 2, colour map file 3, palette file 4) wrapped round the inside of a
// cylinder, the camera on the axis flying forward; from 3b:00 the tunnel twists, from 3d:00 the twist travels
// (stars/re/notes-parts2d-a.md section 3). Ports 0x11e8 (the frame loop), 0x1064 (set-up), 0xf85 (the PIT callback:
// fade, forward motion, twist frequency and speed, the fade out from order 0x43), 0x17ed0 with the script at 0xf71
// (the speed ramp), 0x1158 + 0x11ce (the twist table), 0x10d6 (the projection table), 0x1261 (the voxel columns),
// 0x136a + 0x1320 (the polar map and the blit).
//
// Evaluated exactly instead of the original's shortcuts: per output pixel, a ray at the pixel's continuous radius
// and angle marched through the same 105 depth steps (z = 2 s + 9, one map column per step, the twist W shifting the
// rows), with bilinear height reads, the hit depth found between the two steps (instead of integer radii per step
// and 256 angle columns), the colour read at that depth from the colour map converted to RGB through palette 4 and
// filtered (instead of the nearest index), continuous forward motion col_pos / 256 (the original uses whole texels:
// the landscape jumps 1.5 texels per drawn frame), the twist from the continuous sine, its frequency as a ramp
// through the middle of the original's +1 steps every 4 ticks. Dropped: the row carry when the column address wraps
// (one row = 1/256 turn past the wrap depth).
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "stars/core.h"

using namespace stars;

namespace {

const char* kTunnelFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D hmap;    // file 2: height, 256 x 256 (column, row), linear, wrapping
uniform sampler2D cmap;    // file 3 through palette 4, RGB, mipmapped, wrapping
uniform float c0;          // forward position: map column of steps 0 and 1
uniform float r0;          // map row of angle 0
uniform float W[105];      // twist in rows at step s: W(s - 1), W(-1) = 0
uniform vec3 far;          // colour 0 of palette 4 (where no step hits: the end of the view)
uniform float level;       // fade level / 256
uniform vec2 px;           // VGA units per output pixel

const float kTau = 6.283185307179586;

float height(vec2 q) { return textureLod(hmap, q / 256.0, 0.0).r * 255.0; }

// The colour of the view at VGA point p, and the depth it shows (1000: none).
vec4 view(vec2 p) {
    vec2 d = p - vec2(160.0, 100.0);
    float rho = length(d);                        // original pixels from the centre (rays of 0x136a)
    float th = atan(d.y, -d.x);                   // angle 0 points left (x -= cos per ray step)
    float a = th / kTau * 256.0;                  // map rows: one per 1/256 turn
    // step s at depth z = 2 s + 9 reads column c0 + max(0, s - 1), row r0 + a + W(s - 1); its wall projects to the
    // radius 14 (54 + h) / z (0x10d6), and a pixel shows the first step whose wall is inside its radius
    vec2 qPrev = vec2(0.0);
    float fPrev = -1.0;
    for (int s = 0; s < 105; s++) {
        float z = 2.0 * float(s) + 9.0;
        vec2 q = vec2(c0 + float(max(0, s - 1)), r0 + a + W[s]);
        float f = rho * z / 14.0 - 54.0 - height(q);
        if (f >= 0.0) {
            float zHit = z;
            // between the steps: two secant steps on f. Steps 0 and 1 read the same column, step 0 without the
            // twist (W(-1) = 0): the step 1 region keeps its own row, as in the original
            if (s > 1) {
                float k = fPrev / (fPrev - f);
                vec2 qm = mix(qPrev, q, k);
                float fm = rho * mix(z - 2.0, z, k) / 14.0 - 54.0 - height(qm);
                float k2 = fm >= 0.0 ? k * fPrev / (fPrev - fm) : k + (1.0 - k) * fm / (fm - f);
                q = mix(qPrev, q, k2);
                zHit = mix(z - 2.0, z, k2);
            }
            // footprint of an output pixel in map texels: radially dz = z / rho per original pixel (half a column
            // per depth unit), tangentially 256 / (2 pi rho) rows
            float r = max(rho, 1.0);
            vec2 gr = d / r, gt = vec2(-d.y, d.x) / (r * r);
            vec2 dq = vec2(0.5 * zHit / r, 0.0), dt = vec2(0.0, 256.0 / kTau);
            vec2 gx = (dq * gr.x * px.x + dt * gt.x * px.x) / 256.0;
            vec2 gy = (dq * gr.y * px.y + dt * gt.y * px.y) / 256.0;
            return vec4(textureGrad(cmap, q / 256.0, gx, gy).rgb, zHit);
        }
        qPrev = q;
        fPrev = f;
    }
    return vec4(far, 1000.0);
}

void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec4 c = view(p);
    // anti-aliasing where the depth jumps (silhouettes, the end of the view): four more rays on a rotated grid
    float rho = max(length(p - vec2(160.0, 100.0)), 1.0);
    if (max(abs(dFdx(c.w)), abs(dFdy(c.w))) > 3.0 + 2.0 * c.w / rho * max(px.x, px.y)) {
        c = 0.2 * (c + view(p + px * vec2(0.125, 0.375)) + view(p + px * vec2(-0.375, 0.125)) +
                   view(p + px * vec2(-0.125, -0.375)) + view(p + px * vec2(0.375, -0.125)));
    }
    o = vec4(c.rgb * level, 1.0);
})";

class Tunnel : public Scene {
public:
    ~Tunnel() override {
        heights_.destroy();
        colours_.destroy();
        if (shader_.id()) gl::DeleteProgram(shader_.id());
    }

    void init(Ctx& c) override {
        shader_.build(kFullscreenVS, kTunnelFS, "tunnel");
        const auto& hm = c.exe->file(2);
        const auto& cm = c.exe->file(3);
        Palette pal = vgaPalette(c.exe->fileSpan(4));
        heights_.createRaw(256, 256, GL_R8, GL_RED, GL_UNSIGNED_BYTE, hm.data(), true, true, false);
        Image im;
        im.w = im.h = 256;
        im.rgba.resize(256 * 256 * 4);
        for (size_t i = 0; i < 256 * 256; i++) {
            const Vec3& col = pal[cm[i]];
            im.rgba[i * 4 + 0] = uint8_t(clamp01(col.x) * 255.0f + 0.5f);
            im.rgba[i * 4 + 1] = uint8_t(clamp01(col.y) * 255.0f + 0.5f);
            im.rgba[i * 4 + 2] = uint8_t(clamp01(col.z) * 255.0f + 0.5f);
            im.rgba[i * 4 + 3] = 255;
        }
        colours_.create(im, true, true);
        far_ = pal[0];
        // initial values of the callback's variables (image) and the script entry at 0xf71 {first, last, func, arg}
        colPos0_ = c.exe->u16(0xcba);
        rowPos0_ = c.exe->u16(0xcb8);
        colSpeed0_ = c.exe->s16(0xcbe);
        rowSpeed_ = c.exe->s16(0xcbc);
        phase0_ = c.exe->s32(0xec6);
        phaseSpeed0_ = c.exe->s32(0xeca);
        freq0_ = c.exe->s32(0xed6);
        scriptFirst_ = c.exe->u32(0xf71);
        scriptLast_ = c.exe->u32(0xf71 + 4);
        scriptArg_ = c.exe->s32(0xf71 + 12);
    }

    void render(Ctx& c, double t) override {
        if (start_ != c.start) schedule(c);
        // PIT ticks since the part start (continuous; the callback's counter is its integer part)
        double n = c.ticks(t);
        // fade (0x4a80b in the callback): +1 per tick from 0 to 256; -1 per tick from the first tick in order 0x43
        double level = std::min(std::clamp(n, 0.0, 256.0), 256.0 - std::clamp(c.ticks(t, fadeOut_), 0.0, 256.0));
        // forward: col_pos += col_speed per tick (8.8 texels); the script adds `arg` to col_speed at every drawn
        // frame while the tick counter is in [first, last]. Drawn frame j (35.04 fps in the capture) at 2 j + 0.5
        // retraces after the start.
        double col = colPos0_ + colSpeed0_ * n;
        for (int j = 0;; j++) {
            double nj = (2 * j + 0.5) * kPitHz / kRetraceHz;
            if (nj > n || std::floor(nj + 0.5) > scriptLast_) break;
            if (std::floor(nj + 0.5) >= scriptFirst_) col += scriptArg_ * (n - nj);
        }
        double row = rowPos0_ + rowSpeed_ * n;
        // twist: freq += 1 at the ticks n % 4 == 0 from the first tick in 3b:00 (20 times), phase_speed the same from
        // 3d:00 (30 times), phase += phase_speed per tick
        double freq = freq0_ + freq_.value(n);
        double phase = phase0_ + phaseSpeed0_ * n + phaseSpeed_.sum(n);
        // the twist table (0x1158): W(b) = S[(phase + b freq) & 0xfff] >> 10 rows; step s reads W(s - 1)
        float w[105];
        w[0] = 0.0f;
        for (int s = 1; s < 105; s++) w[s] = float(kSinA(phase + (s - 1) * freq) / 1024.0);

        setBlend(Blend::None);
        shader_.use();
        heights_.bind(0);
        colours_.bind(1);
        shader_.set("hmap", 0);
        shader_.set("cmap", 1);
        shader_.set("c0", float(std::fmod(col / 256.0, 256.0)));
        shader_.set("r0", float(std::fmod(row / 256.0, 256.0)));
        gl::Uniform1fv(shader_.loc("W"), 105, w);
        shader_.set("far", far_);
        shader_.set("level", float(level / 256.0));
        shader_.set("px", 320.0f / float(c.w), 200.0f / float(c.h));
        drawFullscreen();
    }

private:
    // The music events the callback reacts to, as PIT ticks of the part (0x1c7a3: order, row).
    void schedule(const Ctx& c) {
        start_ = c.start;
        // the first tick in 3b:00 / 3d:00 starts a ramp; its steps fall on the ticks with n % 4 == 0
        auto firstStep = [&](int pos) {
            double k = std::ceil(c.ticks(c.positionTime(pos)) + 0.5);  // tick k is taken at k - 0.5 on average
            return 4.0 * std::ceil(k / 4.0);
        };
        freq_ = {firstStep(0x3b00), 20};
        phaseSpeed_ = {firstStep(0x3d00), 30};
        fadeOut_ = c.positionTime(0x4300);  // the first tick with order >= 0x43 comes half a tick later on average
    }

    // A counter the callback raises by 1 at the ticks n0, n0 + 4, ... (`steps` times), as a ramp through the middle
    // of its steps (tick k is taken at k - 0.5): its value after n ticks and its sum over the ticks so far.
    struct Ramp {
        double n0 = 1e9, steps = 0;
        double value(double n) const { return std::clamp((n - n0 + 2.5) / 4.0, 0.0, steps); }
        double sum(double n) const {
            double u = std::max(0.0, n - (n0 - 2.5)), full = 4.0 * steps;
            return u <= full ? u * u / 8.0 : full * full / 8.0 + steps * (u - full);
        }
    };

    Shader shader_;
    Texture heights_, colours_;
    Vec3 far_;
    double colPos0_ = 0, rowPos0_ = 0, colSpeed0_ = 0, rowSpeed_ = 0, phase0_ = 0, phaseSpeed0_ = 0, freq0_ = 0;
    double scriptFirst_ = 0, scriptLast_ = 0, scriptArg_ = 0;
    Ramp freq_, phaseSpeed_;
    double fadeOut_ = 0;
    double start_ = -1;
};

}  // namespace

REGISTER_SCENE(Tunnel, "tunnel");

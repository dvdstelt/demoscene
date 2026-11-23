// The water part (23:63..27:48, inside/re/notes-water.md): a blurred picture seen through water (water/water.raw, the
// picture "water") that ripples sideways, eight small glass fish that swim, turn around at the screen edges and stir
// the water, and a twisted metal spiral (water/obuke.sro) turning on the left, always in front of the fish. Ports the
// init 0x407dec (0x4079f4: the camera, the spiral, the fish placed by rand()), the start 0x407f90, the tick 0x407c78
// (the spiral's rotation per refresh) and the frame 0x407d78 (0x407cac, 0x4078d4: the fish 0x407400, the random drops
// and the ripple 0x401234); the spiral and the fish are drawn by the SurRender port (inside/engine.h). The white
// flash over the start is the framework's.
//
// Step rate. The original steps the fish, the drops and the ripple once per drawn frame (62.2 per second in the
// capture), with Watcom's rand() and the clock M the fish read in that frame: the wave speed, the fish's speed and
// turning and the drops depend on the frame rate. The remake replays the capture's 1,172 frames (water_frames.h,
// measured by tools/water_frames.py): every frame's M, the fish updates that a timer interrupt made read M + 1, and the
// rand() seed before the first frame. So the fish, their wakes and the drops are the capture's, frame for frame, and
// the 320 x 200 height field is the original's integer simulation, bit-exact. A frame is presented at the time it was
// computed (its M) plus the capture's mean latency (1.75 refreshes), and everything is blended between the two frames
// around that time: the motion follows the machine's even frame clock, not the uneven refreshes (1 or 2 apart) at
// which the capture happened to show the frames. With INSIDE_ENGINE=original the remake shows the frame the capture
// shows at each record, as the original drew it. A backwards seek replays the field from the last saved state.
//
// Evaluated exactly instead of the original's shortcuts (notes 10): the picture is shifted by -h / 32 pixels with the
// height h sampled per output pixel from the field with a cubic B-spline (instead of h >> 5 per original pixel; the
// spline also leaves out the checkerboard waves at the grid's own frequency, which at 320 x 200 look like dithering)
// and blended between two frames; the picture is read filtered at that position (a shift past a row's end continues
// on the next row, as the linear addressing does) and supersampled where the shift changes fast. The fish are drawn
// at their unrounded positions, turn smoothly between frames, their tails swing continuously; the spiral turns
// continuously; the engine draws both perspective-correct with filtered texels, the fish blended 50/50 in colour (the
// fit of water.int), anti-aliased. Kept: the simulation (wave speed, damping, the 15-cell wakes, the drops, the rows
// that couple at the edges), the integer fish positions inside it (the drift towards the camera), the spiral in front
// of the fish (their sort bias), the black rows 0, 1, 198 and 199, and the wrap of the tail's environment coordinates.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"
#include "scenes/water_frames.h"

#ifndef GL_RG16I
#define GL_RG16I 0x8239
#endif
#ifndef GL_RG_INTEGER
#define GL_RG_INTEGER 0x8228
#endif
#ifndef GL_SHORT
#define GL_SHORT 0x1402
#endif

namespace {

namespace sr = inside::sr;
namespace wf = water_frames;

constexpr int kW = 320, kH = 200;      // the height field and the screen ([0x422860], [0x422864], checked in init)
constexpr int kFish = 8;                // 0x4079f4 and 0x4078d4 loop over 8 fish
constexpr int kInitSeedCalls = 39;      // rand() calls from the flight init's srand to the water init (notes 3)
constexpr int kCheckpointEvery = 32;    // frames between saved height fields (backwards seeks)
constexpr int kTurnMul1 = 171, kTurnMul2 = 57;   // 0x407400: M + 171 i, M - 57 i (shifts and adds)
constexpr int kTail[3] = {4, 5, 6};               // 0x407400 writes vertices 4, 5, 6 (+0x108, +0x148, +0x188)
constexpr double kTailSign[3] = {-1, -1, 1};      // fchs for vertices 4 and 5

// ---- INSIDE.EXE (notes-water.md 2..6)
// file names passed to 0x400614 by the init 0x407dec
constexpr uint32_t kEnvName = 0x4213a0, kShdName = 0x4213b4, kPalName = 0x4213c4, kFishName = 0x4213e4,
                   kIntName = 0x4213f4, kObukeName = 0x421404;
// 0x407dec: the viewport 0x410618 (W in edx, H in ebx, y1, x1, y0 pushed, x0 = 0), fov, near and ambient
// (mov [scene + d], imm), the drop tries (mov ebx, 3; stored to [0x422854]), the materials' texture size (push 8:
// log2)
constexpr uint32_t kViewY1 = 0x407eb8, kViewX1 = 0x407ebd, kViewH = 0x407ec2, kViewY0 = 0x407ec9, kViewW = 0x407ecb;
constexpr uint32_t kFov = 0x407ef2, kNear = 0x407efe, kAmbient = 0x407f0a, kDropTries = 0x407f11, kTexLog = 0x407f30;
constexpr uint32_t kDropChance = 0x422858;   // dword: 1 / the chance of a drop (kept if > 0, else 1)
constexpr uint32_t kRippleW = 0x422860, kRippleH = 0x422864;
constexpr uint32_t kSrandOperand = 0x40295e;   // the flight init's srand(0x45eafc): the operand of mov eax, imm32
// 0x4079f4: the camera (position: six pushes for 0x413510; rotation: six pushes for 0x4145f4), the spiral (load
// scale in ebx, rasterizer in edx, flags, position, rotation), the fish (scale, rasterizer, flags, the rand() ranges
// of z, x and the heading, the rotation, the sort bias)
constexpr uint32_t kCamPos[6] = {0x407a2a, 0x407a2f, 0x407a31, 0x407a33, 0x407a35, 0x407a42};
constexpr uint32_t kCamEuler = 0x407a51;
constexpr uint32_t kObukeScale = 0x407a94, kObukeRaster = 0x407abe, kObukeFlags = 0x407af1, kObukeEuler = 0x407b05;
constexpr uint32_t kObukePos[6] = {0x407add, 0x407adf, 0x407ae1, 0x407ae3, 0x407ae5, 0x407aef};
constexpr uint32_t kFishScale = 0x407b3f, kFishRaster = 0x407b6b, kFishFlags = 0x407b81, kFishBias = 0x407c29;
constexpr uint32_t kFishZMod = 0x407b92, kFishXMod = 0x407ba5, kFishXSub = 0x407baf, kFishEuler = 0x407bc2,
                   kFishHeadMod = 0x407be6, kHeadDeg = 0x421396;
// 0x407c78: the tick routine's rotation (six pushes for 0x4145f4)
constexpr uint32_t kTickEuler = 0x407c7c;
// 0x407d78: memset(back, 0, 0x280) and memset(back + 0xf780, 0, 0x280): the black rows
constexpr uint32_t kBandSize = 0x407da3, kBandBottom = 0x407dc0;
// 0x407400: turn rate a = sin((M + 171 i) f1) + cos((M - 57 i) f2) + cos(-M f3) w3 degrees, the tail's frequencies
// and swings, the normal swing, the speed a s1 + s2 (at least 15: compared with [0x421314], set from an immediate),
// the turn-around window and its angle (pushed), the wake's default height (immediate high dword), the big wake's
// chance and height range, the wake window's margin (double and the integers W - 3, H - 3)
constexpr uint32_t kTurnF1 = 0x4212e4, kTurnF2 = 0x4212ec, kTurnF3 = 0x4212f4, kTurnW3 = 0x4212fc, kTurnDeg = 0x421304;
constexpr uint32_t kTailF[3] = {0x42130c, 0x42131c, 0x42132c}, kTailA[3] = {0x421314, 0x421324, 0x421334};
constexpr uint32_t kTailN = 0x42133c, kSpeedA = 0x421344, kSpeedB = 0x42134c, kSpeedMin = 0x421314,
                   kSpeedSet = 0x40760c;
constexpr uint32_t kTurnLo = 0x421354, kTurnHiX = 0x42135c, kTurnHiY = 0x421364, kTurnPi[2] = {0x407717, 0x407722};
constexpr uint32_t kWakeH = 0x4077ae, kBigMod = 0x4077bd, kBigH = 0x4077d2, kMargin = 0x42136c,
                   kMarginW = 0x407804, kMarginH = 0x407832;
// 0x4078d4: the drops: x = rand() % (W - 20) + 10, y = rand() % (H - 20), height rand() % 20000
constexpr uint32_t kDropW = 0x40792c, kDropX0 = 0x407934, kDropH = 0x40794a, kDropHeight = 0x40795b;

std::string hexOf(uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof b, "%#x", a);
    return b;
}
// Immediate operands of the instructions at those addresses.
uint32_t pushImm(const InsideExe& x, uint32_t a) {   // push imm32 / imm8
    if (x.u8(a) == 0x68) return x.u32(a + 1);
    if (x.u8(a) == 0x6a) return uint32_t(int32_t(x.s8(a + 1)));
    throw std::runtime_error("water: no push at " + hexOf(a));
}
uint32_t movRegImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("water: no mov r32, imm32 at " + hexOf(a));
    return x.u32(a + 1);
}
int32_t movMemImm(const InsideExe& x, uint32_t a) {    // mov dword [r + disp8 / disp32], imm32
    if (x.u8(a) != 0xc7) throw std::runtime_error("water: no mov [r + d], imm32 at " + hexOf(a));
    int mod = x.u8(a + 1) >> 6;
    return x.s32(a + (mod == 1 ? 3 : 6));
}
int32_t aluImm(const InsideExe& x, uint32_t a) {       // add / sub / cmp r32, imm8 (0x83) or imm32 (0x81, 0x05)
    if (x.u8(a) == 0x83) return x.s8(a + 2);
    if (x.u8(a) == 0x81) return x.s32(a + 2);
    if (x.u8(a) == 0x05) return x.s32(a + 1);
    throw std::runtime_error("water: no add / sub / cmp r32, imm at " + hexOf(a));
}
double doubleOf(uint32_t hi, uint32_t lo) {
    uint64_t bits = uint64_t(hi) << 32 | lo;
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
// Six pushes of three doubles for a routine that takes (a, b, c) on the stack: c high, c low, b high, ... a low.
sr::V3 pushedTriple(const InsideExe& x, const uint32_t (&at)[6]) {
    auto d = [&](int k) { return doubleOf(pushImm(x, at[k]), pushImm(x, at[k + 1])); };
    return {d(4), d(2), d(0)};
}
sr::V3 pushedTripleRun(const InsideExe& x, uint32_t a) {   // six consecutive pushes
    uint32_t at[6];
    for (uint32_t& p : at) {
        p = a;
        a += x.u8(a) == 0x68 ? 5 : 2;
    }
    return pushedTriple(x, at);
}

sr::V3 truncated(sr::V3 p) { return {std::trunc(p.x), std::trunc(p.y), std::trunc(p.z)}; }

// Watcom's rand() 0x4134cd.
struct Rand {
    uint32_t seed = 1;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

struct Params {
    double f1, f2, f3, w3, turnDeg, tailF[3], tailA[3], tailN, speedA, speedB, speedMin, speedSet;
    double turnLo, turnHiX, turnHiY, turnAngle, wakeH, margin;
    int bigMod, bigH, marginW, marginH;
    int dropTries, dropChance, dropW, dropX0, dropH, dropHeight;
    double cx, cy, xs, ys;   // the scene's projection (0x40c4e4 sets it at the first render)
};

struct FishState {
    sr::V3 pf;               // the integer position the engine draws (+0x10..0x18): whole numbers
    sr::V3 p;                // the update's position before the truncation (the remake draws it)
    sr::M3 mat;              // the rotation (+0x28)
    int m = 0;               // the M the update read
};

// A write into the height field the ripple reads as the frame before last: a fish's wake (a 15-cell triangle around
// its projected centre) or a drop (a 7 x 7 square).
struct Write {
    int16_t x, y, h;
    bool drop;
};

struct Frame {
    std::array<FishState, kFish> fish;
    std::vector<Write> writes;
    int m = 0;               // M at fish 0
    int shown = 0;           // the record that shows it in the capture, from the part's first record
};

// The two height fields: a = [0x4265e4] ("water frame 1": the frame before last, which the next frame's wakes and
// drops write and its ripple overwrites), b = [0x4265d8] (the last frame's), swapped after every frame.
struct Field {
    std::vector<int16_t> a = std::vector<int16_t>(size_t(kW) * kH, 0);
    std::vector<int16_t> b = std::vector<int16_t>(size_t(kW) * kH, 0);
    int frames = 0;          // frames stepped
};

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform isampler2D field;   // RG16I: the height field of the frame shown (r) and of the next one (g)
uniform sampler2D pic;      // the picture (premultiplied, opaque): the original's pixels or a redraw
uniform int redraw;
uniform float w;            // weight of the next frame
uniform vec2 res;           // output size in pixels
uniform vec2 band;          // rows band.x .. band.y - 1 show the picture, the others are black
uniform int exact;          // the original's integer shift and nearest pixel (INSIDE_ENGINE=original)

float cell(ivec2 t) {       // rows 1..196: the ones the ripple steps (0 and 197 hold stuck wakes)
    t = clamp(t, ivec2(0, int(band.x) - 1), ivec2(319, int(band.y) - 2));
    vec2 v = vec2(texelFetch(field, t, 0).rg);
    return mix(v.x, v.y, w);
}

vec4 bspline(float f) {
    float g = 1.0 - f, f2 = f * f;
    return vec4(g * g * g, 4.0 - 6.0 * f2 + 3.0 * f2 * f, 1.0 + 3.0 * (f + f2 - f2 * f), f2 * f) / 6.0;
}

// The height under the screen point p (VGA coordinates): screen row y shows height row y - 1 (0x401234), so cell
// (i, j) is centred at (i + 0.5, j + 1.5); a cubic B-spline over the 4 x 4 cells around p.
float height(vec2 p) {
    vec2 t = p - vec2(0.5, 1.5);
    ivec2 i = ivec2(floor(t));
    vec2 f = t - vec2(i);
    vec4 wx = bspline(f.x), wy = bspline(f.y);
    float s = 0.0;
    for (int y = 0; y < 4; y++) {
        float row = 0.0;
        for (int x = 0; x < 4; x++) row += wx[x] * cell(i + ivec2(x - 1, y - 1));
        s += wy[y] * row;
    }
    return s;
}

// The picture at x along row y, the row continuing into the next or previous one past its ends (the ripple reads
// pic[320 y + x - (h >> 5)]).
vec3 picture(float x, float y, vec2 gx, vec2 gy) {
    float k = floor(x / 320.0);
    vec2 q = vec2(x - 320.0 * k, y + k) / vec2(320.0, 200.0);
    return (redraw != 0 ? textureGrad(pic, q, gx, gy) : texBicubic(pic, q)).rgb;
}

void main() {
    vec2 px = vec2(320.0, 200.0) / res;   // one output pixel in VGA units
    vec2 p = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y) * px;
    if (exact != 0) {
        ivec2 q = ivec2(floor(p));
        if (q.y < int(band.x) || q.y >= int(band.y)) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
        int h = texelFetch(field, ivec2(q.x, q.y - 1), 0).r;
        int a = clamp(320 * q.y + q.x - (h >> 5), 0, 63999);
        o = vec4(textureLod(pic, (vec2(a % 320, a / 320) + 0.5) / vec2(320.0, 200.0), 0.0).rgb, 1.0);
        return;
    }
    // the part of this pixel in the rows that show the picture
    float cover = clamp((min(p.y + 0.5 * px.y, band.y) - max(p.y - 0.5 * px.y, band.x)) / px.y, 0.0, 1.0);
    float x = p.x - height(p) / 32.0;
    // the footprint of the pixel in the picture: supersample where the shift changes fast
    vec2 gx = vec2(dFdx(x), 0.0), gy = vec2(dFdy(x), px.y);
    float fp = max(length(gx), length(gy));
    int n = int(clamp(ceil(fp), 1.0, 3.0));
    vec2 s = vec2(320.0, 200.0) * float(n);
    vec3 c = vec3(0.0);
    if (n == 1) {
        c = picture(x, p.y, gx / s, gy / s);
    } else {
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) {
                vec2 q = p + ((vec2(i, j) + 0.5) / float(n) - 0.5) * px;
                c += picture(q.x - height(q) / 32.0, q.y, gx / s, gy / s);
            }
        c /= float(n * n);
    }
    o = vec4(c * cover, 1.0);
}
)";

class WaterScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        if (x.s32(kRippleW) != kW || x.s32(kRippleH) != kH) throw std::runtime_error("water: not a 320 x 200 field");
        readParams(x);
        eng_.init(c);
        // 0x407dec: data, scene, materials
        pal_ = eng_.palette(inside::palette(d, x.cstr(kPalName)));
        int size = 1 << pushImm(x, kTexLog);
        int env = eng_.textureFile(x.cstr(kEnvName), size, size);
        int matObuke = eng_.material({.texture = env, .table1 = eng_.table(d.file(x.cstr(kShdName)))});
        int matFish = eng_.material({.texture = env, .table1 = eng_.table(d.file(x.cstr(kIntName)))});
        scene_.width = int(movRegImm(x, kViewW));
        scene_.height = int(movRegImm(x, kViewH));
        scene_.x0 = 0;   // xor ecx, ecx
        scene_.y0 = int(pushImm(x, kViewY0));
        scene_.x1 = int(pushImm(x, kViewX1));
        scene_.y1 = int(pushImm(x, kViewY1));
        scene_.fov = movMemImm(x, kFov);
        scene_.near = movMemImm(x, kNear);
        scene_.ambient = movMemImm(x, kAmbient);
        // the projection 0x411e80 uses (notes-engine.md, Scene): centre of the viewport, scales from fov and size
        k_.cx = scene_.x0 + (scene_.x1 - scene_.x0) / 2;
        k_.cy = scene_.y0 + (scene_.y1 - scene_.y0) / 2;
        k_.xs = std::trunc(scene_.width * 32768.0 * scene_.fov * (1.0 / 81920));
        k_.ys = std::trunc(scene_.height * 32768.0 * scene_.fov * (1.0 / 61440));
        // 0x4079f4: the camera, the spiral, the fish (scene_add puts each at the head of the list, as 0x411154)
        scene_.add(camera_);
        scene_.camera = &camera_;
        camPos_ = pushedTriple(x, kCamPos);
        sr::V3 ce = pushedTripleRun(x, kCamEuler);
        camera_.rot = sr::euler(ce.x, ce.y, ce.z);
        obuke_.loadSro(d.file(x.cstr(kObukeName)), int32_t(movRegImm(x, kObukeScale)), matObuke);
        obuke_.setRaster(sr::rasterAt(movRegImm(x, kObukeRaster)));
        scene_.add(obuke_);
        obuke_.flags = uint32_t(movMemImm(x, kObukeFlags));
        obukePos_ = pushedTriple(x, kObukePos);
        sr::V3 oe = pushedTripleRun(x, kObukeEuler);
        obukeRot0_ = sr::euler(oe.x, oe.y, oe.z);
        sr::V3 te = pushedTripleRun(x, kTickEuler);
        tickStep_ = sr::euler(te.x, te.y, te.z);
        sr::toAxisAngle(tickStep_, tickAxis_, tickAngle_);
        sr::V3 fe = pushedTripleRun(x, kFishEuler);
        fishRot0_ = sr::euler(fe.x, fe.y, fe.z);
        for (sr::Object& f : fish_) {
            f.loadSro(d.file(x.cstr(kFishName)), int32_t(movRegImm(x, kFishScale)), matFish);
            f.setRaster(sr::rasterAt(movRegImm(x, kFishRaster)));
            f.flags = uint32_t(movMemImm(x, kFishFlags));
            scene_.add(f);
            f.bias = movMemImm(x, kFishBias);
        }
        for (int t = 0; t < 3; t++) tailNormal_[size_t(t)] = fish_[0].verts[size_t(kTail[t])].n;
        simulateFish(x);
        // the frames' presentation: at the time of their M plus the capture's mean latency (in refreshes)
        double lat = 0;
        for (const Frame& f : frames_) lat += f.shown - ((f.m - wf::kRetrace) / 4.0 - wf::kFirstRecord);
        latency_ = lat / double(frames_.size());
        frameM_.resize(frames_.size());
        for (size_t j = 0; j < frames_.size(); j++) frameM_[j] = frames_[j].m;
        mBefore_ = frameM_.front() - (frameM_.back() - frameM_.front()) / double(frameM_.size() - 1);

        checkpoints_.assign(size_t(wf::kFrames / kCheckpointEvery + 1), Field{});
        saved_.assign(checkpoints_.size(), false);
        saved_[0] = true;
        rg_.assign(size_t(kW) * kH * 2, 0);
        fieldTex_.createRaw(kW, kH, GL_RG16I, GL_RG_INTEGER, GL_SHORT, rg_.data(), false, false, false);
        band_[0] = float(movRegImm(x, kBandSize) / kW);
        band_[1] = float(aluImm(x, kBandBottom) / kW);
        shader_.build(kFullscreenVS, kFS, "water");
        c.picture("water");   // load it in the init
        eng_.prepare();   // the textures, tables and upscales now, not at the first frame
    }

    void render(Ctx& c, double r) override {
        const bool ex = sr::fidelity().exact;
        double n = c.refreshes(r);
        int a, b;
        double w = 0, m;
        if (ex) {   // the frame the capture shows at this record, as drawn
            a = -1;
            while (a + 1 < wf::kFrames && frames_[size_t(a + 1)].shown <= std::floor(n)) a++;
            b = a;
            m = a < 0 ? mBefore_ : frameM_[size_t(a)];
        } else {    // the two frames around the time M of the frame presented now
            m = 4.0 * (n - latency_ + wf::kFirstRecord) + wf::kRetrace;
            a = int(std::upper_bound(frameM_.begin(), frameM_.end(), m) - frameM_.begin()) - 1;
            b = std::min(a + 1, wf::kFrames - 1);
            double ma = a < 0 ? mBefore_ : frameM_[size_t(a)];
            if (b > a) w = std::clamp((m - ma) / (frameM_[size_t(b)] - ma), 0.0, 1.0);
        }
        drawWater(c, a, b, w, ex);
        placeObuke(m, a, ex);
        for (int i = 0; i < kFish; i++) placeFish(i, a, b, w, m, ex);
        camera_.pos = ex ? truncated(camPos_) : camPos_;   // 0x407cac copies the positions as ints
        eng_.render(c, scene_, pal_, -1);                  // over the picture (0x40c6ac)
    }

private:
    // ---------------------------------------------------------------- the original's parameters
    void readParams(const InsideExe& x) {
        Params& k = k_;
        k.f1 = x.f64(kTurnF1);
        k.f2 = x.f64(kTurnF2);
        k.f3 = x.f64(kTurnF3);
        k.w3 = x.f64(kTurnW3);
        k.turnDeg = x.f64(kTurnDeg);
        for (int t = 0; t < 3; t++) {
            k.tailF[t] = x.f64(kTailF[t]);
            k.tailA[t] = x.f64(kTailA[t]);
        }
        k.tailN = x.f64(kTailN);
        k.speedA = x.f64(kSpeedA);
        k.speedB = x.f64(kSpeedB);
        k.speedMin = x.f64(kSpeedMin);
        k.speedSet = doubleOf(movRegImm(x, kSpeedSet), 0);   // xor ebx, ebx: the low dword
        k.turnLo = x.f64(kTurnLo);
        k.turnHiX = x.f64(kTurnHiX);
        k.turnHiY = x.f64(kTurnHiY);
        k.turnAngle = doubleOf(pushImm(x, kTurnPi[0]), pushImm(x, kTurnPi[1]));
        k.wakeH = doubleOf(movRegImm(x, kWakeH), 0);         // xor edx, edx: the low dword
        k.bigMod = int(movRegImm(x, kBigMod));
        k.bigH = int(movRegImm(x, kBigH));
        k.margin = x.f64(kMargin);
        k.marginW = aluImm(x, kMarginW);
        k.marginH = aluImm(x, kMarginH);
        k.dropTries = int(movRegImm(x, kDropTries));
        k.dropChance = std::max(x.s32(kDropChance), 1);
        k.dropW = aluImm(x, kDropW);
        k.dropX0 = aluImm(x, kDropX0);
        k.dropH = aluImm(x, kDropH);
        k.dropHeight = int(movRegImm(x, kDropHeight));
    }

    // ---------------------------------------------------------------- the fish, the wakes and the drops (exact)
    // 0x411e80 with the water scene (as the part's model, notes 6.2); the camera never moves.
    void project(sr::V3 world, double& sx, double& sy) const {
        sr::V3 q = camera_.rot.transposed() * (world - camPos_);
        sx = k_.cx + k_.xs / 16384 * (q.x * 128) / q.z;
        sy = k_.cy + k_.ys / 16384 * (q.y * 128) / q.z;
    }

    // 0x407400(i): turn, move, turn around outside the window, leave a wake
    void updateFish(FishState& f, int i, int M, bool first, Rand& rnd, std::vector<Write>& out) const {
        const Params& k = k_;
        double a = std::sin(double(M + kTurnMul1 * i) * k.f1) + std::cos(double(M - kTurnMul2 * i) * k.f2) +
                   std::cos(double(-M) * k.f3) * k.w3;
        f.mat = sr::rotateY(f.mat, a * k.turnDeg);
        f.m = M;
        double s = a * k.speedA + k.speedB;
        if (!(s >= k.speedMin)) s = k.speedSet;
        f.p = f.pf + f.mat * sr::V3(0, 0, s);   // 0x413d58: along the local z axis
        f.pf = truncated(f.p);
        // in the first frame the scene's projection is not set up yet: every fish projects to (0, 0)
        double sx = 0, sy = 0;
        if (!first) project(f.p, sx, sy);
        if (sx < k.turnLo) f.mat = sr::rotateY(f.mat, k.turnAngle);
        if (sx > k.turnHiX) f.mat = sr::rotateY(f.mat, k.turnAngle);
        if (sy < k.turnLo) f.mat = sr::rotateY(f.mat, k.turnAngle);
        if (sy > k.turnHiY) f.mat = sr::rotateY(f.mat, k.turnAngle);
        double h = k.wakeH;
        if (rnd() % k.bigMod == 0) h = double(rnd() % k.bigH);
        if (sx > k.margin && double(kW - k.marginW) > sx && sy > k.margin && double(kH - k.marginH) > sy)
            out.push_back({int16_t(std::trunc(sx)), int16_t(std::trunc(sy)), int16_t(int32_t(std::trunc(h))), false});
    }

    // 0x4079f4's fish from the init's rand() sequence, then every frame of the capture (0x4078d4 without the ripple)
    void simulateFish(const InsideExe& x) {
        Rand rnd{x.u32(kSrandOperand)};
        for (int k = 0; k < kInitSeedCalls; k++) rnd();
        int zMod = int(movRegImm(x, kFishZMod)), xMod = int(movRegImm(x, kFishXMod)), xSub = aluImm(x, kFishXSub);
        int headMod = int(movRegImm(x, kFishHeadMod));
        double deg = x.f64(kHeadDeg);
        std::array<FishState, kFish> fish{};
        for (FishState& f : fish) {
            int z = rnd() % zMod;
            int px = rnd() % xMod - xSub;
            f.pf = f.p = sr::V3(px, 0, z);
            f.mat = sr::rotateY(fishRot0_, (rnd() % headMod) * deg);
        }
        start_ = fish;
        rnd.seed = wf::kSeed;   // the capture's run-time sequence
        frames_.resize(size_t(wf::kFrames));
        int shown = 0;
        size_t late = 0;
        for (int j = 0; j < wf::kFrames; j++) {
            Frame& fr = frames_[size_t(j)];
            shown += wf::kShowSteps[j];
            fr.shown = shown;
            fr.m = 4 * (wf::kFirstRecord + shown) + wf::kClock[j];
            int lateFish = kFish;
            if (late < std::size(wf::kLate) && wf::kLate[late].frame == j) lateFish = wf::kLate[late++].fish;
            for (int i = 0; i < kFish; i++)
                updateFish(fish[size_t(i)], i, fr.m + (i >= lateFish ? 1 : 0), j == 0, rnd, fr.writes);
            for (int t = 0; t < k_.dropTries; t++)
                if (rnd() % k_.dropChance == 0) {
                    int dx = rnd() % (kW - k_.dropW) + k_.dropX0;
                    int dy = rnd() % (kH - k_.dropH);
                    int h = rnd() % k_.dropHeight;
                    fr.writes.push_back({int16_t(dx), int16_t(dy), int16_t(h), true});
                }
            fr.fish = fish;
        }
    }

    // ---------------------------------------------------------------- the height field (exact)
    static void write(std::vector<int16_t>& A, const std::vector<Write>& ws) {
        for (const Write& w : ws) {
            if (w.drop) {   // 0x4078d4: columns x - 3..x + 3, rows y + 7..y + 13
                for (int dx = -3; dx < 4; dx++)
                    for (int y = w.y + 7; y < w.y + 14; y++) A[size_t(y * kW + w.x + dx)] = w.h;
            } else {        // 0x407400: columns x - 2..x + 2, rows y - 3..y + 1, below the diagonal
                for (int dx = -3; dx < 3; dx++)
                    for (int dy = -3; dy < dx; dy++) A[size_t((w.y + dy) * kW + w.x + dx)] = w.h;
            }
        }
    }

    // 0x401234: rows 1..196 of A from the neighbours in B, damped; the buffers are linear (a row's first cell's left
    // neighbour is the previous row's last)
    static void ripple(std::vector<int16_t>& A, const std::vector<int16_t>& B) {
        for (int p = kW; p < kW + (kH - 4) * kW; p++) {
            int v = (B[size_t(p - 1)] + B[size_t(p + 1)] + B[size_t(p - kW)] + B[size_t(p + kW)]) >> 1;
            v -= A[size_t(p)];
            v -= v >> 4;
            A[size_t(p)] = int16_t(std::clamp(v, -0x3fff, 0x3fff));
        }
    }

    void step() {
        write(field_.a, frames_[size_t(field_.frames)].writes);
        ripple(field_.a, field_.b);
        field_.a.swap(field_.b);
        field_.frames++;
        size_t ck = size_t(field_.frames / kCheckpointEvery);
        if (field_.frames % kCheckpointEvery == 0 && ck < checkpoints_.size() && !saved_[ck]) {
            checkpoints_[ck] = field_;
            saved_[ck] = true;
        }
    }

    // The fields after `count` frames: b = the last frame's, a = the one before it.
    void runTo(int count) {
        size_t ck = size_t(count / kCheckpointEvery);
        while (!saved_[ck]) ck--;
        int at = int(ck) * kCheckpointEvery;
        if (field_.frames > count || at > field_.frames) field_ = checkpoints_[ck];
        while (field_.frames < count) step();
    }

    // ---------------------------------------------------------------- drawing
    void drawWater(Ctx& c, int a, int b, double w, bool ex) {
        if (a != texA_ || b != texB_) {
            if (b < 0) {
                std::fill(rg_.begin(), rg_.end(), int16_t(0));
            } else {
                runTo(b + 1);
                const std::vector<int16_t>& fa = a == b ? field_.b : field_.a;
                for (size_t i = 0; i < fa.size(); i++) {
                    rg_[2 * i] = fa[i];
                    rg_[2 * i + 1] = field_.b[i];
                }
            }
            gl::BindTexture(GL_TEXTURE_2D, fieldTex_.id);
            gl::PixelStorei(GL_UNPACK_ALIGNMENT, 2);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kW, kH, GL_RG_INTEGER, GL_SHORT, rg_.data());
            texA_ = a;
            texB_ = b;
        }
        const Picture& pic = c.picture("water");
        setBlend(Blend::None);
        shader_.use();
        fieldTex_.bind(0);
        pic.tex.bind(1);
        shader_.set("field", 0);
        shader_.set("pic", 1);
        shader_.set("redraw", pic.redraw ? 1 : 0);
        shader_.set("w", float(w));
        shader_.set("res", float(c.w), float(c.h));
        shader_.set("band", band_[0], band_[1]);
        shader_.set("exact", ex ? 1 : 0);
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
    }

    // 0x407c78 once per refresh from the part's first record: rot = rot0 S^n, n = the tick calls before the frame's
    // M (continuous between them).
    void placeObuke(double m, int a, bool ex) {
        double n = (m - wf::kRetrace) / 4.0 - wf::kFirstRecord + 1;
        if (ex) {
            sr::M3 R = obukeRot0_;
            for (int i = 0, k = a < 0 ? 0 : int(std::floor(n)); i < k; i++) R = R * tickStep_;
            obuke_.rot = R;
            obuke_.pos = truncated(obukePos_);
        } else {
            obuke_.rot = obukeRot0_ * sr::axisAngle(tickAxis_, tickAngle_ * std::max(0.0, n));
            obuke_.pos = obukePos_;
        }
    }

    // A fish as frame a draws it (exact), or blended between frames a and b, with the tail at M = m.
    void placeFish(int i, int a, int b, double w, double m, bool ex) {
        sr::Object& o = fish_[size_t(i)];
        const FishState& fa = a < 0 ? start_[size_t(i)] : frames_[size_t(a)].fish[size_t(i)];
        const FishState& fb = b < 0 ? start_[size_t(i)] : frames_[size_t(b)].fish[size_t(i)];
        const Params& k = k_;
        if (ex) {
            o.pos = fa.pf;
            o.rot = fa.mat;
            if (a >= 0) m = fa.m;
        } else {
            o.pos = fa.p + (fb.p - fa.p) * w;
            double ta = std::atan2(fa.mat.m[0][2], fa.mat.m[0][0]), tb = std::atan2(fb.mat.m[0][2], fb.mat.m[0][0]);
            o.rot = sr::rotateY(fishRot0_, ta + w * std::remainder(tb - ta, 2 * std::numbers::pi));
        }
        // the tail: three vertices swing sideways, their normals swing (the reflection shimmers)
        for (int t = 0; t < 3; t++) {
            sr::Vertex& v = o.verts[size_t(kTail[t])];
            double s = std::sin(m * k.tailF[t]);
            double px = kTailSign[t] * s * k.tailA[t], nn = s * k.tailN;
            v.p.x = ex ? std::trunc(px) : px;
            v.n = tailNormal_[size_t(t)];
            (t == 0 ? v.n.x : t == 1 ? v.n.y : v.n.z) = ex ? std::trunc(nn) : nn;
        }
        if (!ex) wrapEnvironment(o);
    }

    // 0x41acc4 takes the normal terms of u and v modulo the texture (& 0xfff): the tail's swung normals (longer than
    // the others) wrap, and the face between a wrapped vertex and the others shows the whole map squeezed. The
    // engine's continuous mapping leaves that out; move the normal by whole textures along the two rows of the
    // object-to-camera rotation, which gives the engine the wrapped coordinates.
    void wrapEnvironment(sr::Object& o) const {
        sr::M3 M = camera_.rot.transposed() * o.rot;
        for (int t : kTail) {
            sr::V3& n = o.verts[size_t(t)].n;
            sr::V3 r0(M.m[0][0], M.m[0][1], M.m[0][2]), r1(M.m[1][0], M.m[1][1], M.m[1][2]);
            double U = 2 * n.dot(r0) + 2048, V = 2 * n.dot(r1) + 2048;
            double dU = std::floor(U / 4096) * 4096, dV = std::floor(V / 4096) * 4096;
            n = n - r0 * (dU / 2) - r1 * (dV / 2);
        }
    }

    // ---------------------------------------------------------------- state
    Params k_{};
    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object camera_, obuke_;
    std::array<sr::Object, kFish> fish_;
    std::array<sr::V3, 3> tailNormal_;     // the loader's normals of the tail vertices
    sr::V3 camPos_, obukePos_;
    sr::M3 obukeRot0_, tickStep_, fishRot0_;
    sr::V3 tickAxis_;
    double tickAngle_ = 0;
    int pal_ = 0;

    std::array<FishState, kFish> start_{};   // the init's placement
    std::vector<Frame> frames_;
    std::vector<double> frameM_;
    double latency_ = 0, mBefore_ = 0;

    Field field_;
    std::vector<Field> checkpoints_;
    std::vector<bool> saved_;
    std::vector<int16_t> rg_;
    int texA_ = -3, texB_ = -3;
    Texture fieldTex_;
    float band_[2] = {2, 198};
    Shader shader_;
};

}  // namespace

REGISTER_SCENE(WaterScene, "water");

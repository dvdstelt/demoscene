// The flower part (14:00..19:63, inside/re/notes-flower.md): a camera flies along a LightWave motion path through a
// bent tube textured with flower/jeesus2.raw through flower/jeesus.shd, with fixed per-vertex light levels; only a
// window of 348 triangles of the tube exists at a time, and it advances with the camera. In front of the camera hangs
// a flower (flower/kukka1.sro, environment-mapped with flower/jeesus1.raw) that spins, tilts and morphs between kukka1
// and a second shape; script events hide it and show it again with the next shape and tilt.
//
// Ports the init 0x409730 with the LightWave scene reader 0x408030 (scenes/lws.h), the start 0x4098c4 (scene,
// objects, materials, the light levels with rand() 0x4134cd), the tick 0x40940c (LightWave frame, face window,
// texture scroll, morph phase, spin), the frame 0x4095f8 (the camera and the tube from the motion table 0x4080d8, the
// materials' texture corners, the morph 0x4114c8, the render 0x40c6ac) and the events 0x409358 (show the flower with
// the next shape) and 0x4093f0 (hide it). Their palette flashes, the start's palette and the script's flashes and fade
// are the framework's (inside/fade.cpp). SurRender is the engine's (inside/engine.h): the painter's order with the
// flower's sort bias (it is drawn over the tube except where a tube wall comes within ~1700 units of it), the
// environment mapping 0x41acc4, the rasterizers 0x414e5c (tube) and 0x412db0 (flower), the near plane at camera z 64.
//
// The tick state is replayed exactly from the part start for every refresh: the LightWave frame as the original's
// float32 sum of 0.15 per tick with its wrap to 0 when it passes 260 (tick 1734: the camera jumps back to the start of
// the tube), the face window per whole tick, the morph phase and spin as float32 sums restarted by the events.
// Evaluated exactly instead of the original's shortcuts: continuous quantities (the LightWave frame, the texture
// scroll, the spin, the morph phase) are blended between tick n and n + 1 by the fraction of the refresh, so the
// camera, the scroll and the flower move smoothly at any frame rate (the face window, the events and the wrap stay on
// their tick); the camera position, the morphed vertices and normals and the environment-map coordinates are floats
// instead of truncated integers; the engine interpolates u, v and the light level perspective-correctly, filters the
// texels in colour and blends the shade table's neighbouring rows (continuous level), and anti-aliases the edges.
// Kept: the double spline of the camera (with the original's pi = 3.1415 for the LightWave angles), the light levels
// from rand() with the capture's seed, the window, the morph of kukka1's faces with kukka2's vertices (k = 7, 8), the
// sort bias.
//
// Every parameter is read from INSIDE.EXE: file names by the instructions that pass them to 0x400614, the keywords and
// constants of the reader, the scene's and objects' set-up from the immediates of the start routine, the tables of
// tilt angles, morph pairs and phase steps, and the tick's constants (addresses below).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"
#include "scenes/lws.h"

namespace {

namespace sr = inside::sr;
namespace lws = inside::lws;

// ---- the init 0x409730: `mov eax, <name>; call 0x400614` for every file
constexpr uint32_t kTubeFile = 0x4097a9, kFlowerFile = 0x4097b8, kShdFile = 0x40985d, kPalFile = 0x40986c,
                   kEnvTexFile = 0x40987b, kTubeTexFile = 0x40988a, kLwsFile = 0x4098a0;
constexpr uint32_t kTargetFiles = 0x4097c7;   // morph targets 0..9: ten of these instructions, 15 bytes apart
constexpr int kTargets = 10;                  // file[3..12] (the start's loop: i = 3 .. 12)
constexpr uint32_t kLwsScale = 0x409896;      // push 3000.0f (the reader's scale)
// ---- the reader 0x408030: `add eax, <text length>` (the byte before the end becomes the terminator)
constexpr uint32_t kLwsLength = 0x408053;
constexpr uint32_t kKwScene = 0x42286c, kKwFirst = 0x422871, kKwLast = 0x42287c, kKwObject = 0x422886,
                   kKwCamera = 0x422893;
constexpr uint32_t kLwsDegrees = 0x42149d;    // double 3.1415 / 180 (the reader's degrees -> radians)
// ---- the start 0x4098c4
constexpr uint32_t kTubeScale = 0x40993a, kTubeRaster = 0x40995f;   // mov ebx, 16.16 scale; mov edx, rasterizer
constexpr uint32_t kTubeShown = 0x40997e;                            // mov edx, 350: faces from here on hidden
constexpr uint32_t kTubeBias = 0x4099b7;                             // mov [tube + 0xcc], 0
constexpr uint32_t kLightMax = 0x4099e4, kLightMod = 0x409a32, kLightOff = 0x409a41;   // 0x3ff, 600, 300
constexpr uint32_t kLightX = 0x4216db, kLightA = 0x4216e3, kLightY = 0x4216eb, kLightI = 0x4216f3,
                   kLightB = 0x4216fb;                               // doubles 0.05, 300, 0.04, 1 / 20.3, 700
constexpr uint32_t kFlowerScale = 0x409b1a, kFlowerRaster = 0x409b3f;
constexpr uint32_t kFlowerBias = 0x409b78, kFlowerZ = 0x409b87;      // mov [flower + 0xcc], [flower + 0x18]
constexpr uint32_t kViewW = 0x409ca7, kViewH = 0x409cb3, kViewX0 = 0x409cbf, kViewX1 = 0x409ccb, kViewY0 = 0x409cd7,
                   kViewY1 = 0x409ce3, kFov = 0x409cef, kNear = 0x409cfb;
constexpr uint32_t kTexLog2 = 0x409d68;                              // mov byte [mat1 + 0x40], 8
// ---- the part's state (initial values in the image) and tables
constexpr uint32_t kU0 = 0x42293c, kV0 = 0x422940, kCount = 0x422944, kPhase = 0x422948, kK = 0x42294c,
                   kActive = 0x422950, kSpin = 0x422954;
constexpr uint32_t kPairs = 0x4228ac, kSteps = 0x4228f4, kAngles = 0x422918;
// ---- the tick 0x40940c
constexpr uint32_t kWindowRatio = 0x4214db;   // double 0.75: faces per tick
constexpr uint32_t kWindow = 0x409440;        // add edx, 348
constexpr uint32_t kFrameStep = 0x4214e3, kFrameMax = 0x4214eb;    // double 0.15, float 260
constexpr uint32_t kScroll = 0x4094fe;        // add edi, 0x3b
constexpr uint32_t kSpinStep = 0x4214ef, kTiltDeg = 0x4214f7;     // doubles 0.02, 0.01745329252
constexpr uint32_t kPairMod = 0x409497;       // mov ecx, 10
// ---- the frame 0x4095f8: add eax, 0x1000 (the texture corners: one texture per quad)
constexpr uint32_t kCorner = 0x409640;
// ---- the events
constexpr uint32_t kShow = 0x409358, kHide = 0x4093f0;
constexpr uint32_t kShowMod = 0x409383;       // mov ebx, 9

// The generator's seed at the entry of 0x4098c4 in the capture (notes-flower.md 4; it depends on the frame rate of
// the intro, notes-core.md 9).
constexpr uint32_t kSeed = 0xde09b53bu;
constexpr int kMaxTicks = 4096;   // the replayed ticks (the part has 2,101)

// Immediate operands.
uint32_t movRegImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("flower: no mov r32, imm32 at " + std::to_string(a));
    return x.u32(a + 1);
}
int32_t movMemImm(const InsideExe& x, uint32_t a) {    // mov dword [r + disp8 / disp32], imm32
    if (x.u8(a) != 0xc7) throw std::runtime_error("flower: no mov [r + d], imm32 at " + std::to_string(a));
    int mod = x.u8(a + 1) >> 6;
    return x.s32(a + (mod == 1 ? 3 : 6));
}
int32_t aluImm(const InsideExe& x, uint32_t a) {       // add / sub r32, imm32 / imm8; add eax, imm32
    if (x.u8(a) == 0x81) return x.s32(a + 2);
    if (x.u8(a) == 0x83) return x.s8(a + 2);
    if (x.u8(a) == 0x05) return x.s32(a + 1);
    throw std::runtime_error("flower: no add / sub with an immediate at " + std::to_string(a));
}
int byteImm(const InsideExe& x, uint32_t a) {          // mov byte [r + disp8], imm8
    if (x.u8(a) != 0xc6 || (x.u8(a + 1) >> 6) != 1) throw std::runtime_error("flower: no mov byte at " + std::to_string(a));
    return x.u8(a + 3);
}
float pushedFloat(const InsideExe& x, uint32_t a) {    // push imm32
    if (x.u8(a) != 0x68) throw std::runtime_error("flower: no push imm32 at " + std::to_string(a));
    uint32_t bits = x.u32(a + 1);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
std::string fileName(const InsideExe& x, uint32_t movEax) { return x.cstr(movRegImm(x, movEax)); }

// Watcom's rand() 0x4134cd.
struct Rand {
    uint32_t seed;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

// float32 sum of `step` (a double, or a float widened) over n ticks: s = (float)(s + step).
float floatSum(float s, double step, int n) {
    for (int i = 0; i < n; i++) s = float(double(s) + step);
    return s;
}

class FlowerScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        eng_.init(c);
        readScene(x, d);
        readParameters(x);

        // 0x409730 / 0x4098c4: data, materials (mat1 / mat2: the tube's two triangles of a quad, fmat: the flower)
        pal_ = eng_.palette(inside::palette(d, fileName(x, kPalFile)));
        int shd = eng_.table(d.file(fileName(x, kShdFile)));
        int size = 1 << byteImm(x, kTexLog2);
        int tubeTex = eng_.textureFile(fileName(x, kTubeTexFile), size, size);
        int envTex = eng_.textureFile(fileName(x, kEnvTexFile), size, size);
        mat1_ = eng_.material({.texture = tubeTex, .texture2 = tubeTex, .table1 = shd, .table2 = shd, .fixedUV = true});
        mat2_ = eng_.material({.texture = tubeTex, .texture2 = tubeTex, .table1 = shd, .table2 = shd, .fixedUV = true});
        int fmat = eng_.material({.texture = envTex, .texture2 = envTex, .table1 = shd, .table2 = shd});

        // the scene (0x410878 defaults, then the start's values) and the camera
        scene_.width = movMemImm(x, kViewW);
        scene_.height = movMemImm(x, kViewH);
        scene_.x0 = movMemImm(x, kViewX0);
        scene_.x1 = movMemImm(x, kViewX1);
        scene_.y0 = movMemImm(x, kViewY0);
        scene_.y1 = movMemImm(x, kViewY1);
        scene_.fov = movMemImm(x, kFov);
        scene_.near = movMemImm(x, kNear);
        scene_.add(camera_);
        scene_.camera = &camera_;

        // the tube: every face through 0x414e5c, odd faces with mat2, per-vertex light levels
        tube_.loadSro(d.file(fileName(x, kTubeFile)), int32_t(movRegImm(x, kTubeScale)), mat1_);
        tube_.setRaster(sr::rasterAt(movRegImm(x, kTubeRaster)));
        scene_.add(tube_);
        tube_.bias = movMemImm(x, kTubeBias);
        lightLevels(x);
        for (size_t j = 1; j < tube_.faces.size(); j += 2) tube_.faces[j].material = mat2_;

        // the flower: 0x412db0 with environment mapping (flag 0x1000), parented to the camera at (0, 0, 1500)
        int32_t fscale = int32_t(movRegImm(x, kFlowerScale));
        flower_.loadSro(d.file(fileName(x, kFlowerFile)), fscale, fmat);
        flower_.setRaster(sr::rasterAt(movRegImm(x, kFlowerRaster)));
        flower_.flags |= sr::kEnvMap;
        flower_.parent = &camera_;
        flower_.bias = movMemImm(x, kFlowerBias);
        flower_.pos = sr::V3(0, 0, movMemImm(x, kFlowerZ));
        scene_.add(flower_);
        flowerVerts_ = flower_.verts;
        flowerFaces_ = flower_.faces;
        for (int i = 0; i < kTargets; i++) {   // the morph targets (not in the scene)
            targets_[i].loadSro(d.file(fileName(x, kTargetFiles + 15 * uint32_t(i))), fscale, fmat);
            targets_[i].flags |= sr::kEnvMap;
        }
        replayTicks();
        eng_.prepare();   // the textures, tables and upscales now, not at the first frame
    }

    void render(Ctx& c, double r) override {
        const bool ex = sr::fidelity().exact;
        // ticks run for the frame shown at r: n = r - start (whole ticks w, fraction fr)
        double n = std::max(0.0, c.refreshes(r));
        int w = std::min(int(std::floor(n)), kMaxTicks - 2);
        double fr = std::min(1.0, n - w);
        if (ex) fr = 0;
        const Tick &t0 = ticks_[size_t(w)], &t1 = ticks_[size_t(w + 1)];

        // 0x4080d8: the camera and the tube from the LightWave frame (blended within the tick; at the wrap the
        // frame goes on past 260 until the tick that sets it to 0)
        double next = t1.count == 0 ? double(t0.frame) + frameStep_ : double(t1.frame);
        float f = float(double(t0.frame) + fr * (next - double(t0.frame)));
        for (size_t m = 0; m < lws_.motions.size() && m < 2; m++) {
            lws::Row s = lws::evaluate(lws_.motions[m], lws_.frames, f, tension_);
            sr::Object& o = m == 0 ? camera_ : tube_;
            o.pos = ex ? sr::V3(std::trunc(s.v[0]), std::trunc(s.v[1]), std::trunc(s.v[2])) : sr::V3(s.v[0], s.v[1], s.v[2]);
            o.rot = sr::rotateZ(sr::rotateX(sr::rotateY(sr::M3::identity(), s.v[4]), -double(s.v[3])), -double(s.v[5]));
        }

        // 0x40940c: the window of tube faces (whole ticks)
        window(t0.count);

        // the texture corners (0x4095f8): u0 = v0 advance 59 / 16 texels per tick, one whole texture per quad
        double u0 = ex ? double((scroll_ * w + u0Init_) & 0xfff) : std::fmod(scroll_ * n + u0Init_, 4096.0);
        double v0 = ex ? double((scroll_ * w + v0Init_) & 0xfff) : std::fmod(scroll_ * n + v0Init_, 4096.0);
        double D = corner_;
        sr::Material& m1 = eng_.materialAt(mat1_);
        sr::Material& m2 = eng_.materialAt(mat2_);
        const double uv1[6] = {u0, v0, u0 + D, v0, u0 + D, v0 + D}, uv2[6] = {u0, v0, u0 + D, v0 + D, u0, v0 + D};
        std::copy(uv1, uv1 + 6, m1.uv);
        std::copy(uv2, uv2 + 6, m2.uv);

        flowerState(c, r, n, w, fr);
        eng_.render(c, scene_, pal_, 0);   // 0x41127c(scene, 0), 0x40c6ac
    }

private:
    struct Tick {
        float frame = 0;   // [0x4b685c], the LightWave frame
        int count = 0;     // [0x422944], the window counter
    };

    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object camera_, tube_, flower_;
    sr::Object targets_[kTargets];
    std::vector<sr::Vertex> flowerVerts_;   // kukka1 as loaded (the vertices the morph does not reach keep these)
    std::vector<sr::Face> flowerFaces_;
    lws::Table lws_;
    float tension_ = 0;
    int pal_ = 0, mat1_ = 0, mat2_ = 0;
    std::vector<Tick> ticks_;
    // parameters (read from the exe)
    double windowRatio_ = 0, frameStep_ = 0, spinStep_ = 0, tiltDeg_ = 0, corner_ = 0;
    float frameMax_ = 0, phase0_ = 0, spin0_ = 0;
    int window_ = 0, shown_ = 0, scroll_ = 0, u0Init_ = 0, v0Init_ = 0, count0_ = 0, k0_ = 0, active0_ = 0;
    int showMod_ = 9, pairMod_ = 10;
    int pairs_[9][2] = {};
    float steps_[9] = {}, angles_[9] = {};

    // 0x409730: the LightWave scene flower/tunnel1.lws, scale 3000, tension [0x422868] = 0 (Catmull-Rom)
    void readScene(const InsideExe& x, const InsideDat& d) {
        auto file = d.file(fileName(x, kLwsFile));
        size_t len = std::min<size_t>(size_t(aluImm(x, kLwsLength)), file.size());
        std::string text(file.begin(), file.begin() + std::ptrdiff_t(len));
        if (!text.empty()) text.pop_back();   // text[len - 1] = 0
        lws::Keywords kw{x.cstr(kKwScene), x.cstr(kKwFirst), x.cstr(kKwLast), x.cstr(kKwObject), x.cstr(kKwCamera)};
        tension_ = 0.0f;   // xor edx, edx; mov [0x422868], edx
        lws_ = lws::read(text, kw, pushedFloat(x, kLwsScale), tension_, x.f64(kLwsDegrees));
        if (lws_.frames <= 0) throw std::runtime_error("flower: empty camera motion");
    }

    void readParameters(const InsideExe& x) {
        windowRatio_ = x.f64(kWindowRatio);
        window_ = aluImm(x, kWindow);
        shown_ = int(movRegImm(x, kTubeShown));
        frameStep_ = x.f64(kFrameStep);
        frameMax_ = x.f32(kFrameMax);
        scroll_ = aluImm(x, kScroll);
        spinStep_ = x.f64(kSpinStep);
        tiltDeg_ = x.f64(kTiltDeg);
        corner_ = aluImm(x, kCorner);
        showMod_ = int(movRegImm(x, kShowMod));
        pairMod_ = int(movRegImm(x, kPairMod));
        if (showMod_ < 1 || showMod_ > 9 || pairMod_ < 1 || pairMod_ > kTargets)
            throw std::runtime_error("flower: unexpected table sizes");
        for (int k = 0; k < showMod_; k++) {
            pairs_[k][0] = x.s32(kPairs + 8 * uint32_t(k));
            pairs_[k][1] = x.s32(kPairs + 8 * uint32_t(k) + 4);
            steps_[k] = x.f32(kSteps + 4 * uint32_t(k));
            angles_[k] = x.f32(kAngles + 4 * uint32_t(k));
        }
        u0Init_ = x.s32(kU0);
        v0Init_ = x.s32(kV0);
        count0_ = x.s32(kCount);
        phase0_ = x.f32(kPhase);
        k0_ = x.s32(kK);
        active0_ = x.s32(kActive);
        spin0_ = x.f32(kSpin);
    }

    // 0x4098c4: L = trunc(sin(x 0.05) 300 cos(y 0.04) + sin(i / 20.3) 700) + rand() % 600 - 300, clamped to 0..1023
    void lightLevels(const InsideExe& x) {
        double a = x.f64(kLightX), b = x.f64(kLightA), cy = x.f64(kLightY), di = x.f64(kLightI), e = x.f64(kLightB);
        int maxL = int(movRegImm(x, kLightMax)), mod = int(movRegImm(x, kLightMod)), off = aluImm(x, kLightOff);
        Rand rng{kSeed};
        for (size_t i = 0; i < tube_.verts.size(); i++) {
            sr::Vertex& v = tube_.verts[i];
            double s = std::sin(v.p.x * a) * b * std::cos(v.p.y * cy) + std::sin(double(i) * di) * e;
            int L = int(std::trunc(s));
            L += rng() % mod - off;
            v.shade = std::clamp(L, 0, maxL);
        }
    }

    // 0x40940c, the LightWave frame and the window counter, replayed from the start (0x408090 sets the frame to 0)
    void replayTicks() {
        ticks_.assign(kMaxTicks, Tick());
        Tick t{0.0f, count0_};
        ticks_[0] = t;
        for (int i = 1; i < kMaxTicks; i++) {
            t.count++;
            double sum = double(t.frame) + frameStep_;   // compared before it is stored as a float
            t.frame = float(sum);
            if (sum > double(frameMax_)) {
                t.count = 0;
                t.frame = 0.0f;
            }
            ticks_[size_t(i)] = t;
        }
    }

    // The faces the tick has shown and hidden by window counter `count` (0: the start's 0 .. 349).
    void window(int count) {
        int nf = int(tube_.faces.size()), lo = 0, hi = shown_ - 1;
        if (count > 0) {
            int last = std::min(int(std::trunc(count * windowRatio_)), nf - window_ - 1);   // the last n that ran
            lo = last + 1;
            hi = std::max(shown_ - 1, last + window_);
        }
        for (int i = 0; i < nf; i++) {
            uint32_t& fl = tube_.faces[size_t(i)].flags;
            fl = (i >= lo && i <= hi) ? (fl & ~sr::kFaceHidden) : (fl | sr::kFaceHidden);
        }
    }

    // The flower at refresh r: the events (0x409358 show, 0x4093f0 hide) up to r, the spin and phase since the last
    // show, the morph (0x4114c8), the rotation Rx(tilt) Ry(spin) (0x4108dc).
    void flowerState(Ctx& c, double r, double n, int w, double fr) {
        int k = k0_;
        bool hidden = false;   // the loader clears the flags: shown from the start
        double showAt = -1;    // ticks of the part at the last show event
        for (const ScriptEntry& e : c.script()) {
            if (e.refresh < c.start || e.refresh >= c.end || e.refresh > r) continue;
            if (e.routine == kShow) {
                k = (k + 1) % showMod_;
                hidden = false;
                showAt = e.refresh - c.start;
            } else if (e.routine == kHide) {
                hidden = true;   // phase, A and B freeze; spin and the rotation run on unseen
            }
        }
        if (hidden) {
            flower_.flags |= sr::kHidden;
            return;
        }
        flower_.flags &= ~sr::kHidden;
        const bool ex = sr::fidelity().exact;

        // spin: 0.02 per tick from the start or the last show; phase: the step of k per tick while active (the
        // start's state is active, every show restarts it from 0)
        int since = w - std::max(0, int(showAt));
        bool fromShow = showAt >= 0;
        double spin = floatSum(fromShow ? 0.0f : spin0_, spinStep_, since) + fr * spinStep_;
        double phase = phase0_;
        if (fromShow || active0_)
            phase = floatSum(fromShow ? 0.0f : phase0_, double(steps_[k]), since) + fr * double(steps_[k]);
        if (ex) {
            spin = float(spin);
            phase = float(phase);
        }

        // 0x4114c8: kukka1's faces with the pair's vertices and normals; the vertices past the shorter target keep
        // kukka1's (the first frame after every show morphs the old pair with weight 0)
        const sr::Object& A = targets_[std::abs(pairs_[k][0] % pairMod_)];
        const sr::Object& B = targets_[std::abs(pairs_[k][1] % pairMod_)];
        flower_.verts = flowerVerts_;
        flower_.faces = flowerFaces_;
        flower_.morph(A, B, std::fabs(std::sin(phase)));

        // the rotation: identity until the first tick (0x4108dc with zeros), then Rx(tilt[k]) Ry(spin)
        if (!fromShow && n < 1) flower_.rot = sr::M3::identity();
        else flower_.euler(double(angles_[k]) * tiltDeg_, spin, 0.0);
    }
};

}  // namespace

REGISTER_SCENE(FlowerScene, "flower");

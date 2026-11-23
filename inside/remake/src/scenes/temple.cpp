// The temple (31:63..36:16, inside/re/notes-temple.md): the camera orbits inside a round chrome hall (temple/mesta23,
// mapped with temple/temple.raw through the leftover u, v of the .sro and lit by a pulsing point light at its centre
// through temple/temple.int), with the environment-mapped bowl (mesta21), four translucent environment-mapped pillars
// (mesta22, blended with what is behind through temple.int), three pink glowing pipes (temple/putki.sro three times,
// lighting up what is behind through rows 32.. of temple/flare.shd: one standing, two swinging) and ten flares
// (temple/flare.raw through flare.shd, billboards at positions from rand()) moving in and out on one side of the hall.
// Ports the init 0x404464 (0x403900 the camera rig: a holder with the camera as its child; 0x4039c8 the objects, the
// flares and the light), the tick 0x40407c / 0x403f10 (the holder's orbit, the camera's roll, the pipes' swing), the
// frame 0x404424 / 0x4040b0 (positions, flare distances, light range, pipe glow, camera distance); the engine
// (inside/engine.h) does the render 0x40c6ac. The white flash at the start and the fade to black at 35:48 are the
// framework's.
//
// Clocks (inside/re/PORT.md): the frame shown at refresh r follows n = r - start tick calls (the holder H = Ry(0.01 n)
// and the camera Rx(15 deg) Rz(pi + 0.005 n) as continuous rotations about the steps' axes), the pipes' swing reads
// the last tick's M = callback(r - 1), and the frame routine, which does not wait for the copy, reads
// M = callback(r) - 1 (the flares, the light, the glow, the camera's distance). Before the first tick the pipes take
// the same formula instead of the init's angles (that frame is under the white flash). The random flare positions
// replay Watcom's rand() from the flight init's srand(0x45eafc) after its 9 calls.
//
// Evaluated exactly instead of the original's shortcuts (notes 13): the truncations to integer positions (camera,
// flares, loaded and scaled vertices, light range, glow values) are left out (the pipes' pivot is still the truncated
// vertex 12, as the original's formula has it); the engine projects without the 1/16-pixel steps, interpolates u, v,
// light level and glow perspective-correctly, filters the texels in colour, blends the neighbouring table rows of the
// continuous light and glow levels, and draws anti-aliased edges without the cracks between faces. The tables that
// read the background (the pillars' temple.int, the pipes' and flares' flare.shd) are drawn as the engine's per-row
// colour fits (dual-source blending). The scene is cleared to black every frame, so the inside of the pillars (where
// no opaque face is behind) and the cracks show black instead of the stale content of older frames (notes 11).
//
// Kept (notes 14): the hall's texture coordinates from the .sro's leftover bytes (streaks of the sphere picture), the
// hall's light mixing the texels with palette entries 0..62 (the light level is temple.int's row), the pulsing reach of
// the light, the pipes' glow profile and flare.shd's irregular rows, the flares' vertical position with the horizontal
// focal length (the engine's billboards: 1.2 times as far from the centre as the geometry), the painter's order.
//
// Every parameter is read from INSIDE.EXE: the file names by the addresses of their strings, the constants from the
// data (doubles and floats at the addresses below) and from the immediate operands of the routines' instructions.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"

namespace {

namespace sr = inside::sr;

// ---- 0x404464: the file names (strings passed to 0x400614); putki.sro is loaded three times
constexpr uint32_t kRawName = 0x420b34, kFlareName = 0x420b48, kIntName = 0x420b5c, kShdName = 0x420b70,
                   kPalName = 0x420b84;
constexpr uint32_t kSroName[6] = {0x420b98, 0x420bac, 0x420bc0, 0x420bd4, 0x420bd4, 0x420bd4};
// 0x410618(scene, W = edx, H = ebx, x0 = ecx (xor), pushes y0, x1, y1), the fov (mov [scene + 0x3c]), material 2's
// table = flare.shd + (add ecx, 0x2000), the flare count (mov ebx, stored to [0x425e64])
constexpr uint32_t kViewW = 0x40457a, kViewH = 0x404571, kViewY0 = 0x404578, kViewX1 = 0x40456c, kViewY1 = 0x404567;
constexpr uint32_t kFov = 0x404597, kPipeTableOffset = 0x4045e0, kFlareCount = 0x4045eb;
// ---- 0x403900: the holder's rotation (six pushes for 0x4145f4), the camera's position (0x413510: z, y, x pushed
// high, low, between other instructions) and rotation
constexpr uint32_t kHolderEuler = 0x40391a;
constexpr uint32_t kCamPos[6] = {0x403969, 0x40396e, 0x403970, 0x403980, 0x403985, 0x403992};
constexpr uint32_t kCamEuler = 0x4039a1;
// ---- 0x4039c8: the .sro scale (mov ebx), the last rasterizer each object gets (mov edx before 0x410850)
constexpr uint32_t kSroScale = 0x403a02;
constexpr uint32_t kRaster[6] = {0x403aa3, 0x403ab2, 0x403ad9, 0x403aea, 0x403bb2, 0x403c73};
// pipe 3's pivot: `mov edx, [eax + disp32]` reads the x of a vertex (vertex + 8, 0x40 bytes per vertex)
constexpr uint32_t kPivotLoad = 0x403b4a;
constexpr uint32_t kVertexSize = 0x40, kVertexX = 8;
// the pipes' scales (0x4116c8: float sx, sy, sz pushed sz first) for objects 3, 4, 5
constexpr uint32_t kPipeScale[3][3] = {{0x403b6c, 0x403b62, 0x403b5d}, {0x403c2d, 0x403c23, 0x403c1e},
                                       {0x403cad, 0x403ca3, 0x403c9e}};
constexpr uint32_t kPipe5Euler = 0x403c82;                          // the standing pipe's fixed rotation
constexpr uint32_t kPipePos[3] = {0x403d0c, 0x403d25, 0x403ccb};    // 0x413510 into 0x4259c0 + 0x18 i, i = 3, 4, 5
// the flares: rand() % m + a for z, y, x (in this call order), the sprite fields [0xe0] pitch, [0xe4] w, [0xe8] h,
// [0xec] s, [0xf0] size, the light's intensity [0xcc]
constexpr uint32_t kRandMod[3] = {0x403daf, 0x403dd9, 0x403e00}, kRandAdd[3] = {0x403dbe, 0x403de8, 0x403e0f};
constexpr uint32_t kFlarePitch = 0x403e30, kFlareW = 0x403e40, kFlareH = 0x403e50, kFlareS = 0x403e60,
                   kFlareSize = 0x403e70, kLightIntensity = 0x403eed;
// The flight's init 0x402948 seeds the generator (`mov eax, 0x45eafc` at 0x40295d, `call srand` at 0x402967) and
// calls rand() 9 times (notes-flight.md: x0, z0, three ships, the camera and its control point); the temple's init
// runs right after it.
constexpr uint32_t kSrandOperand = 0x40295e;
constexpr int kFlightRandCalls = 9;
// ---- the tick 0x403f10: the holder's and the camera's step rotations (six pushes each); the pipes' swing
// A = sin(M a1) a2 DEG, B = cos(M b1) b2 DEG, pipe 3 = R(A + pi/2, B, 0), pipe 4 = R(pi/2 - A, -B, 0)
constexpr uint32_t kHolderStep = 0x403f1e, kCamStep = 0x403f47;
constexpr uint32_t kSwingA1 = 0x420aa4, kSwingA2 = 0x420aac, kDeg = 0x420ab4, kSwingB1 = 0x420abc,
                   kSwingB2 = 0x420ac4, kHalfPi = 0x420acc;
// ---- the frame 0x4040b0: flare i at base_i (abs(sin((M + 13 i) f1) f2) + f3) (the 13: add edi, imm8); light range
// abs(sin(M l1)) l2 (l2 a float); pipe glow of vertices first.. (mov ebx = first vertex x 0x40, cmp ebx = end):
// sin((M + p0 + dp j) / (d0 + dd j + g1)) g2 g2 g3 + base (floats 256, 320, 352 for pipes 3, 4, 5); camera position
// = camPos (sin(M c1) c2 + c3)
constexpr uint32_t kFlareF1 = 0x420ad4, kFlareF2 = 0x420adc, kFlareF3 = 0x420b18, kFlarePhase = 0x4041ea;
constexpr uint32_t kLightL1 = 0x420ae4, kLightL2 = 0x420aec;
constexpr uint32_t kGlowFirst = 0x404291, kGlowD0 = 0x404296, kGlowP0 = 0x4042b2, kGlowDp = 0x404379,
                   kGlowDd = 0x40437c, kGlowEnd = 0x404386;
constexpr uint32_t kGlowG1 = 0x420b20, kGlowG2 = 0x420af0, kGlowG3 = 0x420af8;
constexpr uint32_t kGlowBase[3] = {0x420b28, 0x420b2c, 0x420b30};
constexpr uint32_t kCamC1 = 0x420b00, kCamC2 = 0x420b08, kCamC3 = 0x420b10;

// ------------------------------------------------------------------------------------------------ reading the code
[[noreturn]] void bad(const char* what, uint32_t a) {
    throw std::runtime_error(std::string("temple: no ") + what + " at " + std::to_string(a));
}

// The immediate operand of the instruction at a: push imm32 / imm8, mov r32, imm32, and the ModRM forms
// mov r/m32, imm32 (c7 /0), op r/m32, imm32 (81) and op r/m32, imm8 (83).
int32_t imm(const InsideExe& x, uint32_t a) {
    uint8_t op = x.u8(a);
    if (op == 0x68) return x.s32(a + 1);
    if (op == 0x6a) return x.s8(a + 1);
    if (op >= 0xb8 && op <= 0xbf) return x.s32(a + 1);
    if (op != 0xc7 && op != 0x81 && op != 0x83) bad("immediate operand", a);
    uint8_t m = x.u8(a + 1);
    int mod = m >> 6, rm = m & 7;
    uint32_t p = a + 2;
    if (mod != 3) {
        if (rm == 4 && (x.u8(p++) & 7) == 5 && mod == 0) p += 4;   // SIB (base = disp32 without a register)
        else if (rm == 5 && mod == 0) p += 4;                       // disp32
        p += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    }
    return op == 0x83 ? x.s8(p) : x.s32(p);
}

// add r32, imm (83 / 81 with reg field 0) or sub r32, imm (reg field 5) as the value added.
int32_t addend(const InsideExe& x, uint32_t a) {
    int reg = (x.u8(a + 1) >> 3) & 7;
    if ((x.u8(a) != 0x81 && x.u8(a) != 0x83) || (reg != 0 && reg != 5)) bad("add / sub", a);
    return reg == 5 ? -imm(x, a) : imm(x, a);
}

double pushedDouble(const InsideExe& x, uint32_t hi, uint32_t lo) {
    uint64_t bits = uint64_t(uint32_t(imm(x, hi))) << 32 | uint32_t(imm(x, lo));
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

// An immediate operand holding a float (pushed, or stored with mov [r + d], imm32).
float immFloat(const InsideExe& x, uint32_t a) {
    uint32_t bits = uint32_t(imm(x, a));
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Three doubles pushed last-first, high dword first (0x4145f4 and 0x413510 take a, b, c on the stack).
sr::V3 pushedTriple(const InsideExe& x, const uint32_t (&at)[6]) {
    return {pushedDouble(x, at[4], at[5]), pushedDouble(x, at[2], at[3]), pushedDouble(x, at[0], at[1])};
}

// The same from six consecutive pushes starting at a.
sr::V3 pushedTripleRun(const InsideExe& x, uint32_t a) {
    uint32_t at[6];
    for (uint32_t& p : at) {
        if (x.u8(a) != 0x68 && x.u8(a) != 0x6a) bad("push", a);
        p = a;
        a += x.u8(a) == 0x68 ? 5 : 2;
    }
    return pushedTriple(x, at);
}

sr::M3 pushedEuler(const InsideExe& x, uint32_t a) {
    sr::V3 e = pushedTripleRun(x, a);
    return sr::euler(e.x, e.y, e.z);
}

// Watcom's rand() 0x4134cd (srand 0x4134f1 sets the seed).
struct Rand {
    uint32_t seed = 1;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

// A rotation stepped k times (R = R0 S^k): continuous about S's axis, or the original's products in exact mode.
struct Stepped {
    sr::M3 start, step;
    sr::V3 axis;
    double angle = 0;
    void set(const sr::M3& s0, const sr::M3& s) {
        start = s0;
        step = s;
        sr::toAxisAngle(step, axis, angle);
    }
    sr::M3 at(double k) const {
        if (!sr::fidelity().exact) return start * sr::axisAngle(axis, angle * k);
        sr::M3 r = start;
        for (int i = 0, n = int(std::floor(k)); i < n; i++) r = r * step;
        return r;
    }
};

class TempleScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        eng_.init(c);
        // 0x404464: data, scene, materials 1 (temple.raw, temple.int) and 2 (temple.raw, flare.shd from row 32)
        pal_ = eng_.palette(inside::palette(d, x.cstr(kPalName)));
        int raw = eng_.textureFile(x.cstr(kRawName), 256, 256);
        std::span<const uint8_t> shd = d.file(x.cstr(kShdName));
        int intTable = eng_.table(d.file(x.cstr(kIntName)));
        int shdTable = eng_.table(shd);
        int pipeTable = eng_.table(shd.subspan(size_t(imm(x, kPipeTableOffset))));
        int mat1 = eng_.material({.texture = raw, .table1 = intTable});
        int mat2 = eng_.material({.texture = raw, .table1 = pipeTable});
        scene_.width = imm(x, kViewW);
        scene_.height = imm(x, kViewH);
        scene_.x0 = 0;
        scene_.y0 = imm(x, kViewY0);
        scene_.x1 = imm(x, kViewX1);
        scene_.y1 = imm(x, kViewY1);
        scene_.fov = imm(x, kFov);

        // 0x403900: the holder (identity, never moved) and the camera, its child
        holder_.rot = pushedEuler(x, kHolderEuler);
        scene_.add(holder_);
        scene_.add(camera_);
        camera_.parent = &holder_;
        scene_.camera = &camera_;
        camPos_ = pushedTriple(x, kCamPos);
        holderRot_.set(holder_.rot, pushedEuler(x, kHolderStep));
        camRot_.set(pushedEuler(x, kCamEuler), pushedEuler(x, kCamStep));

        // 0x4039c8: the six objects (every face material 1), environment-mapped (0x1000) except the hall, which is
        // lit (0x40) and keeps the .sro's u, v
        int32_t scale = imm(x, kSroScale);
        for (int i = 0; i < 6; i++) {
            sr::Object& o = obj_[i];
            o.loadSro(d.file(x.cstr(kSroName[i])), scale, mat1);
            o.setRaster(sr::rasterAt(uint32_t(imm(x, kRaster[i]))));
            scene_.add(o);
            o.flags |= sr::kEnvMap;
        }
        obj_[2].flags = (obj_[2].flags & ~uint32_t(sr::kEnvMap)) | sr::kLit;
        // the pipes: 3 and 4 moved so that the truncated vertex 12 is their pivot, all three scaled (the bounding box
        // is not updated), normals again, material 2
        uint32_t pivotOffset = x.u32(kPivotLoad + 2);
        if (x.u8(kPivotLoad) != 0x8b || (pivotOffset - kVertexX) % kVertexSize) bad("vertex load", kPivotLoad);
        size_t pivot = (pivotOffset - kVertexX) / kVertexSize;
        for (int p = 0; p < 3; p++) {
            sr::Object& o = obj_[3 + p];
            if (p < 2) {
                sr::V3 v = o.verts.at(pivot).p;
                o.translate(sr::V3(-std::trunc(v.x), -std::trunc(v.y), -std::trunc(v.z)));
            }
            o.scale(immFloat(x, kPipeScale[p][0]), immFloat(x, kPipeScale[p][1]),
                    immFloat(x, kPipeScale[p][2]));
            o.computeNormals();
            for (sr::Face& f : o.faces) f.material = mat2;
            pos_[3 + p] = pushedTripleRun(x, kPipePos[p]);
        }
        pipe5Rot_ = pushedEuler(x, kPipe5Euler);

        // the flares: billboards of flare.raw through flare.shd at rand() positions (calls 10.. after the srand)
        Rand rng{x.u32(kSrandOperand)};
        for (int k = 0; k < kFlightRandCalls; k++) rng();
        sr::Sprite sp;
        sp.w = imm(x, kFlareW);
        sp.h = imm(x, kFlareH);
        sp.texture = eng_.texture(d.file(x.cstr(kFlareName)), sp.w, sp.h, imm(x, kFlarePitch));
        sp.table = shdTable;
        sp.scale = immFloat(x, kFlareS);
        sp.size = immFloat(x, kFlareSize);
        int nf = imm(x, kFlareCount);
        flares_.resize(size_t(nf));   // the scene keeps pointers: no reallocation after this
        flareBase_.resize(size_t(nf));
        for (int i = 0; i < nf; i++) {
            double v[3];   // z, y, x
            for (int k = 0; k < 3; k++) v[k] = rng() % imm(x, kRandMod[k]) + addend(x, kRandAdd[k]);
            flareBase_[size_t(i)] = sr::V3(v[2], v[1], v[0]);
            flares_[size_t(i)].makeBillboard(sp);
            scene_.add(flares_[size_t(i)]);
        }
        // the light at the origin
        light_.flags |= sr::kLight;
        light_.intensity = imm(x, kLightIntensity);
        scene_.add(light_);

        // the tick's and the frame's constants
        swingA1_ = x.f64(kSwingA1);
        swingA2_ = x.f64(kSwingA2);
        deg_ = x.f64(kDeg);
        swingB1_ = x.f64(kSwingB1);
        swingB2_ = x.f64(kSwingB2);
        halfPi_ = x.f64(kHalfPi);
        flareF1_ = x.f64(kFlareF1);
        flareF2_ = x.f64(kFlareF2);
        flareF3_ = x.f64(kFlareF3);
        flarePhase_ = addend(x, kFlarePhase);
        lightL1_ = x.f64(kLightL1);
        lightL2_ = x.f32(kLightL2);
        glowFirst_ = imm(x, kGlowFirst) / int(kVertexSize);
        glowCount_ = (imm(x, kGlowEnd) - imm(x, kGlowFirst)) / int(kVertexSize);
        glowD0_ = imm(x, kGlowD0);
        glowP0_ = imm(x, kGlowP0);
        glowDp_ = addend(x, kGlowDp);
        glowDd_ = addend(x, kGlowDd);
        glowG1_ = x.f64(kGlowG1);
        glowG2_ = x.f64(kGlowG2);
        glowG3_ = x.f64(kGlowG3);
        for (int p = 0; p < 3; p++) glowBase_[p] = x.f32(kGlowBase[p]);
        camC1_ = x.f64(kCamC1);
        camC2_ = x.f64(kCamC2);
        camC3_ = x.f64(kCamC3);
        eng_.prepare();   // the textures, tables and upscales now, not at the first frame
    }

    void render(Ctx& c, double r) override {
        const bool ex = sr::fidelity().exact;
        auto t = [&](double v) { return ex ? std::trunc(v) : v; };
        auto tv = [&](sr::V3 v) { return sr::V3(t(v.x), t(v.y), t(v.z)); };
        // the tick 0x403f10 has run n times (the last at record r - 1, reading M = callback(r - 1)); the frame
        // routine 0x4040b0 reads M = callback(r) - 1
        double n = std::max(0.0, c.refreshes(r));
        double mt = Ctx::callback(r - 1), m = Ctx::callback(r) - 1;
        if (ex) {
            n = std::floor(n);
            mt = Ctx::callback(c.start + n - 1);
            m = std::floor(m);
        }
        holder_.rot = holderRot_.at(n);
        sr::M3 camRot = camRot_.at(n);
        double a = std::sin(mt * swingA1_) * swingA2_ * deg_;
        double b = std::cos(mt * swingB1_) * swingB2_ * deg_;
        obj_[3].rot = sr::euler(a + halfPi_, b, 0);
        obj_[4].rot = sr::euler(halfPi_ - a, -b, 0);
        obj_[5].rot = pipe5Rot_;

        // 0x4040b0
        for (int i = 0; i < 6; i++) obj_[i].pos = tv(pos_[i]);
        for (size_t i = 0; i < flares_.size(); i++) {
            double k = std::fabs(std::sin((m + flarePhase_ * double(i)) * flareF1_) * flareF2_) + flareF3_;
            flares_[i].pos = tv(flareBase_[i] * k);
        }
        light_.range = t(std::fabs(std::sin(m * lightL1_)) * lightL2_);
        for (int j = 0; j < glowCount_; j++) {
            double w = std::sin((m + glowP0_ + glowDp_ * j) / (glowD0_ + glowDd_ * j + glowG1_)) * glowG2_ * glowG2_ *
                       glowG3_;
            for (int p = 0; p < 3; p++) obj_[3 + p].verts.at(size_t(glowFirst_ + j)).shade = t(w + glowBase_[p]);
        }
        double cc = std::sin(m * camC1_) * camC2_ + camC3_;
        camera_.pos = tv(camPos_ * cc);
        camera_.rot = camRot;

        eng_.render(c, scene_, pal_, 0);   // the frame starts black (0x40461c clears to index 0)
    }

private:
    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object holder_, camera_, obj_[6], light_;
    std::vector<sr::Object> flares_;
    std::vector<sr::V3> flareBase_;
    sr::V3 pos_[6], camPos_;
    sr::M3 pipe5Rot_;
    Stepped holderRot_, camRot_;
    int pal_ = 0;
    double swingA1_ = 0, swingA2_ = 0, deg_ = 0, swingB1_ = 0, swingB2_ = 0, halfPi_ = 0;
    double flareF1_ = 0, flareF2_ = 0, flareF3_ = 0, flarePhase_ = 0, lightL1_ = 0, lightL2_ = 0;
    int glowFirst_ = 0, glowCount_ = 0;
    double glowD0_ = 0, glowP0_ = 0, glowDp_ = 0, glowDd_ = 0, glowG1_ = 0, glowG2_ = 0, glowG3_ = 0;
    double glowBase_[3] = {};
    double camC1_ = 0, camC2_ = 0, camC3_ = 0;
};

}  // namespace

REGISTER_SCENE(TempleScene, "temple");

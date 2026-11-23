// The logo part (3:63..5:63, inside/re/notes-intro-logo.md 3-4): the inside of a spiky sphere (logo/obuke.sro,
// textured with logo/twlogo.raw through logo/logo.shd, lit by a point light at its centre) turning around the camera,
// the dark mask of the lettering over it (logo/inmask.raw through logo/inmask.shd, bitmap type 4), and from 4:32 the
// title (logo/inside.raw, a picture). Ports the init 0x402f9c (0x402ca0 the camera, 0x402d0c the object and the
// light), the tick 0x402e30 (R = R S per refresh), the frame 0x402f1c (0x402e44, clear, render, bitmaps) and the
// script's 0x402f10 (the title from 4:32). The flashes are the framework's.
//
// Evaluated exactly instead of the original's shortcuts: the rotation R = S^k stepped once per refresh is a
// continuous rotation about S's axis by k times its angle (k fractional); the engine (inside/engine.h) draws the
// object without the integer steps (positions, normals, light, 1/16-pixel vertices), perspective-correct, with
// filtered texels and continuous light levels, anti-aliased; the mask's 16 levels are blended in colour (the fit of
// inmask.shd's rows) and filtered. Kept: the near plane at camera z 96 (the black wedges at the edges), the camera
// inside the sphere, the 150 degree view, the light's falloff and clamp (the white spike tips).
//
// Every parameter is read from INSIDE.EXE: the file names by the addresses of their strings, the step rotation, the
// camera position, fov, near plane, light range and intensity, the bitmaps' sizes and positions from the immediate
// operands of the routines' instructions (addresses below).
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"

namespace {

namespace sr = inside::sr;

// file names (strings passed to 0x400614 in 0x402f9c)
constexpr uint32_t kTexName = 0x4208b8, kShdName = 0x4208c8, kPalName = 0x4208d8, kSroName = 0x4208f8,
                   kMaskName = 0x420908, kMaskShdName = 0x420918;
// 0x402f9c: S = 0x4145f4(a, b, c): six pushes (c high, c low, b high, b low, a high, a low)
constexpr uint32_t kStepEuler = 0x402fb9;
// 0x410618(scene, W = edx, H = ebx, x0 = ecx (0), y0, x1, y1 pushed)
constexpr uint32_t kViewY1 = 0x403077, kViewX1 = 0x40307c, kViewH = 0x403081, kViewW = 0x403086, kViewY0 = 0x40308b;
// 0x415e50(pixels, pitch, width = ebx, height = ecx): text, mask
constexpr uint32_t kTextH = 0x40309e, kTextW = 0x4030a3, kMaskH = 0x4030b9, kMaskW = 0x4030c3;
// mov dword [scene + 0x3c], fov; mov dword [scene + 0x44], near
constexpr uint32_t kFov = 0x40310a, kNear = 0x403122;
// 0x402ca0: the camera's position (0x413510 pushes z, y, x as high, low dwords) and orientation (six pushes)
constexpr uint32_t kCamPos[6] = {0x402cca, 0x402ccf, 0x402cd1, 0x402cd3, 0x402cd5, 0x402ce2};
constexpr uint32_t kCamEuler = 0x402cf1;
// 0x402d0c: load scale (mov ebx), rasterizer (mov edx), the object's position pushes, its rotation, the light
constexpr uint32_t kSroScale = 0x402d3c, kRaster = 0x402d66;
constexpr uint32_t kObjPos[6] = {0x402d85, 0x402d87, 0x402d89, 0x402d90, 0x402d98, 0x402d9d};
constexpr uint32_t kObjEuler = 0x402daf;
constexpr uint32_t kLightRange = 0x402de3, kLightIntensity = 0x402df2;
// 0x402f1c: the mask's and the title's positions (push y, push x as floats)
constexpr uint32_t kMaskY = 0x402f5c, kMaskX = 0x402f66, kTextY = 0x402f79, kTextX = 0x402f83;
constexpr uint32_t kTextOn = 0x402f10;   // the script's routine that turns the title on (4:32)

// Immediate operands of the instructions at those addresses.
uint32_t pushImm(const InsideExe& x, uint32_t a) {
    if (x.u8(a) == 0x68) return x.u32(a + 1);
    if (x.u8(a) == 0x6a) return uint32_t(int32_t(x.s8(a + 1)));
    throw std::runtime_error("logo: no push at " + std::to_string(a));
}
uint32_t movRegImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("logo: no mov r32, imm32 at " + std::to_string(a));
    return x.u32(a + 1);
}
int32_t movMemImm(const InsideExe& x, uint32_t a) {    // mov dword [r + disp8 / disp32], imm32
    if (x.u8(a) != 0xc7) throw std::runtime_error("logo: no mov [r + d], imm32 at " + std::to_string(a));
    int mod = x.u8(a + 1) >> 6;
    return x.s32(a + (mod == 1 ? 3 : 6));
}
double pushedDouble(const InsideExe& x, uint32_t hi, uint32_t lo) {
    uint64_t bits = uint64_t(pushImm(x, hi)) << 32 | pushImm(x, lo);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
float pushedFloat(const InsideExe& x, uint32_t a) {
    uint32_t bits = pushImm(x, a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
// Six consecutive pushes: three doubles pushed last-first (0x4145f4, 0x413510 take a, b, c on the stack).
sr::V3 pushedTriple(const InsideExe& x, const uint32_t (&at)[6]) {
    return {pushedDouble(x, at[4], at[5]), pushedDouble(x, at[2], at[3]), pushedDouble(x, at[0], at[1])};
}
sr::V3 pushedTripleRun(const InsideExe& x, uint32_t a) {
    uint32_t at[6];
    for (uint32_t& p : at) {
        p = a;
        a += x.u8(a) == 0x68 ? 5 : 2;
    }
    return pushedTriple(x, at);
}

class LogoScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        eng_.init(c);
        // 0x402f9c: data, scene, bitmaps, material
        sr::V3 e = pushedTripleRun(x, kStepEuler);
        step_ = sr::euler(e.x, e.y, e.z);
        sr::toAxisAngle(step_, axis_, angle_);
        pal_ = eng_.palette(inside::palette(d, x.cstr(kPalName)));
        int tex = eng_.texture(d.file(x.cstr(kTexName)), 256, 256);
        int shd = eng_.table(d.file(x.cstr(kShdName)));
        int mat = eng_.material({.texture = tex, .table1 = shd});
        scene_.width = int(movRegImm(x, kViewW));
        scene_.height = int(movRegImm(x, kViewH));
        scene_.x0 = 0;   // ecx = 0 (xor ecx, ecx)
        scene_.y0 = int(pushImm(x, kViewY0));
        scene_.x1 = int(pushImm(x, kViewX1));
        scene_.y1 = int(pushImm(x, kViewY1));
        scene_.fov = movMemImm(x, kFov);
        scene_.near = movMemImm(x, kNear);
        int mw = int(movRegImm(x, kMaskW)), mh = int(movRegImm(x, kMaskH));
        mask_ = eng_.bitmap(d.file(x.cstr(kMaskName)), mw, mw, mh);
        mask_.type = 4;
        mask_.table = eng_.table(d.file(x.cstr(kMaskShdName)));
        textW_ = float(movRegImm(x, kTextW));
        textH_ = float(movRegImm(x, kTextH));
        // 0x402ca0: the camera
        scene_.add(camera_);
        scene_.camera = &camera_;
        camPos_ = pushedTriple(x, kCamPos);
        sr::V3 ce = pushedTripleRun(x, kCamEuler);
        camera_.rot = sr::euler(ce.x, ce.y, ce.z);
        // 0x402d0c: the object (lit, every face through 0x414e5c) and the light at the origin
        object_.loadSro(d.file(x.cstr(kSroName)), int32_t(movRegImm(x, kSroScale)), mat);
        object_.setRaster(sr::rasterAt(movRegImm(x, kRaster)));
        scene_.add(object_);
        object_.flags |= sr::kLit;
        objPos_ = pushedTriple(x, kObjPos);
        sr::V3 oe = pushedTripleRun(x, kObjEuler);
        rot0_ = sr::euler(oe.x, oe.y, oe.z);
        light_.range = movMemImm(x, kLightRange);
        light_.intensity = movMemImm(x, kLightIntensity);
        light_.flags |= sr::kLight;
        scene_.add(light_);
        maskX_ = pushedFloat(x, kMaskX);
        maskY_ = pushedFloat(x, kMaskY);
        textX_ = pushedFloat(x, kTextX);
        textY_ = pushedFloat(x, kTextY);
        textOn_ = c.entryRefresh(kTextOn);
        c.picture("logo_inside");   // load it in the init
    }

    void render(Ctx& c, double r) override {
        // the tick 0x402e30 has run k times for the frame shown at refresh r: R = R0 S^k
        double k = std::max(0.0, c.refreshes(r));
        sr::M3 R;
        if (sr::fidelity().exact) {
            R = rot0_;
            for (int i = 0, n = int(std::floor(k)); i < n; i++) R = R * step_;
        } else {
            R = rot0_ * sr::axisAngle(axis_, angle_ * k);
        }
        // 0x402e44: the positions as ints, the matrices copied
        bool ex = sr::fidelity().exact;
        auto pos = [&](sr::V3 p) { return ex ? sr::V3(std::trunc(p.x), std::trunc(p.y), std::trunc(p.z)) : p; };
        camera_.pos = pos(camPos_);
        object_.pos = pos(objPos_);
        object_.rot = R;
        eng_.render(c, scene_, pal_, 0);                  // 0x41127c(scene, 0), 0x40c6ac
        eng_.drawBitmap(c, mask_, maskX_, maskY_, pal_);  // 0x415df0, type 4
        if (r >= textOn_) {                               // type 3: the title's pixels where they are not 0
            const Picture& p = c.picture("logo_inside");
            c.drawPicture(p, {.x = textX_, .y = textY_, .w = textW_, .h = textH_});
        }
    }

private:
    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object camera_, object_, light_;
    sr::M3 step_, rot0_;
    sr::V3 axis_, camPos_, objPos_;
    double angle_ = 0;
    int pal_ = 0;
    sr::Bitmap mask_;
    float maskX_ = 0, maskY_ = 0, textX_ = 0, textY_ = 0, textW_ = 0, textH_ = 0;
    double textOn_ = 0;
};

}  // namespace

REGISTER_SCENE(LogoScene, "logo");

// The teal part, ticks 6018-7741 (scene "teal"): the finished "present the Paper intro" page peels off over a camera
// flying through the teal canyon (6018-6146), then a grey paper plane with pencil outlines flies in from the right
// (first visible at tick 6537), passes the camera and leads it through the canyon; fade to black from tick 7631.
//
// Ports (paper/re/notes-planes.md §4, §6; notes-init.md §5.12, §8.10):
//   0x687a  the part: plane object 0x187d5 (base colour 0x20, Objects3D::tealPlane), palette 0x158e(0x681a) (teal
//           ramp at 128..159), oscillators 0x2756(300) (OscSet::teal), the canyon texture 0x68a6 (planes_canyon),
//           28 path steps of pre-roll with unadvanced phases, then per frame the canyon (0x2f24 + 0x3095) and either
//           the peel 0x2b6c (while [0x1eaa9] < 0x2bc) or the plane (0x3fb3 + 0x43df, mode 0)
//   0x6434 / 0x6439  per tick: peel progress 0x2b20 (core peelProgress), two oscillator advances 0x26d0, path step
//   0x64f1  flight path: yaw = 5/8 N1, pitch = N2/64 + 25, roll = N3 + 0x180 (N = 0x67b9 noise of oscillators 0, 8,
//           16: core noise4Exact), R = Rz(roll) Rx(pitch) Ry(yaw) Rx(256) (16.16), position += 1.5 * forward (22.10),
//           a 28-entry history: the camera is the path 28 ticks back (its z = N2 >> 11), the plane = the current path
//           shifted by 2k (camera right - camera forward), k = 900 falling by 2 per tick once the music counter
//           passes 0x2f0
//   0x3fb3  the object's light vector m^T (light - pos) summed in 32 bits: it overflows when the path is far from the
//           origin and the plane's shading then inverts (ticks 6575, 6603-6623, 7071-7113, most of 7404-7708).
//           Kept: the wrapped vector is handed to the renderer (Object3D::lightOverride, from imulTVecShr16).
//
// Time base: everything is per tick (exact integer state after each IRQ, precomputed for the whole part); a frame
// at a fractional tick interpolates between the two ticks (positions linearly, matrices by interpMatrix, the light
// overflow from the interpolated integer pose, so its flips fall where the original's do). The original sometimes
// drew the plane one tick ahead of the canyon (IRQ between 0x2f24 and 0x3fb3); here one state per frame.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/core2d.h"
#include "paper/obj3d.h"
#include "paper/pencil.h"
#include "paper/render3d.h"
#include "scenes/planes_canyon.h"

using namespace paper;

namespace {

constexpr int kFirst = 6018;   // first IRQ of the part (first path step and phase advance)
constexpr int kLast = 7741;    // states are kept up to here (the part ends at 7741)
constexpr int kHistory = 28;   // 0x64f1 history ring

struct PathState {
    int32_t x = 0, y = 0;  // [0x6479] position, 22.10 world units
    IMat3 R;               // [0x6455] rows = right, down, forward
};

struct TealTick {
    PathState now;      // the path after this tick's step: the plane's attitude (R^T) and position
    PathState cam;      // the history entry read by this step: the camera (V, position >> 10)
    int32_t camZ = 0;   // N2 >> 11
    IVec3 planePos;     // plane position (world)
};

// 0x687a's pre-roll and then 0x6439 per tick (two 0x26d0 advances, one 0x64f1 step): the state after the IRQ of
// every tick kFirst..kLast.
std::vector<TealTick> simulatePath(const OscSet& osc, int32_t k, const Ctx& c) {
    PathState pos;
    pos.R.m[0] = pos.R.m[4] = pos.R.m[8] = 0;
    std::vector<PathState> hist(kHistory, pos);  // the ring is zeroed at init
    int idx = 0;
    // one 0x64f1 step; the phases of `noiseTick` (kFirst - 1: not advanced yet)
    auto step = [&](int noiseTick) {
        const int n1 = noise4Exact(osc, 0, noiseTick), n2 = noise4Exact(osc, 8, noiseTick),
                  n3 = noise4Exact(osc, 16, noiseTick);
        const int yaw = (n1 * 5) >> 3, pitch = (n2 >> 6) + 25, roll = n3 + 0x180;
        pos.R = imatMul(irotZ(roll), imatMul(irotX(pitch), imatMul(irotY(yaw), irotX(256))));
        pos.x = int32_t(uint32_t(pos.x) + uint32_t((pos.R(2, 0) * 3) >> 1));  // forward * 1.5 (z ignored)
        pos.y = int32_t(uint32_t(pos.y) + uint32_t((pos.R(2, 1) * 3) >> 1));
        TealTick t;
        t.cam = hist[size_t(idx)];
        t.camZ = n2 >> 11;
        hist[size_t(idx)] = pos;
        idx = (idx + 1) % kHistory;
        t.now = pos;
        const IMat3& V = t.cam.R;
        const int32_t p[3] = {pos.x >> 10, pos.y >> 10, 0};
        for (int a = 0; a < 3; a++) t.planePos[a] = p[a] + ((V(0, a) * k) >> 15) - ((V(2, a) * k) >> 15);
        return t;
    };
    for (int i = 0; i < kHistory; i++) step(kFirst - 1);  // pre-roll (counter 704: k stays)
    std::vector<TealTick> out;
    for (int tick = kFirst; tick <= kLast; tick++) {
        out.push_back(step(tick));
        if (c.counter(tick) > 0x2f0 && k != 0) k -= 2;  // after the plane position, as 0x64f1
    }
    return out;
}

class TealPart : public Scene {
public:
    void init(Ctx& c) override {
        const PaperExe& exe = *c.exe;
        lib_ = &c.shared<Objects3D>();
        plane_ = lib_->tealPlane;
        pal_ = ShadePalette::fromExe(exe);
        pal_.setEntries6(128, exe.at(0x681a, 96), 32);
        ramp_ = planes::tealRamp(exe);
        H_ = planes::tealPlaneDistance(exe);
        osc_ = OscSet::teal(exe);
        ticks_ = simulatePath(osc_, exe.s32(0x4843), c);  // k: [0x4843] = 900
        c.shared<planes::Canyon>();
        c.shared<Renderer3D>();
        c.shared<Peel>();
    }

    void render(Ctx& c, double tick) override {
        tick = std::clamp(tick, double(kFirst), double(kLast) - 1e-6);
        const double p = peelProgress(tick, kPeelTealFirstTick);
        const planes::CanyonCamera cam = camera(tick);
        if (p < 700.0) {  // 0x69a5: the peel branch, no plane
            auto canyon = c.temp();
            canyon->bind();
            c.shared<planes::Canyon>().draw(c, cam, ramp_);
            Target& page = logoPage(c);
            c.out->bind();
            c.shared<Peel>().draw(c, page.tex, canyon->tex, p);
            return;
        }
        c.shared<planes::Canyon>().draw(c, cam, ramp_);
        drawPlane(c, tick, cam);
    }

private:
    const TealTick& at(int tick) const { return ticks_[size_t(std::clamp(tick, kFirst, kLast) - kFirst)]; }

    planes::CanyonCamera camera(double tick) const {
        const int n = int(std::floor(tick));
        const double f = tick - n;
        const TealTick &a = at(n), &b = at(n + 1);
        planes::CanyonCamera cam;
        cam.rot = planes::interpMatrix(toMat3d(a.cam.R), toMat3d(b.cam.R), f);
        const Vec3d pa(a.cam.x >> 10, a.cam.y >> 10, a.camZ), pb(b.cam.x >> 10, b.cam.y >> 10, b.camZ);
        cam.pos = lerp(pa, pb, f);
        cam.H = H_;
        return cam;
    }

    // ---- the plane (0x3fb3 + 0x43df, mode 0)
    void drawPlane(Ctx& c, double tick, const planes::CanyonCamera& cam) {
        const int n = int(std::floor(tick));
        const double f = tick - n;
        const TealTick &a = at(n), &b = at(n + 1);
        const IMat3 ma = itranspose(a.now.R), mb = itranspose(b.now.R);
        plane_.rot = planes::interpMatrix(toMat3d(ma), toMat3d(mb), f);
        plane_.pos = lerp(Vec3d(a.planePos), Vec3d(b.planePos), f);
        // the light vector as 0x3fb3 computes it from the (interpolated) integer pose, 32-bit wrap included
        IMat3 m;
        for (int i = 0; i < 9; i++) m.m[i] = int32_t(std::lround(ma.m[i] + (mb.m[i] - ma.m[i]) * f));
        const IVec3 ip(int32_t(std::lround(plane_.pos.x)), int32_t(std::lround(plane_.pos.y)),
                       int32_t(std::lround(plane_.pos.z)));
        const IVec3& L = lib_->ilight;
        plane_.lightOverride = true;
        plane_.light = Vec3d(imulTVecShr16(m, IVec3(L.x - ip.x, L.y - ip.y, L.z - ip.z)));

        Target& frame = *c.out;
        View3D v;
        v.cam = Camera3D{cam.rot, cam.pos};
        v.light = lib_->light;
        v.mode = Mode3D::Colour;
        v.palette = &pal_;
        PencilInk ink(c, frame);
        Renderer3D& r = c.shared<Renderer3D>();
        r.begin(c, frame, v);
        r.group({&plane_});
        r.end(&ink);
        ink.resolve(c, frame, &pal_);
    }

    // The old page of the peel: the logo part's finished page (its tick 6017), rendered once per frame size.
    Target& logoPage(Ctx& c) {
        if (page_.w != c.w || page_.h != c.h || !pageValid_) {
            page_.destroy();
            page_.create(c.w, c.h, false, GL_RGBA16F);
            c.renderPart("logo", kFirst - 1, page_);
            pageValid_ = true;
        }
        return page_;
    }

    Objects3D* lib_ = nullptr;
    Object3D plane_;
    ShadePalette pal_;
    std::array<Vec3, 32> ramp_{};
    double H_ = 1000;
    OscSet osc_;
    std::vector<TealTick> ticks_;
    Target page_;
    bool pageValid_ = false;
};

}  // namespace

REGISTER_SCENE(TealPart, "teal");

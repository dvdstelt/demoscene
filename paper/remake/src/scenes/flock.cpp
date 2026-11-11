// The flock part, ticks 8818-10968 (scene "flock"): 32 grey paper planes with pencil outlines swarm around a
// wandering point in the blue canyon; the camera sways near the origin, turns towards the flock and rolls.
//
// Ports (paper/re/notes-planes.md §5, §6; NOTES.md "the skipped paper-plane flock", "the flock's speed"):
//   0x388b  the part: camera V = 0xb1f(0x100, 0), position (-10000, 0, 0), H = 2000.0, 0x33d0, one extra 0x342f,
//           then per drawn frame 0x342f, the canyon (0x2f24 + 0x3095) and 0x3618 (+ 0x37af), until the fade ends
//   0x33d0  set-up from the random generator (RngPos::kFlock): target = rnd3(63), 32 planes (object 0x1878b, base
//           colour 0x1e: Objects3D::flockPlane) at rnd3(0x3ff) << (11, 11, 10) (20.12 world)
//   0x342f  the target's random walk (velocity += rnd3(1), 1 in 64 a new rnd3(7), clamp 7, bounce at 450/450/14) and
//           each plane's steering: yaw / pitch = -((rnd(63) << 16 + local offset to the target) >> 20), speed +=
//           12 + rnd(3); the camera sway angles
//   0x3618  turn (m = m * 0xb1f(pitch, yaw)), move along the nose (column 2) by the speed, the flock centre with the
//           original's bug: all three camera coordinates are subtracted from its x (kept, it shapes where the camera
//           looks); the camera position from the sway sines
//   0x37af  the camera turns towards that centre (gain 1/128 angle unit per world unit) and rolls 2 units per frame
//   0x3fb3 / 0x43df  the planes, drawn with the camera of the previous frame (core Renderer3D, one depth group)
//
// Time base: the original has no fixed step: everything above advances once per drawn frame. The look chosen is the
// original at 34 fps (the reference capture). Here the flock is stepped at exactly 34 Hz from the part start (frame
// 1 at tick 8818, as in the capture): sigma(tick) = 1 + (tick - 8818) * 34 / 70.0018 frames, with the same random
// sequence, so the flight is the capture's; the capture's own schedule (full.ticks column 5: frame n is shown from
// the tick where the running count minus the greetings' last two frames reaches n) wanders around this line by
// +-8 frames because its frame rate varied with the number of visible faces (33-37 fps). Frames are interpolated
// for display (positions linearly, matrices by interpMatrix). Frame n shows the planes after n moves and the canyon
// and planes through the camera after n - 1 camera updates; frame 1 uses the start camera for the canyon too (the
// original drew it with the teal part's last float camera, at fade level 600 of 0x8000: black).
//
// The skip bug (the original skips the part on fast machines, the loop test 0x397e sees the fade level still 0):
// not reproduced, the part always plays (fade from the core: level 600 after tick 8818, as in the capture).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/common.h"
#include "paper/obj3d.h"
#include "paper/pencil.h"
#include "paper/render3d.h"
#include "scenes/planes_canyon.h"

using namespace paper;

namespace {

constexpr int kFirst = 8818;               // the part's first tick: frame 1 is drawn in it
constexpr int kEnd = 10968;
constexpr double kStepsPerTick = 34.0 / 70.0018;   // the flock at 34 fps
constexpr int kPlanes = 32;

inline int32_t wrap(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }

struct PlaneRec {
    IMat3 m;               // object matrix (model -> world), 16.16
    int32_t p[3] = {0, 0, 0};  // position, 20.12 world
    IVec3 wp;              // world position = p >> 12 (object +0x48)
    int32_t yaw = 0, pitch = 0, speed = 0;
};

struct FlockFrame {
    std::array<IMat3, kPlanes> m;
    std::array<IVec3, kPlanes> pos;
    IMat3 V;     // camera of this frame (after the previous frame's 0x37af)
    IVec3 cam;
};

// The simulation of 0x33d0 / 0x342f / 0x3618 / 0x37af, exact (integer, random generator included).
class FlockSim {
public:
    FlockSim(const PaperExe& exe) : rng_(Rng::at(exe, RngPos::kFlock)) {
        // 0x33d0
        const IVec3 t = rnd3(0x3f);
        tgt_ = t;
        for (PlaneRec& q : rec_) {
            const IVec3 r = rnd3(0x3ff);
            q.p[0] = r.x * 2048;
            q.p[1] = r.y * 2048;
            q.p[2] = r.z * 1024;
        }
        for (int k = 0; k < 3; k++) vel_[k] = exe.s32(0x33a8 + 4 * uint32_t(k));
        for (int k = 0; k < 3; k++) sway_[k] = exe.s32(0x33c4 + 4 * uint32_t(k));  // 0x33c4, 0x33c8, 0x33cc
        V_ = irotXY(0x100, 0);
        cam_ = IVec3(-10000, 0, 0);
        steer();  // 0x391c: the extra call before the loop
    }

    // One loop iteration: 0x342f, then 0x3618 + 0x37af. Returns the frame it draws.
    FlockFrame frame() {
        steer();
        FlockFrame f;
        f.V = V_;
        f.cam = cam_;
        move();
        for (int n = 0; n < kPlanes; n++) {
            f.m[size_t(n)] = rec_[size_t(n)].m;
            f.pos[size_t(n)] = rec_[size_t(n)].wp;
        }
        return f;
    }

private:
    int32_t rnd(uint32_t mask) { return rng_.signedMasked(mask); }  // 0x377f
    IVec3 rnd3(uint32_t mask) {                                      // 0x3793: (r3, r1, r2)
        const int32_t a = rnd(mask), b = rnd(mask), c = rnd(mask);
        return IVec3(c, a, b);
    }

    void steer() {  // 0x342f
        const IVec3 dv = rnd3(1);
        IVec3 v(vel_[0] + dv.x, vel_[1] + dv.y, vel_[2] + dv.z);
        if ((rng_.next() & 63) == 0) v = rnd3(7);
        static constexpr int32_t kLim[3] = {450, 450, 14};
        for (int k = 0; k < 3; k++) {
            v[k] = std::clamp(v[k], -7, 7);
            int32_t n = tgt_[k] + v[k];
            if (n < -kLim[k] || n > kLim[k]) {
                v[k] = -v[k];
                n += v[k];
            }
            tgt_[k] = n;
            vel_[k] = v[k];
        }
        const IVec3 T(tgt_.x * 16, tgt_.y * 16, tgt_.z * 16);
        for (PlaneRec& q : rec_) {
            const int32_t d[3] = {(T.x - q.wp.x) >> 4, (T.y - q.wp.y) >> 4, (T.z - q.wp.z) >> 4};
            const IMat3& m = q.m;
            const int32_t jy = rnd(0x3f);
            q.yaw = -(wrap(int64_t(jy) * 65536 + int64_t(m(0, 0)) * d[0] + int64_t(m(1, 0)) * d[1] +
                           int64_t(m(2, 0)) * d[2]) >> 20);
            const int32_t jp = rnd(0x3f);
            q.pitch = -(wrap(int64_t(jp) * 65536 + int64_t(m(0, 1)) * d[0] + int64_t(m(1, 1)) * d[1] +
                             int64_t(m(2, 1)) * d[2]) >> 20);
            q.speed += rnd(3) + 12;
        }
        sway_[0] += 0x64;
        sway_[2] += 0x8f;
        sway_[1] -= 0x48;
    }

    void move() {  // 0x3618 + 0x37af
        int32_t S[3] = {0, 0, 0};
        for (PlaneRec& q : rec_) {
            q.m = imatMul(q.m, irotXY(q.pitch, q.yaw));
            const int32_t sp = q.speed;
            q.speed = 0;
            for (int k = 0; k < 3; k++) {
                q.p[k] = wrap(int64_t(q.p[k]) + int64_t(q.m(k, 2)) * sp);
                q.wp[k] = q.p[k] >> 12;
                S[k] += q.wp[k];
            }
        }
        for (int k = 0; k < 3; k++) S[k] >>= 5;
        S[0] -= cam_.x + cam_.y + cam_.z;  // the bug at 0x36f8..0x3713
        cam_ = IVec3(sinTab(sway_[0] >> 4) >> 5, sinTab(sway_[2] >> 4) >> 6, sinTab(sway_[1] >> 4) >> 6);
        const int32_t c[3] = {S[0] >> 8, S[1] >> 8, S[2] >> 8};
        const int32_t b = wrap(int64_t(V_(0, 0)) * c[0] + int64_t(V_(0, 1)) * c[1] + int64_t(V_(0, 2)) * c[2]) >> 15;
        const int32_t a = wrap(int64_t(V_(1, 0)) * c[0] + int64_t(V_(1, 1)) * c[1] + int64_t(V_(1, 2)) * c[2]) >> 15;
        V_ = imatMul(irotZ(2), imatMul(irotXY(a, b), V_));
    }

    Rng rng_;
    IVec3 tgt_;
    int32_t vel_[3] = {0, 0, 0};
    int32_t sway_[3] = {0, 0, 0};
    std::array<PlaneRec, kPlanes> rec_;
    IMat3 V_;
    IVec3 cam_;
};

class FlockPart : public Scene {
public:
    void init(Ctx& c) override {
        const PaperExe& exe = *c.exe;
        lib_ = &c.shared<Objects3D>();
        for (Object3D& o : plane_) o = lib_->flockPlane;
        pal_ = ShadePalette::fromExe(exe);
        ramp_ = planes::blueRamp(exe);
        FlockSim sim(exe);
        const int n = int(std::ceil(sigma(kEnd))) + 2;
        frames_.reserve(size_t(n) + 1);
        frames_.emplace_back();  // frame 0: not drawn
        for (int i = 1; i <= n; i++) frames_.push_back(sim.frame());
        c.shared<planes::Canyon>();
        c.shared<Renderer3D>();
    }

    void render(Ctx& c, double tick) override {
        const double s = std::clamp(sigma(tick), 1.0, double(frames_.size() - 2));
        const int n = int(std::floor(s));
        const double f = s - n;
        const FlockFrame &a = frames_[size_t(n)], &b = frames_[size_t(n + 1)];
        planes::CanyonCamera cam;
        cam.rot = planes::interpMatrix(toMat3d(a.V), toMat3d(b.V), f);
        cam.pos = lerp(Vec3d(a.cam), Vec3d(b.cam), f);
        cam.H = 2000.0;  // 0x388b: [0x23e4] = 2000.0
        c.shared<planes::Canyon>().draw(c, cam, ramp_);

        std::vector<const Object3D*> objs;
        for (int i = 0; i < kPlanes; i++) {
            Object3D& o = plane_[size_t(i)];
            o.rot = planes::interpMatrix(toMat3d(a.m[size_t(i)]), toMat3d(b.m[size_t(i)]), f);
            o.pos = lerp(Vec3d(a.pos[size_t(i)]), Vec3d(b.pos[size_t(i)]), f);
            objs.push_back(&o);
        }
        Target& frame = *c.out;
        View3D v;
        v.cam = Camera3D{cam.rot, cam.pos};
        v.light = lib_->light;
        v.mode = Mode3D::Colour;
        v.palette = &pal_;
        PencilInk ink(c, frame);
        Renderer3D& r = c.shared<Renderer3D>();
        r.begin(c, frame, v);
        r.group(objs);
        r.end(&ink);
        ink.resolve(c, frame, &pal_);
    }

private:
    static double sigma(double tick) { return 1.0 + (tick - kFirst) * kStepsPerTick; }

    Objects3D* lib_ = nullptr;
    std::array<Object3D, kPlanes> plane_;
    ShadePalette pal_;
    std::array<Vec3, 32> ramp_{};
    std::vector<FlockFrame> frames_;
};

}  // namespace

REGISTER_SCENE(FlockPart, "flock");

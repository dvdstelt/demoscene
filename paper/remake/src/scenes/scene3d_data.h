// Data and per-tick state of the coloured 3D scene part ("scene3d" and "peel1", src/scenes/scene3d.cpp), all read
// from PAPER.EXE or computed as the original does (paper/re/notes-scene3d.md, notes-sketch.md §9, notes-init.md §5.11):
//   0x7554 / 0x74e8  camera path: Hermite spline through the keys at 0x466f (CameraPath)
//   0x7207 / 0x7244 / 0x726d  the noise: 5 travelling sines, its 0x800-entry table as a continuous function (Waves)
//   0x3b69 / 0x3c3e  the banner cloth surface warp(u, v) + sag (bannerPoint)
//   0x5979  the "Statix + Vic (acme)" drawing: 106 records at 0x18a2f -> 674 points + pen-ups (handwritingPoints)
//   0x5e7f / 0x5e1b / 0x6133  the sketch's stroke table 0x5073 resolved to per-face reveal masks, and the stroke that
//           is still in progress when the sketch ends (SketchReveal)
// Header-only (the part is built from scene3d.cpp alone, see PAPER_PARTS).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "exe.h"
#include "paper/common.h"
#include "paper/math3d.h"
#include "paper/obj3d.h"
#include "paper/render3d.h"

namespace scene3d {

using paper::IVec3;
using paper::Vec3d;

// ---------------------------------------------------------------- time line (IRQ0 ticks, PARTS.md)
constexpr int kPartFirst = 3186;        // 0x5eec renders the finished sketch (screen still shows the sketch)
constexpr int kTransitionFirst = 3187;  // first 0x6199 callback
constexpr int kSceneFirst = 3442;       // 0x620b loop: coloured scene, banner, handwriting
constexpr int kPeelPartFirst = 5480;    // 0x5623 builds the logo page (screen frozen until 5483)
constexpr int kPeelFirst = 5483;        // first peel frame (paper::kPeelSceneFirstTick)
constexpr int kPeelEnd = 5611;

// ---------------------------------------------------------------- 0x2ad0 (integer, as the original)
inline std::vector<int32_t> curve2ad0(int32_t a, int32_t m, int32_t c, int n) {
    const int32_t step = (65536 / n) >> 7;
    const int32_t B = int32_t(uint32_t(4 * m) - uint32_t(c) - uint32_t(3 * a));
    const int32_t C = int32_t(uint32_t(2 * m) - uint32_t(2 * a) - uint32_t(B));
    std::vector<int32_t> out(size_t(n) + 1);
    for (int k = 0, t = 0; k <= n; k++, t += step) {
        const int32_t ctt = int32_t(uint32_t(int32_t(uint32_t(C) * uint32_t(t))) * uint32_t(t));
        const int32_t bt = int32_t(uint32_t(B) * uint32_t(t));
        out[size_t(k)] = ((int32_t(uint32_t(bt) + uint32_t(ctt >> 8)) >> 8) + 2 * a) >> 1;
    }
    return out;
}

// ---------------------------------------------------------------- camera path (0x7554 at tick 1079, 0x74e8)
// Per channel c (yaw, pitch, x, y, z) and segment j: v(s) = a s^3 + b s^2 + c1 s + d, s = u/256 (the original's
// 65536 * H(u/256) in 16.16). Angles in 1/1024 turn; positions: world = 64 * v (the keys quantised to 64 units).
// The coefficients are the original's integers; evaluated here at continuous s without the final floors.
class CameraPath {
public:
    static constexpr uint32_t kKeys = 0x466f;  // 5 int32 per key
    static constexpr int kSegments = 23;       // [0x4827] = 0x17

    struct Pose {
        double yaw = 0, pitch = 0;
        Vec3d pos;
    };

    explicit CameraPath(const PaperExe& exe) {
        auto K = [&](int j, int c) { return exe.s32(kKeys + 20 * uint32_t(j) + 4 * uint32_t(c)); };
        for (int c = 0; c < 5; c++) {
            auto Q = [&](int32_t v) { return c >= 2 ? v >> 16 : v; };
            int32_t tg[kSegments + 1];
            tg[0] = tg[kSegments] = 0;  // not halved: twice the Catmull-Rom tangent
            for (int j = 1; j < kSegments; j++) tg[j] = Q(int32_t(uint32_t(K(j + 1, c)) - uint32_t(K(j - 1, c))));
            for (int j = 0; j < kSegments; j++) {
                const int32_t d = Q(K(j, c)), e = Q(K(j + 1, c)), c1 = tg[j];
                const int32_t a = int32_t(2u * uint32_t(d) - 2u * uint32_t(e) + uint32_t(tg[j]) + uint32_t(tg[j + 1]));
                coef_[j][c][0] = a;
                coef_[j][c][1] = int32_t(uint32_t(e) - uint32_t(a) - uint32_t(c1) - uint32_t(d));
                coef_[j][c][2] = c1;
                coef_[j][c][3] = d;
            }
        }
    }

    // Continuous camera at path parameter p (8.8: segment p / 256).
    Pose at(double p) const {
        p = std::max(p, 0.0);
        const int seg = std::min(int(p / 256.0), kSegments - 1);
        const double s = p / 256.0 - seg;
        double v[5];
        for (int c = 0; c < 5; c++) {
            const int32_t* k = coef_[seg][c];
            v[c] = ((k[0] * s + k[1]) * s + k[2]) * s + k[3];
        }
        return {v[0], v[1], Vec3d(64 * v[2], 64 * v[3], 64 * v[4])};
    }
    // The original's integer camera at p = 0 (key 0): yaw, pitch and the camera position [0x1ea9d] = v >> 10.
    void exactKey0(int& yaw, int& pitch, IVec3& pos) const {
        yaw = coef_[0][0][3];
        pitch = coef_[0][1][3];
        pos = IVec3(coef_[0][2][3] * 64, coef_[0][3][3] * 64, coef_[0][4][3] * 64);
    }

private:
    int32_t coef_[kSegments][5][4];
};

// ---------------------------------------------------------------- the noise (0x7207 / 0x7244 / 0x726d)
// N[i] = clamp(((sum_k sinT[((freq_k (0x800 - i) + phase_k) >> 5) & 1023] amp_k) >> 18) + 128, 0, 250) >> 1.
// Only phase mod 2^15 matters. Continuous form at real i and real phases: Nc(i) = clamp(128 + sum amp/4 sin, 0,
// 250) / 2; the original's table is floor(Nc).
struct Waves {
    double phase[5] = {0, 0, 0, 0, 0};  // mod 32768
    int speed[5] = {0, 0, 0, 0, 0}, freq[5] = {0, 0, 0, 0, 0}, amp[5] = {0, 0, 0, 0, 0};

    // 0x7207 with the random generator at `rngPos` calls.
    static Waves init(const PaperExe& exe, uint64_t rngPos) {
        paper::Rng r = paper::Rng::at(exe, rngPos);
        Waves w;
        for (int k = 0; k < 5; k++) {
            w.phase[k] = double(r.next() & 0x7fff);
            w.speed[k] = int32_t(r.next() & 0xff) - 0x80;
            w.freq[k] = int32_t(r.next() & 0x3ff) - 0x200;
            w.amp[k] = int32_t(r.next() & 0xff);
        }
        return w;
    }
    // After `ticks` (real) calls of 0x7244 with shift cl: phase += speed << cl per tick.
    Waves advanced(double ticks, int shift) const {
        Waves w = *this;
        for (int k = 0; k < 5; k++) {
            const double p = phase[k] + double(speed[k] << shift) * ticks;
            w.phase[k] = p - 32768.0 * std::floor(p / 32768.0);
        }
        return w;
    }
    // Continuous table value Nc at a real index (the original's byte is floor(Nc)).
    double Nc(double i) const {
        const double x = 2048.0 - i;
        double s = 0;
        for (int k = 0; k < 5; k++) {
            double a = (freq[k] * x + phase[k]) / 32768.0;
            a -= std::floor(a);
            s += amp[k] * 0.25 * std::sin(2.0 * 3.14159265358979323846 * a);
        }
        return std::clamp(128.0 + s, 0.0, 250.0) * 0.5;
    }
    // The table value with the mean of the original's floor (for geometry driven by it).
    double N(double i) const { return Nc(i) - 0.5; }
};

// ---------------------------------------------------------------- banner surface (0x3b69 + 0x3c3e)
// Banner parameters u = 0..2048 along, v = 0..950 down the cloth. `w` null: the set-up shape (noise table all 0).
// sag: 0x2ad0(0, 90, 0, 17) at column u / 128, i.e. 90 s (2 - s) with s = u * 30 / 32768 (the truncated step makes
// the right end sag 21 units: kept). The handwriting uses the warp without the sag (0x7418: kept, see scene3d.cpp).
inline Vec3d bannerPoint(const Waves* w, double u, double v, bool sag) {
    const double i = 3.0 * u / 32.0, j = v / 16.0 + 128.0;
    auto N = [&](double k) { return w ? w->N(k) : 0.0; };
    const double X = 0x7bc + u + (N(i) - 64) * v / 2048.0 + (N(j) - 64) * v / 2048.0;
    double Y = -0x123 + v + (N(i + 256) - 64) * v / 2048.0 + (N(j + 256) - 64) * v / 2048.0;
    const double Z = -3 + u / 512.0 + (N(i + 512) - 80) * v / 256.0 + (N(j + 512) - 80) * v / 256.0;
    if (sag) {
        const double s = u * 30.0 / 32768.0;
        Y += 90.0 * s * (2.0 - s);
    }
    return Vec3d(X, Y, Z);
}

// ---------------------------------------------------------------- the drawing on the banner (0x5979)
struct HwPoint {
    int32_t u = 0, v = 0;  // banner parameters; (0, 0) = pen up (after every record) / end (twice)
};
inline std::vector<HwPoint> handwritingPoints(const PaperExe& exe) {
    constexpr uint32_t kData = 0x18a2f;
    constexpr int kRecords = 0x6a;
    auto U = [](int32_t w) { return int32_t((int64_t(w) * 0x2d000) >> 16) + 300; };
    auto V = [](int32_t w) { return int32_t((int64_t(w) * 0x23000) >> 16) + 100; };
    std::vector<HwPoint> out;
    uint32_t p = kData;
    for (int r = 0; r < kRecords; r++) {
        const int type = exe.u8(p);
        int n = exe.u8(p + 1) >> 2;
        if (n == 0) n = 1;
        p += 2;
        auto w = [&](int k) { return int32_t(exe.u16(p + 2 * uint32_t(k))); };
        if (type == 2) {  // circle: cx, cy, r
            n = std::max(n, 3);
            const int32_t step = paper::recipTab(n) >> 6;
            for (int32_t a = 0;; a += step) {
                const int32_t px = int32_t(uint32_t(paper::sinTab(a) * w(2)) + (uint32_t(w(0)) << 16)) >> 16;
                const int32_t py = int32_t(uint32_t(paper::cosTab(a) * w(2)) + (uint32_t(w(1)) << 16)) >> 16;
                out.push_back({U(px), V(py)});
                if (a >= 0x400) break;
            }
            p += 6;
        } else {  // 1: line (curve through the midpoint), other: parabola through 3 points
            int32_t x[3], y[3];
            if (type == 1) {
                x[0] = w(0); y[0] = w(1); x[2] = w(2); y[2] = w(3);
                x[1] = (x[0] + x[2]) >> 1; y[1] = (y[0] + y[2]) >> 1;
                p += 8;
            } else {
                for (int k = 0; k < 3; k++) { x[k] = w(2 * k); y[k] = w(2 * k + 1); }
                p += 12;
            }
            const auto xs = curve2ad0(U(x[0]), U(x[1]), U(x[2]), n);
            const auto ys = curve2ad0(V(y[0]), V(y[1]), V(y[2]), n);
            for (int k = 0; k <= n; k++) out.push_back({xs[size_t(k)], ys[size_t(k)]});
        }
        out.push_back({0, 0});
    }
    out.push_back({0, 0});
    return out;
}

// ---------------------------------------------------------------- the finished sketch (0x5e7f, 0x6133, 0x6267)
// The stroke table at 0x5073 (275 x {u32 ordinal, u8 flags}: edge bit 0..2, bit 3 reversed) numbers the faces in the
// painter's order of a reference render with the static camera at 0x5de8, groups {0,1} {2..7} {9,10}. When the
// sketch ends (tick 3186) strokes 0..273 are complete (n_done = stroke_idx + travel - 1 = 274) and stroke 274 is in
// progress: its face draws the line from the pen to the stroke's start (0x43df mode 2), in screen pixels.
struct SketchReveal {
    static constexpr uint32_t kTable = 0x5073;
    static constexpr int kStrokes = 0x113;
    static constexpr uint32_t kStaticCamera = 0x5de8;  // yaw, pitch, x, y, z (16.16 positions)

    std::vector<uint8_t> mask[paper::Objects3D::kSlots];  // per object slot, per face: edge reveal bits
    int curSlot = -1, curFace = -1;                       // the face of the stroke in progress
    int penX = 0, penY = 0, startX = 0, startY = 0;       // pen (end of the sketch) and the stroke's start, pixels

    SketchReveal(const PaperExe& exe, const paper::Objects3D& lib, int camYaw, int camPitch, const IVec3& camPos) {
        using namespace paper;
        for (int s = 0; s < Objects3D::kSlots; s++) mask[s].assign(lib.scene[s].face.size(), 0);
        // 0x5e7f: number the visible faces of the reference render
        ExactCamera ref;
        ref.rot = irotYawPitch(exe.s32(kStaticCamera), exe.s32(kStaticCamera + 4));
        ref.pos = IVec3(exe.s32(kStaticCamera + 8) >> 10, exe.s32(kStaticCamera + 12) >> 10,
                        exe.s32(kStaticCamera + 16) >> 10);
        ref.light = lib.ilight;
        ref.mode = 2;
        std::vector<std::pair<int, int>> ordinal;  // (slot, face)
        const std::vector<std::vector<int>> groups = {{0, 1}, {2, 3, 4, 5, 6, 7}, {9, 10}};
        for (const auto& g : groups) {
            std::vector<ExactFrame> frames(g.size());
            std::vector<const ExactFrame*> ptrs;
            for (size_t k = 0; k < g.size(); k++) {
                transformExact(lib.scene[g[k]], IMat3(), IVec3(), ref, frames[k]);
                ptrs.push_back(&frames[k]);
            }
            for (const auto& of : paintersOrder(ptrs)) ordinal.push_back({g[size_t(of.first)], of.second});
        }
        auto entry = [&](int k, int& slot, int& face, int& flags) {
            const uint32_t ord = exe.u32(kTable + 5 * uint32_t(k));
            flags = exe.u8(kTable + 5 * uint32_t(k) + 4);
            if (ord >= ordinal.size()) return false;
            slot = ordinal[ord].first;
            face = ordinal[ord].second;
            return true;
        };
        const int done = kStrokes - 1;
        for (int k = 0; k < done; k++) {
            int slot, face, flags;
            if (entry(k, slot, face, flags)) mask[slot][size_t(face)] |= uint8_t(flags & 7);
        }
        // the stroke in progress (0x6267): pen down from A to B, one step done when the sketch part ends
        int slot, face, flags;
        if (!entry(done, slot, face, flags)) return;
        const Object3D& o = lib.scene[slot];
        const int k = (flags & 1) ? 0 : ((flags & 2) ? 1 : 2);
        int a = o.face[size_t(face)].v[k], b = o.face[size_t(face)].v[(k + 1) % 3];
        if (flags & 8) std::swap(a, b);
        ExactCamera cam;
        cam.rot = irotYawPitch(camYaw, camPitch);
        cam.pos = camPos;
        cam.light = lib.ilight;
        cam.mode = 2;
        ExactFrame f;
        transformExact(o, IMat3(), IVec3(), cam, f);
        startX = f.sx[size_t(a)] >> 5;
        startY = f.sy[size_t(a)] >> 5;
        const int ex = f.sx[size_t(b)] >> 5, ey = f.sy[size_t(b)] >> 5;
        const double L = std::sqrt(double((startX - ex) * (startX - ex) + (startY - ey) * (startY - ey)));
        const int n = std::max(0, int(std::nearbyint(std::sqrt(L) * double(1.73f))) - 3);
        const int step = std::clamp(n - 1, 0, 1);
        const int32_t r = n > 0 ? recipTab(n) : 0;
        penX = startX + ((int32_t(uint32_t(ex - startX) * uint32_t(step)) * r) >> 16);
        penY = startY + ((int32_t(uint32_t(ey - startY) * uint32_t(step)) * r) >> 16);
        curSlot = slot;
        curFace = face;
    }
};

}  // namespace scene3d

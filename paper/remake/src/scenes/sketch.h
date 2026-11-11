// The pencil sketch's stroke plan: which edge of which face each of the 275 strokes draws, when, and where the red
// pencil is at any (fractional) tick of the sketch part. Header-only, so that other parts (scene3d starts from the
// finished sketch) can use it without linking sketch.cpp: `sketch::sketchRevealAt(ctx, tick)`.
//
// Ports (paper/re/notes-sketch.md §9, §10, Appendix A):
//   0x5e7f / 0x5e1b  the stroke table 0x5073 (275 x {u32 ordinal, u8 flags}) resolved to faces: the ordinal is the
//                    index of a face in the painter's order of a reference render with the static camera at 0x5de8
//                    (groups {0,1} {2..7} {9,10}, buckets far -> near)
//   0x61ee           the pencil moves in (x +1, z -2, a_cnt -1 per tick)
//   0x6267           the stroke sequencer: pen-up move to the next stroke start (2 n ticks), then the stroke itself
//                    (n ticks), n = max(0, round(1.73 sqrt(L)) - 3) for the integer length L in 320x200 pixels
//   0x6133           reveal masks: the completed strokes' edges in face +0x20
//
// Ticks: the remake's convention (BRIEF.md: tick T shows the state after the IRQ that starts tick T). The notes'
// sequencer ticks ("stroke 0 starts at tick 1334") count the IRQ at the end of a tick, so they are one less: here
// the first 0x61ee step is tick 1182, the first 0x6267 step 1305, stroke 0 starts at 1335 and the 275th stroke at
// 3186, the first tick of scene3d (checked against the capture: the pencil first moves in record 1182).
//
// Remake: the sequencer runs on the original's integer endpoints (its step counts decide the timing); a second,
// float copy of it places the pen on the exact projections (pen = start + k/n (end - start) after step k, as the
// original's integer pen without its truncation). Between ticks the pen is interpolated linearly, and the stroke
// being drawn is the part of its final line from the stroke start to start + (tick - t0)/n of the edge, so while the
// pencil is down the line end is exactly under the tip and grows continuously; at integer ticks it is the
// original's. The original's pen stops one step short of the stroke end (at k = n-1, then lifts and starts the
// pen-up move from there): during that last tick the line still completes to the end while the pencil lifts off.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "paper/common.h"
#include "paper/obj3d.h"
#include "paper/render3d.h"

namespace sketch {

constexpr int kPartStart = 1079;  // white paper, the pencil lying above it
constexpr int kMoveFirst = 1182;  // first 0x61ee step (the pencil moves in)
constexpr int kMoveTicks = 123;   // [0x55d2] counts down from 123
constexpr int kDrawFirst = 1305;  // first 0x6267 step
constexpr int kPartEnd = 3186;    // the 275th stroke starts: the sketch is finished (scene3d)
constexpr int kStrokes = 0x113;

constexpr uint32_t kStrokeTable = 0x5073;  // 275 x 5 bytes
constexpr uint32_t kCamera = 0x5de8;       // static camera: yaw, pitch, x, y, z (positions 16.16 << 10)
constexpr uint32_t kSpeed = 0x483f;        // float 1.73

struct Stroke {
    int obj = 0, face = 0;   // object slot (paper::Objects3D::Slot) and face index
    int edge = 0;            // edge k: v[k] -> v[(k+1) % 3] of the face
    uint8_t mask = 0;        // reveal bits (table flags & 7)
    bool reversed = false;   // drawn from v[(k+1) % 3] to v[k] (flag 8)
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the sequencer's integer endpoints (vertex sx, sy >> 5), from -> to
    paper::Vec2f a, b;       // exact projections (VGA), from -> to
    int n = 0;               // draw ticks
    int start = 0;           // tick whose IRQ starts it (pen down at a); complete at start + n
};

// The original's per-tick state of the pencil (after the IRQ of the tick).
struct PenTick {
    int x = 100, y = 120;    // [0x55d6], [0x55da] (integer 320x200 pixels)
    int z = -120;            // [0x55de]
    int acnt = kMoveTicks;   // [0x55d2]
    bool travel = false;     // [0x506b]: pen lifted (drawn 50 units higher)
    int strokeIdx = 0;       // [0x1ea95]
    int height() const { return z - (travel ? 50 : 0); }
};

// The sketch at a fractional tick.
struct Reveal {
    int done = 0;            // completed strokes (0..275): their edges are drawn in full
    int partial = -1;        // the stroke being drawn, -1 when none
    float frac = 0;          // ... drawn from its start a to a + frac (b - a)
    paper::Vec2f pen{100.5f, 120.5f};  // the pencil tip over the paper (VGA, continuous)
    double z = -120;         // world z of the tip (0 = on the paper, negative = towards the viewer)
    double acnt = kMoveTicks;
};

class SketchPlan {
public:
    explicit SketchPlan(Ctx& c) : SketchPlan(*c.exe, c.shared<paper::Objects3D>()) {}

    SketchPlan(const PaperExe& exe, const paper::Objects3D& lib) {
        resolveStrokes(exe, lib);
        projectStrokes(exe, lib);
        runSequencer(exe);
    }

    const std::vector<Stroke>& strokes() const { return strokes_; }
    // This part's scene camera: the spline key 0 at t = 0 (yaw 20, pitch -28, position (-704, 576, -1600)).
    const paper::Camera3D& camera() const { return cam_; }
    int yaw() const { return yaw_; }
    int pitch() const { return pitch_; }

    // The original's integer state after the IRQ of `tick` (clamped to the part).
    PenTick tickState(int tick) const {
        if (tick < kDrawFirst) {
            PenTick p;
            const int k = std::clamp(tick - (kMoveFirst - 1), 0, kMoveTicks);
            p.x = 100 + k;
            p.z = -120 - 2 * k;
            p.acnt = kMoveTicks - k;
            return p;
        }
        return ticks_[size_t(std::min(tick - kDrawFirst, int(ticks_.size()) - 1))];
    }
    // The pen at the end of `tick` on the exact projections (VGA): phases 1 and 2 the centre of the original's
    // pixel (x, y), phase 3 the float sequencer.
    paper::Vec2f penAt(int tick) const {
        if (tick < kDrawFirst) {
            const PenTick p = tickState(tick);
            return paper::Vec2f{p.x + 0.5f, p.y + 0.5f};
        }
        return pens_[size_t(std::min(tick - kDrawFirst, int(pens_.size()) - 1))];
    }

    Reveal at(double tick) const {
        Reveal r;
        const int t0 = int(std::floor(tick));
        const double f = tick - t0;
        const PenTick p0 = tickState(t0), p1 = tickState(t0 + 1);
        r.z = p0.height() + (p1.height() - p0.height()) * f;
        r.acnt = p0.acnt + (p1.acnt - p0.acnt) * f;
        const paper::Vec2f q0 = penAt(t0), q1 = penAt(t0 + 1);
        r.pen = paper::Vec2f{float(q0.x + (q1.x - q0.x) * f), float(q0.y + (q1.y - q0.y) * f)};
        if (tick < kDrawFirst - 1) return r;
        // strokes complete at start + n, in order
        int lo = 0, hi = int(strokes_.size());
        while (lo < hi) {
            const int m = (lo + hi) / 2;
            if (strokes_[size_t(m)].start + strokes_[size_t(m)].n <= tick) lo = m + 1;
            else hi = m;
        }
        r.done = lo;
        if (lo < int(strokes_.size()) && strokes_[size_t(lo)].start <= tick && strokes_[size_t(lo)].n > 0) {
            r.partial = lo;
            r.frac = float((tick - strokes_[size_t(lo)].start) / strokes_[size_t(lo)].n);
        }
        return r;
    }

    // 0x6133: the reveal masks (Face3D::sketch) of the first `done` strokes; scene = the Objects3D slots (0..10 used).
    void applyMasks(paper::Object3D* scene, int done) const {
        for (int s = 0; s <= paper::Objects3D::Flower; s++)
            for (paper::Face3D& f : scene[s].face) f.sketch = 0;
        for (int k = 0; k < std::min(done, int(strokes_.size())); k++) {
            const Stroke& s = strokes_[size_t(k)];
            scene[s.obj].face[size_t(s.face)].sketch |= s.mask;
        }
    }

    // The stroke in progress as a line of its face (hidden as the face's own edges are), in the face's edge order
    // so that it is exactly the part of the finished edge's line (same jitter and darkness).
    bool partialLine(const Reveal& r, const paper::Object3D* scene, paper::FaceLine& out) const {
        if (r.partial < 0 || r.frac <= 0) return false;
        const Stroke& s = strokes_[size_t(r.partial)];
        out.obj = &scene[s.obj];
        out.face = s.face;
        out.line = s.reversed ? paper::PencilLine(s.b, s.a, 1.0f - r.frac, 1.0f) : paper::PencilLine(s.a, s.b, 0.0f, r.frac);
        return true;
    }

    // Development: the resolved list in the format of notes-sketch.md Appendix A (notes' ticks = start - 1), and
    // whether the float renderer draws the stroke's edge (face visible, edge a silhouette or crease) in this view.
    void dump(const char* path) const {
        FILE* f = std::fopen(path, "w");
        if (!f) return;
        for (size_t k = 0; k < strokes_.size(); k++) {
            const Stroke& s = strokes_[k];
            const paper::Face3D& F = faceOf(s);
            const int va = F.v[s.edge], vb = F.v[(s.edge + 1) % 3];
            const paper::Object3D& o = lib_->scene[s.obj];
            const bool vis = visible(o, s.face);
            const bool drawn = vis && ((F.outline >> s.edge) & 1 || !visible(o, F.nb[s.edge]));
            std::fprintf(f, "%3d %2d/%-3d %3d>%-3d (%3d,%3d)>(%3d,%3d) n%d t%d%s\n", int(k), s.obj, s.face,
                         s.reversed ? vb : va, s.reversed ? va : vb, s.x0, s.y0, s.x1, s.y1, s.n, s.start - 1,
                         drawn ? "" : " NOT-DRAWN");
        }
        std::fclose(f);
    }
    // Renderer3D's visibility test (all vertices beyond z = 400, clockwise on screen)
    bool visible(const paper::Object3D& o, int face) const {
        paper::Vec2f p[3];
        for (int i = 0; i < 3; i++) {
            double z;
            if (!cam_.project(o.rot * o.vert[o.face[size_t(face)].v[i]] + o.pos, p[i], &z)) return false;
        }
        return double(p[2].y - p[0].y) * double(p[1].x - p[0].x) > double(p[2].x - p[0].x) * double(p[1].y - p[0].y);
    }

private:
    const paper::Face3D& faceOf(const Stroke& s) const { return lib_->scene[s.obj].face[size_t(s.face)]; }

    // 0x5e7f: number the visible faces in the painter's order of the static camera and resolve the table.
    void resolveStrokes(const PaperExe& exe, const paper::Objects3D& lib) {
        using namespace paper;
        lib_ = &lib;
        ExactCamera nc;
        nc.rot = irotYawPitch(exe.s32(kCamera), exe.s32(kCamera + 4));
        nc.pos = IVec3(exe.s32(kCamera + 8) >> 10, exe.s32(kCamera + 12) >> 10, exe.s32(kCamera + 16) >> 10);
        nc.light = lib.ilight;
        nc.mode = 2;
        std::vector<std::pair<int, int>> order;  // painter index -> (slot, face)
        const std::vector<std::vector<int>> groups = {{0, 1}, {2, 3, 4, 5, 6, 7}, {9, 10}};
        for (const auto& g : groups) {
            std::vector<ExactFrame> ef(g.size());
            std::vector<const ExactFrame*> ptr;
            for (size_t i = 0; i < g.size(); i++) {
                transformExact(lib.scene[g[i]], IMat3(), IVec3(), nc, ef[i]);
                ptr.push_back(&ef[i]);
            }
            for (const auto& of : paintersOrder(ptr)) order.push_back({g[size_t(of.first)], of.second});
        }
        strokes_.resize(kStrokes);
        for (int k = 0; k < kStrokes; k++) {
            const uint32_t e = kStrokeTable + 5 * uint32_t(k);
            const uint32_t ord = exe.u32(e);
            const uint8_t fl = exe.u8(e + 4);
            Stroke& s = strokes_[size_t(k)];
            if (ord < order.size()) { s.obj = order[ord].first; s.face = order[ord].second; }
            s.mask = fl & 7;
            s.edge = (fl & 4) ? 2 : (fl & 2) ? 1 : 0;  // 0x6267 tests bit 0, 1, 2 in turn: the last one set wins
            s.reversed = (fl & 8) != 0;
        }
    }

    // Integer endpoints with this part's camera (0x3fb3: vertex sx, sy in 1/32 px, >> 5) and exact projections
    // (as Renderer3D computes them, so a partial stroke hashes its jitter like the finished edge).
    void projectStrokes(const PaperExe& exe, const paper::Objects3D& lib) {
        using namespace paper;
        yaw_ = exe.s32(kCamera);
        pitch_ = exe.s32(kCamera + 4);
        // 0x7554 key 0 = the static camera; 0x74e8 at t = 0: positions (K >> 16) << 16, CAMPOS = that >> 10
        const IVec3 pos((exe.s32(kCamera + 8) >> 16) * 64, (exe.s32(kCamera + 12) >> 16) * 64,
                        (exe.s32(kCamera + 16) >> 16) * 64);
        cam_ = Camera3D::yawPitch(yaw_, pitch_, Vec3d(pos));
        ExactCamera pc;
        pc.rot = irotYawPitch(yaw_, pitch_);
        pc.pos = pos;
        pc.light = lib.ilight;
        pc.mode = 2;
        ExactFrame ef[Objects3D::kSlots];
        bool done[Objects3D::kSlots] = {};
        for (Stroke& s : strokes_) {
            const Object3D& o = lib.scene[s.obj];
            if (!done[s.obj]) { transformExact(o, IMat3(), IVec3(), pc, ef[s.obj]); done[s.obj] = true; }
            const Face3D& F = o.face[size_t(s.face)];
            int va = F.v[s.edge], vb = F.v[(s.edge + 1) % 3];
            if (s.reversed) std::swap(va, vb);
            s.x0 = ef[s.obj].sx[size_t(va)] >> 5;
            s.y0 = ef[s.obj].sy[size_t(va)] >> 5;
            s.x1 = ef[s.obj].sx[size_t(vb)] >> 5;
            s.y1 = ef[s.obj].sy[size_t(vb)] >> 5;
            const Mat3d C = cam_.rot * o.rot;
            const Vec3d T = cam_.rot * (o.pos - cam_.pos);
            auto proj = [&](int v) {
                const Vec3d p = C * o.vert[size_t(v)] + T;
                return Vec2f{float(160.0 + Renderer3D::kFocal * p.x / p.z), float(100.0 + Renderer3D::kFocal * p.y / p.z)};
            };
            s.a = proj(va);
            s.b = proj(vb);
        }
    }

    // 0x6267 per tick from the end of 0x61ee (pen (223, 120), z -366) until the 275th stroke starts.
    void runSequencer(const PaperExe& exe) {
        const double speed = double(exe.f32(kSpeed));
        auto steps = [&](int sx, int sy, int ex, int ey, bool travel) {
            const double d2 = double(sx - ex) * double(sx - ex) + double(sy - ey) * double(sy - ey);
            const int n = std::max(0, int(std::nearbyint(std::sqrt(std::sqrt(d2)) * speed)) - 3);
            return travel ? 2 * n : n;
        };
        PenTick p = tickState(kDrawFirst - 1);
        int step = 0, nsteps = 0, sx = 0, sy = 0, ex = 0, ey = 0;
        paper::Vec2f pen = penAt(kDrawFirst - 1), s0 = pen, s1 = pen;  // the float copy
        for (int t = kDrawFirst; t <= kPartEnd + 1; t++) {
            if (p.z < 0) p.z = std::min(p.z + 10, 0);
            for (;;) {
                if (step < nsteps) {
                    const int32_t r = paper::recipTab(nsteps);
                    p.x = sx + (((ex - sx) * step * r) >> 16);
                    p.y = sy + (((ey - sy) * step * r) >> 16);
                    const float u = float(step) / float(nsteps);
                    pen = paper::Vec2f{s0.x + (s1.x - s0.x) * u, s0.y + (s1.y - s0.y) * u};
                    step++;
                    break;
                }
                if (p.strokeIdx >= kStrokes) break;
                Stroke& s = strokes_[size_t(p.strokeIdx)];
                step = 0;
                p.travel = !p.travel;
                if (p.travel) {  // pen up: from where it is to the stroke start
                    sx = p.x; sy = p.y; ex = s.x0; ey = s.y0;
                    s0 = pen;
                    s1 = s.a;
                } else {         // pen down: the stroke
                    sx = s.x0; sy = s.y0; ex = s.x1; ey = s.y1;
                    s0 = s.a;
                    s1 = s.b;
                    s.start = t;
                    p.strokeIdx++;
                }
                nsteps = steps(sx, sy, ex, ey, p.travel);
                if (!p.travel) s.n = nsteps;
            }
            ticks_.push_back(p);
            pens_.push_back(pen);
        }
    }

    const paper::Objects3D* lib_ = nullptr;
    std::vector<Stroke> strokes_;
    std::vector<PenTick> ticks_;     // from kDrawFirst
    std::vector<paper::Vec2f> pens_;  // from kDrawFirst
    paper::Camera3D cam_;
    int yaw_ = 0, pitch_ = 0;
};

// The plan of a context (built once) and the sketch at a tick.
inline const SketchPlan& sketchPlan(Ctx& c) { return c.shared<SketchPlan>(); }
inline Reveal sketchRevealAt(Ctx& c, double tick) { return sketchPlan(c).at(tick); }

}  // namespace sketch

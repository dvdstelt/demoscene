// The particle parts' state on the CPU, stepped exactly as the original steps it, once per drawn frame, at the
// capture's frame times (clone/re/notes-particles.md sections 3.1, 3.2, 4..7): part 1 0x405527 (spawn 0x4051d5,
// bookkeeping 0x405442), part 6 0x406677 (set-up 0x4065c5, spawn 0x40628f, 0x406615), part 3 0x405c45 (spawn
// 0x40590c, 0x405b60), the removal 0x404b0e, the generator 0x420491 (cmc::Random), main's dt 0x400f76.
//
// What the parts draw depends on the frame step: the spawns per frame (deaths are proportional to dt, and part 1's
// emitter advances once per spawn), the truncating per-frame growth and cut of the target counts. So the frames are
// stepped at the capture's timer values (particle_frames.h), with dt computed as main does, the events applied at
// the frame the capture applied them, and the float variables stored with truncation toward zero as the x87 does
// (RC = truncate): the particle array, the counts and the random numbers are then those of the capture
// (tools/particlecheck.cpp drives the original's 8-bit pipeline with this state: every captured frame of the four
// runs comes out pixel for pixel). No GL here: particles.h draws.
//
// One sequence of random numbers runs through parts 1, 6, 3 and part 1's second run, and the emitter phase and the
// blur phase carry over from part to part, so Chain (built once per context) runs all four in order and keeps the
// state each one starts from.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "clone/core.h"
#include "exe.h"
#include "scenes/particle_frames.h"

namespace particles {

// x stored as float32 with rounding toward zero (fstp dword under RC = truncate): the nearest float, one step
// towards zero if that was rounded away from zero (one less in the magnitude bits; f is not 0 then).
inline float f32t(double x) {
    float f = float(x);
    uint32_t b;
    std::memcpy(&b, &f, 4);
    b -= uint32_t(std::fabs(double(f)) > std::fabs(x));
    std::memcpy(&f, &b, 4);
    return f;
}

// ------------------------------------------------------------------------------------------------ frame times
// The frames of one run of a part: frame 0 is the state the part starts from (at the previous part's last frame),
// frames 1..last the capture's, a few more after it at the last step (the remake interpolates up to the next part).
struct Frames {
    std::vector<double> time;   // music time each frame is drawn with (timer / PIT clock)
    std::vector<float> dt;      // main's dt for the frame (0x400f76: (float)(timer * C - t70_prev)); dt[0] unused
    int last = 0;               // the capture's last frame

    int count() const { return int(time.size()); }
    // The latest frame drawn at music time t (0 before the first).
    int at(double t) const {
        auto it = std::upper_bound(time.begin() + 1, time.end(), t);
        return int(it - time.begin()) - 1;
    }
    // The frame an event at music time e applies to: main reads the sync table before each frame with the position
    // of the timer that frame is drawn with (notes-core 5.3), so the first frame with time >= e. 0 when the event
    // lies outside the run.
    int eventFrame(double e) const {
        if (!(e > time[0]) || e > time[size_t(last)]) return 0;
        return int(std::lower_bound(time.begin() + 1, time.begin() + last + 1, e) - time.begin());
    }
};

// timers: the capture's frames (PIT clocks); origin: the timer t70_prev comes from (the previous part's last frame);
// c70: main's t70 per PIT clock (the double 0x41a128).
inline Frames makeFrames(int64_t origin, int64_t first, const uint16_t* steps, int nSteps, double c70) {
    std::vector<int64_t> timer{origin, first};
    for (int i = 0; i < nSteps; i++) timer.push_back(timer.back() + steps[i]);
    Frames f;
    f.last = int(timer.size()) - 1;
    for (int i = 0; i < 4; i++) timer.push_back(timer.back() + steps[nSteps - 1]);  // beyond the capture
    double prev = double((long double)timer[0] * c70);
    for (size_t k = 0; k < timer.size(); k++) {
        long double t = (long double)timer[k] * c70;
        f.time.push_back(double(timer[k]) / kPitHz);
        f.dt.push_back(k ? f32t(double(t - prev)) : 0.0f);
        prev = double(t);
    }
    // part 1's first run: frame 1 has the negative timer of before the music start; keep the times increasing
    if (f.time[1] < f.time[0]) f.time[0] = f.time[1] - double(steps[0]) / kPitHz;
    return f;
}

// ------------------------------------------------------------------------------------------------ particles
struct Particle {
    float x, y, z;     // position (+0x00)
    float vx, vy, vz;  // velocity (+0x24; part 6 leaves it)
    float life;        // +0x30, in units of 0.6 per t70 unit
};
constexpr int kCapacity = 10000;

// The state the three parts share: the array 0xed8450 with its live count [0x4224f8] and target [0x4224fc], the
// generator 0x420491, the emitter phase [0xf5939c] and the radial blur's phase [0xeaf40c].
struct Pool {
    std::vector<Particle> p = std::vector<Particle>(kCapacity);
    int count = 0, target = 0;
    cmc::Random rng{1u};
    uint64_t draws = 0;  // random numbers drawn (cmc::kRandomAtPart6 etc. give the capture's)
    float emitter = 0, phase = 0;

    int rnd() { draws++; return rng.next(); }

    // 0x405442 / 0x405b60 / 0x406615: life -= 0.6 dt (and pos += 0.3 v dt in parts 1 and 3); a particle whose life
    // fell below 0 is replaced by the last one (0x404b0e). Returns how many were removed.
    int ageMove(float dt, bool move) {
        int n = count;
        double age = double(dt) * 0.6;
        for (int i = 0; i < n; i++) {
            Particle& q = p[size_t(i)];
            q.life = f32t(double(q.life) - age);
            if (move) {
                q.x = f32t(double(q.x) + double(q.vx) * dt * 0.3);
                q.y = f32t(double(q.y) + double(q.vy) * dt * 0.3);
                q.z = f32t(double(q.z) + double(q.vz) * dt * 0.3);
            }
        }
        int removed = 0;
        for (int i = 0; i < n;) {
            if (p[size_t(i)].life < 0) {
                p[size_t(i)] = p[size_t(--n)];
                removed++;
            } else {
                i++;
            }
        }
        count = n;
        return removed;
    }
};

// ------------------------------------------------------------------------------------------------ part 1
struct Swarm {
    float a = 0, b = 0, c = 0;  // [0xf593a0], a4, a8: the rotation
    float fish = 0;             // [0xf593ac]: the projection's fisheye phase
    int mode = -1;              // [0x422500]: -1 nothing grows, 0 the target grows (103), 1 blur on (101, 104)
    float flash = 0;            // [0x422504]: event 105 sets 255, -2.3 dt per frame
    bool flashOn = false;       // this frame filled buffer 1 with the flash level

    void event(Pool& s, int ev) {
        switch (ev) {
            case 103: mode = 0; break;
            case 101: s.count = s.target = 2000; mode = 1; break;
            case 102: s.count = s.target = 0; break;
            case 104: s.count = 0; s.target = 2000; mode = 1; break;
            case 105: s.count = s.target = 0; flash = 255.0f; break;
            default: break;                           // 106, 107, 111: the title and credits (drawn by the scene)
        }
    }
    // 0x4051d5: a point of the Lissajous path at the emitter phase, jittered by +-24, slow random velocity
    void spawn(Pool& s, float dt) {
        s.emitter = f32t(double(s.emitter) + double(dt) * 1.2);
        if (s.count >= s.target) return;
        Particle& q = s.p[size_t(s.count)];
        double ph = s.emitter;
        q.x = f32t(std::sin(ph * 0.002) * 200.0 + (s.rnd() % 48 - 24));
        q.y = f32t(std::sin(ph * 0.0042) * 200.0 + (s.rnd() % 48 - 24));
        q.z = f32t(std::sin(ph * 0.0067) * 200.0 + (s.rnd() % 48 - 24));
        q.vx = f32t((s.rnd() * 5.439512619669277e-05 + -1.0) * 0.03);
        q.vy = f32t((s.rnd() * 5.439512619669277e-05 + -1.0) * 0.03);
        q.vz = f32t((s.rnd() * 5.439512619669277e-05 + -1.0) * 0.03);
        q.life = float(s.rnd() % 696);
        s.count++;
    }
    void step(Pool& s, float dt) {
        fish = f32t(double(dt) * 0.02 + fish);
        if (mode == 0 && s.target < 5000) s.target = std::min(5000, int(double(dt) * 10.0 + s.target));
        a = f32t(a + double(dt) * 0.001);
        b = f32t(b + double(dt) * 0.002);
        c = f32t(c + double(dt) * 0.003);
        int n = s.target - s.count;
        n += s.ageMove(dt, true);
        for (int i = 0; i < n; i++) spawn(s, dt);
        if (mode == 1) s.phase = f32t(s.phase + double(dt) * 0.04);  // 0x403df7 advances it on every call
        flashOn = flash != 0;
        if (flashOn) flash = std::max(0.0f, f32t(flash - double(dt) * 2.3));
    }
    // The values at clock T inside a frame (from the frame's values at Tk: they advance linearly).
    float blurLevel(double da) const { return float(0.8 + 0.19 * std::sin(13.0 * (a + 0.001 * da))); }
    double fisheyeK(double da) const { return 0.007 + 0.0065 * std::sin(fish + 0.02 * da); }
};

// ------------------------------------------------------------------------------------------------ part 6
struct Ring {
    float a = 0, b = 0, c = 0;  // [0xf5d3d0], d4, d8
    float g = 0, g8 = 0, g13 = 0;  // [0xf5d3c4], c8, cc: the breathing phases
    float rate = 0.03f;         // [0x422520]
    bool end = false;           // [0x42252c] (event 601)

    void event(Pool&, int ev) { if (ev == 601) end = true; }
    // 0x40628f: a random point of the lattice (spacing 8) on the surface of the cube [-80, 80]^3
    void spawn(Pool& s) {
        if (s.count >= s.target) return;
        Particle& q = s.p[size_t(s.count)];
        int face = s.rnd() % 6, u = s.rnd() % 20 - 10, v = s.rnd() % 20 - 10;
        int X = 0, Y = 0, Z = 0;
        switch (face) {
            case 0: X = -10; Y = u; Z = v; break;
            case 1: X = 10; Y = u; Z = v; break;
            case 2: X = u; Y = -10; Z = v; break;
            case 3: X = u; Y = 10; Z = v; break;
            case 4: X = v; Y = u; Z = -10; break;
            default: X = v; Y = u; Z = 10; break;
        }
        q.x = 8.0f * float(X);
        q.y = 8.0f * float(Y);
        q.z = 8.0f * float(Z);
        q.life = float(s.rnd() % 96);
        s.count++;
    }
    void setup(Pool& s) {  // 0x4065c5
        s.count = 0;
        s.target = 3000;
        for (int i = 0; i < 3000; i++) spawn(s);
    }
    void step(Pool& s, float dt) {
        a = f32t(a + double(dt) * 0.002);
        b = f32t(b + double(dt) * 0.003);
        c = f32t(c + double(dt) * 0.004);
        if (s.target != 0 && end) {
            s.target = std::max(0, int(s.target - double(dt) * 35.0));
            s.count = s.target;
        }
        int n = s.ageMove(dt, false);
        for (int i = 0; i < n; i++) spawn(s);
        double gx = double(dt) * rate + g;  // the register keeps the unrounded sum for the other two
        g = f32t(gx);
        g8 = f32t(gx * 0.8);
        g13 = f32t(gx * 1.3);
        s.phase = f32t(s.phase + double(dt) * 0.04);
    }
    float blurLevel(double da) const { return float(0.85 + 0.14 * std::sin(6.0 * (a + 0.002 * da))); }
};

// ------------------------------------------------------------------------------------------------ part 3
struct Dots {
    float a = 0, b = 0, c = 0;  // [0xf5b3b8], bc, c0
    bool end = false;           // [0x422518] (event 301)

    void event(Pool&, int ev) { if (ev == 301) end = true; }
    // 0x40590c: a random point in the cube [-48, 47]^3, speed 3 along a random direction
    void spawn(Pool& s, float dt) {
        s.emitter = f32t(double(s.emitter) + double(dt) * 0.2);
        if (s.count >= s.target) return;
        Particle& q = s.p[size_t(s.count)];
        double A = s.rnd() % 200 - 100, B = s.rnd() % 200 - 100, C = s.rnd() % 200 - 100;
        float len = f32t(std::sqrt(A * A + B * B + C * C));
        q.x = float(s.rnd() % 96 - 48);
        q.y = float(s.rnd() % 96 - 48);
        q.z = float(s.rnd() % 96 - 48);
        double inv = len != 0 ? 1.0 / len : HUGE_VAL;
        q.vx = f32t(A * inv * 3.0);
        q.vy = f32t(B * inv * 3.0);
        q.vz = f32t(C * inv * 3.0);
        q.life = float(s.rnd() % 696);
        s.count++;
    }
    void setup(Pool& s) { s.count = 0; s.target = 0; }  // 0x405b10
    void step(Pool& s, float dt) {
        if (s.target < 5000 && !end) {
            s.target = std::min(5000, int(double(dt) * 5.0 + s.target));
        } else if (s.target != 0 && end) {
            s.target = std::max(0, int(s.target - double(dt) * 50.0));
            s.count = s.target;
        }
        a = f32t(a + double(dt) * 0.003 * 1.0051);
        b = f32t(b + double(dt) * 0.004 * 1.0051);
        c = f32t(c + double(dt) * 0.005 * 1.0051);
        int n = s.target - s.count;
        n += s.ageMove(dt, true);
        for (int i = 0; i < n; i++) spawn(s, dt);
    }
};

// ------------------------------------------------------------------------------------------------ runs
// One run of a part: its frames, the events by frame, the state it starts from, and the state at a frame (stepped
// forwards, replayed from the start after a backwards jump).
template <class Part>
struct Run {
    Frames frames;
    std::vector<std::pair<int, int>> events;  // (frame, event), by frame
    Pool pool0;
    Part part0;
    // the state at the frame last stepped to
    Pool pool;
    Part part;
    TickStepper stepper{1};

    int frame() const { return stepper.done; }
    void restart() { stepper.done = INT_MIN; }
    bool eventAt(int k, int ev) const {
        for (const auto& [f, e] : events)
            if (f == k && e == ev) return true;
        return false;
    }
    // Steps to frame k: forwards from the current frame, or replayed from frame 0 after a backwards jump.
    void stepTo(int k) {
        k = std::clamp(k, 0, frames.count() - 1);
        stepper.advance(double(k), [&] { pool = pool0; part = part0; }, [&](int j) {
            for (const auto& [f, ev] : events)
                if (f == j) part.event(pool, ev);
            part.step(pool, frames.dt[size_t(j)]);
        });
    }
    void addEvents(const Ctx& c, std::initializer_list<int> evs) {
        for (const SyncEntry& e : c.syncTable())
            for (int ev : evs)
                if (e.event == ev)
                    if (int f = frames.eventFrame(e.time)) events.push_back({f, ev});
        std::stable_sort(events.begin(), events.end(), [](auto& x, auto& y) { return x.first < y.first; });
    }
    // Fraction of the way from frame k to frame k + 1 at music time t (0..1).
    double fraction(int k, double t) const {
        if (k + 1 >= frames.count()) return 0.0;
        double a = frames.time[size_t(k)], b = frames.time[size_t(k + 1)];
        return std::clamp((t - a) / (b - a), 0.0, 1.0);
    }
};

// All four runs in the capture's order, from the image's state: part 1, part 6, part 3, part 1 again. Built once per
// context (c.shared<particles::Chain>()): about 50 ms.
struct Chain {
    Run<Swarm> swarm1, swarm2;
    Run<Ring> ring;
    Run<Dots> dots;
    uint64_t drawsAt[4] = {};  // random numbers drawn before each run (the capture: 0, 84546, 216354, 315145)

    explicit Chain(Ctx& c) {
        const CloneExe& exe = *c.exe;
        double c70 = exe.f64(0x41a128);  // main's t70 per PIT clock (1/17043)
        swarm1.frames =
            makeFrames(kSwarmFirstOrigin, kSwarmFirstFirst, kSwarmFirstSteps, int(std::size(kSwarmFirstSteps)), c70);
        ring.frames = makeFrames(kRingOrigin, kRingFirst, kRingSteps, int(std::size(kRingSteps)), c70);
        dots.frames = makeFrames(kDotsOrigin, kDotsFirst, kDotsSteps, int(std::size(kDotsSteps)), c70);
        swarm2.frames =
            makeFrames(kSwarmAgainOrigin, kSwarmAgainFirst, kSwarmAgainSteps, int(std::size(kSwarmAgainSteps)), c70);
        swarm1.addEvents(c, {103, 101, 102, 104, 105});
        swarm2.addEvents(c, {103, 101, 102, 104, 105});
        ring.addEvents(c, {601});
        dots.addEvents(c, {301});

        // part 1: the set-up 0x4053f2 (count and target 0), the image's mode, then its first frame (flip 2) with
        // the image's dt [0x420489] before any timer read
        Pool s;
        s.rng = cmc::Random(exe);
        Swarm p1;
        p1.mode = exe.s32(0x422500);
        p1.step(s, exe.f32(0x420489));
        run(swarm1, s, p1);

        Ring p6;
        p6.rate = exe.f32(0x422520);
        Pool s6 = swarm1.pool;  // generator, emitter and blur phase continue
        drawsAt[1] = s6.draws;
        p6.setup(s6);
        run(ring, s6, p6);

        Dots p3;
        Pool s3 = ring.pool;
        drawsAt[2] = s3.draws;
        p3.setup(s3);
        run(dots, s3, p3);

        Pool s1 = dots.pool;  // no set-up: event 104 restarts the count
        drawsAt[3] = s1.draws;
        run(swarm2, s1, swarm1.part);
    }

    template <class Part>
    static void run(Run<Part>& r, const Pool& s, const Part& p) {
        r.pool0 = s;
        r.part0 = p;
        r.restart();
        r.stepTo(r.frames.last);
    }
};

}  // namespace particles

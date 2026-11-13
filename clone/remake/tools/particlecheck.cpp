// Checks the particle parts' state (src/scenes/particles_sim.h) against the capture: drives the original's 8-bit
// pipeline with it (the projection 0x404b84 with its float32 truncations, the stamps 0x404772 / 0x40489c, the radial
// blur 0x403df7 as the sequential pass, the feedback 0x4045b4, the flash, part 3's 0x404621 / 0x405f8c, the palettes
// of 0x404a3e) and compares every frame of parts 1, 6, 3 and 1 again with clone/re/full.frames at the ticks it was on
// screen (outside the pictures, which the remake draws itself). Also checks the remake's formulation of the blur
// (src/scenes/particles.h): the in-place pass as Jacobi iterations of its own order (must equal the pass bit for
// bit), and the float rule the shader runs (mean difference from the pass per frame).
//
// In clone/remake (inside the toolbox):
//   g++ -std=c++20 -O2 -Isrc -Ithird_party -o particlecheck tools/particlecheck.cpp src/exe.cpp src/unpack.cpp
//       src/clone/core.cpp src/demo.cpp src/render.cpp src/gl.cpp src/pictures.cpp src/timeline.cpp -lSDL3
//   ./particlecheck ../original/CLONE.EXE ../re [--blur N]     (N: check the blur in every N-th frame)
// Result: every frame equal (3,174 compared); the Jacobi iterations equal the pass in every frame checked; the float
// rule 1.5 levels (mode 0) and 1.7 levels (mode 1) from the pass on average, at most 9 in a frame (--blur 6).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "exe.h"
#include "scenes/particles_sim.h"

using namespace particles;

namespace {
CloneExe exe;
std::string reDir;
int blurEvery = 0;

void initCtx(Ctx& c) {  // the sync table as Demo::init reads it
    c.exe = &exe;
    c.speed = exe.u8(0x406862 + 0x18);
    c.tickClocks = double(0x2d86bf / exe.u8(0x406862 + 0x19));
    for (uint32_t a = 0x420499; a + 8 <= exe.end(); a += 8) {
        int pos = exe.s32(a), ev = exe.s32(a + 4);
        c.sync.push_back({pos, ev, c.positionTime(pos)});
        if (ev == -1) break;
    }
}

// ---------------------------------------------------------------- the original's 8-bit pipeline
using Buf = std::vector<uint8_t>;
Buf buf0(64000), buf1(64000);

struct M34 { double m[12]; };
M34 matrix(float ax, float ay, float az) {  // 0x404c9f, 0x404f0f, 0x404e36, 0x404d5c with float32 stores
    M34 M{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    auto rot = [&](double a, int r0, int r1) {
        double s = f32t(std::sin(a)), c = f32t(std::cos(a));
        for (int k = 0; k < 4; k++) {
            double u = M.m[4 * r0 + k], v = M.m[4 * r1 + k];
            M.m[4 * r0 + k] = f32t(u * c - v * s);
            M.m[4 * r1 + k] = f32t(v * c + u * s);
        }
    };
    rot(ax, 1, 2);
    rot(ay, 2, 0);
    rot(az, 0, 1);
    return M;
}

void plot(const std::vector<std::array<double, 3>>& pts, const M34& M, double k, int mode) {  // 0x404b84
    static const int st[2][16] = {{21, 41, 41, 21, 41, 73, 73, 41, 41, 73, 73, 41, 21, 41, 41, 21},
                                  {3, 5, 5, 3, 5, 6, 6, 5, 5, 6, 6, 5, 3, 5, 5, 3}};
    std::vector<int> lst;
    for (auto& p : pts) {
        double t[3];
        for (int r = 0; r < 3; r++)
            t[r] = f32t(((M.m[4 * r + 1] * p[1] + M.m[4 * r] * p[0]) + M.m[4 * r + 2] * p[2]) + M.m[4 * r + 3]);
        double d = f32t(t[2] + 300.0);
        if (!(d > 0)) continue;
        double q = f32t(((t[0] * t[0] + t[1] * t[1]) * k + d) * 0.0078125), inv = f32t(1.0 / q);
        int sy = int(t[1] * inv + 100.0), sx = int(t[0] * inv * 1.2 + 160.0);
        if (sx < 0 || sx > 316 || sy < 20 || sy > 176) continue;
        lst.push_back(sy * 320 + sx);
    }
    if (lst.size() <= 2) return;
    for (size_t j = 1; j < lst.size(); j++)
        for (int i = 0; i < 16; i++) {
            int o = lst[j] + (i / 4) * 320 + i % 4;
            buf0[size_t(o)] = uint8_t(std::min(255, buf0[size_t(o)] + st[mode][i]));
        }
}

struct BlurMaps { int col[320], row[200], lev[256]; };
BlurMaps blurMaps(float level, int mode, double phase) {  // 0x403df7, 0x40407c
    BlurMaps m;
    double zoom = double(-0.025f), zy = f32t(zoom * 0.8333333333333333), s = std::sin(phase);
    for (int i = 0; i < 320; i++) m.col[i] = int((i - 160.0) * 64.0 * zoom);
    for (int j = 0; j < 200; j++) {
        double v = f32t(j - 100.0) * 64.0 * zy;
        m.row[j] = int(mode == 0 ? v : s * v);
    }
    for (int i = 0; i < 256; i++) m.lev[i] = int(i * double(level)) & 0xff;
    return m;
}
// one pixel of 0x404137 / 0x404278: get(x, y) returns what the pass reads there
template <class Get>
int blurPixel(const BlurMaps& m, int x, int y, int self, Get get) {
    int rv = m.row[y], fy = rv & 63, cv = m.col[x], fx = cv & 63, sx = x + (cv >> 6), sy = y + (rv >> 6);
    int a = get(sx, sy), b = get(sx, sy + 1), a2 = get(sx + 1, sy), b2 = get(sx + 1, sy + 1);
    int top = (a + fy * (b - a) / 64) & 255, top2 = (a2 + fy * (b2 - a2) / 64) & 255;  // 0x403f10: truncated
    return std::min(255, self + m.lev[(top + fx * (top2 - top) / 64) & 255]);
}
void radialBlur(Buf& b, float level, int mode, double phase) {  // the sequential in-place pass
    BlurMaps m = blurMaps(level, mode, phase);
    auto get = [&](int x, int y) -> int {
        return x >= 0 && x < 320 && y >= 0 && y < 200 ? b[size_t(y * 320 + x)] : 0;
    };
    for (int dir : {-1, 1})
        for (int side = 0; side < 2; side++)
            for (int y = dir < 0 ? 100 : 101; y >= 0 && y < 200; y += dir) {
                if (side == 0)
                    for (int x = 159; x >= 0; x--) b[size_t(y * 320 + x)] = uint8_t(blurPixel(m, x, y, get(x, y), get));
                else
                    for (int x = 160; x < 320; x++) b[size_t(y * 320 + x)] = uint8_t(blurPixel(m, x, y, get(x, y), get));
            }
}

// ---------------------------------------------------------------- the remake's formulation of the blur
int orderKey(int x, int y) {  // as kJacobiFS
    int pass = y <= 100 ? 0 : 1, side = x <= 159 ? 0 : 1;
    int ri = pass == 0 ? 100 - y : y - 101, ci = side == 0 ? 159 - x : x - 160;
    return ((pass * 2 + side) * 256 + ri) * 512 + ci;
}
struct BlurStats { double sum = 0, orig = 0, worst = 0; int n = 0, jacobiBad = 0, maxIter = 0; };
BlurStats blurStats[2];

void checkBlur(const Buf& in, float level, int mode, double phase) {
    Buf ex = in;
    radialBlur(ex, level, mode, phase);
    BlurMaps m = blurMaps(level, mode, phase);
    // Jacobi iterations of the integer pass: corners the pass reaches before the pixel read the iterate
    std::vector<int> X(in.begin(), in.end()), Y = X;
    int it = 0;
    for (; it < 400; it++) {
        int changed = 0;
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                int kp = orderKey(x, y);
                auto get = [&](int cx, int cy) -> int {
                    if (cx < 0 || cx >= 320 || cy < 0 || cy >= 200) return 0;
                    return orderKey(cx, cy) < kp ? X[size_t(cy * 320 + cx)] : in[size_t(cy * 320 + cx)];
                };
                int v = blurPixel(m, x, y, in[size_t(y * 320 + x)], get);
                changed += v != Y[size_t(y * 320 + x)];
                Y[size_t(y * 320 + x)] = v;
            }
        std::swap(X, Y);
        if (!changed) break;
    }
    BlurStats& S = blurStats[mode];
    for (int i = 0; i < 64000; i++) S.jacobiBad += X[size_t(i)] != ex[size_t(i)];
    S.maxIter = std::max(S.maxIter, it);
    // the float rule of kJacobiFS: continuous sample, exact bilinear weights, loss 0.5 per step, 100 iterations
    double sig = mode == 0 ? 1.0 : std::sin(phase), zy = -0.025 * 0.8333333333333333 * sig;
    std::vector<float> A(64000, 0.0f), B(64000);
    for (int iter = 0; iter < 100; iter++) {
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                int kp = orderKey(x, y);
                auto val = [&](int cx, int cy) -> double {
                    if (cx < 0 || cx >= 320 || cy < 0 || cy >= 200) return 0.0;
                    return orderKey(cx, cy) < kp ? A[size_t(cy * 320 + cx)] : in[size_t(cy * 320 + cx)];
                };
                double qx = x + (x - 160.0) * -0.025, qy = y + (y - 100.0) * zy;
                int x0 = int(std::floor(qx)), y0 = int(std::floor(qy));
                double fx = qx - x0, fy = qy - y0;
                double v = (val(x0, y0) * (1 - fx) + val(x0 + 1, y0) * fx) * (1 - fy) +
                           (val(x0, y0 + 1) * (1 - fx) + val(x0 + 1, y0 + 1) * fx) * fy;
                double e = std::max(0.0, level * v - 0.5);
                B[size_t(y * 320 + x)] = float(std::min(255.0, in[size_t(y * 320 + x)] + e));
            }
        std::swap(A, B);
    }
    double d = 0, o = 0;
    for (int i = 320 * 20; i < 320 * 180; i++) {
        d += std::fabs(A[size_t(i)] - ex[size_t(i)]);
        o += ex[size_t(i)];
    }
    d /= 51200;
    S.sum += d;
    S.orig += o / 51200;
    S.worst = std::max(S.worst, d);
    S.n++;
}

uint32_t palColour(int i, int R, int G, int B) {  // 0x404a3e
    return uint32_t(std::min(255, R * i / 92) << 16 | std::min(255, G * i / 92) << 8 | std::min(255, B * i / 92));
}

// ---------------------------------------------------------------- the capture
std::map<int64_t, int> flipOfTimer;  // full.flips: timer -> flip
std::map<int, int> tickOfFlip;       // full.ticks: flip -> first tick it is on screen
std::ifstream frames;

bool loadCapture() {
    std::ifstream fl(reDir + "/full.flips"), tk(reDir + "/full.ticks");
    frames.open(reDir + "/full.frames", std::ios::binary);
    if (!fl || !tk || !frames) return false;
    std::string line;
    while (std::getline(fl, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        int64_t flip, tick, timer;
        double time;
        ss >> flip >> tick >> time >> timer;
        flipOfTimer[timer] = int(flip);
    }
    while (std::getline(tk, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        double v[9];
        for (double& x : v) ss >> x;
        tickOfFlip.emplace(int(v[8]), int(v[0]));
    }
    return true;
}

struct Rect { int x, y, w, h; };
// pixels of the picture that differ from the capture's record of frame k (-1: not on screen at a tick end)
int compare(const std::vector<uint32_t>& img, const Frames& f, int k, const std::vector<Rect>& skip) {
    auto fi = flipOfTimer.find(std::llround(f.time[size_t(k)] * kPitHz));
    if (fi == flipOfTimer.end()) return -1;
    auto ti = tickOfFlip.find(fi->second);
    if (ti == tickOfFlip.end()) return -1;
    std::vector<uint8_t> cap(192000);
    frames.seekg(32 + int64_t(ti->second) * 192000);
    frames.read(reinterpret_cast<char*>(cap.data()), 192000);
    int n = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            bool inside = false;
            for (const Rect& r : skip) inside |= x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
            if (inside) continue;
            uint32_t v = img[size_t(y * 320 + x)];
            for (int ch = 0; ch < 3; ch++)
                if (int((v >> (16 - 8 * ch)) & 255) != cap[size_t((y * 320 + x) * 3 + ch)]) { n++; break; }
        }
    return n;
}

std::vector<std::array<double, 3>> positions(const Pool& s) {
    std::vector<std::array<double, 3>> v;
    for (int i = 0; i < s.count; i++) v.push_back({s.p[size_t(i)].x, s.p[size_t(i)].y, s.p[size_t(i)].z});
    return v;
}

struct Tally {
    int compared = 0, differ = 0;
    void add(const char* run, int k, int n) {
        if (n < 0) return;
        compared++;
        if (n) {
            differ++;
            std::printf("%s frame %d: %d pixels differ\n", run, k, n);
        }
    }
};

Tally swarmRun(const Ctx& c, Run<Swarm>& r, bool again) {
    Tally t;
    std::fill(buf0.begin(), buf0.end(), 0);
    std::fill(buf1.begin(), buf1.end(), 0);
    std::vector<uint32_t> img(64000);
    r.restart();
    for (int k = 1; k <= r.frames.last; k++) {
        r.stepTo(k);
        const Pool& s = r.pool;
        const Swarm& p = r.part;
        plot(positions(s), matrix(p.a, p.b, p.c), f32t(std::sin(double(p.fish)) * 0.0065 + 0.007), 0);
        float level = f32t(std::sin(p.a * 13.0) * 0.19 + 0.8);
        if (p.mode == 1 && blurEvery && k % blurEvery == 0) checkBlur(buf0, level, 0, s.phase);
        if (p.mode == 1) radialBlur(buf0, level, 0, s.phase);
        for (int i = 0; i < 64000; i++) buf1[size_t(i)] = uint8_t((15 * buf1[size_t(i)] + buf0[size_t(i)]) >> 4);
        if (p.flashOn) std::fill(buf1.begin(), buf1.end(), uint8_t(int(p.flash)));
        std::fill(img.begin(), img.end(), 0u);
        for (int i = 320 * 20; i < 320 * 180; i++) img[size_t(i)] = palColour(buf1[size_t(i)], 127, 95, 274);
        std::fill(buf0.begin(), buf0.end(), 0);
        double tm = r.frames.time[size_t(k)];
        std::vector<Rect> skip;
        if (tm >= c.eventTime(106) - 0.05 && tm < c.eventTime(111)) skip.push_back({141, 104, 101, 9});
        if (tm >= c.eventTime(107) - 0.05) skip.push_back({141, 74, 37, 32});
        t.add(again ? "part 1 (second run)" : "part 1", k, compare(img, r.frames, k, skip));
    }
    return t;
}

Tally ringRun(Run<Ring>& r) {
    Tally t;
    std::vector<uint32_t> img(64000);
    r.restart();
    for (int k = 1; k <= r.frames.last; k++) {
        r.stepTo(k);
        const Pool& s = r.pool;
        const Ring& p = r.part;
        std::fill(buf0.begin(), buf0.end(), 0);
        std::vector<std::array<double, 3>> pts;
        for (int i = 0; i < s.count; i++) {  // 0x40600f
            const Particle& q = s.p[size_t(i)];
            double ix = std::trunc(q.x), iy = std::trunc(q.y), iz = std::trunc(q.z);
            double k6 = 0.1666666666666666, k23 = 0.6666666666666666;  // the doubles 0x41a3e0, 0x41a3e8
            double A = std::sin(ix / 56.0 + p.g) * k6 + k23;
            double B = (std::sin(iy / 56.0 + p.g8) * k6 + k23) + A;
            double S = (std::sin(iz / 56.0 + p.g13) * k6 + k23) + B;
            pts.push_back({f32t(ix * S), f32t(iy * S), f32t(iz * S)});
        }
        plot(pts, matrix(p.a, p.b, p.c), double(0.008f), 1);
        float level = f32t(std::sin(p.a * 6.0) * 0.14 + 0.85);
        if (blurEvery && k % blurEvery == 0) checkBlur(buf0, level, 1, s.phase);
        radialBlur(buf0, level, 1, s.phase);
        std::fill(img.begin(), img.end(), 0u);
        for (int i = 320 * 20; i < 320 * 180; i++) img[size_t(i)] = palColour(buf0[size_t(i)], 95, 127, 274);
        t.add("part 6", k, compare(img, r.frames, k, {}));
    }
    return t;
}

Tally dotsRun(Run<Dots>& r) {
    Tally t;
    std::vector<uint32_t> img(64000);
    std::fill(buf0.begin(), buf0.end(), 0);
    std::fill(buf1.begin(), buf1.end(), 0);
    r.restart();
    for (int k = 1; k <= r.frames.last; k++) {
        r.stepTo(k);
        const Dots& p = r.part;
        plot(positions(r.pool), matrix(p.a, p.b, p.c), 0.0, 1);
        for (int i = 320; i < 63680; i++)  // 0x404621
            buf1[size_t(i)] =
                uint8_t((buf0[size_t(i - 320)] + buf0[size_t(i - 1)] + buf0[size_t(i + 1)] + buf0[size_t(i + 320)]) >> 2);
        std::fill(img.begin(), img.end(), 0u);
        for (int i = 320 * 20; i < 320 * 180; i++) img[size_t(i)] = palColour(buf1[size_t(i)], 127, 159, 255);
        for (int y = 0; y < 200; y++)  // 0x405f8c
            for (int x = 0; x < 320; x++) {
                int sr = (((y - 100) * 32000) >> 16) + 100, sc = (((x - 160) * 32000) >> 16) + 160;
                buf0[size_t(y * 320 + x)] = uint8_t((buf1[size_t(sr * 320 + sc)] * 3) >> 2);
            }
        t.add("part 3", k, compare(img, r.frames, k, {{162, 135, 148, 40}, {212, 87, 68, 68}}));
    }
    return t;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: particlecheck CLONE.EXE RE_DIR [--blur N]\n");
        return 2;
    }
    reDir = argv[2];
    if (argc > 4 && std::string(argv[3]) == "--blur") blurEvery = std::atoi(argv[4]);
    std::string err;
    if (!exe.load(argv[1], &err)) {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    if (!loadCapture()) {
        std::printf("no capture in %s (full.frames, full.ticks, full.flips)\n", reDir.c_str());
        return 1;
    }
    Ctx c;
    initCtx(c);
    Chain ch(c);
    std::printf("random numbers drawn before parts 6, 3, 1 again: %llu %llu %llu (capture: %llu %llu %llu)\n",
                (unsigned long long)ch.drawsAt[1], (unsigned long long)ch.drawsAt[2],
                (unsigned long long)ch.drawsAt[3],
                (unsigned long long)cmc::kRandomAtPart6, (unsigned long long)cmc::kRandomAtPart3,
                (unsigned long long)cmc::kRandomAtPart1Again);
    Tally t[4] = {swarmRun(c, ch.swarm1, false), ringRun(ch.ring), dotsRun(ch.dots), swarmRun(c, ch.swarm2, true)};
    const char* names[4] = {"part 1", "part 6", "part 3", "part 1 again"};
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        std::printf("%-13s %4d frames compared with the capture, %d differ\n", names[i], t[i].compared, t[i].differ);
        bad += t[i].differ;
    }
    for (int mode = 0; mode < 2 && blurEvery; mode++) {
        const BlurStats& S = blurStats[mode];
        if (!S.n) continue;
        std::printf("blur mode %d, %d frames: Jacobi iterations equal to the pass: %s (converged within %d); float rule "
                    "mean |difference| %.2f levels (worst frame %.2f), the pass's mean %.1f\n",
                    mode, S.n, S.jacobiBad ? "NO" : "yes", S.maxIter, S.sum / S.n, S.worst, S.orig / S.n);
        bad += S.jacobiBad;
    }
    return bad ? 1 : 0;
}

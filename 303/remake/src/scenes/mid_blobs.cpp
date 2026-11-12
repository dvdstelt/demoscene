// 2:00 - 2:30  The sub-parts of 0x2e8e4 between the face and the fish.fad part, ported from the exe
// (notes-mid2.md; the flowers and the face sheet from notes-mid1.md sections 4, 6 and 7).
//
//  typo sub-part (loop 0x2efd4, pos 0x940 - 0xa00): the faceo.dat relief sheet (textured with the 0x29ae8
//      stripe warp, phong1 shading) seen from a spring camera that dollies back from the full-screen face view
//      into a corridor of two hex planes at y = +-290. From pos 0x9a1 the "wire mode" is on: the 3D frame is
//      rendered with wire.raw / wire.fad and the face as a hidden-line wireframe, and only used to whiten one of
//      the bd2.raw pictures through wirecut.fad; the palette strobes every 2 rows and the picture toggles
//      between the first two bd2.raw text pictures every 16 rows.
//  sub-part 2 (to ~0xa30): the camera swings over to the metaball site at (-1000, 0, 60) while the face sheet
//      is still drawn; the first ball flies in.
//  sub-part 3 (0xa40 - 0xb80): up to four metaballs (sum 1/r^2, iso 0.2) arrive one by one and wobble, env
//      mapped with tex3d #0; from 0xb40 wire mode again with bd2 frame 2: black-filled faces with glowing edges.
//
// The scenes "facebox", "typo", "blobs" and "wireblobs" all run this one renderer; what is drawn is decided
// by the exe's music position, so the cue boundaries only matter at the outer ends.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../exefx.h"

namespace {
// ---------------------------------------------------------------------------------------------------------
// timing
constexpr int kPosTypo = 0x940, kPosTypoEnd = 0xa00, kPosSub3 = 0xa30, kPosWire2 = 0xb40;
// tau ([0xc1d6c], += 4 per tick since the 0x2e8e4 setup) at the typo start: fitted on the face sheet's
// screen rectangle in the capture (120.0 - 124.4 s, rms 2 px). The flowers and the blob init are timed
// from the same setup call.
constexpr double kTau0 = 4614.0;
// look-at target left over from the web part: (0, 0, 1000 + 0.01 * webCamZ); 1000 fits the capture.
constexpr double kTargetZ = 1000.0;

float exeNow(const Ctx& c) { return exeTime(c.T); }

// Palette strobe values of 0x2e534 (bright = rand() * 0.000125 + 0.5 at pos 0x9a4, 0x9a8, ... 0xa00),
// fitted to the capture's mean frame brightness 0.07 - 0.13 s after each kick.
const float kKicks[24] = {1.25f, 3.0f, 0.75f, 0.5f, 2.5f, 2.0f, 3.0f, 2.5f, 3.5f, 4.6f, 2.0f, 4.0f,
                          4.0f, 4.0f, 3.0f, 3.5f, 3.0f, 3.5f, 4.0f, 4.6f, 4.0f, 1.5f, 3.0f, 4.6f};

// ---------------------------------------------------------------------------------------------------------
// small double-precision vector for the camera integrator
struct D3 {
    double x = 0, y = 0, z = 0;
    D3() = default;
    D3(double a, double b, double c) : x(a), y(b), z(c) {}
    D3 operator+(const D3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    D3 operator-(const D3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    D3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const D3& o) const { return x * o.x + y * o.y + z * o.z; }
    double len() const { return std::sqrt(dot(*this)); }
    Vec3 f() const { return Vec3(float(x), float(y), float(z)); }
};

struct Ball { D3 dir; double f[3], ph[3]; };

// rand() state at the blob init 0x268bc of the setup. The 128 calls there are followed directly by the 12 of
// the flowers init 0x29ae8. The state is not reproducible from the exe (all earlier parts use rand()); this
// one was found by a search over all 2^32 states for ball directions that match the capture (balls 0 and 2
// to 3 px, ball 1 to 10 px rms).
constexpr uint32_t kRandState = 0x181e72de;

// 0x268bc: 16 balls, rand() order a, b, f0..f2, ph0..ph2 (only 4 are ever used); then 0x29ae8's three poles
// (a, b, da, db each), moved by 0x29aac every tick.
struct Random {
    Ball b[16];
    double a[3], bb[3], da[3], db[3];
    Random() {
        ExeRand r(kRandState);
        for (Ball& k : b) {
            double a0 = r.next() * kPi / 32768.0, b0 = r.next() * kPi / 65536.0;
            k.dir = D3(std::cos(a0) * std::sin(b0), std::sin(a0) * std::sin(b0), std::cos(b0));
            for (double& f : k.f) f = double(r.next() / 250 + 48);
            for (double& p : k.ph) p = double(r.next());
        }
        for (int k = 0; k < 3; k++) {
            a[k] = r.next();
            bb[k] = r.next();
            da[k] = (r.next() - 16384) * 1.111e-6;
            db[k] = (r.next() - 16384) * 1.111e-6;
        }
    }
};

// ---------------------------------------------------------------------------------------------------------
// The state the 70 Hz callbacks and the frame loops keep, replayed from the typo start every frame.
struct Sim {
    int sub = 1;                 // 1 typo, 2 blobs + face sheet, 3 blobs
    int wireMode = 0, wireReq = 0, bd2 = 0, lastPos = -1;
    bool strobe = true;          // 0x2e534 registered
    ExePalette pal;
    D3 P{0, 0, -290}, V, S, T{0, 0, kTargetZ}, Bc{-1000, 0, 60};
    D3 R[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // camera rows: right, down, forward
    double tau = kTau0, turn = 0.01, damp = 0.6;
    double n = 0;                // [0x57d78]
    int count = 1;
    double d[4] = {50, 50, 50, 50};
};

D3 rowTimes(const D3& row, const double m[3][3]) {
    return {row.x * m[0][0] + row.y * m[1][0] + row.z * m[2][0], row.x * m[0][1] + row.y * m[1][1] + row.z * m[2][1],
            row.x * m[0][2] + row.y * m[1][2] + row.z * m[2][2]};
}

// 0x278a4 / 0x27804 path, then one step of the integrator 0x27444(3)
void camStep(Sim& s) {
    const double R = s.sub == 1 ? 230.0 : 130.0;
    const double r = R + R / 6.0 * std::sin(s.tau * 0.0005 + 64.0);
    if (s.sub == 1) {
        double A = 4.0 * s.tau / 1100.0, B = 4.0 * s.tau / 700.0 + 34.0;
        s.S = D3(2.0 * r * std::sin(B) * std::cos(A), r * std::sin(B) * std::sin(A), r * std::cos(B) - 700.0);
    } else {
        double A = s.tau / 1100.0, B = s.tau / 700.0 + 34.0;
        s.S = s.Bc + D3(std::sin(B) * std::cos(A), std::sin(B) * std::sin(A), std::cos(B)) * r;
    }
    D3 d = s.T - s.P;
    double len = d.len();
    if (len > 0.01) {
        d = d * (s.turn / len);
        const D3& f = s.R[2];
        D3 ax(d.y * f.z - d.z * f.y, d.z * f.x - d.x * f.z, d.x * f.y - d.y * f.x);
        double L = ax.len();
        if (L >= 1e-8) {   // 0x2721c: rotation by asin(L) about ax, applied as M * Rot
            double th = std::asin(std::min(L, 1.0)), c = std::cos(th), sn = std::sin(th), k = 1.0 - c;
            double nx = ax.x / L, ny = ax.y / L, nz = ax.z / L;
            const double m[3][3] = {{c + nx * nx * k, nx * ny * k - nz * sn, nx * nz * k + ny * sn},
                                    {nx * ny * k + nz * sn, c + ny * ny * k, ny * nz * k - nx * sn},
                                    {nx * nz * k - ny * sn, ny * nz * k + nx * sn, c + nz * nz * k}};
            for (D3& row : s.R) row = rowTimes(row, m);
        }
    }
    s.V = s.V * s.damp + (s.S - s.P) * 0.002;
    s.P = s.P + s.V;
}

void tick(Sim& s, int pos) {
    s.pal.tick();                                    // 0x10124
    if (s.strobe) {                                  // 0x2e534
        if (s.wireMode) {
            if (s.lastPos != pos) {
                s.lastPos = pos;
                if ((pos & 3) == 0) {
                    int i = std::clamp((pos - 0x9a4) / 4, 0, 23);
                    s.pal.mult = kKicks[i];
                }
                if ((pos & 0x1f) == 0 && s.bd2 < 2) s.bd2 ^= 1;
            }
            s.pal.mult = (s.pal.mult - 1.0f) * 0.9575f + 1.0f;
        } else {
            s.pal.mult = 1.0f;
        }
    }
    s.n += 2;                                        // 0x23384
    s.tau += 4;                                      // 0x2764c (+ the frame's path / integrator step)
    camStep(s);
    if (s.wireReq != s.wireMode) {                   // 0x2e684
        s.wireMode = s.wireReq;
        s.pal.level = 0x8000;
        s.pal.target = 0x4000;
        s.pal.speed = 1000;
    }
    if (s.sub >= 2) {
        if (s.turn < 0.05) s.turn += 0.0004;         // 0x2e438 (its damp ramp never runs)
        s.n += 1;                                    // 0x2685c
        for (int k = 0; k < s.count; k++)
            if (s.d[k] > 0) s.d[k] *= 0.99;
    }
}

// the frame loops' own bookkeeping (0x2efd4 / 0x2f2cb / 0x2f415)
void frameLogic(Sim& s, int pos) {
    if (s.sub == 1) {
        if (pos >= kPosTypoEnd) {
            s.sub = 2;
            s.wireReq = 0;
            s.strobe = false;
            s.Bc = s.T = D3(-1000, 0, 60);
            s.S = D3(-1000, 0, -10);
        } else {
            s.wireReq = pos > kPosTypo + 0x60 ? 1 : 0;
        }
    } else if (s.sub == 2 && pos >= kPosSub3) {
        s.sub = 3;
    }
    if (s.sub == 3 && pos >= kPosWire2) s.wireReq = 1;
    if (s.sub >= 2 && s.count < 4 && s.d[s.count - 1] < 2.0) s.count++;
}

// What one frame needs, interpolated between the last two ticks.
struct View {
    int sub = 1, wire = 0, bd2 = 0, count = 0;
    ExePalette pal;
    Vec3 P, R0, R1, R2;
    double n = 0, ticks = 0;
    double d[4] = {50, 50, 50, 50};
};

View simulate(float m) {
    const double t0 = musicTimeOfPos(kPosTypo), tL3 = musicTimeOfPos(0x878);
    Sim s, prev;
    s.n = 200.0 + 2.0 * std::floor((t0 - tL3) * kTickHz);
    const double ticksF = std::max(0.0, (double(m) - t0) * kTickHz);
    const int K = int(ticksF);
    prev = s;
    for (int k = 1; k <= K + 1; k++) {
        if (k == K + 1) prev = s;
        int pos = musicPos(float(t0 + k / kTickHz));
        tick(s, pos);
        frameLogic(s, pos);
    }
    const double a = ticksF - K;
    View v;
    v.sub = prev.sub;
    v.wire = prev.wireMode;
    v.bd2 = prev.sub == 3 && prev.wireMode ? 2 : prev.bd2;
    v.count = prev.count;
    v.pal = prev.pal;
    v.pal.frac = float(a);  // blend the fade towards the next tick
    auto lerp = [&](const D3& p, const D3& q) { return p + (q - p) * a; };
    v.P = lerp(prev.P, s.P).f();
    D3 r2 = lerp(prev.R[2], s.R[2]), r0 = lerp(prev.R[0], s.R[0]);
    r2 = r2 * (1.0 / r2.len());
    r0 = r0 - r2 * r0.dot(r2);
    r0 = r0 * (1.0 / r0.len());
    D3 r1(r2.y * r0.z - r2.z * r0.y, r2.z * r0.x - r2.x * r0.z, r2.x * r0.y - r2.y * r0.x);
    v.R0 = r0.f();
    v.R1 = r1.f();
    v.R2 = r2.f();
    v.n = prev.n + (s.n - prev.n) * a;
    for (int k = 0; k < 4; k++) v.d[k] = prev.d[k] + (s.d[k] - prev.d[k]) * a;
    v.ticks = ticksF;
    return v;
}

// ---------------------------------------------------------------------------------------------------------
// Marching cubes as in 0x26c14 / 0x25d00: 4-unit cells, surface following from each ball, the exe's case table.
// corner i = (i >> 2, (i >> 1) & 1, i & 1); edges as decoded from 0x236c4
const int kMcEdge[12][2] = {{0, 4}, {1, 5}, {2, 6}, {3, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 1}, {2, 3}, {4, 5}, {6, 7}};
// The case table at 0x532a4 (256 cases of 38 bytes: edge count, 12 edges, triangle count, 8 triangles as edge
// numbers with outward right-hand winding), as stored in the exe (0x268bc rewrites it in memory at run time).
struct McCases {
    int8_t tris[256][25] = {};  // triangle count, then the triangles' edge numbers
    void load(const Exe303& x) {
        for (uint32_t m = 0; m < 256; m++) {
            const uint32_t o = 0x532a4 + 38 * m;
            const int n = std::min<int>(x.u8(o + 13), 8);
            tris[m][0] = int8_t(n);
            for (int j = 0; j < 3 * n; j++) tris[m][1 + j] = x.s8(o + 14 + uint32_t(j));
        }
    }
};

void polygonise(const McCases& mcc, const Vec3* pos, int count, std::vector<Vec3>& tris) {
    tris.clear();
    auto field = [&](float x, float y, float z) {
        float s = 0;
        for (int k = 0; k < count; k++) {
            float dx = x - pos[k].x, dy = y - pos[k].y, dz = z - pos[k].z;
            s += 1.0f / (dx * dx + dy * dy + dz * dz + 1e-5f);
        }
        return s;
    };
    std::unordered_set<int64_t> seen;
    auto key = [](int x, int y, int z) { return (int64_t(x + 8192) << 28) | (int64_t(y + 8192) << 14) | int64_t(z + 8192); };
    struct Cell { int x, y, z; };
    std::vector<Cell> stack;
    for (int k = 0; k < count; k++) {
        int ix = int(pos[k].x), iy = int(pos[k].y), iz = int(pos[k].z);
        for (int guard = 0; guard < 64 && field(float(ix + 1), float(iy), float(iz)) > 0.2f; guard++) ix++;
        stack.push_back({ix, iy, iz});
        while (!stack.empty() && tris.size() < 30000) {
            Cell c = stack.back();
            stack.pop_back();
            if (!seen.insert(key(c.x, c.y, c.z)).second) continue;
            float g[8];
            int mask = 0;
            for (int i = 0; i < 8; i++) {
                g[i] = field(float(c.x + (i >> 2)), float(c.y + ((i >> 1) & 1)), float(c.z + (i & 1))) - 0.2f;
                if (g[i] > 0) mask |= 1 << i;
            }
            const int8_t* t = mcc.tris[mask];
            for (int j = 0; j < 3 * t[0]; j++) {
                int a = kMcEdge[t[1 + j]][0], b = kMcEdge[t[1 + j]][1];
                float u = g[a] != g[b] ? std::clamp(g[a] / (g[a] - g[b]), 0.0f, 1.0f) : 0.5f;
                float ax = float(a >> 2), ay = float((a >> 1) & 1), az = float(a & 1);
                float bx = float(b >> 2), by = float((b >> 1) & 1), bz = float(b & 1);
                tris.push_back(Vec3(4.0f * (c.x + ax + (bx - ax) * u), 4.0f * (c.y + ay + (by - ay) * u),
                                    4.0f * (c.z + az + (bz - az) * u)));
            }
            auto mixed = [&](int m) { return (mask & m) != 0 && (mask & m) != m; };
            if (mixed(0x0f)) stack.push_back({c.x - 1, c.y, c.z});
            if (mixed(0xf0)) stack.push_back({c.x + 1, c.y, c.z});
            if (mixed(0x33)) stack.push_back({c.x, c.y - 1, c.z});
            if (mixed(0xcc)) stack.push_back({c.x, c.y + 1, c.z});
            if (mixed(0x55)) stack.push_back({c.x, c.y, c.z - 1});
            if (mixed(0xaa)) stack.push_back({c.x, c.y, c.z + 1});
        }
        stack.clear();
    }
}

// ---------------------------------------------------------------------------------------------------------
// shaders (original screen pixels: px = (uv.x * 320, (1 - uv.y) * 200))
const char* kFlowersFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 pole[3];
uniform float scroll;
void main() {   // 0x29ae8: tex3d #5 columns bent by three 1/r poles, scrolling one texel per tick
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec2 t = vec2(scroll, 0.0);
    for (int k = 0; k < 3; k++) {
        vec2 d = pole[k].xy - px;
        t += d * (10000.0 / (1.0 + dot(d, d)));
    }
    frag = vec4(texture(tex, t / 256.0).rgb, 1.0);
}
)";

const char* kFloorFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 P, R0, R1, R2;
uniform int wire;
void main() {   // 0x27014 + 0x1cddc: planes y = +-290, column = x / 2, row = z / 2, light 1200 / t
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 d = 37.5 * R2 + (px.x / 8.0 - 20.5) * R0 + (px.y / 8.0 - 13.0) * R1;
    float t = abs(d.y) > 1e-9 ? (290.0 - P.y) / d.y : 1.7e24;
    if (t < 0.0) t = (-290.0 - P.y) / d.y;
    vec2 tc = (P.xz + t * d.xz) * (0.5 / 256.0);
    float Lr = 1200.0 / t, L = clamp(Lr, 1.0, 32.0);
    if (wire == 0) {   // tex3d #1 through 3d.fad rows 1..32
        frag = vec4(texture(tex, tc).rgb * (L / 32.0), 1.0);
        return;
    }
    // wire.raw (grey index 0..31) through wire.fad: row 32 = 4 * (i / 4 + 1), rows < 32 = 4 * floor(i / 4 * L / 32);
    // the result is the whitening index for wirecut.fad
    float g = texture(tex, tc).r * 255.0 / 4.0;
    float idxNear = 4.0 * (max(g - 0.5, 0.0) + 1.0);
    float idxFar = 4.0 * max(g * L / 32.0 - 0.5, 0.0);
    float nearW = clamp((Lr - 32.0) / max(fwidth(Lr), 1e-6) + 0.5, 0.0, 1.0);
    frag = vec4(mix(idxFar, idxNear, nearW), 0.0, 0.0, 1.0);
}
)";

const char* kBlobFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D env;
uniform vec3 P, R0, R1, R2;
uniform vec3 ball[4];
uniform int count;
float field(vec3 v) {   // sum 1 / r^2 in grid units (1 grid unit = 4 world units)
    float s = 0.0;
    for (int k = 0; k < 4; k++) {
        if (k >= count) break;
        vec3 q = v - ball[k];
        s += 16.0 / (dot(q, q) + 16e-5);
    }
    return s;
}
vec3 shadeAt(vec3 v) {   // 0x236c4 normal (sum of (c - v) / |c - v|^3, pointing inward), env 128 + 126 n.R
    vec3 n = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        if (k >= count) break;
        vec3 q = ball[k] - v;
        float l = length(q);
        n += q / max(l * l * l, 1e-5);
    }
    n = normalize(n);
    vec2 e = 128.0 + 126.0 * vec2(dot(n, R0), dot(n, R1));
    return textureLod(env, e / 256.0, 0.0).rgb;   // (non-uniform control flow: no derivatives)
}
vec4 trace(vec2 px) {
    vec3 d = (px.x - 160.0) / 300.0 * R0 + (px.y - 100.0) / 300.0 * R1 + R2;   // one unit of view depth
    float dd = dot(d, d), dl = sqrt(dd);
    float Rb = sqrt(80.0 * float(count));
    float lo = 1e9, hi = -1e9;
    for (int k = 0; k < 4; k++) {
        if (k >= count) break;
        vec3 o = P - ball[k];
        float b = dot(o, d), c = dot(o, o) - Rb * Rb, disc = b * b - dd * c;
        if (disc > 0.0) {
            float s = sqrt(disc);
            lo = min(lo, (-b - s) / dd);
            hi = max(hi, (-b + s) / dd);
        }
    }
    lo = max(lo, 40.0);   // triangles nearer than depth 40 are dropped by 0x26524
    if (hi <= lo) return vec4(0.0);
    float t = lo, tp = lo;
    float fp = field(P + t * d) - 0.2;
    if (fp > 0.0) return vec4(shadeAt(P + t * d), 1.0);
    for (int i = 0; i < 400; i++) {
        vec3 v = P + t * d;
        float rmin = 1e9;
        for (int k = 0; k < 4; k++) {
            if (k >= count) break;
            rmin = min(rmin, length(v - ball[k]));
        }
        float stepW = max(rmin - Rb, 0.3);
        tp = t;
        t += stepW / dl;
        if (t > hi) return vec4(0.0);
        float f = field(P + t * d) - 0.2;
        if (f > 0.0) {
            float a = tp, b = t;
            for (int j = 0; j < 10; j++) {
                float m = 0.5 * (a + b);
                if (field(P + m * d) > 0.2) b = m; else a = m;
            }
            return vec4(shadeAt(P + b * d), 1.0);
        }
    }
    return vec4(0.0);
}
float gEst(vec2 px) {   // field maximum along the ray (closest approach to each ball), for edge detection
    vec3 d = (px.x - 160.0) / 300.0 * R0 + (px.y - 100.0) / 300.0 * R1 + R2;
    float g = 0.0;
    for (int k = 0; k < 4; k++) {
        if (k >= count) break;
        float t = max(dot(ball[k] - P, d) / dot(d, d), 40.0);
        g = max(g, field(P + t * d));
    }
    return g;
}
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float g = gEst(px);
    float w = fwidth(g);
    vec4 c;
    if (abs(g - 0.2) < 3.0 * w + 0.01) {   // near a silhouette: 4 rays
        vec2 o = vec2(dFdx(px.x), dFdy(px.y)) * 0.25;
        c = (trace(px + vec2(-o.x, -o.y)) + trace(px + vec2(o.x, -o.y)) + trace(px + vec2(-o.x, o.y)) + trace(px + o)) * 0.25;
    } else {
        c = trace(px);
    }
    frag = vec4(c.rgb, c.a);
}
)";

const char* kFaceVS = R"(
layout(location = 0) in vec3 cpos;   // camera space (x right, y down, z depth)
layout(location = 1) in vec2 tuv;    // flowers frame pixel
layout(location = 2) in vec2 nxy;    // camera-space normal x, y (0x22e14: (v0 - v1) x (v2 - v1))
out vec2 vt, vn;
void main() {
    vt = tuv;
    vn = nxy;
    gl_Position = vec4(1.875 * cpos.x, -3.0 * cpos.y, ((cpos.z - 40.0) / 40000.0 * 2.0 - 1.0) * cpos.z, cpos.z);
}
)";
const char* kFaceFS = R"(
in vec2 vt, vn;
out vec4 frag;
uniform sampler2D flowers, phong;
void main() {   // 0x1e223: 3d.fad[phong1[120 n.y + 128][120 n.x + 128]][flowers texel]
    vec3 c = texture(flowers, vec2(vt.x / 320.0, 1.0 - vt.y / 200.0)).rgb;
    float p = texture(phong, (120.0 * vn + 128.0) / 256.0).r * 255.0;
    c = p < 32.0 ? c * (p / 32.0) : mix(c, vec3(1.0), (p - 32.0) / 31.0);
    frag = vec4(c, 1.0);
}
)";

// Hidden-line triangles (0x23118): painter's order, each triangle fills with index 0 and adds glow strokes
// along its edges (0x2acf0: +1 +2 +1 across the line). Blend ONE, ONE_MINUS_SRC_ALPHA: fills have alpha 1 and
// value 0, strokes alpha 0.
const char* kHiddenVS = R"(
layout(location = 0) in vec2 pos;
layout(location = 1) in vec3 attr;   // across, value, fill
out vec3 a;
out vec2 p;
void main() {
    a = attr;
    p = pos;
    gl_Position = vec4(pos.x / 160.0 - 1.0, 1.0 - pos.y / 100.0, 0.0, 1.0);
}
)";
const char* kHiddenFS = R"(
in vec3 a;
in vec2 p;
out vec4 frag;
void main() {
    if (a.z > 0.5) { frag = vec4(0.0, 0.0, 0.0, 1.0); return; }
    if (p.x < 2.5 || p.x > 317.5 || p.y < 2.5 || p.y > 197.5) discard;   // line clip of 0x2acf0
    frag = vec4(a.y * max(0.0, 1.0 - abs(a.x)), 0.0, 0.0, 0.0);
}
)";

// wire mode: bd2 frame whitened through wirecut.fad by the render (0x1c490), bilinear between bd2 pixels
const char* kWireFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D render, bd2, lut;
uniform float frame;
vec3 lutAt(ivec2 q, float L) {
    float i = texelFetch(bd2, ivec2(q.x, int(frame) * 200 + q.y), 0).r * 255.0 + 0.5;
    return texture(lut, vec2(i / 256.0, (L + 0.5) / 64.0)).rgb;
}
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float L = 32.0 + min(2.0 * max(texture(render, uv).r, 0.0), 31.0);
    vec2 p = px - 0.5, f = fract(p);
    ivec2 i = ivec2(floor(p));
    ivec2 i0 = clamp(i, ivec2(0), ivec2(319, 199)), i1 = clamp(i + 1, ivec2(0), ivec2(319, 199));
    vec3 c = mix(mix(lutAt(i0, L), lutAt(ivec2(i1.x, i0.y), L), f.x), mix(lutAt(ivec2(i0.x, i1.y), L), lutAt(i1, L), f.x), f.y);
    frag = vec4(exePalette(c), 1.0);
}
)";

const char* kFinalFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D src;
void main() { frag = vec4(exePalette(texture(src, uv).rgb), 1.0); }
)";

GLuint makeTex(int w, int h, GLenum ifmt, GLenum fmt, const void* data, bool linear, bool repeat, bool mip) {
    GLuint id = 0;
    gl::GenTextures(1, &id);
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(ifmt), w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    if (mip) gl::GenerateMipmap(GL_TEXTURE_2D);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    if (mip) {
        gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        gl::GetError();
    }
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return id;
}

void bindTex(int unit, GLuint id) {
    gl::ActiveTexture(GL_TEXTURE0 + unit);
    gl::BindTexture(GL_TEXTURE_2D, id);
}

struct Stream {   // dynamic vertex buffer with float attributes
    GLuint vao = 0, vbo = 0;
    size_t cap = 0;
    void init(std::initializer_list<int> sizes) {
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        int stride = 0;
        for (int s : sizes) stride += s;
        int off = 0, i = 0;
        for (int s : sizes) {
            gl::VertexAttribPointer(GLuint(i), s, GL_FLOAT, GL_FALSE, stride * 4, (void*)(intptr_t)(off * 4));
            gl::EnableVertexAttribArray(GLuint(i));
            off += s;
            i++;
        }
        gl::BindVertexArray(0);
    }
    void draw(const std::vector<float>& v, int floatsPerVertex) {
        if (v.empty()) return;
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        size_t bytes = v.size() * sizeof(float);
        if (bytes > cap) {
            cap = bytes * 2;
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(cap), nullptr, GL_DYNAMIC_DRAW);
        }
        gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), v.data());
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / size_t(floatsPerVertex)));
        gl::BindVertexArray(0);
    }
};

// hidden-line geometry in original pixels
struct HiddenLines {
    std::vector<float> v;
    void vert(float x, float y, float across, float value, float fill) {
        float q[5] = {x, y, across, value, fill};
        v.insert(v.end(), q, q + 5);
    }
    void fill(const float* x, const float* y) {
        for (int i = 0; i < 3; i++) vert(x[i], y[i], 0, 0, 1);
    }
    // like LineBatch::add: offsets in square units (an original pixel is 1.2x taller than wide)
    void stroke(float x0, float y0, float x1, float y1, float value, float hw) {
        float dx = x1 - x0, dy = (y1 - y0) * 1.2f, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) { dx = 1; dy = 0; len = 1; }
        dx /= len;
        dy /= len;
        float nx = -dy * hw, ny = dx * hw / 1.2f, ex = dx * hw * 0.5f, ey = dy * hw * 0.5f / 1.2f;
        float ax = x0 - ex, ay = y0 - ey, bx = x1 + ex, by = y1 + ey;
        vert(ax + nx, ay + ny, 1, value, 0);
        vert(ax - nx, ay - ny, -1, value, 0);
        vert(bx + nx, by + ny, 1, value, 0);
        vert(bx + nx, by + ny, 1, value, 0);
        vert(ax - nx, ay - ny, -1, value, 0);
        vert(bx - nx, by - ny, -1, value, 0);
    }
};

// camera-space triangle for the painter's sort
struct CTri { float sx[3], sy[3], zsum; };

// project, cull (0x1f88b: screen cross >= 0 rejected, all depths > 40) and sort far to near, then emit
void emitHiddenLines(std::vector<CTri>& tris, HiddenLines& out, float strokeValue, float hw) {
    std::stable_sort(tris.begin(), tris.end(), [](const CTri& a, const CTri& b) { return a.zsum > b.zsum; });
    for (const CTri& t : tris) {
        out.fill(t.sx, t.sy);
        out.stroke(t.sx[0], t.sy[0], t.sx[1], t.sy[1], strokeValue, hw);
        out.stroke(t.sx[2], t.sy[2], t.sx[1], t.sy[1], strokeValue, hw);
        out.stroke(t.sx[0], t.sy[0], t.sx[2], t.sy[2], strokeValue, hw);
    }
}

bool cameraTri(const Vec3* w, const View& v, CTri& t) {
    float z[3];
    for (int i = 0; i < 3; i++) {
        Vec3 q = w[i] - v.P;
        z[i] = q.dot(v.R2);
        if (z[i] <= 40.0f) return false;
        t.sx[i] = 160.0f + 300.0f * q.dot(v.R0) / z[i];
        t.sy[i] = 100.0f + 300.0f * q.dot(v.R1) / z[i];
    }
    float cross = (t.sx[1] - t.sx[0]) * (t.sy[2] - t.sy[1]) - (t.sy[1] - t.sy[0]) * (t.sx[2] - t.sx[1]);
    if (cross >= 0.0f) return false;
    t.zsum = z[0] + z[1] + z[2];
    return true;
}

// ---------------------------------------------------------------------------------------------------------
struct Renderer {
    bool ready = false;
    Shader flowersSh, floorSh, blobSh, faceSh, hiddenSh, wireSh, finalSh;
    GLuint wireTex = 0, phongTex = 0, bd2Tex = 0, lutTex = 0;
    Stream faceStream, hiddenStream;
    Mesh face;
    Random rnd;
    std::vector<Vec3> mc;
    McCases mcCases;
    std::vector<CTri> ctris;
    HiddenLines hidden;
    std::vector<float> faceVerts;

    void init(Ctx& c) {
        if (ready) return;
        ready = true;
        flowersSh.build(kFullscreenVS, kFlowersFS, "typo flowers");
        floorSh.build(kFullscreenVS, kFloorFS, "typo floor");
        blobSh.build(kFullscreenVS, kBlobFS, "metaballs");
        faceSh.build(kFaceVS, kFaceFS, "face sheet");
        hiddenSh.build(kHiddenVS, kHiddenFS, "hidden lines");
        wireSh.build(kFullscreenVS, std::string(kExePaletteGLSL) + kWireFS, "wire composite");
        finalSh.build(kFullscreenVS, std::string(kExePaletteGLSL) + kFinalFS, "typo final");
        // Read as palette indices and levels, so art/ redraws do not replace them: wire.raw (levels 0..31 into
        // wire.fad), phong1.raw (3d.fad rows) and bd2.raw (its index through the wirecut.fad table below; a redraw
        // of bd2 would need that table as a blend towards white, re/notes-pictures.md).
        const Bytes& wire = c.pak.get("wire.raw");
        wireTex = makeTex(256, 256, GL_R8, GL_RED, wire.data() + 768, true, true, true);
        const Bytes& phong = c.pak.get("phong1.raw");
        phongTex = makeTex(256, 256, GL_R8, GL_RED, phong.data(), true, false, false);
        const Bytes& bd2 = c.pak.get("bd2.raw");
        bd2Tex = makeTex(320, 600, GL_R8, GL_RED, bd2.data() + 768, false, false, false);
        const Bytes& cut = c.pak.get("wirecut.fad");
        std::vector<uint8_t> lut(256 * 64 * 4);
        for (int L = 0; L < 64; L++)
            for (int i = 0; i < 256; i++) {
                int idx = cut[size_t(L) * 256 + size_t(i)];
                for (int k = 0; k < 3; k++)
                    lut[(size_t(L) * 256 + size_t(i)) * 4 + size_t(k)] = uint8_t(std::min(63, int(bd2[size_t(idx) * 3 + size_t(k)])) * 255 / 63);
                lut[(size_t(L) * 256 + size_t(i)) * 4 + 3] = 255;
            }
        lutTex = makeTex(256, 64, GL_RGBA8, GL_RGBA, lut.data(), true, false, false);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        faceStream.init({3, 2, 2});
        hiddenStream.init({2, 3});
        face = c.meshData("faceo");
        mcCases.load(c.exe);
    }

    // face sheet geometry 0x23540 / 0x22be0 / 0x22e14; returns false when the exe would skip it this frame
    bool faceGeometry(const View& v, std::vector<Vec3>& world, std::vector<Vec3>& cam, std::vector<float>& tu,
                      std::vector<float>& tv) {
        const double n = v.n;
        const double A = 0.2 * std::cos(n * 0.00694444 + 54.0), B = 0.3 * std::sin(n * 0.011627907);
        const float cA = float(std::cos(A)), sA = float(std::sin(A)), cB = float(std::cos(B)), sB = float(std::sin(B));
        const float push = float(std::max(0.0, 150.0 * std::sin(n * 0.005 + 1.0) + 50.0));
        const size_t nv = face.pos.size() / 3;
        world.resize(nv);
        cam.resize(nv);
        tu.resize(nv);
        tv.resize(nv);
        for (size_t i = 0; i < nv; i++) {
            Vec3 p(face.pos[i * 3], face.pos[i * 3 + 1], face.pos[i * 3 + 2]), w;
            if (p.z < -1.0f) {
                w = Vec3(cA * cB * p.x + sA * p.y - cA * sB * p.z, -sA * cB * p.x + cA * p.y + sA * sB * p.z, sB * p.x + cB * p.z);
                w.z = std::min(w.z + push, 0.0f);
            } else {
                w = Vec3(p.x, p.y, 0.0f);
            }
            world[i] = w;
            tu[i] = w.x + 160.0f;
            tv[i] = w.y + 100.0f;
            Vec3 q = w - v.P;
            cam[i] = Vec3(q.dot(v.R0), q.dot(v.R1), q.dot(v.R2));
            if (cam[i].z > 40.0f) {
                float sx = std::trunc(160.0f + 300.0f * cam[i].x / cam[i].z);
                if (sx > 640.0f || sx < -320.0f) return false;
            }
        }
        return true;
    }

    void drawFaceTextured(Ctx& c, const View& v, const Target& flowers) {
        std::vector<Vec3> world, cam;
        std::vector<float> tu, tv;
        if (!faceGeometry(v, world, cam, tu, tv)) return;
        const size_t nv = cam.size();
        std::vector<Vec3> nrm(nv, Vec3(0, 0, 0));
        const auto& idx = face.idx;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const Vec3 &a = cam[idx[t]], &b = cam[idx[t + 1]], &d = cam[idx[t + 2]];
            Vec3 n = (a - b).cross(d - b);
            for (int k = 0; k < 3; k++) nrm[idx[t + size_t(k)]] = nrm[idx[t + size_t(k)]] + n;
        }
        for (Vec3& n : nrm) n = n.len() > 1e-5f ? n * (1.0f / n.len()) : Vec3(0, 0, 0);
        faceVerts.clear();
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            bool ok = true;
            float sx[3], sy[3];
            for (int k = 0; k < 3; k++) {
                const Vec3& q = cam[idx[t + size_t(k)]];
                if (q.z <= 40.0f) { ok = false; break; }
                sx[k] = 160.0f + 300.0f * q.x / q.z;
                sy[k] = 100.0f + 300.0f * q.y / q.z;
            }
            if (!ok) continue;
            if ((sx[1] - sx[0]) * (sy[2] - sy[1]) - (sy[1] - sy[0]) * (sx[2] - sx[1]) >= 0.0f) continue;
            for (int k = 0; k < 3; k++) {
                uint32_t i = idx[t + size_t(k)];
                float q[7] = {cam[i].x, cam[i].y, cam[i].z, tu[i], tv[i], nrm[i].x, nrm[i].y};
                faceVerts.insert(faceVerts.end(), q, q + 7);
            }
        }
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LESS);
        gl::DepthMask(GL_TRUE);
        setBlend(Blend::None);
        faceSh.use();
        flowers.bindTex(0);
        bindTex(1, phongTex);
        faceSh.set("flowers", 0);
        faceSh.set("phong", 1);
        faceStream.draw(faceVerts, 7);
        gl::Disable(GL_DEPTH_TEST);
        (void)c;
    }

    void faceHidden(const View& v) {
        std::vector<Vec3> world, cam;
        std::vector<float> tu, tv;
        if (!faceGeometry(v, world, cam, tu, tv)) return;
        const auto& idx = face.idx;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            Vec3 w[3] = {world[idx[t]], world[idx[t + 1]], world[idx[t + 2]]};
            CTri ct;
            if (cameraTri(w, v, ct)) ctris.push_back(ct);
        }
    }

    void ballCentres(const View& v, Vec3* pos) {   // grid units (0x26c14)
        const D3 c(-250.0, 0.0, 15.0);
        for (int k = 0; k < v.count; k++) {
            const Ball& b = rnd.b[k];
            double amp = 3.8 / (1.0 + v.d[k]);
            D3 p = c + b.dir * v.d[k] +
                   D3(std::sin(v.n / b.f[0] + b.ph[0]), std::sin(v.n / b.f[1] + b.ph[1]), std::sin(v.n / b.f[2] + b.ph[2])) * amp;
            pos[k] = p.f();
        }
    }

    void render(Ctx& c) {
        init(c);
        const float m = exeNow(c);
        const View v = simulate(m);
        // textures (create before binding anything)
        const Texture& tex1 = c.tex("tex3d#1");
        const Texture& env = c.tex("tex3d#0");
        const Texture& stripes = c.tex("tex3d#5");
        const bool faceOn = v.sub <= 2;
        Target& frame = c.scratch[0];
        Target& flowers = c.scratch[1];

        if (faceOn && !v.wire) {   // 0x29ae8 into B1
            flowers.bind();
            setBlend(Blend::None);
            flowersSh.use();
            stripes.bind(0);
            flowersSh.set("tex", 0);
            const double ts = (double(m) - musicTimeOfPos(kPosTypo)) * kTickHz + kTau0 / 4.0;   // ticks since setup
            float pole[9] = {};
            for (int k = 0; k < 3; k++) {
                pole[3 * k] = float(160.0 + 120.0 * std::sin(rnd.a[k] + rnd.da[k] * ts));
                pole[3 * k + 1] = float(100.0 + 80.0 * std::sin(rnd.bb[k] + rnd.db[k] * ts));
            }
            gl::Uniform3fv(flowersSh.loc("pole"), 3, pole);
            flowersSh.set("scroll", float(std::fmod(ts, 256.0)));
            drawFullscreen();
        }

        frame.bind();
        setBlend(Blend::None);
        floorSh.use();
        if (v.wire) bindTex(0, wireTex); else tex1.bind(0);
        floorSh.set("tex", 0);
        floorSh.set("P", v.P);
        floorSh.set("R0", v.R0);
        floorSh.set("R1", v.R1);
        floorSh.set("R2", v.R2);
        floorSh.set("wire", v.wire);
        drawFullscreen();

        Vec3 bpos[4];
        if (v.sub >= 2) ballCentres(v, bpos);
        if (!v.wire) {
            if (v.sub >= 2) {   // 0x26c14 metaballs, env mapped
                blobSh.use();
                env.bind(0);
                blobSh.set("env", 0);
                blobSh.set("P", v.P);
                blobSh.set("R0", v.R0);
                blobSh.set("R1", v.R1);
                blobSh.set("R2", v.R2);
                float bw[12];
                for (int k = 0; k < 4; k++)
                    for (int j = 0; j < 3; j++) {
                        Vec3 p = k < v.count ? bpos[k] * 4.0f : Vec3(0, 0, 0);
                        bw[k * 3 + j] = j == 0 ? p.x : (j == 1 ? p.y : p.z);
                    }
                gl::Uniform3fv(blobSh.loc("ball"), 4, bw);
                blobSh.set("count", v.count);
                setBlend(Blend::Premul);
                drawFullscreen();
                setBlend(Blend::None);
            }
            if (faceOn) drawFaceTextured(c, v, flowers);
            c.out->bind();
            setBlend(Blend::None);
            finalSh.use();
            frame.bindTex(0);
            finalSh.set("src", 0);
            v.pal.apply(finalSh);
            drawFullscreen();
            return;
        }

        // wire mode: hidden-line face (typo) or blobs (sub-part 3) into the index frame
        ctris.clear();
        if (v.sub == 1) faceHidden(v);
        if (v.sub >= 2) {
            polygonise(mcCases, bpos, v.count, mc);
            for (size_t i = 0; i + 2 < mc.size(); i += 3) {
                CTri ct;
                if (cameraTri(&mc[i], v, ct)) ctris.push_back(ct);
            }
        }
        hidden.v.clear();
        emitHiddenLines(ctris, hidden, 4.0f, 1.5f);
        hiddenSh.use();
        gl::Enable(GL_BLEND);
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        hiddenStream.draw(hidden.v, 5);
        setBlend(Blend::None);

        c.out->bind();
        wireSh.use();
        frame.bindTex(0);
        bindTex(1, bd2Tex);
        bindTex(2, lutTex);
        wireSh.set("render", 0);
        wireSh.set("bd2", 1);
        wireSh.set("lut", 2);
        wireSh.set("frame", float(v.bd2));
        v.pal.apply(wireSh);
        drawFullscreen();
    }
};

Renderer& renderer() {
    static Renderer r;
    return r;
}

struct MidBlobs : Scene {
    void render(Ctx& c, float, float) override { renderer().render(c); }
};
struct FaceBox : MidBlobs {};
struct Typo : MidBlobs {};
struct Blobs : MidBlobs {};
struct WireBlobs : MidBlobs {};
REGISTER_SCENE(FaceBox, "facebox");
REGISTER_SCENE(Typo, "typo");
REGISTER_SCENE(Blobs, "blobs");
REGISTER_SCENE(WireBlobs, "wireblobs");

}  // namespace

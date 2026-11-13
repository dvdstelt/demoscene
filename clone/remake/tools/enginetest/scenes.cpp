// Test scenes for the engine checker (enginetest.cpp): minimal ports of how parts 7..10 use the engine, to compare it
// with the capture through the real timeline (tools/compare.py). They are registered under the parts' scene names
// only inside the checker. Motion from clone/re/notes-parts3d.md (part 7: 3; part 8: 4; part 9: 5, with the capture's
// switch times of 5.3; part 10: 6), as closed forms of the part's clock; the tile band of 2.4 as flat rectangles.
// They are not the parts (the parts3d job): no attention to the event frames' lag beyond part 9's switches, nothing
// but what the engine needs to be compared.
#include <algorithm>
#include <cmath>
#include <functional>

#include "clone/core.h"
#include "clone/engine.h"
#include "demo.h"
#include "exe.h"

using namespace cmc;

// enginetest options
Order g_testOrder = Order::Depth;           // --painter
TexStyle g_testStyle = TexStyle::Smooth;    // --style texel
int g_geoLevel = 4;                         // --geo N: geosphere level (exact floats when not 4)
bool g_geoExact = false;                    // --exact: exact floats on the sphere
bool g_logMips = false;                     // --logmips: Engine::Debug::logMips

namespace {

Engine& engine(Ctx& c) {
    Engine& e = c.shared<Engine>();
    e.order = g_testOrder;
    e.style = g_testStyle;
    e.debug.logMips = g_logMips;
    return e;
}

// The tile band (notes-parts3d 2.4): an 8 x 8 grid of 40 x 24 rectangles from row 4, colours from the table 0x428164
// at dword index start + 3 n (tile n = 8 (row + 1) + col), times the part's factor, clamped; invisible tiles black.
void tiles(Ctx& c, int start, const std::function<bool(int, int)>& visible, const std::function<float(int)>& factor) {
    setBlend(Blend::None);
    for (int row = -1; row <= 6; row++)
        for (int col = 0; col <= 7; col++) {
            int idx = start + 3 * (8 * (row + 1) + col);
            Vec3 v(0, 0, 0);
            if (visible(row, col)) {
                float f = factor(idx);
                Vec3 tc = tileColour(*c.exe, idx);
                v = Vec3(std::min(1.0f, tc.x * f), std::min(1.0f, tc.y * f), std::min(1.0f, tc.z * f));
            }
            c.rect(40.0f * col, 24.0f * row + 28.0f, 40, 24, v);
        }
}

Geosphere makeGeo(Ctx& c) {
    return Geosphere::make(*c.exe, g_geoLevel, g_geoExact || g_geoLevel != 4);
}

// ------------------------------------------------------------------------------------------------- part 7
// 0x416fce: the geosphere with map A deformed by 0x416e37, the 6 x 5 tile block, the fade-in, the 701 events.
struct BumpsTest : Scene {
    Geosphere geo;
    EngineMesh mesh;
    float K = 112, speed = 0.03f;

    void init(Ctx& c) override {
        geo = makeGeo(c);
        mesh = geo.mesh(Tex::MapA, c.exe->f64(0x41a4d0));   // 0.004
        engine(c).prepare(Tex::MapA);
        K = c.exe->f32(0x427f7c);                            // 112.0f
        speed = c.exe->f32(0x427f78);                        // 0.03f
    }
    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        int n701 = c.eventCount(701, t);                     // [0x427f74]
        double Tm = n701 >= 1 ? c.clock(c.eventTime(701, 0)) : T;   // the motion stops at 28:00
        float A7 = float(0.0075 * Tm), P = float(speed * Tm);
        float P8 = float(P * 0.8), P13 = float(P * 1.3);
        double fade = std::min(1.0, 0.0045 * T);
        float light = fade >= 0.5 ? 1.0f : float(2 * fade);
        transform(Mat34::part(A7, float(1.1 * A7), float(0.8 * A7), 4.0f / 3.0f), geo.verts, mesh.pos);
        for (size_t i = 0; i < mesh.pos.size(); i++) {      // 0x416e37
            const Vec3& o = geo.verts[i];
            float s = float(0.5 * std::sin(o.x / K + P) + 2.0 / 3 + 0.5 * std::sin(o.y / K + P8) + 2.0 / 3 +
                            0.5 * std::sin(o.z / K + P13) + 2.0 / 3);
            if (s > 2) s = float(s * std::min(1.0 + 3.0 * (s - 2), 1.5));
            Vec3& p = mesh.pos[i];
            p = Vec3(p.x * s, p.y * s, p.z * s + 2000.0f);
        }
        tiles(c, 0, [&](int row, int col) { return n701 < 2 && row >= 0 && row <= 5 && col >= 1 && col <= 5; },
              [&](int) { return float(fade); });
        if (n701 >= 3) return;
        Engine& eng = engine(c);
        eng.begin(c, Window::at(c, t));
        eng.draw(mesh, light);
        eng.end();
    }
};

// ------------------------------------------------------------------------------------------------- part 8
// 0x417929: the geosphere with map B deformed by 0x4176d0, flying in from Z = 9400 (the far cut), the tile band.
struct BlobTest : Scene {
    Geosphere geo;
    EngineMesh mesh;
    float K = 69.115036f, speed = 0.03f, Z0 = 9400;

    void init(Ctx& c) override {
        geo = makeGeo(c);
        mesh = geo.mesh(Tex::MapB, c.exe->f64(0x41a554));   // 0.004
        engine(c).prepare(Tex::MapB);
        K = c.exe->f32(0x427ff8);                            // 22 pi
        speed = c.exe->f32(0x427ff4);
        Z0 = c.exe->f32(0x427fe4);
    }
    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        int n803 = c.eventCount(803, t);
        float Z = float(std::max(1400.0, Z0 - 75.0 * T) - n803);
        float light = n803 ? float(1.0 - 0.25 * n803) : 1400.0f / Z;
        double fade = n803 ? std::max(0.0, 1.0 - 0.011 * (c.t70(t) - c.t70(c.eventTime(803, 0)))) : 1.0;
        float tt = float(0.05 * T), A8 = float(0.0075 * T), P = float(speed * T);
        float P8 = float(P * 0.8), P13 = float(P * 1.3);
        transform(Mat34::part(A8, float(1.1 * A8), float(0.8 * A8), 40.0f / 27.0f), geo.verts, mesh.pos);
        for (size_t i = 0; i < mesh.pos.size(); i++) {      // 0x4176d0 ([0x427fe8] = 0: event 802 never fires)
            const Vec3& o = geo.verts[i];
            float s = float(2 + 0.5 * (std::sin(o.x / K + P) + std::sin(o.y / K + P8) + std::sin(o.z / K + P13)));
            if (s < 2) s = 2;
            else if (s > 3) s = 2.5f;
            else if (s > 2) s = std::min(2.5f, float(s * std::min(1.0 + (s - 2) * 0.1, 1.5)));
            Vec3& p = mesh.pos[i];
            p = Vec3(p.x * s, p.y * s, p.z * s + Z);
        }
        int cols = 2 * c.eventCount(801, t);
        tiles(c, 24, [&](int row, int col) { return row >= 1 && row <= 4 && col < cols; },
              [&](int idx) {
                  double R = idx < 3 * kTileEntries ? c.exe->s32(0x428164 + 4 * uint32_t(idx)) : 0;
                  return float(fade * std::min(1.0, std::sin(R * tt * 0.01f + idx * 0.2) * 0.1 + 0.9));
              });
        Engine& eng = engine(c);
        eng.begin(c, Window::at(c, t));
        eng.draw(mesh, light);
        eng.end();
    }
};

// ------------------------------------------------------------------------------------------------- parts 9, 10
// The cubes 0x417c48 / 0x418b86: 8 corners at (x, y, z) +- s, the faces of the table 0x428020 whose mask bit is set,
// shades 192 + 31 (three sines) at the corners, uv from the full cell's corners (0x4280e0).
struct Cubes {
    int tab[12][4] = {};
    float corner[8][3] = {};
    void read(const CloneExe& exe) {
        for (int j = 0; j < 12; j++)
            for (int k = 0; k < 4; k++) tab[j][k] = exe.s32(0x428020 + 16 * j + 4 * k);
        for (int i = 0; i < 8; i++)
            for (int k = 0; k < 3; k++) corner[i][k] = exe.f32(0x4280e0 + 12 * i + 4 * k);
    }
    void cube(std::vector<Vec3>& src, EngineMesh& m, float x, float y, float z, int mask, float s, double uvk,
              const double C[3]) const {
        uint32_t n = uint32_t(src.size());
        for (int i = 0; i < 8; i++)
            src.emplace_back(x + (corner[i][0] < 0 ? -s : s), y + (corner[i][1] < 0 ? -s : s),
                             z + (corner[i][2] < 0 ? -s : s));
        for (int j = 0; j < 12; j++) {
            if (!(mask & tab[j][3])) continue;
            EngineFace f;
            for (int k = 0; k < 3; k++) {
                int i = tab[j][k];
                f.v[k] = n + uint32_t(i);
                const Vec3& p = src[n + uint32_t(i)];
                f.shade[k] = float(192.0 + 31.0 * (std::sin(0.035 * p.x + C[0]) + std::sin(0.035 * p.y + C[1]) +
                                                   std::sin(0.035 * p.z + C[2])));
                double qx = x + corner[i][0], qy = y + corner[i][1], qz = z + corner[i][2];
                f.uv[k][0] = float(uvk * qx + uvk * qz);
                f.uv[k][1] = float(uvk * qy - uvk * qz);
            }
            m.faces.push_back(f);
        }
    }
};

// Wyvill's falloff of the fields 0x418094 / 0x418fd2
double wyvill(double a) { return ((-4.0 / 9 * a + 17.0 / 9) * a - 22.0 / 9) * a + 1; }

double blobField(double X, double Y, double Z, const double (*b)[3], const int R[3]) {
    double d[3];
    for (int k = 0; k < 3; k++)
        d[k] = (X - b[k][0]) * (X - b[k][0]) + (Y - b[k][1]) * (Y - b[k][1]) + (Z - b[k][2]) * (Z - b[k][2]);
    double r0 = double(R[0]) * R[0], r1 = double(R[1]) * R[1], r2 = double(R[2]) * R[2];
    double v = -135;
    if (r0 > d[0]) v = 270 * wyvill(d[0] / r0) - 135;
    if (r1 > d[1]) v += 270 * wyvill(d[1] / r1);
    if (r1 > d[2]) v += 270 * wyvill(d[2] / r2);   // reach tested with R1, falloff with R2
    return v;
}

// part 9's angles: k switches at the capture's timer values (notes-parts3d 5.3), t70 units since its origin
constexpr double kOrigin9 = 5973.9528;
constexpr double kSwitch9[21] = {6221.136, 6252.660, 6284.763, 6470.633, 6502.980, 6532.472, 6720.286,
                                 6751.827, 6782.598, 6968.555, 7000.307, 7031.674, 7218.121, 7249.855,
                                 7279.050, 7466.759, 7498.406, 7529.124, 7715.729, 7745.307, 7777.902};

struct CubesTest : Scene {
    Cubes cubes;
    EngineMesh mesh;
    std::vector<Vec3> src;
    float amp = 2.5f;
    int R[3] = {35, 35, 35};

    void init(Ctx& c) override {
        cubes.read(*c.exe);
        amp = c.exe->f32(0x428000);
        for (int k = 0; k < 3; k++) R[k] = c.exe->s32(0x428004 + 4 * k);
        mesh.shade = Shade::Corners;
        mesh.map = Tex::MapC;
        engine(c).prepare(Tex::MapC);
    }
    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        double A[3] = {0, 0, 0}, from = 0;
        int k = 0;
        for (double s : kSwitch9) {
            double sw = s - kOrigin9;
            if (sw >= T) break;
            A[k] += 0.003 * (k + 1) * (sw - from);
            from = sw;
            k = (k + 1) % 3;
        }
        A[k] += 0.003 * (k + 1) * (T - from);
        double t901 = c.eventTime(901);
        double fade = t >= t901 ? std::min(1.0, 0.005 * (c.t70(t) - c.t70(t901))) : 0.0;
        double C[3] = {0.02 * T, 0.03 * T, 0.04 * T}, E = 0.2 * T;
        // 0x418094: three blobs on Lissajous paths
        auto cc = [&](double div, double ph) { return 8 * (amp * std::sin(E / div + ph) + 4); };
        double b[3][3] = {{cc(43, 2), cc(33, 1), cc(23, 0)}, {cc(24, 5), cc(43, 4), cc(30, 3)},
                          {cc(24, 8), cc(43, 7), cc(30, 6)}};
        float G[512] = {};
        for (int x = 1; x <= 6; x++)
            for (int y = 1; y <= 6; y++)
                for (int z = 1; z <= 6; z++) G[z * 64 + y * 8 + x] = float(blobField(8 * x, 8 * y, 8 * z, b, R) * fade);
        // 0x418445
        src.clear();
        mesh.faces.clear();
        for (int z = 1; z <= 6; z++)
            for (int y = 1; y <= 6; y++)
                for (int x = 1; x <= 6; x++) {
                    int i = z * 64 + y * 8 + x;
                    float size = float(G[i] * (1 / 6.0f));
                    if (!(size > 0.5f)) continue;
                    int mask = 0x3f;
                    if (size >= 15) {
                        size = 15;
                        if (G[i - 64] >= 90) mask &= ~0x01;
                        if (G[i + 64] >= 90) mask &= ~0x02;
                        if (G[i - 8] >= 90) mask &= ~0x04;
                        if (G[i + 8] >= 90) mask &= ~0x08;
                        if (G[i - 1] >= 90) mask &= ~0x20;
                        if (G[i + 2] >= 90) mask &= ~0x10;   // sic: x + 2
                    }
                    if (mask) cubes.cube(src, mesh, 30.0f * (x - 4), 30.0f * (y - 4), 30.0f * (z - 4), mask, size, 0.003, C);
                }
        float a = float(A[0] + A[1] - A[2]), bb = float(1.1 * (A[0] - A[1] + A[2])), cr = float(0.8 * (A[1] - A[0] + A[2]));
        mesh.pos.resize(src.size());
        transform(Mat34::part(a, bb, cr, 4.0f / 3.0f, 180.0f), src, mesh.pos);
        tiles(c, 0, [](int row, int) { return row >= 2 && row <= 3; }, [](int) { return 1.0f; });
        Engine& eng = engine(c);
        eng.begin(c, Window::at(c, t));
        eng.draw(mesh, 1.2f);
        eng.end();
    }
};

struct SlabsTest : Scene {
    Cubes cubes;
    EngineMesh mesh;
    std::vector<Vec3> src;
    float amp = 2.3f;
    int R[3] = {35, 35, 35};

    void init(Ctx& c) override {
        cubes.read(*c.exe);
        amp = c.exe->f32(0x428144);
        for (int k = 0; k < 3; k++) R[k] = c.exe->s32(0x428148 + 4 * k);
        mesh.shade = Shade::Corners;
        mesh.map = Tex::MapB;
    }
    void render(Ctx& c, double t) override {
        double T = c.clock(t);
        const double T9 = 1990.64;   // part 9's length in the capture: its colour phases continue
        double A10 = 0.002 * T, E = 0.3 * T;
        double C[3] = {0.02 * (T9 + T), 0.03 * (T9 + T), 0.04 * (T9 + T)};
        // 0x418fd2
        auto c3 = [&](double div, double ph) { return 8 * (amp * std::sin(E / div + ph) + 3); };
        auto c6 = [&](double div, double ph) { return 8 * (2 * amp * std::sin(E / div + ph) + 6); };
        double b[3][3] = {{c3(23, 0), c6(33, 1), c3(43, 2)}, {c3(30, 3), c6(43, 4), c3(24, 5)},
                          {c3(30, 6), c6(43, 7), c3(24, 8)}};
        float g[256] = {};
        for (int e = 1; e <= 4; e++)
            for (int bi = 1; bi <= 10; bi++)
                for (int a = 1; a <= 4; a++) g[36 * a + 6 * bi + e] = float(blobField(8 * a, 8 * bi, 8 * e, b, R));
        g[224] = float(E);   // the reads past the array: E10 and part 2's camera time (notes-parts3d 6.4)
        g[225] = 994.8f;
        // 0x4193b6
        src.clear();
        mesh.faces.clear();
        for (int a = 1; a <= 4; a++)
            for (int bi = 1; bi <= 10; bi++)
                for (int e = 1; e <= 4; e++) {
                    int k = 36 * a + 6 * bi + e;
                    float size = float(g[k] * (1 / 6.0f));
                    if (!(size > 0.5f)) continue;
                    int mask = 0x3f;
                    if (size >= 15) {
                        size = 15;
                        if (g[k - 36] >= 90) mask &= ~0x01;
                        if (g[k + 36] >= 90) mask &= ~0x02;
                        if (g[k - 6] >= 90) mask &= ~0x04;
                        if (g[k + 6] >= 90) mask &= ~0x08;
                        if (g[k - 1] >= 90) mask &= ~0x20;
                        if (g[k + 2] >= 90) mask &= ~0x10;
                    }
                    if (mask) cubes.cube(src, mesh, 30.0f * (e - 3), 30.0f * (bi - 3), 30.0f * (a - 3), mask, size, 0.009, C);
                }
        mesh.pos.resize(src.size());
        transform(Mat34::part(float(A10), float(1.1 * A10), float(0.8 * A10), 4.0f / 3.0f, 150.0f), src, mesh.pos);
        tiles(c, 24, [](int row, int) { return row >= 2 && row <= 5; }, [](int) { return 1.0f; });
        c.rect(0, 0, 320, 4, Vec3(0, 0, 0));
        float light = c.lastEvent(1011, t) > c.lastEvent(1012, t) ? 1.2f : 1.0f;
        Engine& eng = engine(c);
        eng.begin(c, Window::at(c, t));
        eng.draw(mesh, light);
        eng.end();
    }
};

}  // namespace

REGISTER_SCENE(BumpsTest, "bumps");
REGISTER_SCENE(BlobTest, "blob");
REGISTER_SCENE(CubesTest, "cubes");
REGISTER_SCENE(SlabsTest, "slabs");

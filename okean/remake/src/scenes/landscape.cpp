// The fifth part (0x38520 with its set-up 0x38469, okean/re/notes-landscapes.md sections 2 and 3): a 32 x 32 wireframe
// window over a generated 256 x 256 height map, light blue over dark blue noise, scrolling towards a camera that
// pitches, turns and rolls slowly; its edges are drawn near to far with a floating horizon, so the lines behind nearer
// ridges are hidden ("moving lightsource and shadow": there is no light, the dark areas are the hidden lines). It fades
// in over 256 callbacks and out from order 18, with the captions "hmmm", "moving lightsource and shadow" and "woww".
// Ports the set-up 0x38469 (midpoint displacement 0x374da, smoothing 0x377b2), the part's set-up 0x38520..0x38729
// (edges 0x38585, vertices 0x385f5), the main loop 0x3872e..0x38973 (projection 0x38331, the depth sort 0x3881b, the
// line drawer 0x3801c with its kernel 0x381ba and the floating horizon) and the callback 0x38985 (closed forms, 3.3).
//
// Time: n = the callbacks done before the draw that record r shows (n = 0 at the first drawn frame, ctx.start),
// fractional between records; the camera and the map position are evaluated continuously at n (the map window, its
// rows and columns popping in at the far and side edges, follows them). The palette is computed after the retrace
// wait: the fade after n + 1 callbacks, blended between whole callbacks.
//
// The floating horizon (3.5) is tested per point, as the original does, at 8 columns per original pixel: the lines are
// drawn near to far (by the sum of their ends' depths, sorted exactly instead of in steps of 8), and each point of a
// line is drawn where the 1-pixel column centred on it is still below it (its blob weighted by the part of the column
// that is), then that column is raised to it. So, as in the original: lines behind nearer ridges are hidden; the
// columns between the points of a flat line are not raised, so a farther line shows through a single nearer one (the
// dotted lines in the dark areas); a steep line, which the drawer runs down from its upper end, shows one point per
// pixel of x (the original draws only the first point in each pixel column, the others are below it); a line coming
// down from above the screen is run upwards from its lower end and drawn whole; points above the screen are clipped
// before the test. Where the original ties all this to its 320 columns, the remake ties it to each point (its column
// is centred on it), so the hidden parts and the gaps move with the lines instead of jumping from column to column.
// The extra +1 of the drawer's clipped path is left out.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"
#include "okean/text.h"
#include "scenes/landlines.h"

namespace {

constexpr uint32_t kPalette = 0x371da;
constexpr uint32_t kKernel = 0x381ba;    // the blob of 0x3801c's fast path
constexpr uint32_t kSeed = 0x37130;      // the generator's random state (part 5's noise continues from it)
constexpr uint32_t kEye = 0x371cc;       // 5600
constexpr uint32_t kFade = 0x36e2c;
constexpr uint32_t kZpos = 0x371d4;
constexpr int kGrid = 32, kVerts = kGrid * kGrid, kEdges = 2 * kGrid * (kGrid - 1);
constexpr int kFadeOrder = 18;           // the fade-out
constexpr double kPrinterLine = 830.0;   // the slow draw calls the printer at about line 205 of the record's refresh

// The set-up 0x38520 adds a start phase to each of the callback's angles (& 0xfff); the callback 0x38985 steps them.
struct Phase { uint32_t addr; int start; int step; };
constexpr Phase kP0{0x37180, 0x2b08, 9}, kP1{0x37184, 0x17e8, 5};      // pitch
constexpr Phase kQ0{0x37188, 0x17e8, 5}, kQ1{0x3718c, 0xe58, 3};       // yaw
constexpr Phase kR0{0x37190, 0x1320, 4}, kR1{0x37194, 0x2178, 7};      // roll
constexpr Phase kH{0x3715c, 0x2178, 7}, kW{0x37158, 0x8f70, 0x1e};    // height, sideways position

// ---------------------------------------------------------------- the height map (set-up 0x38469)
// 0x374da: recursive midpoint displacement over a 256 x 256 map that wraps in both directions (the original addresses
// it with a 16-bit index), "0 = not set yet" (displaced values are at least 30), then 0x377b2: an in-place smoothing in
// memory order (the left and upper neighbours already smoothed, the right one counted twice).
class HeightMap {
public:
    explicit HeightMap(uint32_t seed) : s_(seed) {
        subdivide(0, 0x100, 0, 0x100);
        for (int i = 0; i < 0x10000; i++) {
            // the right neighbour is read once as the high byte of a word: past the last cell that is the byte after
            // the map (0 in the original's allocation)
            int right = i < 0xffff ? m_[size_t(i + 1)] : 0;
            int e = m_[size_t(i)] + right + m_[size_t((i - 1) & 0xffff)] + m_[size_t((i + 1) & 0xffff)] +
                    m_[size_t((i - 256) & 0xffff)];
            m_[size_t(i)] = uint8_t((e * 0x33) >> 8);
        }
    }
    int at(int row, int col) const { return m_[size_t(((row & 0xff) << 8) | (col & 0xff))]; }

private:
    uint32_t rnd() {
        uint32_t x = s_ + 0x3b76;
        s_ = ((x >> 13) | (x << 19)) ^ 0x2cfb;
        return s_;
    }
    uint8_t& m(int row, int col) { return m_[size_t(((row & 0xff) << 8) | (col & 0xff))]; }
    uint8_t mid(int a, int b, int size) {
        int d = ((int(rnd() & 0xff) - 0x80) * size) >> 5;
        int v = ((a + b) >> 1) - d;
        return uint8_t(v <= 0x1e ? 0x1e : v >= 0xe1 ? 0xe1 : v);
    }
    void subdivide(int x0, int x1, int y0, int y1) {
        int size = x1 - x0, h = size >> 1;
        if (h == 0) return;
        int xm = x0 + h, ym = y0 + ((y1 - y0) >> 1);
        int c00 = m(y0, x0), c10 = m(y0, x1), c11 = m(y1, x1), c01 = m(y1, x0);
        if (m(ym, x0) == 0) m(ym, x0) = mid(c00, c01, size);
        if (m(ym, x1) == 0) m(ym, x1) = mid(c10, c11, size);
        if (m(y0, xm) == 0) m(y0, xm) = mid(c00, c10, size);
        if (m(y1, xm) == 0) m(y1, xm) = mid(c01, c11, size);
        m(ym, xm) = uint8_t((c00 + c10 + c01 + c11) >> 2);
        subdivide(x0, xm, y0, ym);
        subdivide(x0, xm, ym, y1);
        subdivide(xm, x1, ym, y1);
        subdivide(xm, x1, y0, ym);
    }

    std::array<uint8_t, 0x10000> m_{};
    uint32_t s_;
};

// ---------------------------------------------------------------- the floating horizon (3.5)
// Per column (8 per original pixel, x -16..336), the y of the highest line drawn so far; 200 at the start of a frame.
class Horizon {
public:
    static constexpr int kPerPixel = 8;
    static constexpr double kLeft = -16.0;
    static constexpr int kColumns = 352 * kPerPixel;

    void reset() { top_.assign(kColumns, 200.0f); }

    // 0x3801c: draws the line's blobs where it is above the horizon, then raises the horizon to it.
    void line(okean::Canvas& canvas, double x1, double y1, double x2, double y2) {
        landlines::Lattice L = landlines::lattice(x1, y1, x2, y2);
        if (!L.count) return;
        // the points drawn: the drawer's, or on a line steeper than a column per step that the drawer runs down (from
        // its upper end) the first in each pixel column, a point per pixel of x. A line coming down from above the
        // screen is run upwards from its lower end: each point is above the one before, all are drawn.
        double adx = std::fabs(L.dx), sx = L.dx, sy = L.dy;
        int m = L.count;
        if (adx < 1.0 && L.dy >= 0.0) {
            if (adx > 0.0) {
                m = int(std::floor((L.count - 1) * adx)) + 1;
                sx = L.dx / adx;
                sy = L.dy / adx;
            } else {
                m = 1;
            }
        }
        // each point: drawn where its pixel column (1 pixel wide, centred on it) is below it, the visible part of
        // the column weighting its blob; then the column is raised to it. The columns between the points of a flat
        // line stay as they were. A point above the screen is neither drawn nor raises anything.
        for (int j = 0; j < m; j++) {
            double px = L.x + sx * j, py = L.y + sy * j;
            if (!(py >= 0.0) || !landlines::reaches(px, py)) continue;
            int c0 = int(std::floor((px - 0.5 - kLeft) * kPerPixel)), seen = 0;
            for (int col = c0; col < c0 + kPerPixel; col++) {
                if (col < 0 || col >= kColumns) continue;
                float& top = top_[size_t(col)];
                if (py < top) {
                    seen++;
                    top = float(py);
                }
            }
            if (seen) canvas.blob(float(px), float(py), float(seen) / kPerPixel);
        }
    }

private:
    std::vector<float> top_;
};

// ---------------------------------------------------------------- the part
struct Camera {
    double pitch, yaw, roll, height, xpos, zpos;
};

class LandscapeScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& x = *c.exe;
        pal_ = okean::palette(x, kPalette);
        kern_ = okean::kernel(x, kKernel);
        eye_ = x.s32(kEye);
        fade0_ = x.s32(kFade);
        zpos0_ = double(x.s32(kZpos)) - 0x9900;   // the set-up's subtraction, not masked
        auto phase = [&](const Phase& p) { return double((x.s32(p.addr) + p.start) & 0xfff); };
        p0_ = phase(kP0); p1_ = phase(kP1); q0_ = phase(kQ0); q1_ = phase(kQ1);
        r0_ = phase(kR0); r1_ = phase(kR1); h0_ = phase(kH); w0_ = phase(kW);
        map_ = std::make_unique<HeightMap>(x.u32(kSeed));
        // 0x38585: the 992 edges along the rows, then the 992 across them, column by column
        for (int r = 0; r < kGrid; r++)
            for (int col = 0; col < kGrid - 1; col++) edges_.push_back({kGrid * r + col, kGrid * r + col + 1});
        for (int col = 0; col < kGrid; col++)
            for (int i = 0; i < kGrid - 1; i++) edges_.push_back({col + kGrid * i, col + kGrid * (i + 1)});
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        const double n = std::max(0.0, c.callbacks(r));
        canvas.begin(c);
        canvas.noise(c, 16, uint32_t(std::floor(r)));
        canvas.setKernel(kern_);

        // 0x38331: the 1024 vertices of the window
        const Camera cam = camera(n);
        for (int k = 0; k < kVerts; k++) {
            Vertex& v = verts_[size_t(k)];
            v = project(cam, k);
            if (v.z <= 0.0) {
                // behind the eye: the projection keeps the screen position of the last drawn frame in which the vertex
                // was in front (3.4; in the capture drawn frames 181..183)
                for (int m = int(std::ceil(n)) - 1, i = 0; m >= 0 && i < 64; m--, i++) {
                    Vertex u = project(camera(m), k);
                    if (u.z > 0.0) {
                        v.x = u.x;
                        v.y = u.y;
                        break;
                    }
                }
            }
        }
        // 0x387c1 and 0x3881b: the edges in front of the eye, near to far by the sum of their ends' depths (equal sums
        // in edge order)
        order_.clear();
        for (int e = 0; e < kEdges; e++) {
            double s = verts_[size_t(edges_[size_t(e)].a)].z + verts_[size_t(edges_[size_t(e)].b)].z;
            if (s > 0.0) order_.push_back({s, e});
        }
        std::sort(order_.begin(), order_.end(),
                  [](const Key& p, const Key& q) { return p.s < q.s || (p.s == q.s && p.e < q.e); });
        horizon_.reset();
        for (const Key& key : order_) {
            const Vertex& a = verts_[size_t(edges_[size_t(key.e)].a)];
            const Vertex& b = verts_[size_t(edges_[size_t(key.e)].b)];
            horizon_.line(canvas, a.x, a.y, b.x, b.y);
        }
        canvas.flush(c);

        // the palette x fade / 256, the fade after n + 1 callbacks
        Steps st = stepsOf(n);
        double f = fadeAt(c, st.n + 1) * (1.0 - st.frac) + fadeAt(c, st.n + 2) * st.frac;
        std::array<Vec3, 256> pal = okean::scaled(pal_, f, 256.0);
        canvas.resolve(c, pal);
        c.shared<okean::Printer>().drawCaption(c, r, pal, kPrinterLine);
    }

private:
    struct Vertex { double x = 0, y = 0, z = 0; };
    struct Edge { int a, b; };
    struct Key { double s; int e; };

    // The callback's closed forms after n callbacks (3.3), continuous in n.
    Camera camera(double n) const {
        auto s = [](double a) { return okean::Sine::s(a); };
        Camera k;
        k.pitch = ((s(p0_ + kP0.step * n) + s(p1_ + kP1.step * n)) / 4.0 + 0x32c) / 2.0;
        k.yaw = (s(q0_ + kQ0.step * n) + s(q1_ + kQ1.step * n)) / 2.0;
        k.roll = (s(r0_ + kR0.step * n) + s(r1_ + kR1.step * n)) / 4.0;
        k.height = s(h0_ + kH.step * n) / 8.0 + 0x464;
        k.xpos = s(w0_ + kW.step * n) / 16.0 + 0x5a00;   // map column x 256
        k.zpos = zpos0_ - 32.0 * n;                       // map row x 256
        return k;
    }

    // Vertex k = 32 row + column of the window: the map cell (Z - row, X - column) at x = -4096 + 256 column + fx,
    // z = -4096 + 256 row + fz (X, Z and fx, fz: the map position's whole cells and fractions), its height
    // (m - 128) x 32; turned by the yaw, pitched, rolled, 5600 units in front of the eye, projected.
    Vertex project(const Camera& cam, int k) const {
        int row = k / kGrid, col = k % kGrid;
        double X = std::floor(cam.xpos / 256.0), Z = std::floor(cam.zpos / 256.0);
        double fx = cam.xpos - 256.0 * X, fz = cam.zpos - 256.0 * Z;
        int h = map_->at(int(Z) - row, int(X) - col);
        double x = -4096.0 + 256.0 * col + fx, y = (h - 0x80) * 32.0 + cam.height, z = -4096.0 + 256.0 * row + fz;
        landlines::rotate(cam.yaw, x, z);
        landlines::rotate(cam.pitch, y, z);
        landlines::rotate(cam.roll, x, y);
        z += eye_;
        // (behind the eye the caller looks for an earlier projection; failing that, far out in the vertex's direction)
        double f = 256.0 / std::max(z, 1.0);
        Vertex v;
        v.x = x * f + 160.0;
        v.y = y * f + 100.0;
        v.z = z;
        return v;
    }

    // The fade after k callbacks (0x38985): one up per callback to 256, one down per callback in order 18.
    double fadeAt(Ctx& c, int k) {
        if (c.start != fadeStart_) {
            fadeStart_ = c.start;
            fades_.assign(1, fade0_);
            int f = fade0_;
            int last = int(std::ceil(c.end - c.start)) + 3;
            for (int j = 1; j <= last; j++) {
                if (c.positionAtCallback(j) / 64 != kFadeOrder) {
                    if (f != 0x100) f++;
                } else if (f) {
                    f--;
                }
                fades_.push_back(f);
            }
        }
        return fades_[size_t(std::clamp(k, 0, int(fades_.size()) - 1))];
    }

    std::array<Vec3, 256> pal_{};
    std::array<uint8_t, 64> kern_{};
    std::unique_ptr<HeightMap> map_;
    std::vector<Edge> edges_;
    double eye_ = 0, zpos0_ = 0, p0_ = 0, p1_ = 0, q0_ = 0, q1_ = 0, r0_ = 0, r1_ = 0, h0_ = 0, w0_ = 0;
    int fade0_ = 0;
    std::array<Vertex, kVerts> verts_{};
    std::vector<Key> order_;
    Horizon horizon_;
    double fadeStart_ = -1;
    std::vector<int> fades_;
};

}  // namespace

REGISTER_SCENE(LandscapeScene, "landscape");

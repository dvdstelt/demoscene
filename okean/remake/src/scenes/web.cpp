// The third part (0x3554b, okean/re/notes-landscapes.md section 1): a 16 x 16 wireframe of a canyon floor between two
// walls, glowing yellow-green over dark noise, flown over along the canyon with a sway, a heading that turns left and
// right and a bank into the turns. The grid slides in from behind the camera; from 10:16 the palette cross-fades to
// red and the grid flies on past the camera without new rows. Ports the set-up 0x3554b..0x3558b, the main loop
// 0x35590..0x357a7 with the frame 0x353be and the line drawer 0x350c8 (its kernel 0x35252), and the callback 0x357b3
// (closed forms, 1.3).
//
// Time: n = the callbacks done before the draw that record r shows (n = 0 at the first drawn frame, ctx.start),
// fractional between records; the camera, the scroll and the slide are evaluated continuously at n. The window of
// map rows moves on by one row at every eighth callback (whole callbacks, as the original) and stops at the first
// callback that sees 10:16. The bank uses the heading of 32 drawn frames earlier: the remake draws every refresh, so
// 32 callbacks (a frame-rate dependent effect of the original, as the capture shows it). The palette is computed
// before the retrace wait: the cross-fade after n callbacks, blended between whole callbacks.
//
// Kept from the original: the blobs' top-left corners on the line (the glow 3.5 pixels right of and below it), the
// vertices behind the eye thrown out by (1 - z) and the spokes drawn to them from the vertices in front (landlines.h),
// the popping rows at the window's ends. Left out: the truncations of the integer rotations and projection (the
// original's lines sit up to a pixel up and towards the centre), the 8-bit step counter of off-screen lines, the
// whole-blob clip at x 311 / y 191, and the truncation of the cross-fade's (green w1 + red w2) >> 7.
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"
#include "okean/text.h"
#include "scenes/landlines.h"

namespace {

constexpr uint32_t kGreen = 0x347a8, kRed = 0x34aa8;   // palettes
constexpr uint32_t kMap = 0x2f2b0;                     // 200 rows x 16 heights
constexpr uint32_t kVertices = 0x2ffa8;                // 256 x (x, y, z) dwords; x, y static
constexpr uint32_t kEdges = 0x30ba8;                   // 480 x (a, b) dwords
constexpr uint32_t kEye = 0x2ff48, kPitch = 0x2ff50, kTilt = 0x2ff54;
constexpr uint32_t kPhaseA = 0x2ff30, kPhaseB = 0x2ff34, kSlide = 0x2ff40, kW1 = 0x2f21c, kW2 = 0x2f220;
constexpr uint32_t kYaw = 0x2ff58, kHistory = 0x2f228;   // the heading and its history of 33 drawn frames
constexpr uint32_t kKernel = 0x35252;                  // the blob of 0x350c8's fast path
constexpr int kRows = 200, kCols = 16, kVerts = 256, kEdgeCount = 480, kHistoryLen = 33;
constexpr int kFadeOrder = 10, kFadeRow = 16;          // 10:16: the cross-fade and the fly-off

struct Edge { int a, b; };

class WebScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& x = *c.exe;
        green_ = okean::palette(x, kGreen);
        red_ = okean::palette(x, kRed);
        kern_ = okean::kernel(x, kKernel);
        map_ = x.bytes(kMap, kRows * kCols);
        for (int k = 0; k < kVerts; k++) {
            vx_[size_t(k)] = x.s32(kVertices + 12u * uint32_t(k));
            vy_[size_t(k)] = x.s32(kVertices + 12u * uint32_t(k) + 4);
        }
        for (int i = 0; i < kEdgeCount; i++) {
            uint32_t a = kEdges + 8u * uint32_t(i);
            edges_[size_t(i)] = {int(x.u32(a) & 0xff), int(x.u32(a + 4) & 0xff)};
        }
        eye_ = x.s32(kEye);
        pitch_ = x.s32(kPitch);
        tilt_ = x.s32(kTilt);
        a0_ = x.s32(kPhaseA);
        b0_ = x.s32(kPhaseB);
        slide0_ = x.s32(kSlide);
        w1_ = x.s32(kW1);
        w2_ = x.s32(kW2);
        yaw0_ = signedAngle(x.s32(kYaw));
        for (int i = 0; i < kHistoryLen; i++) hist_[size_t(i)] = signedAngle(x.s32(kHistory + 4u * uint32_t(i)));
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        const double n = std::max(0.0, c.callbacks(r));
        const int whole = int(std::floor(n));
        const int n1 = flyOff(c);
        canvas.begin(c);
        canvas.noise(c, 16, uint32_t(std::floor(r)));
        canvas.setKernel(kern_);

        // the callback's state (1.3): a row of the map comes on at every callback 8k + 1 until 10:16; the rows move 32
        // units per callback (scroll), the grid slides in over the first 64 callbacks
        const int window = (std::min(whole, n1 - 1) + 7) / 8;
        const double scroll = 256.0 * window - 32.0 * n;
        const double slide = std::min(0.0, slide0_ + 64.0 * n);
        const double a = a0_ + 8.0 * n, b = b0_ + 19.0 * n;
        // the main loop (1.4): sway, heading, and the bank from the heading of 32 drawn frames before
        const double sway = (okean::Sine::s(a + 0xc00) + okean::Sine::s(b + 0xc00)) / 2.0;
        const double yaw = yawAt(n);
        const double roll = okean::Sine::s(yawAt(n - 32.0) - yaw) / 2.0;

        // 0x353be: the 256 vertices, the walls (columns 0 and 15) at a fixed height
        std::array<double, 2 * kVerts> P{};
        for (int k = 0; k < kVerts; k++) {
            int col = k / 16, row = k % 16;
            double x = vx_[size_t(k)], y = vy_[size_t(k)] + scroll + slide;
            double z = (col == 0 || col == 15) ? 0x800
                                               : map_[size_t(16 * ((window + row) % kRows) + col)] * 8.0 - 0x400;
            landlines::rotate(tilt_, y, z);   // the map's y becomes depth, its height -y
            landlines::rotate(roll, x, y);
            x -= sway;
            landlines::rotate(yaw, x, z);
            landlines::rotate(pitch_, y, z);
            z += eye_;
            // behind the eye: thrown out by (1 - z), drawn as spokes from the vertices in front
            double f = z > 0.0 ? 256.0 / z : 1.0 - z;
            P[size_t(2 * k)] = x * f + 160.0;
            P[size_t(2 * k + 1)] = y * f + 100.0;
        }
        // 0x350c8 for the 480 edges: the glows add up, no hidden lines
        for (const Edge& e : edges_) {
            landlines::Lattice L = landlines::lattice(P[size_t(2 * e.a)], P[size_t(2 * e.a + 1)], P[size_t(2 * e.b)],
                                                      P[size_t(2 * e.b + 1)]);
            for (int i = 0; i < L.count; i++) {
                double px = L.x + L.dx * i, py = L.y + L.dy * i;
                if (landlines::reaches(px, py)) canvas.blob(float(px), float(py));
            }
        }
        canvas.flush(c);

        // the cross-fade green -> red from 10:16, 128 callbacks
        Steps st = stepsOf(n);
        double w2 = fadeW2(st.n, n1) * (1.0 - st.frac) + fadeW2(st.n + 1, n1) * st.frac;
        double t = w2 / double(w1_ + w2_);
        std::array<Vec3, 256> pal{};
        for (size_t i = 0; i < 256; i++) pal[i] = green_[i] + (red_[i] - green_[i]) * float(t);
        canvas.resolve(c, pal);
        c.shared<okean::Printer>().drawCaption(c, r, pal);   // its pages in this part are empty
    }

private:
    static double signedAngle(int32_t v) {
        v &= 0xfff;
        return v >= 2048 ? v - 4096 : v;
    }

    // The heading the draw after m callbacks computes, ((S[a] + S[b]) >> 2) & 0xfff; before the first draw the yaw
    // and the history the part starts with (the draw of m callbacks banks against the heading of m - 32).
    double yawAt(double m) const {
        auto at = [&](int k) {
            if (k >= 0) return (okean::Sine::s(a0_ + 8.0 * k) + okean::Sine::s(b0_ + 19.0 * k)) / 4.0;
            if (k == -1) return yaw0_;
            return hist_[size_t(std::max(k + kHistoryLen, 0))];
        };
        if (m >= 0.0) return (okean::Sine::s(a0_ + 8.0 * m) + okean::Sine::s(b0_ + 19.0 * m)) / 4.0;
        Steps s = stepsOf(m);
        return at(s.n) * (1.0 - s.frac) + at(s.n + 1) * s.frac;
    }

    // w2 after k callbacks: from the first callback that sees 10:16 (n1) w1 hands one step per callback to w2.
    double fadeW2(int k, int n1) const { return w2_ + std::clamp(k - n1 + 1, 0, w1_); }

    // The first callback that sees 10:16 (order 10, row >= 16): the cross-fade starts and the map window stops.
    int flyOff(Ctx& c) {
        if (c.start != flyOffStart_) {
            flyOffStart_ = c.start;
            flyOff_ = INT_MAX / 2;
            int last = int(std::ceil(c.end - c.start)) + 2;
            for (int k = 1; k <= last; k++) {
                int pos = c.positionAtCallback(k);
                if (pos / 64 == kFadeOrder && pos % 64 >= kFadeRow) {
                    flyOff_ = k;
                    break;
                }
            }
        }
        return flyOff_;
    }

    std::array<Vec3, 256> green_{}, red_{};
    std::array<uint8_t, 64> kern_{};
    std::vector<uint8_t> map_;
    std::array<double, kVerts> vx_{}, vy_{};
    std::array<Edge, kEdgeCount> edges_{};
    std::array<double, kHistoryLen> hist_{};
    double eye_ = 0, pitch_ = 0, tilt_ = 0, a0_ = 0, b0_ = 0, slide0_ = 0, yaw0_ = 0;
    int w1_ = 128, w2_ = 0;
    double flyOffStart_ = -1;
    int flyOff_ = INT_MAX / 2;
};

}  // namespace

REGISTER_SCENE(WebScene, "web");

// The sixth part (0x3c8da, okean/re/notes-morph-star.md section 3): a tube of 16 rings x 8 points along the view axis
// whose rings stream towards the camera, a new ring with new radii at the far end every 8 callbacks; the far rings
// overlap into the white star in the middle, the near ones make the magenta web, and the edges to the vertices behind
// the camera, thrown far out, make its spokes. Glowing blobs over dark magenta noise; the caption "the sea robot of
// love" in order 22. Ports the set-up 0x3c8da (palette 0x3bb24, edges 0x39c24, the radius history 0x39a04), the main
// loop 0x3c94b (tube 0x3c955, angles 0x3c9e7, projection 0x3c7b5, the line drawer 0x3c46e with its kernel 0x3c710,
// the palette x fade / 16 of 0x3c128, the shared printer 0x4cacc) and the callback 0x3cad3 (closed forms, 3.4).
//
// Time: cb = the callbacks before the draw that record r shows (record F shows 0); the tube moves 128 units per
// callback, evaluated at the fractional cb. Ring s (s = 1, 2, ...) is created by callback 8s - 7 at the far end and
// sits at y = 13312 + 1024 s - 128 cb; the 16 rings of a state are the newest 16 created by its whole callbacks
// (closed form of the history 0x39a04: ring i holds ring s = ceil(n / 8) - 15 + i, or the file's radius before
// ring 1). The palette is computed before the retrace wait: the fade after the record's own callbacks, blended between
// whole callbacks.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"
#include "okean/text.h"
#include "scenes/blobline.h"

namespace {

constexpr uint32_t kPalette = 0x3bb24;
constexpr uint32_t kKernel = 0x3c710;
constexpr uint32_t kEdgeList = 0x39c24;     // 248 x 8 bytes: vertex a at +0, b at +4
constexpr uint32_t kHistory = 0x39a04;      // 16 rings x 8 radii
constexpr uint32_t kRadiusPhases = 0x38dd0; // 8 dwords
constexpr uint32_t kPitchPhases = 0x38dc0;  // 3 dwords: [0x38dc0], [0x38dc4], [0x38dc8]
constexpr uint32_t kYawPhases = 0x38da4;    // 3 dwords: [0x38da4], [0x38da8], [0x38dac]
constexpr uint32_t kDepth = 0x38db8;        // the z offset (0x400)
constexpr int kRings = 16, kPoints = 8, kEdges = 248;
// the callback's increments of the radius phases (0x3cc80..), the pitch and the yaw phases
constexpr int kRadiusInc[kPoints] = {0x29, 0x2b, 0x2c, 0x29, 0x2d, 0x2c, 0x2a, 0x2b};
constexpr double kPitchInc[3] = {0x10, -6, 0x17};
constexpr double kYawInc[3] = {-0xd, 0x13, 8};
constexpr double kDeg = 2.0 * M_PI / 4096.0;   // one table step in radians
constexpr double kAmp = 1022.0 / 1024.0;       // a product with a table value, shifted by 10

struct Edge { int a, b; };

class StarScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& x = *c.exe;
        pal_ = okean::palette(x, kPalette);
        kern_ = okean::kernel(x, kKernel);
        for (int e = 0; e < kEdges; e++)
            edges_.push_back({x.u8(kEdgeList + 8u * uint32_t(e)), x.u8(kEdgeList + 8u * uint32_t(e) + 4u)});
        for (int i = 0; i < kRings * kPoints; i++) hist0_[size_t(i)] = x.s32(kHistory + 4u * uint32_t(i));
        for (int k = 0; k < kPoints; k++) ph0_[size_t(k)] = x.s32(kRadiusPhases + 4u * uint32_t(k));
        for (int k = 0; k < 3; k++) {
            pitch0_[size_t(k)] = x.s32(kPitchPhases + 4u * uint32_t(k));
            yaw0_[size_t(k)] = x.s32(kYawPhases + 4u * uint32_t(k));
        }
        depth_ = x.s32(kDepth);
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        fades(c);
        double cb = std::max(0.0, c.callbacks(r));
        int n = int(std::floor(cb));
        canvas.begin(c);
        canvas.noise(c, 32, uint32_t(std::floor(r)));
        canvas.setKernel(kern_);

        // the angles of 0x3c9e7 from the callback's phases: the tube's axis pitched into the view direction
        // (A = 1024 +- 39 degrees) and yawed (B = +- 39 degrees)
        auto sn = [](double a) { return okean::Sine::s(a); };
        const double* p = pitch0_.data();
        const double* q = yaw0_.data();
        double A = 1024.0 + ((sn(p[0] + kPitchInc[0] * cb) / 2.0 + sn(p[1] + kPitchInc[1] * cb)) / 2.0 +
                             sn(p[2] + kPitchInc[2] * cb)) / 4.0;
        double B = ((sn(q[0] + kYawInc[0] * cb) / 2.0 + sn(q[1] + kYawInc[1] * cb)) / 2.0 +
                    sn(q[2] + kYawInc[2] * cb)) / 4.0;
        double sa = std::sin(A * kDeg) * kAmp, ca = std::cos(A * kDeg) * kAmp;
        double sb = std::sin(B * kDeg) * kAmp, cbb = std::cos(B * kDeg) * kAmp;

        // the tube (0x3c955): ring i of the state holds ring s = m - 15 + i (m = ceil(n / 8) rings created); y moves
        // continuously: -0x800 + 0x400 i + scroll, scroll = 1024 m - 128 cb (= -128 cb mod 1024 at whole callbacks)
        int m = (n + 7) / 8;
        double scroll = 1024.0 * m - 128.0 * cb;
        std::array<double, kRings * kPoints * 2> P{};
        for (int i = 0; i < kRings; i++) {
            int s = m - 15 + i;
            double y = -2048.0 + 1024.0 * i + scroll;
            for (int k = 0; k < kPoints; k++) {
                // ring s was made by callback 8s - 7 from the phases then: 1024 + T[ph0 + inc 8 (s - 1)] / 2
                double rad = s >= 1 ? 1024.0 + sn(ph0_[size_t(k)] + double(kRadiusInc[k]) * 8.0 * (s - 1)) / 2.0
                                    : double(hist0_[size_t(8 * (i + m) + k)]);
                double a = 512.0 * k;
                double x = sn(a) * rad / 256.0, z = rad * okean::Sine::c(a) / 256.0;
                // 0x3c7b5: pitch (y, z) by A, yaw (x, z) by B, depth + 0x400
                double y1 = y * ca - sa * z, z1 = z * ca + sa * y;
                double x1 = x * cbb - sb * z1, z2 = z1 * cbb + sb * x;
                double zz = z2 + depth_;
                double X, Y;
                if (zz > 0.0) {
                    // the original's smallest positive depth is 1 (scale 64); between 0 and 1 the scale grows on
                    // without bound, kept finite here (the vertex is millions of pixels out either way)
                    double f = 64.0 / std::max(zz, 1.0 / 1024.0);
                    X = x1 * f;
                    Y = y1 * f;
                } else {
                    // behind the camera: thrown out along (x1, y1) by 1 - z; the edges to it are the web's spokes
                    X = x1 * (1.0 - zz);
                    Y = y1 * (1.0 - zz);
                }
                P[size_t(2 * (8 * i + k))] = X + 160.0;
                P[size_t(2 * (8 * i + k) + 1)] = Y + 100.0;
            }
        }
        for (const Edge& e : edges_)
            okean_scenes::blobLine(canvas, P[size_t(2 * e.a)], P[size_t(2 * e.a + 1)], P[size_t(2 * e.b)],
                                   P[size_t(2 * e.b + 1)]);
        canvas.flush(c);

        // the palette x fade / 16 (0x3c128), the fade after the record's callbacks, blended between them; the
        // callbacks after the last frame (which raise the fade again in order 23) are never shown
        int last = std::max(0, int(c.end - c.start) - 1);
        Steps st = stepsOf(std::min(cb, double(last)));
        double f = fadeAt(st.n) * (1.0 - st.frac) + fadeAt(std::min(st.n + 1, last)) * st.frac;
        std::array<Vec3, 256> shown = okean::scaled(pal_, f, 16.0);
        std::array<Vec3, 256> ramp = shown;
        ramp[254] = ramp[255];   // the font's grey is not part of the ramp
        canvas.resolve(c, ramp);
        c.shared<okean::Printer>().drawCaption(c, r, shown, 591.0);   // its printer call: line ~591
    }

private:
    // The fade after k callbacks (callback 0x3cad3): +1 up to 16 outside order 22, -1 down to 0 from order 22 row 32.
    void fades(Ctx& c) {
        if (fadeStart_ == c.start && !fade_.empty()) return;
        fadeStart_ = c.start;
        int count = std::max(1, int(c.end - c.start) + 2);
        fade_.assign(size_t(count), 0);
        int fade = 0;
        for (int j = 1; j < count; j++) {
            int pos = c.positionAtCallback(j);
            int order = pos / 64, row = pos % 64;
            if (order != 22) { if (fade != 16) fade++; }
            else if (row >= 32) { if (fade != 0) fade--; }
            fade_[size_t(j)] = fade;
        }
    }
    double fadeAt(int k) const {
        if (fade_.empty()) return 0.0;
        return double(fade_[size_t(std::clamp(k, 0, int(fade_.size()) - 1))]);
    }

    std::array<Vec3, 256> pal_{};
    std::array<uint8_t, 64> kern_{};
    std::vector<Edge> edges_;
    std::array<int, kRings * kPoints> hist0_{};
    std::array<double, kPoints> ph0_{};
    std::array<double, 3> pitch0_{}, yaw0_{};
    double depth_ = 1024.0;
    std::vector<int> fade_;
    double fadeStart_ = -1.0;
};

}  // namespace

REGISTER_SCENE(StarScene, "star");

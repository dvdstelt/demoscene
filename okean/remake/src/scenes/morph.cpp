// The fourth part (0x367e5, okean/re/notes-morph-star.md section 2): a sphere of 32 rings x 16 meridians whose radius
// two travelling sine waves ripple, tumbling and coming close, drawn as rows of glowing blobs over dark red noise.
// Ports the set-up 0x367e5..0x3693e (edges 0x3683a, sphere 0x368ba), the main loop 0x36943 (morph 0x36978, rotate and
// project 0x366bd, the line drawer 0x363d0 with its kernel 0x36618) and the callback 0x36af4 (closed forms, 2.5).
//
// Time: c = the callbacks whose result record r shows (c = 0 at the first drawn frame, ctx.start); the motion is
// evaluated at the fractional c. The edges appear 8 per callback (rings first, then meridians). The fade runs in
// order 15 (palette x fade / 64, the fade after the callback of the record's refresh), with its jump back to 47 in
// row 5; the part ends when it reaches 0 (timeline.cpp).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "music.h"
#include "okean/glow.h"

namespace {

constexpr uint32_t kPalette = 0x35db0;
constexpr uint32_t kKernel = 0x36618;
constexpr int kRings = 32, kPoints = 16, kEdges = 992;
constexpr double kDeg = 2.0 * M_PI / 4096.0;   // one table step in radians
constexpr double kAmp = 1022.0 / 1024.0;       // a product with a table value, shifted by 10

struct Edge { int a, b; };

class MorphScene : public Scene {
public:
    void init(Ctx& c) override {
        pal_ = okean::palette(*c.exe, kPalette);
        kern_ = okean::kernel(*c.exe, kKernel);
        // 0x3683a: 512 ring edges ring by ring, then 480 meridian edges column by column (ring 0 not joined)
        for (int r = 0; r < kRings; r++)
            for (int i = 0; i < kPoints; i++) edges_.push_back({16 * r + i, 16 * r + ((i + 1) & 15)});
        for (int k = 0; k < kPoints; k++)
            for (int j = 0; j < 30; j++) edges_.push_back({k + 16 * (j + 1), k + 16 * (j + 2)});
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        double cb = std::max(0.0, c.callbacks(r));
        canvas.begin(c);
        canvas.noise(c, 16, uint32_t(std::floor(r)));
        canvas.setKernel(kern_);

        // the callback's closed forms (2.5) at the fractional callback count
        const double S = 1022.0;
        auto sn = [](double a) { return okean::Sine::s(a); };
        double a64 = 4.0 * cb;
        double a60 = (sn(14.0 * cb) + sn(11.0 * cb)) / 4.0;
        double a68 = (sn(5.0 * cb) + sn(12.0 * cb)) / 4.0;
        double p4c = 28.0 * cb, p50 = -25.0 * cb;
        double zoff = cb >= 1.0 ? sn(7.0 * cb) + 2960.0 : 2900.0;
        int nedge = std::min(8 * int(std::floor(cb)) + 8, kEdges);

        // the sphere (0x368ba) rippled by the two waves (0x36978), rotated and projected (0x366bd)
        double c64 = std::cos(a64 * kDeg) * kAmp, s64 = std::sin(a64 * kDeg) * kAmp;
        double c60 = std::cos(a60 * kDeg) * kAmp, s60 = std::sin(a60 * kDeg) * kAmp;
        double c68 = std::cos(a68 * kDeg) * kAmp, s68 = std::sin(a68 * kDeg) * kAmp;
        std::array<float, kRings * kPoints * 2> P{};
        for (int ring = 0; ring < kRings; ring++) {
            double phi = 64.0 * ring;               // table steps
            double sp = S * std::sin(phi * kDeg), cp = S * std::cos(phi * kDeg);
            double e = sn(p4c + 384.0 * ring) / 8.0 + 512.0;
            for (int k = 0; k < kPoints; k++) {
                double th = 256.0 * k;
                double x = S * std::sin(th * kDeg) * sp / 1024.0, y = cp, z = S * std::cos(th * kDeg) * sp / 1024.0;
                double s = (sn(p50 + 256.0 * k) / 16.0 + e) / 512.0;
                x *= s; y *= s; z *= s;
                double x1 = x * c64 - z * s64, z1 = z * c64 + x * s64;
                double y1 = y * c60 - z1 * s60, z2 = z1 * c60 + y * s60;
                double x2 = x1 * c68 - y1 * s68, y2 = y1 * c68 + x1 * s68;
                double zz = std::max(1.0, z2 + zoff);
                P[size_t(2 * (16 * ring + k))] = float(256.0 * x2 / zz + 160.0);
                P[size_t(2 * (16 * ring + k) + 1)] = float(256.0 * y2 / zz + 100.0);
            }
        }
        for (int i = 0; i < nedge; i++) {
            const Edge& e = edges_[size_t(i)];
            canvas.line(P[size_t(2 * e.a)], P[size_t(2 * e.a + 1)], P[size_t(2 * e.b)], P[size_t(2 * e.b + 1)]);
        }
        canvas.flush(c);

        // the fade (order 15), stepped per callback, blended between the records
        Steps st = stepsOf(r);
        double f = fade(c, st.n) * (1.0 - st.frac) + fade(c, st.n + 1) * st.frac;
        canvas.resolve(c, okean::scaled(pal_, f, 64.0));
    }

private:
    // The fade after the callback of record k's refresh (2.5): 64 until order 15; in order 15 every callback
    // decrements it, and every callback of row 5 first sets it to 48.
    double fade(Ctx& c, int k) const {
        const Soundtrack& m = *c.music;
        double t15 = m.timeOfPosition(15 * 64);
        int first = int(std::ceil(t15 * kRefreshHz - 502.0 / 625.0));   // the first callback that sees order 15
        if (k < first) return 64.0;
        int fade = 64;
        for (int j = first; j <= k; j++) {
            int pos = m.positionAtTime((j + 502.0 / 625.0) / kRefreshHz);
            if (pos / 64 != 15) continue;
            if (pos % 64 == 5) fade = 48;
            if (fade) fade--;
        }
        return fade;
    }

    std::array<Vec3, 256> pal_{};
    std::array<uint8_t, 64> kern_{};
    std::vector<Edge> edges_;
};

}  // namespace

REGISTER_SCENE(MorphScene, "morph");

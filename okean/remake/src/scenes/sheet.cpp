// The seventh part (0x5334f, okean/re/notes-sheet.md): a 16 x 16 wireframe sheet, tilted by 225 degrees and swinging
// on two axes, whose height is a ring wave of the squared distance from vertex (8, 8) running outwards with an
// amplitude that grows from 0; lavender glowing blobs over dark noise, the captions of orders 24..26; from order 27
// the sheet falls off the bottom while the palette cross-fades to a grey one, and the part ends in bright grey noise.
// Ports the part 0x5334f (heights 0x5339b, swing angles, the palette fade and cross-fade 0x53493, the shared printer
// 0x4cacc), the projection and edge loop 0x531fe (grid 0x4decd, edges 0x4eacd, tilt immediate 0x53441), the line
// drawer 0x52fed with its stamp 0x53158, and the callback 0x5355a (closed forms, section 8).
//
// Time: cb = the callbacks before the draw that record r shows (record F shows 0); the heights, swings and the fall
// are evaluated at the fractional cb. The palette is written before the retrace wait by the next draw: record F + n
// shows palette level b after n + 1 callbacks (blended between whole callbacks; the last record keeps its own).
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

constexpr uint32_t kPalette1 = 0x526cd;
constexpr uint32_t kPalette2 = 0x529cd;     // read as [esi + 0x300] at 0x534e9
constexpr uint32_t kStamp = 0x53158;
constexpr uint32_t kGrid = 0x4decd;         // 256 x (x, y, z) int32
constexpr uint32_t kEdgeList = 0x4eacd;     // 480 x (a, b) int32
constexpr uint32_t kTilt = 0x53441;         // the immediate of `mov dword [0x4de40], 0xa00`
constexpr uint32_t kCamera = 0x4de14;       // depth added after the rotation (2048)
constexpr uint32_t kLevel = 0x4db54;        // palette level b in the file (1)
constexpr int kVerts = 256, kEdges = 480, kSide = 16;
constexpr int kAmpMax = 256;                // the callback: amp + 1 up to 256
constexpr int kEndOrder = 27;
constexpr double kDeg = 2.0 * M_PI / 4096.0;   // one table step in radians
constexpr double kAmp = 1022.0 / 1024.0;       // a product with a table value, shifted by 10

struct Edge { int a, b; };

class SheetScene : public Scene {
public:
    void init(Ctx& c) override {
        const OkeanExe& x = *c.exe;
        pal1_ = okean::palette(x, kPalette1);
        pal2_ = okean::palette(x, kPalette2);
        kern_ = okean::kernel(x, kStamp);
        for (int k = 0; k < kVerts; k++) {
            gx_[size_t(k)] = x.s32(kGrid + 12u * uint32_t(k));
            gy_[size_t(k)] = x.s32(kGrid + 12u * uint32_t(k) + 4u);
        }
        for (int e = 0; e < kEdges; e++)
            edges_.push_back({x.s32(kEdgeList + 8u * uint32_t(e)), x.s32(kEdgeList + 8u * uint32_t(e) + 4u)});
        tilt_ = x.s32(kTilt);
        camera_ = x.s32(kCamera);
        level0_ = x.s32(kLevel);
    }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        callbacks(c);
        double cb = std::max(0.0, c.callbacks(r));
        canvas.begin(c);
        canvas.noise(c, 32, uint32_t(std::floor(r)));
        canvas.setKernel(kern_);

        // the callback's counters at the fractional cb
        auto sn = [](double a) { return okean::Sine::s(a); };
        double amp = std::min(cb, double(kAmpMax));
        double phase = -32.0 * cb;
        double beta = 3.0 * sn(8.0 * cb) / 8.0;     // [0x4de44] from [0x4dbf4]: swing about y, +-33.7 degrees
        double alpha = 3.0 * sn(7.0 * cb) / 8.0;    // [0x4de68] from [0x4de6c]: swing about z
        double m = std::max(0.0, cb - double(first27_ - 1));   // callbacks run in order 27
        double fall = m * (m + 1.0);                            // [0x4d84c]: + 2, 4, 6, ... per callback

        double s1 = std::sin(alpha * kDeg) * kAmp, c1 = std::cos(alpha * kDeg) * kAmp;
        double s2 = std::sin(beta * kDeg) * kAmp, c2 = std::cos(beta * kDeg) * kAmp;
        double s3 = std::sin(tilt_ * kDeg) * kAmp, c3 = std::cos(tilt_ * kDeg) * kAmp;
        std::array<double, kVerts * 2> P{};
        for (int i = 0; i < kSide; i++) {
            double X = -64.0 + 8.0 * i;            // 0x5339b: grid x / 32
            for (int j = 0; j < kSide; j++) {
                double Y = -64.0 + 8.0 * j;
                int k = kSide * i + j;
                double x = gx_[size_t(k)], y = gy_[size_t(k)];
                double z = sn(X * X + Y * Y + phase) * amp / 512.0;
                // 0x531fe: about z by alpha, about y by beta, about x by the tilt; fall, depth, perspective
                double x1 = x * c1 - y * s1, y1 = y * c1 + x * s1;
                double x2 = x1 * c2 - z * s2, z1 = z * c2 + x1 * s2;
                double y2 = y1 * c3 - z1 * s3, z2 = z1 * c3 + y1 * s3;
                y2 += fall;
                z2 += camera_;
                double PX, PY;
                if (z2 > 0.0) {
                    double f = 256.0 / std::max(z2, 1.0 / 1024.0);   // the original's smallest depth is 1
                    PX = x2 * f;
                    PY = y2 * f;
                } else {
                    PX = x2 * (1.0 - z2);   // behind the camera (never on the screen, notes-sheet.md 4.2)
                    PY = y2 * (1.0 - z2);
                }
                P[size_t(2 * k)] = PX + 160.0;
                P[size_t(2 * k + 1)] = PY + 100.0;
            }
        }
        for (const Edge& e : edges_)
            okean_scenes::blobLine(canvas, P[size_t(2 * e.a)], P[size_t(2 * e.a + 1)], P[size_t(2 * e.b)],
                                   P[size_t(2 * e.b + 1)]);
        canvas.flush(c);

        // the palette of the next draw (n + 1 callbacks), blended between whole callbacks
        int last = std::max(0, int(c.end - c.start));
        Steps st = stepsOf(std::min(cb + 1.0, double(last)));
        std::array<Vec3, 256> a = paletteAt(st.n), b = paletteAt(std::min(st.n + 1, last)), shown{};
        for (size_t i = 0; i < 256; i++) shown[i] = a[i] * float(1.0 - st.frac) + b[i] * float(st.frac);
        std::array<Vec3, 256> ramp = shown;
        ramp[254] = ramp[255];   // the font's grey is not part of the ramp
        canvas.resolve(c, ramp);
        c.shared<okean::Printer>().drawCaption(c, r, shown, 597.0);   // its printer call: line ~597
    }

private:
    // The callback 0x5355a's palette level b and the first callback in order 27, from the music positions.
    void callbacks(Ctx& c) {
        if (tabStart_ == c.start && !level_.empty()) return;
        tabStart_ = c.start;
        int count = std::max(1, int(c.end - c.start) + 2);
        level_.assign(size_t(count), level0_);
        in27_.assign(size_t(count), 0);
        first27_ = count + 1;
        int b = level0_;
        for (int j = 1; j < count; j++) {
            int order = c.positionAtCallback(j) / 64;
            if (order != kEndOrder && b != 64) b++;
            if (order == kEndOrder) {
                if (b != 0) b--;
                first27_ = std::min(first27_, j);
            }
            level_[size_t(j)] = b;
            in27_[size_t(j)] = first27_ <= j;
        }
    }
    // 0x53493: palette 1 x b / 64, in order 27 (palette 1 x b + palette 2 x (64 - b)) / 64
    std::array<Vec3, 256> paletteAt(int k) const {
        k = std::clamp(k, 0, int(level_.size()) - 1);
        float b = float(level_[size_t(k)]) / 64.0f;
        std::array<Vec3, 256> p{};
        for (size_t i = 0; i < 256; i++)
            p[i] = in27_[size_t(k)] ? pal1_[i] * b + pal2_[i] * (1.0f - b) : pal1_[i] * b;
        return p;
    }

    std::array<Vec3, 256> pal1_{}, pal2_{};
    std::array<uint8_t, 64> kern_{};
    std::array<double, kVerts> gx_{}, gy_{};
    std::vector<Edge> edges_;
    double tilt_ = 0xa00, camera_ = 2048.0;
    int level0_ = 1;
    std::vector<int> level_;
    std::vector<uint8_t> in27_;
    int first27_ = 1 << 30;
    double tabStart_ = -1.0;
};

}  // namespace

REGISTER_SCENE(SheetScene, "sheet");

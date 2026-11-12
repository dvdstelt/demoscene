// bee: a golden env-mapped bee (the `WLDD` world of file 28, 68 parts, env map file 29 through the gold ramp of
// palette 26) with flapping wings, twitching antennae and mouth part, seen by a camera that moves through 9 keys and
// bobs, in front of a still of the cloud effect (file 27); the text "23806 faces" (file 32) blinks at the bottom
// left during keys 4..6; a flash, a fade from white at the start and a fade out at the end
// (stars/re/notes-parts3d-a.md section 3). Ports 0x11e7f (the part and its frame loop), 0x11d8e (set-up), the PIT
// callback 0x12136 (tick, key and pose counters), 0x121b4 (camera keys, bob, env angles), 0x122d7 (the 7 animated
// parts), 0x11fa1 (palette phases 0x120df / fade out, the text blink) and 0x12102 (the text sprite); the drawing is
// stars::Engine's (0x78905, 0x79483 in mode 3 with [0x582ac] = 1).
//
// Evaluated exactly instead of the original's shortcuts: the camera, the bob, the env angles and the wing poses are
// continuous in the PIT ticks (the original interpolates in whole ticks with integer divisions and the 1023-amplitude
// cosine table); the fades are colour * level / 64 and white + (colour - white) * level / 64 in float with
// continuous levels. The background: file 27 is a still of the cloud effect colour-reduced into palette 26 (notes
// 3.6), so with the redraws on it is rendered by the cloud effect (cloudfx.h) at the offsets that reproduce it
// (layer 1 angle 86, depth 108; layer 2 angle 218, depth 228: found by FFT correlation in bee_bg_fft.py) and at the
// cloud's shown brightness 63 / 64 (file 27's colours equal floor(file 31 * 63 / 64) for 86 % of its pixels);
// --original-art shows file 27's pixels. The whole frame is drawn (the original leaves rows 0, 198, 199 at colour
// 0, the dark purple of file 31).
//
// The first frames: the frame after the part starts keeps the last cloud frame; the next one shows 0x11e7f's
// doubled work palette, which in the original is uninitialised memory (garbage colours, notes 3.5): here the last
// cloud frame with its palette doubled, the evident intent. Then white, and the fade from white in 16 ticks,
// one tick behind the callback's counter as the capture shows it (the palette is computed before the retrace wait).
// The text blink counts drawn frames (7 on, 7 off): it is shown in the tick intervals the capture shows it.
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "scenes/cloudfx.h"
#include "stars/core.h"
#include "stars/engine.h"

using namespace stars;

namespace {

constexpr double kTwoPi = 6.283185307179586;

// The PIT ticks of the part during which the capture shows the text (records 6511..6858: the blink counter of
// 0x11fa1 toggles every 7 drawn frames at the capture's 25.6 fps; measured from full.frames).
constexpr double kTextOn[][2] = {{564, 588}, {616, 644}, {666, 687}, {704, 718}, {732, 746},
                                 {760, 774}, {788, 802}, {821, 842}, {862, 883}, {906, 912}};

// One record of a table of int16 (x, y, z, a, b, c) at addr.
Pose key6(const StarsExe& exe, uint32_t addr) {
    return {double(exe.s16(addr)),     double(exe.s16(addr + 2)), double(exe.s16(addr + 4)),
            double(exe.s16(addr + 6)), double(exe.s16(addr + 8)), double(exe.s16(addr + 10))};
}

Pose lerp(const Pose& p, const Pose& q, double f) {
    return {p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, p.z + (q.z - p.z) * f,
            p.a + (q.a - p.a) * f, p.b + (q.b - p.b) * f, p.c + (q.c - p.c) * f};
}

class Bee : public Scene {
public:
    ~Bee() override {
        env_.tex.destroy();
        pal_.tex.destroy();
    }

    void init(Ctx& c) override {
        const StarsExe& x = *c.exe;
        world_.load(x, 28);
        env_.makeLevel(x.fileSpan(29).first(65536));   // 64 grey levels: indices 0..63 of the gold ramp
        // 0x11d8e: palette 26 with colour 0 replaced by the first 3 bytes of file 31 ([0x11b58])
        std::vector<uint8_t> p = x.file(26);
        for (size_t i = 0; i < 3; i++) p[i] = x.file(31)[i];
        pal26_ = vgaPalette(p);
        pal_.set(pal26_);
        for (uint32_t k = 0; k < 9; k++) camKeys_[k] = key6(x, 0x11d12 + 12 * k);
        // 0x11b8e: the pose offsets (the 5th wraps to pose 0) from the base 0x11bbe; 0x11ba2: the animated parts
        for (uint32_t q = 0; q < 5; q++)
            for (uint32_t i = 0; i < 7; i++) wings_[q][i] = key6(x, 0x11bbe + x.u32(0x11b8e + 4 * q) + 12 * i);
        for (uint32_t i = 0; i < 7; i++) animated_[i] = x.s32(0x11ba2 + 4 * i);
        cloud_.init(x);
        list_.reserve(world_.objects.size());
    }

    void render(Ctx& c, double t) override {
        double frames = c.retraces(t);
        if (frames < 2) {
            // the last cloud frame (record 5947), then the flash of 0x11e7f's doubled palette (5948)
            float k = frames < 1 ? 63.0f / 64.0f : 2.0f;
            cloud_.draw(c, cloudfx::partOffsets(*c.exe, cloudfx::speedSum(520)), Vec3(k, k, k));
            return;
        }
        double n = std::max(0.0, c.ticks(t));
        Vec3 mul(1, 1, 1), add(0, 0, 0);
        fade(n, frames < 3, &mul, &add);
        background(c, mul, add);
        drawBee(c, n, mul, add);
        // 0x12102: file 32 into the frame at (0, 163), over the bee
        if (textOn(n)) c.drawPicture(c.picture(32, {.palette = &pal26_}), {.x = 0, .y = 163, .w = 80, .h = 36, .mul = mul});
    }

private:
    // 0x11fa1: from white in 16 ticks (L = 4 n, colour = 63 - (63 - p) L / 64; as the capture shows it: white for
    // one frame, then 4 (n - 1)), the palette, from n = 910 the level 63 and from 942 down by one per 2 ticks to 0
    // at 1068 (the end).
    static void fade(double n, bool white, Vec3* mul, Vec3* add) {
        if (n < 17) {
            float l = white ? 0.0f : float(std::clamp(4 * (n - 1), 0.0, 64.0) / 64);
            *mul = Vec3(l, l, l);
            *add = Vec3(1 - l, 1 - l, 1 - l);
        } else if (n >= 0x38e) {
            float l = float(std::clamp(63 + (0x3ae - n) / 2, 0.0, 63.0) / 64);
            *mul = Vec3(l, l, l);
        }
    }

    // The background (0x78905 refills the frame from file 27): the cloud still, or file 27's pixels.
    void background(Ctx& c, Vec3 mul, Vec3 add) {
        if (c.art()) {
            // file 27 = the cloud at si = 0x566c, di = 0xdae4 (notes 3.6), shown at the cloud's level 63 / 64
            cloudfx::Offsets o{108, 86, 228, 218};
            float k = 63.0f / 64.0f;
            cloud_.draw(c, o, Vec3(mul.x * k, mul.y * k, mul.z * k), add);
        } else {
            c.drawPicture(c.picture(27, {.palette = &pal26_}), {.mul = mul, .add = add});
        }
    }

    // 0x121b4 (camera), 0x122d7 (wings), 0x79483 (the scene list in mode 3, faces in file order).
    void drawBee(Ctx& c, double n, Vec3 mul, Vec3 add) {
        // camera: key k = n / 140 to key k + 1, linear; a 64-tick bob of the height and the env angles
        double kf = n / 140.0;
        int k = std::min(int(std::floor(kf)), 8);
        Camera cam;
        cam.pose = lerp(camKeys_[k], camKeys_[(k + 1) % 9], kf - k);
        double s = 1023 * std::sin(kTwoPi * n / 64);   // COS[(64 n - 0x400) & 0xfff]
        cam.envA = 100 - s / 8;
        cam.envB = 400 + s / 16 + (cam.pose.b - 0x122);
        cam.pose.y += s / 64;
        cam.F = 30;     // [0x57ab6]
        cam.shift = 1;  // [0x5594c]
        // the 7 animated parts: pose p = (n / 8) & 3 to p + 1 over 8 ticks
        double pf = n / 8.0;
        int q = int(std::floor(pf)) & 3;
        list_.clear();
        for (size_t i = 0; i < world_.objects.size(); i++) list_.push_back({&world_.objects[i], world_.poses[i]});
        for (int i = 0; i < 7; i++)
            if (animated_[i] >= 0 && animated_[i] < int(list_.size()))
                list_[size_t(animated_[i])].pose = lerp(wings_[q][i], wings_[q + 1][i], pf - std::floor(pf));
        Shading sh;
        sh.mode = 3;
        sh.order = Order::File;     // [0x582ac] = 1: the abdomen's inner layers need the file order
        sh.palette = &pal_;
        sh.env = env_.level(0, 1);  // file 29: indices 0..63 of palette 26's gold ramp
        sh.mul = mul;
        sh.add = add;
        Engine& eng = c.shared<Engine>();
        eng.begin(c, cam, View::fullScreen());
        eng.drawScene(list_, sh);
        eng.end();
    }

    static bool textOn(double n) {
        for (const auto& iv : kTextOn)
            if (n >= iv[0] && n < iv[1]) return true;
        return false;
    }

    World world_;
    EnvTexture env_;
    Palette pal26_;
    PaletteTexture pal_;
    Pose camKeys_[9];
    Pose wings_[5][7];
    int animated_[7] = {};
    std::vector<SceneObject> list_;
    cloudfx::CloudFx cloud_;
};

}  // namespace

REGISTER_SCENE(Bee, "bee");

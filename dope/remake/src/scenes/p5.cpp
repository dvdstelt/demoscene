// p5: "phong ENV mapping" and "BUMP glow" (dope/re/notes-p3p5.md sections 1, 2 and 4). The star-shaped torus
// (object 1, record 144c, mesh 1402) over p4's chequerboard: first gold, env-mapped with the left map of the env RIX
// (the env-only filler 2500 / 2370 / 2420), then red, bump-mapped with the right map through texture 1c96 and a bump
// depth that pulses with |sin| (the bump filler 1d51 / 1b30 / 1c20, the table 3099 rebuilt per retrace); the labels;
// and the hand-over cross-fade to p6's swirl (3038). Ports the set-up and main script 30e2 (read from the image,
// scenes/p3p5.h), the per-retrace routine 1a70 (camera motion, label slide, the bump phase), the frame 2b66 -> 2a17,
// 2a52, 27e0, 2adc, and the hand-over 369c .. 3746.
//
// Evaluated exactly instead of the original's shortcuts (notes-p3p5 4.10; the engine's part is listed in engine.h):
//   - the bump depth A / 256 of 3099 (A = ((s >> 4) * 3) >> 4 of the sine entry s = 32767 |sin(pi phase / 2048)|)
//     without the truncations and with the phase continuous between retraces; the offsets (k - 8) A / 256 texels
//     without the floor and without the extra row on negative column offsets (bumpNibbles());
//   - the bump texels as the original reads them (nearest, one offset per texel: the patchy relief of the glow),
//     with only the env read filtered and an edge about one pixel wide between texels (makeTexelBump). Filtering
//     the offsets between texels instead turns the patches into smooth relief with thin bright veins: another
//     look (dope/re/dump/p5-bump-compare.jpg);
//   - the red map's lookup clamped to its 128 x 128 map (the original reads the gold map's last columns and the RIX
//     header at the extreme silhouette);
//   - the hand-over: the palette mix DAC[16 hi + lo] = (1 - w) pal1[lo] + w pal2[hi] (A000 = the chequerboard in the
//     low nibble, the swirl in the high one) as a true-colour blend of the two pictures, w continuous between the
//     palette steps; one step per retrace (the hardware speed, timeline.cpp), not four as in the capture.
// p4 drops the original's one-step-darker DAC at its end (its README), so p5 shows the chequerboard at the
// brightness of its picture too: there is no step at the change.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"
#include "scenes/p3p5.h"

using namespace dope;

namespace {

// Addresses (notes-p3p5 2.1, 2.2, 4.1 - 4.8).
constexpr uint16_t kScript = 0x315f;     // the first script write after the timer set-up (30e2 .. 3699)
constexpr uint16_t kRun = 0x2b66;        // run N retraces
constexpr int kRetraces = 4480;          // the script's length: states 0 .. 4479 are drawn
constexpr uint16_t kObject = 0x144c;     // object 1, the torus (object 2 is disabled)
constexpr uint16_t kMaterial = 0x1476;   // object 1's material add +2a: 0000h ENV, 0101h BUMP
constexpr uint16_t kLabelPos = 0x1558, kLabelSpeed = 0x155a, kLabelSeg = 0x1556;
constexpr uint16_t kEnvAdd = 0x1550, kTexAdd = 0x154e;   // the global material adders (0, never written)
constexpr uint16_t kPhase = 0x1546, kPhaseSpeed = 0x1548;   // 3099's bump phase and its speed per retrace
// The hand-over (369c .. 3746): mix(0) once (36cb, step [2f97] = 0), then mix(w) for w = 0, 40h, ..., while the
// count [1524] < 40h (370f: step 40h; 3719: cmp 40h), each after one wait for the retrace; then the swirl alone,
// written right after the last step. Its first step is on screen one retrace after the last frame's record.
constexpr int kFadeStep = 0x40, kFadeSteps = 0x40;
constexpr int kFadeStart = kRetraces + 1;   // c.retraces(t) of step 0 (timeline.cpp: record 25572)

// The palette weight of the hand-over's step j (0b00: w = dx >> 4, / 256), the swirl alone from step 64.
double handoverWeight(int j) {
    if (j <= 1) return 0.0;
    if (j >= kFadeSteps) return 1.0;
    return weight(double((j - 1) * kFadeStep));
}

class EnvBumpScene : public Scene {
public:
    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(5);
        part_ = EnginePart::of(5);
        log_ = p3p5::ScriptLog::read(img, kScript, kRun);
        p3p5::checkLength(log_, kRetraces, "p5");
        cam_.build(img, part_.objects, log_);
        label_ = p3p5::labelSlide(img, log_, kLabelPos, kLabelSpeed);
        phase_ = Integrator{double(img.cs16(kPhase)), double(img.css16(kPhaseSpeed))};
        log_.each(kPhaseSpeed, [&](int k, uint32_t v) { phase_.velocity(k, double(int16_t(v))); });
        material0_ = img.cs16(kMaterial);
        labelSeg0_ = img.cs16(kLabelSeg);
        rec_ = ObjectRecord::read(img, kObject);
        mesh_.load(img, rec_.seg);

        // the materials (env table 0010:0000 = 03d7 03df 0bd7 0bdf, texture table 0010:0008 = 0 1c96 0 0): ENV
        // (selector 0) the gold map 03d7 (the env RIX's left half), env only; BUMP (selector 1) the red map 03df
        // (right half) through texture 1c96. Selectors 2 and 3 (32 KB further, outside the picture) are never used.
        const auto texSegs = segmentTable(img, part_.texTable);
        bump_.create(img, texSegs[1], bumpNibbles(), part_);
        makeTexelBump(img, texSegs[1]);
        const Picture& env = c.picture("p5-envmaps");
        mat_.env[0] = EnvMap::picture(env, 0);
        mat_.env[1] = EnvMap::picture(env, 1);
        mat_.tex[1] = &bump_;
        mat_.envAdd = img.cs8(kEnvAdd), mat_.texAdd = img.cs8(kTexAdd);

        for (const char* n : {"p4-chequerboard", "p5-swirl", "p5-label-env", "p5-label-bump"}) c.picture(n);
        c.shared<Engine>();   // its shaders, built now rather than in the first frame
    }

    void render(Ctx& c, double t) override {
        double r = c.retraces(t);
        if (r >= kFadeStart) return handover(c, r - kFadeStart);
        // the frames of states 0 .. 4479; the last one stays until the hand-over's first step
        frame(c, std::clamp(r, 0.0, double(kRetraces - 1)));
    }

private:
    // 1b30's texel read: one texel of the bump texture (nearest, rows 4j + 1) and its offset for the whole texel.
    // The offsets are held constant over each texel of a texture kSub times finer (kSub x 4 kSub per texel: the
    // texels are 4 times as long in rows as in columns), so the engine's filtered read gives the texel's own offset
    // inside it and a transition about one pixel wide at its edges (the mipmaps widen it to the pixel's footprint);
    // only the env read is filtered. BumpMap's addressing (rows, scales) stays; its texture is replaced.
    void makeTexelBump(const DopeImage& img, uint16_t seg) {
        constexpr int kSub = 8;
        const int s = part_.texShift - 8, rows = bump_.rows, sx = kSub, sy = kSub << s;
        const BumpOffsets o = bumpNibbles();
        const int w = 128 * sx, h = rows * sy;
        std::vector<float> px(size_t(w) * size_t(h) * 2);
        for (int r = 0; r < rows; r++) {
            int src = (r << s) + part_.rowPhase;   // the row the original reads (BumpMap::create's lowResRows)
            for (int col = 0; col < 128; col++) {
                const auto& d = o[img.u8(seg, uint16_t(src * 256 + col))];
                for (int y = r * sy; y < (r + 1) * sy; y++)
                    for (int x = col * sx; x < (col + 1) * sx; x++) {
                        px[(size_t(y) * size_t(w) + size_t(x)) * 2 + 0] = d[0];
                        px[(size_t(y) * size_t(w) + size_t(x)) * 2 + 1] = d[1];
                    }
            }
        }
        bump_.tex.destroy();
        bump_.tex.createRaw(w, h, GL_RG16F, GL_RG, GL_FLOAT, px.data(), true, true, true);
    }

    void frame(Ctx& c, double r) {
        int k = int(std::floor(r));
        uint16_t m = uint16_t(log_.valueAt(kMaterial, k, material0_));
        Object o = p3p5::objectAt(rec_, &mesh_, r);
        o.envAdd = m & 0xff, o.texAdd = m >> 8;
        // 3099: A / 256 with A = 3 s / 256, s = 32767 |sin(pi phase / 2048)|
        double s = 32767.0 * std::fabs(std::sin(kTwoPi / 2.0 * phase_.at(r) / 2048.0));
        mat_.bumpScale = float(3.0 * s / 256.0 / 256.0);
        FrameLayers layers{&c.picture("p4-chequerboard"), &labelPicture(c, k), label_.position(r + 1)};
        drawFrame(c, layers, [&] {
            Engine& eng = c.shared<Engine>();
            eng.begin(c, part_, cam_.at(r));
            eng.draw(o, mat_);
            eng.end();
        });
    }

    // The label picture of segment [1556] (248F "phong ENV mapping", 258A "BUMP glow") as the frame copy finds it.
    const Picture& labelPicture(Ctx& c, int k) {
        uint32_t seg = log_.valueAt(kLabelSeg, k, labelSeg0_);
        for (const char* n : {"p5-label-env", "p5-label-bump"})
            if (const PictureDef* d = findPicture(n); d && d->seg == seg) return c.picture(n);
        return c.picture("p5-label-env");
    }

    // 3038 x 65: the chequerboard (low nibble, pal1 = p4's DAC 0..15) and the swirl (high nibble, pal2 = its RIX
    // palette) mixed (1 - w) pal1 + w pal2; x = retraces since step 0, step j on screen during [j, j + 1).
    void handover(Ctx& c, double x) {
        int j = int(std::floor(x));
        double w = handoverWeight(j) + (x - j) * (handoverWeight(j + 1) - handoverWeight(j));
        c.drawPicture(c.picture("p4-chequerboard"));
        if (w > 0) c.drawPicture(c.picture("p5-swirl"), PictureDraw{.alpha = float(w)});
    }

    EnginePart part_;
    p3p5::ScriptLog log_;
    p3p5::Camera cam_;
    LabelSlide label_;
    Integrator phase_;
    uint16_t material0_ = 0, labelSeg0_ = 0;
    ObjectRecord rec_;
    EngineMesh mesh_;
    BumpMap bump_;
    Materials mat_;
};

REGISTER_SCENE(EnvBumpScene, "p5");

}  // namespace

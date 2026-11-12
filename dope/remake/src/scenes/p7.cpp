// p7: "multiple LIGHT sources", "1995 WORLD domination" and "dope" (dope/re/notes-p6p7.md sections 1, 2, 4..7). A
// flat eight-armed tentacle object comes down over the desert horizon, turning about the view axis, lit by five
// light spots that move over its light map; it tilts, comes close and leaves to the left; then "dope" stands above
// the horizon for 350 retraces and the DAC fades to black (text mode: the end of the demo).
//
// Ports: the script 31ca (set-up: mode 13h, DAC 0..239 from the horizon's palette, 240..255 the label palette; the
// 7 steps; "dope" 3395, the 350-retrace wait 33a8, the exit fade 33be), the per-retrace routine 1f40 (label slide,
// motion of the group 1810, the light counter [19b8]), the frame loop 3070 with the light map 13b4 (the base 03b3
// plus five spots 37ea on the paths of 308f..3166), the label and frame copy 2f8c (the horizon 5ab4 restored). The
// 3D engine (src/dope/engine.h) draws the tentacle (record 18f8, mesh 23b4, F 2500) with the wavy bump 2fe9 addressed
// by the object-space normal (the 15-bit wrap kept) and the light map as a level map through the grey ramp.
//
// Evaluated exactly instead of the original's shortcuts:
//   - motion, angles and the label are per-retrace sums at the continuous retrace count;
//   - the light map (256 x 256 bytes rebuilt per drawn frame from whole-texel spot positions taken from the 2048-entry
//     sine table, the photo and the spot read nearest) is rendered at 4 x 4 samples per texel on the GPU: the photo
//     and the spot sampled bicubically, the five spots at continuous positions (the sine of the continuous counter,
//     no >> 9 truncation, no every-second-retrace step of light 1); the level indexes the grey ramp continuously
//     (interpolated between its entries) instead of in 216 integer steps;
//   - the bump table 0010:0000 without the carry of its packed words (codes with column offset 0 read one row up);
//   - the exit fade (v * L) >> 8 is a colour multiply interpolated between its 257 steps (one per retrace).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"

using namespace dope;

namespace {

// Image addresses (notes-p6p7 2.1, 4.1, 4.3, 5).
constexpr uint16_t kTentacleRecord = 0x18f8;
constexpr uint16_t kBaseSeg = 0x03b3;      // 3070: copied to 13b4:0000 every frame (256 x 256)
constexpr uint16_t kSpotSeg = 0x37ea;      // 128 x 128 spot, 0..63
constexpr uint16_t kDacSeg = 0x5a84;       // 31ee: DAC 0..239 (720 bytes)
constexpr uint16_t kLabelPalSeg = 0x3beb;  // 3201: DAC 240..255 from 3beb:02da

// 3070 ends step 7 after the frame copied after call 2100 (state 2099); 3395 pastes "dope" right after it, in the
// same record; 33a8 waits 350 retraces, 33be's first step is on screen in the part's record 2451 (PORT.md "Time"),
// L = 256 - j; step 256 is text mode (the cue ends there).
constexpr double kLastState = 2099;
constexpr double kDopeRetrace = 2099;
constexpr int kFadeRetrace = 2451, kFadeSteps = 256;

// The light map is rendered for light-map texels kLightOrigin .. kLightOrigin + kLightSpan (the env lookup reads
// 128 + 53.82 n +- 8: texels 66..190) at kLightScale samples per texel.
constexpr int kLightOrigin = 48, kLightSpan = 160, kLightScale = 4;
constexpr int kLightRes = kLightSpan * kLightScale;

const char* kLightFS = R"(
out vec4 o;
uniform sampler2D uBase;    // 256 x 256 levels (the photo, 32 around it)
uniform sampler2D uSpot;    // 128 x 128 levels
uniform vec2 uSpots[5];     // top-left corners (column, row) in light-map texels
uniform float uOrigin, uTexel;
void main() {
    vec2 p = vec2(uOrigin) + gl_FragCoord.xy * uTexel;   // (column, row), texel i covers [i, i + 1]
    float v = texBicubic(uBase, p / 256.0).r;
    for (int i = 0; i < 5; i++) {
        vec2 q = p - uSpots[i];
        if (q.x > 0.0 && q.y > 0.0 && q.x < 128.0 && q.y < 128.0) v += texBicubic(uSpot, q / 128.0).r;
    }
    o = vec4(v, 0.0, 0.0, 1.0);
}
)";

// The group record's motion (1f40: value += velocity per retrace), angles unwrapped.
struct Motion {
    Integrator x, y, z, a, b, c;
    Pose at(double r) const { return Pose{x.at(r), y.at(r), z.at(r), a.at(r), b.at(r), c.at(r)}; }
    Motion& vel(int k, double vx, double vy, double vz) {
        x.velocity(k, vx), y.velocity(k, vy), z.velocity(k, vz);
        return *this;
    }
    Motion& avel(int k, double va, double vb, double vc) {
        a.velocity(k, va), b.velocity(k, vb), c.velocity(k, vc);
        return *this;
    }
};

// The five spots' top-left corners (column, row) after t retraces of the light counter [19b8] (308f..3166), with
// the table's sine as sin(2 pi i / 2048) scaled by 32767 and the >> 9 as / 512:
//   1: (64 + S(t / 2), 64)            2: (64, 64 + C(4 t))           3: (64 + C(2 t), 64 + S(2 t))
//   4: (64 + S(t), 64 + C(t))         5: (64 + C(i), 64 + S(i)) with the table index i = 32767 sin(2 pi (3 t / 2)
//                                        / 2048) / 16 (the entry >> 4 & 7ffh)
std::array<std::array<float, 2>, 5> spotCorners(double t) {
    auto S = [](double i) { return 64.0 + sine2048(i) / 512.0; };
    auto C = [](double i) { return 64.0 + sine2048(i + 512.0) / 512.0; };
    double i5 = sine2048(1.5 * t) / 16.0;
    return {{{float(S(t / 2)), 64.0f},
             {64.0f, float(C(4 * t))},
             {float(C(2 * t)), float(S(2 * t))},
             {float(S(t)), float(C(t))},
             {float(C(i5)), float(S(i5))}}};
}

class TentacleScene : public Scene {
public:
    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(7);
        const EnginePart part = EnginePart::of(7);
        rec_ = ObjectRecord::read(img, kTentacleRecord);
        mesh_.load(img, rec_.seg);
        // the bump table 0010:0000: word = (dy << 8) + dx; with dx = 0 (high nibble 8) the packed row byte is one
        // less (the carry the negative column bytes take from it is missing there): undone
        BumpOffsets off = bumpTable(img, 0x0000);
        for (auto& o : off)
            if (o[0] == 0.0f) o[1] += 1.0f;
        // the texture table cs:0408 = {2fe9, 2ff1, 2fe9, 2fe9}: the wavy map, and p6's fur in the right half (never
        // selected: no mouse)
        std::array<uint16_t, 4> segs = segmentTable(img, part.texTable);
        for (int i = 0; i < 4; i++) {
            int j = 0;
            while (j < i && segs[size_t(j)] != segs[size_t(i)]) j++;
            if (j == i && segs[size_t(i)]) bump_[size_t(i)].create(img, segs[size_t(i)], off, part);
            mat_.tex[i] = segs[size_t(i)] ? &bump_[size_t(j)] : nullptr;
        }
        Palette dac = vgaPalette(img.span(kDacSeg, 0x0000, 720));
        dac = vgaPalette(img.span(kLabelPalSeg, 0x02da, 48), 240, dac);
        pal_.set(dac);
        label_ = LabelSlide{-1, {{210, 1}, {700, -1}, {910, 1}, {1890, -1}}};   // 321d: -1; 32b5, 32f5, 3342, 3388
        horizon_ = &c.picture("p7-horizon");
        dope_ = &c.picture("p7-dope");
        labels_[0] = &c.picture("p7-label-light");   // [19c6] = 3beb (image)
        labels_[1] = &c.picture("p7-label-world");   // 333b: 3ce6
        // the script 31ca (notes-p6p7 1): step k starts after the sum of the previous steps' N
        group_.x = {0, 0}, group_.y = {2304000, -4000}, group_.z = {3584000, 0};   // 322b..323f, 3273..3287
        group_.a = {0, 0}, group_.b = {0, 0}, group_.c = {0, 48};                  // 3249..326c
        group_.vel(210, 0, -2000, 0);                                       // 3297 step 2
        group_.vel(700, 0, -1200, -3000).avel(700, -18, 7, 58);             // 32c2 / 32e0 step 3
        group_.vel(770, 700, -500, -3000);                                  // 3302 step 4
        group_.avel(910, -18, 7, 68);                                       // 3326 step 5
        group_.vel(1330, 2400, 0, 0).avel(1330, -18, 7, 78);                // 334f / 336d step 6
        // the light map's sources: levels as floats
        std::vector<float> base(65536), spot(16384);
        std::span<const uint8_t> b = img.span(kBaseSeg, 0x0000, 65536), s = img.span(kSpotSeg, 0x0000, 16384);
        std::copy(b.begin(), b.end(), base.begin());
        std::copy(s.begin(), s.end(), spot.begin());
        base_.createRaw(256, 256, GL_R32F, GL_RED, GL_FLOAT, base.data(), true, false, false);
        spot_.createRaw(128, 128, GL_R32F, GL_RED, GL_FLOAT, spot.data(), true, false, false);
        light_.create(kLightRes, kLightRes, false, GL_R32F);
        gl::BindTexture(GL_TEXTURE_2D, light_.tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        lightShader_.build(kFullscreenVS, kLightFS, "p7 light map");
    }

    void render(Ctx& c, double t) override {
        double r = c.retraces(t);
        double rs = std::clamp(r, 0.0, kLastState);   // the 3D state (held after the last frame)
        buildLight(c, rs);                            // [19b8] = retraces since the part's start
        // the light map window 17b8 = 13b4 + 4040h: texels 64..191, read with clamp = false (the bump reaches out)
        EnvMap lm = EnvMap::levelMap(light_.tex, kLightRes, kLightRes, float((64 - kLightOrigin) * kLightScale),
                                     float((64 - kLightOrigin) * kLightScale), float(128 * kLightScale), &pal_, 0, 1);
        lm.clamp = false;
        for (EnvMap& e : mat_.env) e = lm;   // env table cs:0400 = {17b8 x 4}
        Fade fade{c.retraceTime(kFadeRetrace), kRetraceHz, 256, -1, kFadeSteps};
        Vec3 mul = fadeMul(fade.level(t));
        mat_.mul = mul;
        int j = int(std::floor(rs)) + 1;   // the label after call n + 1; [19c6] changes while it is out
        const Picture* lbl = labels_[j <= 910 ? 0 : 1];
        drawFrame(c, {horizon_, lbl, label_.position(rs + 1), mul}, [&] {
            Engine& eng = c.shared<Engine>();
            eng.begin(c, EnginePart::of(7), group_.at(rs));
            eng.draw(Object::from(rec_, &mesh_), mat_);
            eng.end();
        });
        // 3395: "dope" (320 x 64) into video memory at A000:3e80 = rows 50..113, over the last frame
        if (r >= kDopeRetrace) c.drawPicture(*dope_, {.y = 50, .h = 64, .mul = mul});
    }

private:
    // 3070: the light map = base + five spots at the counter's positions, as levels (the grey ramp's indices).
    void buildLight(Ctx& c, double t) {
        auto corners = spotCorners(t);
        setBlend(Blend::None);
        light_.bind();
        lightShader_.use();
        base_.bind(0);
        spot_.bind(1);
        lightShader_.set("uBase", 0);
        lightShader_.set("uSpot", 1);
        static const char* const kNames[5] = {"uSpots[0]", "uSpots[1]", "uSpots[2]", "uSpots[3]", "uSpots[4]"};
        for (int i = 0; i < 5; i++) lightShader_.set(kNames[i], corners[size_t(i)][0], corners[size_t(i)][1]);
        lightShader_.set("uOrigin", float(kLightOrigin));
        lightShader_.set("uTexel", 1.0f / float(kLightScale));
        drawFullscreen();
        gl::BindTexture(GL_TEXTURE_2D, light_.tex);
        gl::GenerateMipmap(GL_TEXTURE_2D);
        c.out->bind();
    }

    EngineMesh mesh_;
    ObjectRecord rec_;
    std::array<BumpMap, 4> bump_;
    Materials mat_;
    Motion group_;
    LabelSlide label_;
    PaletteTexture pal_;
    Texture base_, spot_;
    Target light_;
    Shader lightShader_;
    const Picture* horizon_ = nullptr;
    const Picture* dope_ = nullptr;
    const Picture* labels_[2] = {nullptr, nullptr};
};

}  // namespace

REGISTER_SCENE(TentacleScene, "p7");

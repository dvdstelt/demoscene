// The intro (0:00..3:63): spots of light ("drops") land near a point that zips across the screen, the light spreads,
// and intro/intro.raw (a digitised photo) appears through the shade table intro/intro.shd wherever there is light
// (inside/re/notes-intro-logo.md section 2). Ports the init 0x403770, the tick routine 0x403214 (the light source X,
// Y), the frame routine 0x403748 with the drops 0x40337c, the blur 0x403674 and the shade 0x4036e8, and the 3:00
// event 0x4031a0 (all drops to random places, then more drops). The palette fade from black over the first 120
// refreshes is the framework's (inside/fade.cpp).
//
// The light field is the simulation's state, a 320 x 200 int16 grid that the original steps once per drawn frame
// (0.69 frames per refresh in the capture), with the tick routine's and the frame routine's rand() calls interleaved.
// The remake replays it exactly as the capture ran it: the frames are stepped at the capture's frame steps
// (intro_frames.h, measured from the capture by tools/intro_frames.py), with the 3:00 event and the one tick that fell
// inside a drop loop where the capture had them, from the generator's state at the intro's first frame (notes 2.7). So
// it shows the original's drops, frame for frame: frame j appears at the record at which the capture shows it.
//
// Evaluated exactly instead of the original's shortcuts (notes 2.9): the light keeps the original's grid, deposits
// and truncating in-place blur (they decide where the light goes and how far it spreads), but it is sampled with a
// Catmull-Rom filter of the level clamp(L, 0, 63) at every output pixel instead of one level per original pixel, and
// blended linearly between the frame shown and the next one (by the time between the records that show them) instead
// of jumping once per drawn frame; the 64 shade levels become a continuous level (the colours of the shade table's two
// neighbouring rows blended); the photo is filtered in colour (Catmull-Rom over its texels, each shaded at the level
// of the output pixel) instead of being drawn with nearest-neighbour pixels.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/code.h"
#include "inside/indexed.h"
#include "scenes/intro_frames.h"

#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif

namespace {

constexpr int kW = 320, kH = 200, kLevels = 64, kDrops = 130;

// The file names the init 0x403770 passes to 0x400614.
constexpr uint32_t kShdName = 0x4209fc, kPalName = 0x420a0c, kRawName = 0x420a1c;
// The init's stores of X, Y, the grow flag and the drop count (immediates; read with traceStores).
constexpr uint32_t kInitStores = 0x40387e, kInitStoresEnd = 0x4038b2;
constexpr uint32_t kX = 0x425548, kY = 0x425550, kFlag = 0x425570, kCount = 0x425574;
// The tick routine's doubles: X += sin(T a0) a1 a2 + cos(T a3) + sin(T a4) a5; Y += cos(T b0) b1 b2 + cos(T b3) +
// sin(T b4) b5 (twelve doubles from 0x42095c, in this order), the bounds 319.0 and 199.0.
constexpr uint32_t kTickConsts = 0x42095c, kMaxXd = 0x4209bc, kMaxYd = 0x4209c4;
// The drop loop's constants: double -40.0 (the respawn offset), floats 319.0, 199.0 (bounds), 80.0 (the dark disc's
// centre), double 60.0 (its radius).
constexpr uint32_t kBack = 0x4209cc, kMaxXf = 0x4209d4, kMaxYf = 0x4209d8, kDiscC = 0x4209dc, kDiscR = 0x4209e0;
// The flight's init 0x402948 seeds the shared generator: `mov eax, 0x45eafc` at 0x40295d, `call srand` at 0x402967.
constexpr uint32_t kSrandOperand = 0x40295e;
constexpr int kCheckpointEvery = 32;   // frames between saved simulation states (for backwards seeks)

// Watcom's rand() 0x4134cd.
struct Rand {
    uint32_t seed = 1;
    int operator()() {
        seed = seed * 0x41c64e6du + 0x3039u;
        return int((seed >> 16) & 0x7fff);
    }
};

struct Drop {
    float x = 0, y = 0;
    int32_t intensity = 0;
};

// The intro's state (notes 2.1) plus how far it has run.
struct Sim {
    Rand rng;
    double X = 0, Y = 0;
    int flag = 0, count = 0;
    std::array<Drop, kDrops> drops{};
    std::vector<int16_t> light = std::vector<int16_t>(size_t(kW) * kH, 0);   // L[y][x], row-major
    int ticks = 0;    // tick routine calls so far (tick k at record start + k)
    int frames = 0;   // frames drawn so far
};

struct Consts {
    double a[6], b[6], maxX, maxY, back, discR;
    float maxXf, maxYf, discC;
};

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D lev;      // RG8: clamp(L, 0, 63) of the frame shown before (r) and of the next one (g), / 255
uniform sampler2D shaded;   // 8 x 8 tiles of 320 x 200: tile l = palette[intro.shd[l][intro.raw]]
uniform float w;            // weight of the next frame
uniform vec2 res;           // output size in pixels

vec4 catmullRom(float f) {
    return vec4(f * (-0.5 + f * (1.0 - 0.5 * f)), 1.0 + f * f * (-2.5 + 1.5 * f), f * (0.5 + f * (2.0 - 1.5 * f)),
                f * f * (-0.5 + 0.5 * f));
}

float levelAt(ivec2 t) {
    vec2 v = texelFetch(lev, clamp(t, ivec2(0), ivec2(319, 199)), 0).rg * 255.0;
    return mix(v.x, v.y, w);
}

// The picture's texel t shaded at level l0 + f: the colours of the shade table's rows l0 and l0 + 1, blended.
vec3 shadedAt(ivec2 t, int l0, float f) {
    t = clamp(t, ivec2(0), ivec2(319, 199));
    int l1 = l0 + 1;
    vec3 a = texelFetch(shaded, t + ivec2((l0 & 7) * 320, (l0 >> 3) * 200), 0).rgb;
    vec3 b = texelFetch(shaded, t + ivec2((l1 & 7) * 320, (l1 >> 3) * 200), 0).rgb;
    return mix(a, b, f);
}

void main() {
    vec2 p = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y) * vec2(320.0, 200.0) / res;   // VGA coordinates
    vec2 t = p - 0.5;                       // texel n covers [n, n + 1): its centre is at n + 0.5
    ivec2 i = ivec2(floor(t));
    vec2 f = t - vec2(i);
    vec4 wx = catmullRom(f.x), wy = catmullRom(f.y);
    float level = 0.0;
    for (int y = 0; y < 4; y++) {
        float row = 0.0;
        for (int x = 0; x < 4; x++) row += wx[x] * levelAt(i + ivec2(x - 1, y - 1));
        level += wy[y] * row;
    }
    level = clamp(level, 0.0, 63.0);
    int l0 = min(int(level), 62);
    float lf = level - float(l0);
    vec3 c = vec3(0.0);
    for (int y = 0; y < 4; y++) {
        vec3 row = vec3(0.0);
        for (int x = 0; x < 4; x++) row += wx[x] * shadedAt(i + ivec2(x - 1, y - 1), l0, lf);
        c += wy[y] * row;
    }
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

class IntroScene : public Scene {
public:
    void init(Ctx& c) override {
        readConstants(c);
        buildShaded(c);
        // when the capture shows each frame, in refreshes since the intro's first record
        int at = 0;
        for (int j = 0; j < intro_frames::kFrames; j++) shownAt_[size_t(j)] = at += intro_frames::kShowSteps[j];
        levCur_.assign(size_t(kW) * kH, 0);
        levPrev_.assign(size_t(kW) * kH, 0);
        rg_.assign(size_t(kW) * kH * 2, 0);
        levTex_.createRaw(kW, kH, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, rg_.data(), false, false, false);
        shader_.build(kFullscreenVS, kFS, "intro");
    }

    void render(Ctx& c, double r) override {
        tick0_ = c.partTicks(c.callback(c.start));   // T at the intro's first tick (2)
        // the frame shown at r and the next one; frame -1 is the empty light field of the start
        double n = std::max(0.0, c.refreshes(r));
        int a = int(std::upper_bound(shownAt_.begin(), shownAt_.end(), n) - shownAt_.begin()) - 1;
        int b = std::min(a + 1, intro_frames::kFrames - 1);
        double ta = a < 0 ? 0.0 : shownAt_[size_t(a)], tb = shownAt_[size_t(b)];
        float w = b > a ? float(std::clamp((n - ta) / (tb - ta), 0.0, 1.0)) : 0.0f;
        if (a != texA_ || b != texB_) uploadLevels(a, b);

        setBlend(Blend::None);
        shader_.use();
        levTex_.bind(0);
        shaded_.bind(1);
        shader_.set("lev", 0);
        shader_.set("shaded", 1);
        shader_.set("w", w);
        shader_.set("res", float(c.w), float(c.h));
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
    }

private:
    // ---------------------------------------------------------------- the original's data
    void readConstants(Ctx& c) {
        const InsideExe& e = *c.exe;
        for (int k = 0; k < 6; k++) {
            k_.a[k] = e.f64(kTickConsts + 8 * uint32_t(k));
            k_.b[k] = e.f64(kTickConsts + 48 + 8 * uint32_t(k));
        }
        k_.maxX = e.f64(kMaxXd);
        k_.maxY = e.f64(kMaxYd);
        k_.back = e.f64(kBack);
        k_.maxXf = e.f32(kMaxXf);
        k_.maxYf = e.f32(kMaxYf);
        k_.discC = e.f32(kDiscC);
        k_.discR = e.f64(kDiscR);

        // the init 0x403770: 130 drops at (rand() % 320, rand() % 200) with intensity 0 (the program's first rand()
        // calls: seed 1), X, Y, the flag and the count from its stores
        Sim s;
        Rand r;
        for (Drop& d : s.drops) {
            d.x = float(r() % 320);
            d.y = float(r() % 200);
            d.intensity = 0;
        }
        auto stores = inside::traceStores(e, kInitStores, kInitStoresEnd);
        auto f64 = [&](uint32_t addr) {
            uint64_t v = uint64_t(stores.at(addr)) | uint64_t(stores.at(addr + 4)) << 32;
            double d;
            std::memcpy(&d, &v, 8);
            return d;
        };
        s.X = f64(kX);
        s.Y = f64(kY);
        s.flag = int(stores.at(kFlag));
        s.count = int(stores.at(kCount));
        // the generator at the intro's first frame: srand in the flight's init, then the later parts' inits
        s.rng.seed = e.u32(kSrandOperand);
        for (int k = 0; k < intro_frames::kSeedCalls; k++) s.rng();
        sim_ = s;
        checkpoints_.assign(size_t(intro_frames::kFrames / kCheckpointEvery + 1), Sim{});
        saved_.assign(checkpoints_.size(), false);
        checkpoints_[0] = s;
        saved_[0] = true;
    }

    // palette[intro.shd[l][intro.raw]] for every level l, as 8 x 8 tiles of the picture (RGB10_A2)
    void buildShaded(Ctx& c) {
        auto shd = c.dat->file(c.exe->cstr(kShdName));
        auto pic = c.dat->file(c.exe->cstr(kRawName));
        auto pal = inside::palette(*c.dat, c.exe->cstr(kPalName));
        std::array<uint32_t, 256> packed{};
        for (int i = 0; i < 256; i++) {
            auto ch = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 1023.0f)); };
            const Vec3& p = pal[size_t(i)];
            packed[size_t(i)] = ch(p.x) | ch(p.y) << 10 | ch(p.z) << 20 | 3u << 30;
        }
        const int aw = 8 * kW, ah = 8 * kH;
        std::vector<uint32_t> atlas(size_t(aw) * ah);
        for (int l = 0; l < kLevels; l++) {
            const uint8_t* row = &shd[size_t(l) * 256];
            int ox = (l & 7) * kW, oy = (l >> 3) * kH;
            for (int y = 0; y < kH; y++)
                for (int x = 0; x < kW; x++)
                    atlas[size_t(oy + y) * aw + size_t(ox + x)] = packed[row[pic[size_t(y) * kW + size_t(x)]]];
        }
        shaded_.createRaw(aw, ah, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, atlas.data(), false, false,
                          false);
    }

    // ---------------------------------------------------------------- the simulation (notes 2.3, 2.4, 2.5)
    // 0x403214: the light source moves; T = the part tick counter at the call (tick k runs in the retrace callback
    // of record start + k, 4 callbacks after tick k - 1).
    void tick(Sim& s) {
        double T = tick0_ + double(kTicksPerRefresh * s.ticks);
        s.X = std::sin(T * k_.a[0]) * k_.a[1] * k_.a[2] + std::cos(T * k_.a[3]) + std::sin(T * k_.a[4]) * k_.a[5] + s.X;
        s.Y = std::cos(T * k_.b[0]) * k_.b[1] * k_.b[2] + std::cos(T * k_.b[3]) + std::sin(T * k_.b[4]) * k_.b[5] + s.Y;
        if (s.X > k_.maxX) s.X = s.rng() % 40 + 30;
        if (0.0 > s.X) s.X = s.rng() % 50 + 250;
        if (s.Y > k_.maxY) s.Y = s.rng() % 20 + 30;
        if (0.0 > s.Y) s.Y = s.rng() % 20 + 160;
        s.ticks++;
    }

    // 0x4031a0, the 3:00 event: more drops from now on, every drop to a random place with intensity 7000
    static void event(Sim& s) {
        s.flag = 1;
        for (Drop& d : s.drops) {
            d.x = float(s.rng() % 320);
            d.y = float(s.rng() % 200);
            d.intensity = 7000;
        }
    }

    // 0x40337c: respawn some drops near the source, deposit every drop's intensity at its pixel. `split`: a tick
    // routine call that falls before drop `split` (after the loop if split >= count; none if < 0).
    void drops(Sim& s, int split) {
        if (s.flag > 0 && s.count < 128) s.count++;
        for (int i = 0; i < s.count; i++) {
            if (i == split) tick(s);
            Drop& d = s.drops[size_t(i)];
            if (s.rng() % 20 == 0) {
                int xi = int(std::trunc(double(s.rng() % 90) + s.X + k_.back));
                d.y = float(std::trunc(double(s.rng() % 80) + s.Y + k_.back));
                d.x = float(xi);
                d.intensity = s.rng() % 6500 + s.count * 100;
                if (int(s.rng() == 0) % 3) d.intensity = -d.intensity;   // (sic) only when rand() returns 0
                d.intensity += (s.count - 22) * 30;
            }
            if (0.0f > d.x) d.x = float(s.rng() % 50 + 260);
            if (0.0f > d.y) d.y = float(s.rng() % 30 + 160);
            if (d.x > k_.maxXf) d.x = float(s.rng() % 30 + 50);
            if (d.y > k_.maxYf) d.x = float(s.rng() % 30 + 45);          // (sic) sets x
            if (1.0f > d.x || d.x >= k_.maxXf || 1.0f > d.y || d.y >= k_.maxYf) continue;
            double dy = double(k_.discC) - d.y, dx = double(k_.discC) - d.x;
            if (std::sqrt(dy * dy + dx * dx) < k_.discR) continue;       // the dark disc
            int16_t& px = s.light[size_t(int(d.y)) * kW + size_t(int(d.x))];
            px = int16_t(std::clamp(int(px) + d.intensity, -30000, 30000));
        }
        if (split >= s.count) tick(s);
    }

    // 0x403674: in place, rows 1..197, columns 1..318, in raster order, dividing toward zero
    static void blur(std::vector<int16_t>& L) {
        for (int y = 1; y < 198; y++) {
            int16_t* p = &L[size_t(y) * kW];
            for (int x = 1; x < 319; x++) p[x] = int16_t((4 * p[x] + p[x - 1] + p[x + 1] + p[x - kW] + p[x + kW]) / 8);
        }
    }

    // One drawn frame (0x403748) at the capture's place among the ticks: the tick routine calls of the records
    // before the one that shows the previous frame, the 3:00 event where the capture ran it, the drops (with a tick
    // inside the loop where the capture had one), the blur.
    void stepFrame(Sim& s) {
        int j = s.frames;
        int before = j == 0 ? 0 : shownAt_[size_t(j - 1)];
        bool ev = j == intro_frames::kEventFrame;
        while (s.ticks < before && !(ev && s.ticks == intro_frames::kEventTicks)) tick(s);
        if (ev) {
            event(s);
            while (s.ticks < before) tick(s);
        }
        int split = -1;
        for (const auto& m : intro_frames::kMidLoop)
            if (m.frame == j) split = m.drop;
        drops(s, split);
        blur(s.light);
        s.frames++;
    }

    // ---------------------------------------------------------------- frames for the renderer
    void step() {
        stepFrame(sim_);
        size_t ck = size_t(sim_.frames / kCheckpointEvery);
        if (sim_.frames % kCheckpointEvery == 0 && ck < checkpoints_.size() && !saved_[ck]) {
            checkpoints_[ck] = sim_;
            saved_[ck] = true;
        }
        levPrev_.swap(levCur_);
        prevFrame_ = curFrame_;
        takeLevel();
    }

    void takeLevel() {
        for (size_t i = 0; i < levCur_.size(); i++) levCur_[i] = uint8_t(std::clamp<int>(sim_.light[i], 0, 63));
        curFrame_ = sim_.frames - 1;
    }

    // clamp(L, 0, 63) after frame j (frame -1: before the first frame, all 0)
    const std::vector<uint8_t>& levelOf(int j) {
        if (j == curFrame_) return levCur_;
        if (j == prevFrame_) return levPrev_;
        if (sim_.frames > j + 1) {   // backwards: from the last saved state at or before it
            size_t ck = size_t(j + 1) / kCheckpointEvery;
            while (!saved_[ck]) ck--;
            sim_ = checkpoints_[ck];
            prevFrame_ = -2;
            takeLevel();
        }
        while (sim_.frames < j + 1) step();
        return levCur_;
    }

    void uploadLevels(int a, int b) {
        const auto& la = levelOf(a);
        for (size_t i = 0; i < la.size(); i++) rg_[2 * i] = la[i];
        const auto& lb = levelOf(b);
        for (size_t i = 0; i < lb.size(); i++) rg_[2 * i + 1] = lb[i];
        gl::BindTexture(GL_TEXTURE_2D, levTex_.id);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kW, kH, GL_RG, GL_UNSIGNED_BYTE, rg_.data());
        texA_ = a;
        texB_ = b;
    }

    Consts k_{};
    double tick0_ = 2;
    std::array<int, intro_frames::kFrames> shownAt_{};
    Sim sim_;
    std::vector<Sim> checkpoints_;
    std::vector<bool> saved_;
    std::vector<uint8_t> levCur_, levPrev_, rg_;
    int curFrame_ = -1, prevFrame_ = -2;   // the frames levCur_ / levPrev_ hold (-1: the empty start)
    int texA_ = -3, texB_ = -3;
    Texture levTex_, shaded_;
    Shader shader_;
};

}  // namespace

REGISTER_SCENE(IntroScene, "intro");

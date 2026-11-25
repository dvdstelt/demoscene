// The bump mapping of part II (vividexp/re/notes-bump.md): "SUOMI BUMP" (the map of Finland standing in waves),
// "ICE BUMP" (a rock texture sliding over a bumpy relief) and "DOOMSDAY BUMP" (the word "doomsday" in waves).
// Ports InitBUMP 0x402ec4 (with 0x402948 the base light coordinates, 0x4029dc the light picture, 0x402ad0 the wave
// distance planes, 0x402dd8 the static normals), RunBUMPEFEK 0x403434 (its stepping loop), the draw 0x4031a4 and the
// assembler drawers 0x40cf55 (water surface), 0x40ce79 (type 0 shading) and 0x40cdc8 (type 1).
//
// Every pixel inside a 2-pixel border of index 0 gets the index light[v][u] (+ 128 on land) with
//   u = u0(x) + H(x-1, y) - H(x+1, y) + lx,   v = v0(y) + H(x, y-1) - H(x, y+1) + ly   (mod 256),
// H the height picture (type 1) or max(height, wave level) (type 0), u0 / v0 bumptab.val, (lx, ly) the light offset.
// Everything is read from VIVID.EXE at run time: the ini file named by the segment's InitBUMP line, the pictures and
// .val files it names, the constants of the draw and the ring generator by address.
//
// What is evaluated exactly instead of the original's shortcuts:
//   - the height picture is interpolated (Catmull-Rom, through the original's values at its pixel centres), and the
//     heights, the wave level and the slopes are evaluated per output pixel at the continuous position, one
//     original pixel to each side, as the drawers do at the pixel centres;
//   - the wave level: the ring table of bumpprm1.val is 11 + trunc(11 sin) + 16 + trunc(16 cos) of the index
//     (its generator 0x402820), the index floor(8 x distance) - trunc(b) (bumpwave.val, mirrored by 0x402ad0); the
//     remake evaluates amp0 (1 + sin(k0 (8 d0 - b))) + amp1 (1 + cos(k1 (8 d1 - a))) + add with the float distances
//     and continuous phases (amp, n and the speeds from bumpprm1.val's header, pi/128 and 8 from the exe);
//   - the light offset lx = amp_x sin(0.01745 angle_x), ly = amp_y cos(0.01745 angle_y) as floats of continuous time
//     instead of truncated bytes;
//   - bumptab.val, integer steps of a smooth curve (the direction to a light above the screen centre), is replaced
//     by the least-squares fit of that curve to the file's 320 + 200 values (within +-0.5 of every value);
//   - the light picture is read bilinearly with wrap-around, filtered in colour: each tap's index is looked up in
//     the palette (the DAC's) and the colours are blended;
//   - the land / water switch is anti-aliased with the coverage of the coastline height(p) = level(p) in the pixel,
//     and the border of index 0 with the coverage of the drawn rectangle.
// The palette is the DAC's (c.paletteTex()); InitBUMP with flag 0 (ICE, DOOMSDAY) sets the light picture's own
// palette, which the timeline applies. ICE's light picture is a colour texture: a redraw of it (art/phong3.png) is
// used while its own palette is shown, when there is one.
// Kept: the coastline moving with the waves and the rim at it, the wrap of the light picture, the border, the wave
// phase carried over from the earlier bump effects (b, a are never reset), the plane-1 centre clamped to (316, 196).
// Dropped: the tearing, the previous part's frame in the new palette while InitBUMP loads, the cleared (black)
// records at each Run start, and the quantisation noise of the integer wave level.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "demo.h"
#include "exe.h"

namespace {

// Constants the original reads by address.
constexpr uint32_t kAngleScale = 0x820476;   // double 0.01745: degrees to radians in the draw 0x4031a4 (not pi/180)
constexpr uint32_t kEight = 0x821e74;        // int 8: wave speed multiplier (0x403434) and 1/8 in the generator 0x402820
constexpr uint32_t kPi128 = 0x8203f4;        // double pi/128 in the ring generator 0x402820

// The drawn rectangle (the drawers' loops: x = 2..317, y = 2..197); the rest stays index 0 (notes-bump.md 7).
constexpr float kDrawX0 = 2, kDrawX1 = 318, kDrawY0 = 2, kDrawY1 = 198;
// 0x402ad0 clamps the wave centres to 4..316 / 4..196 on its first call, before the second call reads plane 1's.
constexpr int kClampLo = 4, kClampX = 316, kClampY = 196;

// u0(X) = base + amp * d / sqrt(d^2 + dist^2), d = X - centre, X in VGA coordinates (pixel i's centre at i + 0.5).
struct AxisFit {
    float base = 128, amp = 0, centre = 0, dist = 1;
    double maxErr = 0;
};

// Least-squares fit of bumptab.val's staircase (notes-bump.md 3.5): a grid over the centre and the distance, base
// and amplitude solved linearly for each.
AxisFit fitAxis(const uint8_t* t, int n) {
    // the squared error of the best base and amplitude for centre c and distance D (and the fit itself)
    auto solve = [&](double c, double D, AxisFit* out) {
        double sg = 0, sgg = 0, st = 0, sgt = 0, stt = 0;
        for (int i = 0; i < n; ++i) {
            double d = i + 0.5 - c, g = d / std::sqrt(d * d + D * D);
            sg += g; sgg += g * g; st += t[i]; sgt += g * t[i]; stt += double(t[i]) * t[i];
        }
        double amp = (n * sgt - sg * st) / (n * sgg - sg * sg), base = (st - amp * sg) / n;
        if (out) {
            *out = {float(base), float(amp), float(c), float(D), 0.0};
            for (int i = 0; i < n; ++i) {
                double d = i + 0.5 - c;
                out->maxErr = std::max(out->maxErr, std::abs(base + amp * d / std::sqrt(d * d + D * D) - t[i]));
            }
        }
        return stt - base * st - amp * sgt;
    };
    double bestC = n / 2.0, bestD = n, best = 1e300;
    for (int ci = -8; ci <= 8; ++ci)
        for (double D = 40; D <= 1200; D += 8) {
            double c = n / 2.0 + ci * 0.25, e = solve(c, D, nullptr);
            if (e < best) { best = e; bestC = c; bestD = D; }
        }
    const double d0 = bestD;
    for (double D = d0 - 8; D <= d0 + 8; D += 0.1) {
        double e = solve(bestC, D, nullptr);
        if (e < best) { best = e; bestD = D; }
    }
    AxisFit f;
    solve(bestC, bestD, &f);
    return f;
}

// One InitBUMP: the ini's parameters and the buffers built from its files.
struct Effect {
    int type = 0;                    // 0 water waves (SUOMI, DOOMSDAY), 1 static normals (ICE)
    int flag = 1;                    // the script's flag: 0 = InitPCX sets the light picture's palette
    float add = 0;                   // byte added to the wave level
    float ampX = 0, ampY = 0, speedX = 0, speedY = 0;
    std::string light;               // the light picture's file
    Texture height, lightIdx;
    AxisFit fu, fv;
    float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;   // wave centres (plane 1 clamped)
    float amp0 = 0, amp1 = 0, k0 = 0, k1 = 0;   // ring profiles: amplitude, radians per index unit
    double db = 0, da = 0;           // wave phases per refresh (index units): speed * 8
    ~Effect() { height.destroy(); lightIdx.destroy(); }
};

template <class T> T readAt(std::span<const uint8_t> f, size_t off) {
    if (off + sizeof(T) > f.size()) throw std::out_of_range("bump: short parameter file");
    T v;
    std::memcpy(&v, f.data() + off, sizeof(T));
    return v;
}

using FitCache = std::map<std::string, std::pair<AxisFit, AxisFit>>;   // bumptab.val fits by file name

std::unique_ptr<Effect> loadEffect(Ctx& c, const vivid::ScriptLine& init, FitCache& fits) {
    auto e = std::make_unique<Effect>();
    // InitBUMP 0x402ec4: sscanf(ini, "%d %d %f %f %f %f %s %s %s %s %s")
    std::span<const uint8_t> f = c.exe->file(init.arg(0));
    std::string text(f.begin(), f.end());
    int add = 0;
    char light[128], prm[128], wave[128], height[128], tab[128];
    if (std::sscanf(text.c_str(), "%d %d %f %f %f %f %127s %127s %127s %127s %127s", &e->type, &add, &e->ampX,
                    &e->ampY, &e->speedX, &e->speedY, light, prm, wave, height, tab) != 11)
        throw std::runtime_error("bump: cannot read " + init.arg(0));
    e->add = float(uint8_t(add));
    e->flag = init.argInt(1);
    e->light = light;

    // the height picture (InitPCX slot 63, 64000 bytes) and the light picture (0x4029dc: the first 65536 bytes)
    const vivid::Indexed& hp = c.pictures->indexed(height);
    const vivid::Indexed& lp = c.pictures->indexed(light);
    if (hp.w * hp.h < 64000 || lp.px.size() < 65536) throw std::runtime_error("bump: unexpected picture size");
    e->height = vivid::uploadIndices(std::span<const uint8_t>(hp.px.data(), 64000), 320, 200);
    e->lightIdx = vivid::uploadIndices(std::span<const uint8_t>(lp.px.data(), 65536), 256, 256);

    // bumptab.val (0x402948): u0 per column, v0 per row
    auto fit = fits.find(tab);
    if (fit == fits.end()) {
        std::span<const uint8_t> t = c.exe->file(tab);
        if (t.size() < 520) throw std::runtime_error("bump: short " + std::string(tab));
        fit = fits.emplace(tab, std::make_pair(fitAxis(t.data(), 320), fitAxis(t.data() + 320, 200))).first;
    }
    e->fu = fit->second.first;
    e->fv = fit->second.second;
    if (e->fu.maxErr > 1 || e->fv.maxErr > 1)
        SDL_Log("bump: %s fits bumptab.val only within %.2f / %.2f", init.arg(0).c_str(), e->fu.maxErr, e->fv.maxErr);

    // bumpprm1.val: int32 cx0, cy0, cx1, cy1, n0, n1; float amp0, speed0, amp1, speed1; then the ring table
    std::span<const uint8_t> p = c.exe->file(prm);
    const int cx0 = readAt<int32_t>(p, 0), cy0 = readAt<int32_t>(p, 4);
    const int cx1 = readAt<int32_t>(p, 8), cy1 = readAt<int32_t>(p, 12);
    const int n0 = readAt<int32_t>(p, 16), n1 = readAt<int32_t>(p, 20);
    const float amp0 = readAt<float>(p, 24), speed0 = readAt<float>(p, 28);
    const float amp1 = readAt<float>(p, 32), speed1 = readAt<float>(p, 36);
    const int eight = c.exe->s32(kEight);
    const double pi128 = c.exe->f64(kPi128);
    e->cx0 = float(cx0);
    e->cy0 = float(cy0);
    e->cx1 = float(std::clamp(cx1, kClampLo, kClampX));
    e->cy1 = float(std::clamp(cy1, kClampLo, kClampY));
    e->amp0 = amp0;
    e->amp1 = amp1;
    // ring[i] = amp + trunc(amp * sin(i * (n * pi/128) * (1/8))): radians per index unit
    e->k0 = float(n0 * pi128 / eight);
    e->k1 = float(n1 * pi128 / eight);
    // 0x403434: b += [0x82d49c] * [0x821e74] per refresh (the float speed times the int 8)
    e->db = double(speed0) * eight;
    e->da = double(speed1) * eight;
    (void)wave;   // bumpwave.val is floor(8 * distance) (notes-bump.md 3.4): evaluated as the distance itself
    return e;
}

bool setsPalette(const vivid::ScriptLine& l) {
    using vivid::Cmd;
    return l.cmd == Cmd::LoadPAL || l.cmd == Cmd::InitFADE || (l.cmd == Cmd::InitPCX && l.argInt(2) == 0);
}

const char* kBumpFS = R"(
in vec2 uv;
out vec4 o;
uniform usampler2D heightTex;   // 320 x 200 heights (indices of the height picture)
uniform usampler2D lightTex;    // 256 x 256 light picture indices
uniform sampler2D pal;          // the DAC
uniform sampler2D lightRgb;     // type 1: a redraw of the light picture (true colour)
uniform int type;               // 0: water waves, 1: static relief
uniform int useRgb;
uniform vec2 lightOff;          // lx, ly
uniform vec4 fitU, fitV;        // base light coordinate per column / row: base, amp, centre, dist
uniform vec4 centres;           // wave centres: plane 0 (x, y), plane 1 (x, y)
uniform vec4 waves;             // amp0, k0, amp1, k1
uniform vec2 phase;             // b, a (index units, mod 2048)
uniform float levelAdd;
uniform vec2 pix;               // one output pixel in VGA units
uniform vec4 drawRect;          // the rectangle the drawers write: x0, y0, x1, y1

float hAt(ivec2 t) { return float(texelFetch(heightTex, clamp(t, ivec2(0), textureSize(heightTex, 0) - 1), 0).r); }

vec4 catmullRom(float f) {
    return vec4(f * (-0.5 + f * (1.0 - 0.5 * f)), 1.0 + f * f * (-2.5 + 1.5 * f), f * (0.5 + f * (2.0 - 1.5 * f)),
                f * f * (-0.5 + 0.5 * f));
}

// horizontal interpolation of row b.y + y around columns b.x (-1 .. 2)
float row4(ivec2 b, int y, vec4 w) {
    return dot(w, vec4(hAt(b + ivec2(-1, y)), hAt(b + ivec2(0, y)), hAt(b + ivec2(1, y)), hAt(b + ivec2(2, y))));
}
// ... around b.x - 1, b.x and b.x + 1 at once (columns -2 .. 3)
vec3 row6(ivec2 b, int y, vec4 w) {
    float t0 = hAt(b + ivec2(-2, y)), t1 = hAt(b + ivec2(-1, y)), t2 = hAt(b + ivec2(0, y));
    float t3 = hAt(b + ivec2(1, y)), t4 = hAt(b + ivec2(2, y)), t5 = hAt(b + ivec2(3, y));
    return vec3(dot(w, vec4(t0, t1, t2, t3)), dot(w, vec4(t1, t2, t3, t4)), dot(w, vec4(t2, t3, t4, t5)));
}

float axisBase(vec4 f, float x) { float d = x - f.z; return f.x + f.y * d * inversesqrt(d * d + f.w * f.w); }

// the water level at VGA point p: two ring waves (0x40cf55 with the ring table's formula)
float level(vec2 p) {
    float d0 = length(abs(p - centres.xy) + 0.5);
    float d1 = length(abs(p - centres.zw) + 0.5);
    return waves.x * (1.0 + sin(waves.y * (8.0 * d0 - phase.x))) + waves.z * (1.0 + cos(waves.w * (8.0 * d1 - phase.y))) +
           levelAdd;
}

vec3 tap(ivec2 t, uint plus) { return palColor(pal, (texelFetch(lightTex, t & 255, 0).r + plus) & 255u); }
// the light picture at continuous (u, v) (texel n at n), wrapping, filtered in colour
vec3 lightColour(vec2 q, uint plus) {
    vec2 f = floor(q);
    vec2 w = q - f;
    ivec2 t = ivec2(f);
    vec3 a = mix(tap(t, plus), tap(t + ivec2(1, 0), plus), w.x);
    vec3 b = mix(tap(t + ivec2(0, 1), plus), tap(t + ivec2(1, 1), plus), w.x);
    return mix(a, b, w.y);
}

void main() {
    vec2 P = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates
    // heights at P and one original pixel to each side: Catmull-Rom with the same weights for all five
    vec2 p = P - 0.5;                                     // texel i's centre at i
    vec2 fl = floor(p);
    ivec2 b = ivec2(fl);
    vec4 wx = catmullRom(p.x - fl.x), wy = catmullRom(p.y - fl.y);
    vec3 ra = row6(b, -1, wx), rb = row6(b, 0, wx), rc = row6(b, 1, wx), rd = row6(b, 2, wx);
    vec4 r0 = vec4(ra.y, rb.y, rc.y, rd.y);
    float hC = dot(wy, r0);
    float hL = dot(wy, vec4(ra.x, rb.x, rc.x, rd.x));
    float hR = dot(wy, vec4(ra.z, rb.z, rc.z, rd.z));
    float hU = dot(wy, vec4(row4(b, -2, wx), r0.xyz));
    float hD = dot(wy, vec4(r0.yzw, row4(b, 3, wx)));

    float land = 1.0;
    if (type == 0) {
        // water where the level is above the height (0x40cf55); the slopes use the surface on top
        float lC = level(P);
        hL = max(hL, level(P - vec2(1.0, 0.0)));
        hR = max(hR, level(P + vec2(1.0, 0.0)));
        hU = max(hU, level(P - vec2(0.0, 1.0)));
        hD = max(hD, level(P + vec2(0.0, 1.0)));
        float f = hC - lC;
        float fw = length(vec2(dFdx(f), dFdy(f)));
        land = clamp(f / max(fw, 1e-6) + 0.5, 0.0, 1.0);
    }
    vec2 q = vec2(axisBase(fitU, P.x) + hL - hR, axisBase(fitV, P.y) + hU - hD) + lightOff;

    vec3 col;
    if (type == 0) {
        vec3 water = land < 1.0 ? lightColour(q, 0u) : vec3(0.0);
        vec3 ground = land > 0.0 ? lightColour(q, 128u) : vec3(0.0);
        col = mix(water, ground, land);
    } else if (useRgb != 0) {
        col = texture(lightRgb, (q + 0.5) / 256.0).rgb;
    } else {
        col = lightColour(q, 0u);
    }
    // the border of index 0
    vec2 lo = clamp((P - drawRect.xy) / pix + 0.5, 0.0, 1.0);
    vec2 hi = clamp((drawRect.zw - P) / pix + 0.5, 0.0, 1.0);
    o = vec4(mix(palColor(pal, 0u), col, lo.x * lo.y * hi.x * hi.y), 1.0);
}
)";

class BumpScene : public Scene {
public:
    void init(Ctx& c) override {
        std::string fs = std::string(vivid::kIndexedGLSL) + kBumpFS;
        if (!shader_.build(kFullscreenVS, fs, "bump")) return;
        ok_ = true;
        // one effect per InitBUMP line (the Init lines of the segments this scene draws)
        FitCache fits;
        for (const vivid::Segment& s : c.timeline->segments()) {
            if (s.scene != "bump" || s.init < 0 || effects_.count(s.init)) continue;
            const vivid::ScriptLine& l = c.timeline->line(s.init).line;
            try {
                effects_[s.init] = loadEffect(c, l, fits);
            } catch (const std::exception& e) {
                SDL_Log("bump: script line %d: %s", l.line, e.what());
            }
        }
    }

    void render(Ctx& c, double r) override {
        const vivid::Segment* s = c.seg;
        auto it = s ? effects_.find(s->init) : effects_.end();
        if (!ok_ || it == effects_.end()) return;
        const Effect& e = *it->second;
        const vivid::Timeline& tl = *c.timeline;

        // Clocks (notes-bump.md 5): the wave phases b, a step in every RunBUMPEFEK loop of the program and are never
        // reset; the light angles start at 0 at InitBUMP and step in that effect's Runs. Each Run steps once per
        // refresh until its line ends (then the last frame stays).
        const vivid::TimedLine& run = tl.line(s->run);
        double n = std::clamp(r - s->start, 0.0, std::max(0.0, run.end - run.start));
        double waveN = n, lightN = n;
        for (const vivid::Segment& o : tl.segments()) {
            if (o.scene != s->scene || o.run < 0 || o.run >= s->run) continue;
            const vivid::TimedLine& l = tl.line(o.run);
            waveN += l.end - l.start;
            if (o.init == s->init) lightN += l.end - l.start;
        }
        const double k = c.exe->f64(kAngleScale);
        const double lx = e.ampX * std::sin(k * e.speedX * lightN);
        const double ly = e.ampY * std::cos(k * e.speedY * lightN);
        const double b = std::fmod(e.db * waveN, 2048.0), a = std::fmod(e.da * waveN, 2048.0);

        // InitBUMP with flag 0 sets the light picture's own palette (InitPCX 0x401884), which holds until the next
        // palette command of the script: while it does, a redraw of a colour light picture can stand in for it.
        bool own = e.flag == 0;
        for (int i = s->init + 1; own && i < int(tl.lines().size()) && tl.line(i).start <= r; ++i)
            if (setsPalette(tl.line(i).line)) own = false;

        const Texture* rgb = e.type == 1 && own ? c.pictures->redraw(e.light) : nullptr;
        shader_.use();
        e.height.bind(0);
        e.lightIdx.bind(1);
        c.paletteTex().bind(2);
        shader_.set("heightTex", 0);
        shader_.set("lightTex", 1);
        shader_.set("pal", 2);
        if (rgb) {
            rgb->setRepeat(true);
            rgb->bind(3);
        }
        shader_.set("lightRgb", rgb ? 3 : 2);   // never a sampler2D on an integer texture
        shader_.set("useRgb", rgb ? 1 : 0);
        shader_.set("type", e.type == 1 ? 1 : 0);
        shader_.set("lightOff", float(lx), float(ly));
        shader_.set("fitU", e.fu.base, e.fu.amp, e.fu.centre, e.fu.dist);
        shader_.set("fitV", e.fv.base, e.fv.amp, e.fv.centre, e.fv.dist);
        shader_.set("centres", e.cx0, e.cy0, e.cx1, e.cy1);
        shader_.set("waves", e.amp0, e.k0, e.amp1, e.k1);
        shader_.set("phase", float(b), float(a));
        shader_.set("levelAdd", e.add);
        shader_.set("pix", 320.0f / float(c.out->w), 200.0f / float(c.out->h));
        shader_.set("drawRect", kDrawX0, kDrawY0, kDrawX1, kDrawY1);
        setBlend(Blend::None);
        drawFullscreen();
    }

private:
    Shader shader_;
    bool ok_ = false;
    std::map<int, std::unique_ptr<Effect>> effects_;   // by the index of the InitBUMP line
};

}  // namespace

REGISTER_SCENE(BumpScene, "bump");

// The twirl (29:63..31:63): a 256 x 256 picture wrapped around the screen centre in polar coordinates and twisted,
// its picture and motion changing at four script events (inside/re/notes-twirl.md). Ports the init 0x409ea0 (the
// polar table 0x40a29c / 0x40a408 and the five parameter sets), the start 0x40a678, the event 0x40a098, the tick
// 0x40a20c and the frame 0x40a264 (ring table 0x40a5a4, pixels 0x40a54c).
//
// The original's table holds, per screen pixel, the last polar sample (radius 0.43 j, angle 0.006135923 i) that
// landed on it: ring row = j >> 2, column = i >> 2. The remake evaluates the mapping per output pixel (notes
// section 4, "continuous form"): q = rho / (4 x 0.43) + 0.315 rings, 256 theta / 2pi + 0.2 columns (the offsets put
// the continuous coordinates where the table's last-written samples are on average), the per-ring waves with a
// continuous ring, the shifts as floats instead of truncated integers, and a filtered read: the four taps around the
// sample, each by the original's address rule pic[(256 row + column) & 0xffff] (a column past 255 carries into the
// next row, which makes the seam), looked up in the palette and blended. Near the centre, where many columns meet in
// one pixel, the pixel is supersampled. With the art on, a picture with an upscale (art/tex_<name>.jpg, pictures.h)
// is read from the upscale instead (the pictures go straight through the palette here), at the same coordinates with
// the same carry, mipmapped by the sample's footprint.
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "inside/code.h"
#include "inside/indexed.h"

namespace {

constexpr uint32_t kInit = 0x409ea0, kInitStoresEnd = 0x40a067;  // the run of constant stores in the init
constexpr uint32_t kSets = 0x4b6c68;                               // 5 sets x 8 floats (BSS, written by the init)
constexpr uint32_t kAngleStep = 0x4217cf, kRadiusStep = 0x4217d7;  // doubles used by 0x40a29c
constexpr uint32_t kEvent = 0x40a098;                              // the script's twirl event
// The event's switch on the picture index: jump table 0x40a084, five cases; each case is `mov eax, <picture name>;
// call 0x400614; ...; mov eax, <palette name>` with the immediates at case + 1 and case + 0x10. The init starts with
// case 0's files.
constexpr uint32_t kPictureCases = 0x40a084;

// One parameter set as the event copies it (notes section 2: set field -> variable).
struct Set {
    float d18, p1Speed, A, s58Speed, s2cSpeed, p2Speed, B, d14;
};

const char* kFS = R"(
in vec2 uv;
out vec4 o;
uniform usampler2D pic;
uniform sampler2D pal;
uniform sampler2D det;         // the picture's upscale (useDet), repeating, mipmapped
uniform int useDet;
uniform vec2 res;              // output size in pixels
uniform float ringPx;          // radius per ring row (4 x 0.43)
uniform float colPerRad;       // columns per radian (1 / (4 x 0.006135923))
uniform float s58, s2c, p1, p2, A, B, d18, d14;

vec3 tap(int c, int r) {
    int a = (256 * r + c) & 0xffff;   // the 64 KB picture as a torus, columns carrying into rows
    return palColor(pal, texelFetch(pic, ivec2(a & 255, a >> 8), 0).r);
}

vec3 sampleAt(vec2 p, float lod) {   // p in VGA coordinates; lod: the upscale's mipmap level
    vec2 d = p - vec2(160.0, 100.0);
    float rho = length(d);
    float theta = atan(d.y, d.x);  // 0 = right, growing downwards (clockwise on screen)
    if (theta < 0.0) theta += 2.0 * PI;
    float q = rho / ringPx + 0.315;
    float k = 128.0 + q;
    float R = q + s2c + A * sin(p1 + k * d18);
    float C = 128.0 + theta * colPerRad + 0.2 + s58 + B * sin(p2 + k * d14);
    if (useDet != 0)   // a column past 255 carries into the next row, as the address rule does
        return textureLod(det, vec2(C, R + floor(C / 256.0)) / 256.0, lod).rgb;
    // bilinear between the four texel centres around (C, R) (texel n covers [n, n + 1))
    vec2 t = vec2(C, R) - 0.5;
    vec2 f = fract(t);
    ivec2 i = ivec2(floor(t));
    vec3 c00 = tap(i.x, i.y), c10 = tap(i.x + 1, i.y), c01 = tap(i.x, i.y + 1), c11 = tap(i.x + 1, i.y + 1);
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

void main() {
    vec2 px = vec2(320.0, 200.0) / res;                      // one output pixel in VGA units
    vec2 p = vec2(gl_FragCoord.x * px.x, (res.y - gl_FragCoord.y) * px.y);
    // columns per output pixel: the angular footprint grows towards the centre
    float rho = max(length(p - vec2(160.0, 100.0)), 1e-3);
    float colsPerPx = colPerRad * max(px.x, px.y) / rho;
    int n = int(clamp(ceil(colsPerPx), 1.0, 6.0));
    float lod = log2(max(4.0 * max(colsPerPx, max(px.x, px.y) / ringPx) / float(n), 1.0));
    vec3 acc = vec3(0.0);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            acc += sampleAt(p + (vec2(x, y) + 0.5) / float(n) * px - 0.5 * px, lod);
    o = vec4(acc / float(n * n), 1.0);
}
)";

class TwirlScene : public Scene {
public:
    void init(Ctx& c) override {
        // the five parameter sets, as the init stores them (immediates in its code)
        auto stores = inside::traceStores(*c.exe, kInit, kInitStoresEnd);
        for (int s = 0; s < 5; s++) {
            auto f = [&](int k) { return inside::storedFloat(stores, kSets + 0x20 * s + 4 * k); };
            sets_[s] = {f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7)};
        }
        angleStep_ = c.exe->f64(kAngleStep);
        radiusStep_ = c.exe->f64(kRadiusStep);
        for (uint32_t k = 0; k < 5; k++) {
            uint32_t cs = c.exe->u32(kPictureCases + 4 * k);
            std::string file = c.exe->cstr(c.exe->u32(cs + 1)), pal = c.exe->cstr(c.exe->u32(cs + 0x10));
            pics_[k] = inside::uploadIndices(c.dat->file(file), 256, 256);
            // every event writes black into entry 0 of the palette it sets (0x40a098); the start keeps jeesus.pal's
            // entry 0, which is black in the file
            pals_[k] = inside::uploadPalette(inside::palette(*c.dat, pal, true));
            const Image* up = c.art() ? c.pictures->detail(detailName(file)) : nullptr;
            if (up && up->w == 4 * 256 && up->h == 4 * 256) dets_[k].create(*up, true, true);
        }
        shader_.build(kFullscreenVS, std::string(inside::kIndexedGLSL) + kFS, "twirl");
    }

    void render(Ctx& c, double r) override {
        // events: picture index = events % 5, set index = (1 + events) % 5 (0x40a678 starts with picture 0, set 1)
        std::vector<double> ev;
        for (const ScriptEntry& e : c.script())
            if (e.routine == kEvent && e.refresh >= c.start && e.refresh < c.end) ev.push_back(e.refresh);
        auto eventsBy = [&](double at) {
            int n = 0;
            for (double x : ev) n += x <= at;
            return n;
        };
        // the phases: the tick routine adds the current set's speeds once per refresh; the state shown at refresh
        // start + n is the one after n steps; a step uses the speeds of the events that were visible before it
        double n = std::max(0.0, c.refreshes(r));
        int whole = int(std::floor(n));
        double frac = n - whole;
        double s58 = 0, s2c = 0, p1 = 0, p2 = 0;
        auto add = [&](const Set& s, double w) {
            s58 += w * s.s58Speed;
            s2c += w * s.s2cSpeed;
            p1 += w * s.p1Speed;
            p2 += w * s.p2Speed;
        };
        for (int k = 1; k <= whole; k++) add(sets_[(1 + eventsBy(c.start + k - 1)) % 5], 1.0);
        add(sets_[(1 + eventsBy(c.start + whole)) % 5], frac);
        int nev = eventsBy(r);
        const Set& s = sets_[(1 + nev) % 5];
        int pic = nev % 5;

        setBlend(Blend::None);
        shader_.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, pics_[pic].id);
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(GL_TEXTURE_2D, pals_[pic].id);
        gl::ActiveTexture(GL_TEXTURE2);
        gl::BindTexture(GL_TEXTURE_2D, dets_[pic].id);
        shader_.set("pic", 0);
        shader_.set("pal", 1);
        shader_.set("det", 2);
        shader_.set("useDet", dets_[pic].id ? 1 : 0);
        shader_.set("res", float(c.w), float(c.h));
        shader_.set("ringPx", float(4.0 * radiusStep_));
        shader_.set("colPerRad", float(1.0 / (4.0 * angleStep_)));
        shader_.set("s58", float(s58));
        shader_.set("s2c", float(s2c));
        shader_.set("p1", float(p1));
        shader_.set("p2", float(p2));
        shader_.set("A", s.A);
        shader_.set("B", s.B);
        shader_.set("d18", s.d18);
        shader_.set("d14", s.d14);
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
    }

private:
    Set sets_[5];
    double angleStep_ = 0, radiusStep_ = 0;
    Texture pics_[5], pals_[5], dets_[5];   // dets_: the upscales (id 0: none)
    Shader shader_;
};

}  // namespace

REGISTER_SCENE(TwirlScene, "twirl");

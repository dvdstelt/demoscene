// p3: "7800 PHONG polys" (dope/re/notes-p3p5.md sections 1-3). The lumpy object (object 4, record 18f8, mesh 1362)
// tumbling towards the camera over the nebula and drifting off to the left, shaded by the Phong + bump filler
// (2211 / 1ff0 / 20e0) through the shared engine (src/dope/engine.h), the label "7800 PHONG polys" sliding in and
// out, and the set-up screen. Ports the set-up and main script 2c22 (read from the image, scenes/p3p5.h), the
// per-retrace routine 1f60 (camera motion, label slide) and the frame 2ba4 -> 2a5a, 2a95, 2820, 2b1c (background,
// object, label, buffer row 1 = screen row 0).
//
// Evaluated exactly instead of the original's shortcuts (notes-p3p5 3.8; the engine's part is listed in engine.h):
//   - the Phong map 0337:0000 is a radial gradient drawn with a paint program (index = floor(240.5 - d) along the
//     palette ramp 128..239, a white disc of index 239 around the highlight; notes 3.6). It is fitted at start-up to
//     the stored map (the disc's centroid and area, the ramp's offset and slope by least squares) and evaluated
//     continuously into a 512 x 512 level map, coloured along the object palette by interpolating between its
//     entries: no 1-index steps, no paint-program ripples of +-2 indices, and the disc's edge anti-aliased. The same
//     in both art modes (the map is generated, not a picture);
//   - camera and label motion continuous between retraces (dope::Integrator, dope::LabelSlide).
// Kept: the bump texture 2341 addressed by the object-space normal with the 15-bit wrap (the engine's
// EnginePart::of(3)), whose wrapped faces draw the zig-zag contour bands; the bump table's dx = 0 rows one row up;
// the point-mirrored map; the 0.75 vertical scale and the centre (160, 99).
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"
#include "scenes/p3p5.h"

using namespace dope;

namespace {

// Addresses (notes-p3p5 2.1, 2.2, 3.1 - 3.6).
constexpr uint16_t kScript = 0x2c9b;     // the first script write after the timer set-up (2c22 .. 2f57)
constexpr uint16_t kRun = 0x2ba4;        // run N retraces
constexpr int kRetraces = 3605;          // the script's length (notes 3.2): states 0 .. 3604 are drawn
constexpr uint16_t kObject = 0x18f8;     // object 4, the one drawn (objects 1 - 3 are disabled)
constexpr uint16_t kLabelPos = 0x19c4, kLabelSpeed = 0x19c6;
constexpr uint16_t kEnvAdd = 0x19be, kTexAdd = 0x19bc;   // the global material adders (0, never written)
constexpr uint16_t kPhongPal = 0x0306;   // the Phong-map RIX: palette at +0010 (DAC 0..239)
constexpr uint16_t kNebulaPal = 0x4b81;  // the nebula's palette entries 0..15 (DAC 0..15 after the Phong's)

// The Phong map's gradient, fitted to the stored indices.
struct PhongFit {
    double cx = 64, cy = 64;   // the highlight's centre (map columns / rows; texel i covers [i, i + 1])
    double disc = 0;           // the radius of the white disc
    double l0 = 240, slope = 1;// level = l0 - slope * d outside the disc
    int lo = 128, hi = 239;    // the ramp's ends (the map's lowest and highest index)

    double level(double x, double y) const {
        double d = std::hypot(x - cx, y - cy);
        return d < disc ? hi : std::clamp(l0 - slope * d, double(lo), double(hi));
    }

    // From the 128 x 128 indices at seg:0000 (rows of 256 bytes).
    static PhongFit fit(const DopeImage& img, uint16_t seg) {
        PhongFit f;
        std::vector<int> m(128 * 128);
        int lo = 255, hi = 0;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                int v = img.u8(seg, uint16_t(y * 256 + x));
                m[size_t(y) * 128 + size_t(x)] = v;
                lo = std::min(lo, v), hi = std::max(hi, v);
            }
        f.lo = lo, f.hi = hi;
        double sx = 0, sy = 0, n = 0;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
                if (m[size_t(y) * 128 + size_t(x)] == hi) sx += x + 0.5, sy += y + 0.5, n++;
        if (n > 0) f.cx = sx / n, f.cy = sy / n, f.disc = std::sqrt(n / 3.141592653589793);
        // the ramp: index = l0 - slope * d at the texels' centres, least squares on the unclamped texels (the
        // continuous level then has the mean colour of the original's 1-index steps)
        double s1 = 0, sd = 0, sdd = 0, sv = 0, sdv = 0;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                int v = m[size_t(y) * 128 + size_t(x)];
                if (v <= lo || v >= hi) continue;
                double d = std::hypot(x + 0.5 - f.cx, y + 0.5 - f.cy), l = v;
                s1 += 1, sd += d, sdd += d * d, sv += l, sdv += d * l;
            }
        double det = s1 * sdd - sd * sd;
        if (s1 > 2 && std::fabs(det) > 1e-9) {
            f.slope = -(s1 * sdv - sd * sv) / det;
            f.l0 = (sv + f.slope * sd) / s1;
        }
        return f;
    }
};

class PhongScene : public Scene {
public:
    ~PhongScene() override {
        phong_.destroy();
        pal_.tex.destroy();
    }

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(3);
        part_ = EnginePart::of(3);
        p3p5::ScriptLog log = p3p5::ScriptLog::read(img, kScript, kRun);
        p3p5::checkLength(log, kRetraces, "p3");
        cam_.build(img, part_.objects, log);
        label_ = p3p5::labelSlide(img, log, kLabelPos, kLabelSpeed);
        rec_ = ObjectRecord::read(img, kObject);
        mesh_.load(img, rec_.seg);

        // the material of selector 0 (the mesh's faces all have material 0; the object's and global adds are 0):
        // Phong map 0337 (env table 0010:0400) and bump texture 2341 (0010:0408) through the table at 0010:0000
        const auto envSegs = segmentTable(img, part_.envTable), texSegs = segmentTable(img, part_.texTable);
        bump_.create(img, texSegs[0], bumpTable(img, uint16_t(part_.bumpTable)), part_);
        makePhongMap(img, envSegs[0]);
        mat_.env[0] = EnvMap::levelMap(phong_.id, kPhongSize, kPhongSize, 0, 0, kPhongSize, &pal_, float(fit_.lo),
                                       float(fit_.hi - fit_.lo));
        mat_.tex[0] = &bump_;
        mat_.envAdd = img.cs8(kEnvAdd), mat_.texAdd = img.cs8(kTexAdd);

        // the set-up screen: colour 0 after the palette loads (the nebula's entry 0)
        background_ = vgaPalette(img.span(kNebulaPal, 0x0000, 48))[0];
        for (const char* n : {"p3-nebula", "p3-label-phong"}) c.picture(n);
        c.shared<Engine>();   // its shaders, built now rather than in the first frame
    }

    void render(Ctx& c, double t) override {
        if (t < c.start) return c.clear(background_);   // 2c22: mode 13h, the screen filled with colour 0
        // the state after r calls, up to the last one drawn (the held frame shows it too)
        double r = std::clamp(c.retraces(t), 0.0, double(kRetraces - 1));
        Object o = p3p5::objectAt(rec_, &mesh_, r);
        FrameLayers layers{&c.picture("p3-nebula"), &c.picture("p3-label-phong"), label_.position(r + 1)};
        drawFrame(c, layers, [&] {
            Engine& eng = c.shared<Engine>();
            eng.begin(c, part_, cam_.at(r));
            eng.draw(o, mat_);
            eng.end();
        });
    }

private:
    static constexpr int kPhongSize = 512;   // texels of the level map over the 128 x 128 Phong map

    // The fitted gradient as levels (lo .. hi -> 0 .. 1), 4 x 4 samples per texel where the disc's edge crosses it.
    void makePhongMap(const DopeImage& img, uint16_t seg) {
        fit_ = PhongFit::fit(img, seg);
        pal_.set(vgaPalette(img.span(kPhongPal, 0x0010, 720)));
        const int n = kPhongSize;
        const double k = 128.0 / n, range = fit_.hi - fit_.lo;
        std::vector<float> px(size_t(n) * size_t(n));
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                double u = (x + 0.5) * k, v = (y + 0.5) * k;
                double l;
                if (std::fabs(std::hypot(u - fit_.cx, v - fit_.cy) - fit_.disc) < k) {
                    l = 0;
                    for (int j = 0; j < 4; j++)
                        for (int i = 0; i < 4; i++)
                            l += fit_.level(u + (i - 1.5) * k / 4, v + (j - 1.5) * k / 4);
                    l /= 16;
                } else {
                    l = fit_.level(u, v);
                }
                px[size_t(y) * size_t(n) + size_t(x)] = float((l - fit_.lo) / range);
            }
        phong_.createRaw(n, n, GL_R16F, GL_RED, GL_FLOAT, px.data(), true, false, true);
    }

    EnginePart part_;
    p3p5::Camera cam_;
    LabelSlide label_;
    ObjectRecord rec_;
    EngineMesh mesh_;
    BumpMap bump_;
    PhongFit fit_;
    Texture phong_;
    PaletteTexture pal_;
    Materials mat_;
    Vec3 background_;
};

REGISTER_SCENE(PhongScene, "p3");

}  // namespace

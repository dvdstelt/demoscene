// The interlaced textured planes of script entries 18/19 (scenes/dada.cpp) and 23 (scenes/tomboz.cpp)
// (reve/re/notes-parts2d-b.md section 4). Ports 0x259de4 (the camera matrix), 0x259ed0 (points), 0x259f70 and 0x25a048
// (per row: the ray / plane intersections of the row's ends) and the row drawers 0x259a08 (64 x 64 texture, even
// lines), 0x259b50 (256 x 256, even lines) and 0x259c98 (256 x 256, odd lines).
//
// Each layer is a camera turned by the angle words (a, b) (4096 a turn): a tilts about the x axis, b turns about the
// vertical z axis. It looks from E = M (0, 512, 0) through a 256 x 256 view quad M (x, 0, z) at the origin at the
// plane z = 0, which is covered with the layer's texture, tiled; the texel (row, column) is (1.25 H.x, 1.25 H.y) of the
// hit point H. Layer A shows on the even lines of the original's 200-line screen, layer B on the odd lines; the plane
// row k (0..99, z = 128 - 2.56 k) is shown on lines 2k (A) and 2k + 1 (B), the screen column x (x = 128 - 0.8 x).
//
// The remake keeps the line interlace in the original's grid (each original line shows one layer, box-filtered at
// the lines' edges) and evaluates the rest exactly: the angles are continuous, every output pixel has its own ray
// (each layer is continuous across its lines), the textures are read filtered in colour (palette colours, mip-mapped,
// anisotropic, repeated), and the horizon, where the rays stop meeting the plane, is anti-aliased. The original's
// integer eye and corners, 16.16 ray parameters, 100 rows per layer and nearest-texel reads are not reproduced.
#pragma once
#include <cmath>
#include <string>

#include "demo.h"
#include "exe.h"

namespace planes {

// A plane texture: indices of a RAW file through a palette, as colours (the upscale art/tex_<name>.jpg when there is
// one: exactly 4 x the RAW file's size; a 64 x 64 tile takes its part of it).
inline void loadTexture(Ctx& c, Texture& tex, uint32_t fileName, uint32_t paletteName, int x0, int y0, int size) {
    std::string file = c.exe->cstr(fileName);
    RawImage px = rawImage(*c.exe, file);
    RawImage pal = rawImage(*c.exe, c.exe->cstr(paletteName));
    Image im;
    const Image* up = c.pictures ? c.pictures->detail(detailName(file)) : nullptr;
    if (up && up->w == 4 * px.w && up->h == 4 * px.h) {
        im.w = im.h = 4 * size;
        im.rgba.resize(size_t(im.w) * im.h * 4);
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++)
                for (int k = 0; k < 4; k++)
                    im.rgba[(size_t(y) * im.w + x) * 4 + k] =
                        up->rgba[((size_t(4 * y0 + y)) * up->w + 4 * x0 + x) * 4 + k];
    } else {
        im.w = im.h = size;
        im.rgba.resize(size_t(size) * size * 4);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                int i = px.px[size_t(y0 + y) * px.w + x0 + x];
                uint8_t* q = &im.rgba[(size_t(y) * size + x) * 4];
                for (int k = 0; k < 3; k++) q[k] = uint8_t((pal.pal6[i * 3 + k] * 255 + 31) / 63);
                q[3] = 255;
            }
    }
    tex.create(im, /*repeat=*/true, /*mipmap=*/true);
}

// One layer's camera for the shader.
struct Camera {
    Vec3 eye, ax, az;   // E, M (1, 0, 0), M (0, 0, 1)
};

// The constants of the plane routines, read from the image.
struct Geometry {
    float eyeY = 0;      // 0x259938: the eye (0, 512, 0)
    float cx = 0, cz = 0;  // 0x25994c / 0x259954: the view quad's corner (128, 0, 128)
    float rows = 0;      // 0x259a6d: 100 rows per layer
    float pixels = 0;    // 0x25a154: 320 pixels per row (imul edx, edx, 0x140)
    void read(const ReveExe& e) {
        eyeY = float(e.s32(0x259938));
        cx = float(e.s32(0x25994c));
        cz = float(e.s32(0x259954));
        rows = float(e.s32(0x259a6d));
        pixels = float(e.s32(0x25a154));
    }
    // 0x259de4 / 0x259ed0 with the sine table's exact function: M p = (cb x + sb y', -sb x + cb y', sa y + ca z),
    // y' = ca y - sa z.
    Camera camera(double a, double b) const {
        constexpr double kTurn = 6.283185307179586 / 4096;   // the sine table 0x2176c8: 4096 entries a turn
        double al = a * kTurn, be = b * kTurn;
        float sa = float(std::sin(al)), ca = float(std::cos(al)), sb = float(std::sin(be)), cb = float(std::cos(be));
        Camera k;
        k.eye = Vec3(sb * ca, cb * ca, sa) * eyeY;
        k.ax = Vec3(cb, -sb, 0);
        k.az = Vec3(-sb * sa, -cb * sa, ca);
        return k;
    }
};

inline const char* kPlaneFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D texA, texB;
uniform float periodA, periodB;   // texels per tile (64 or 256)
uniform vec3 eyeA, axA, azA, eyeB, axB, azB;
uniform vec4 quad;                // view quad point (x, 0, z): x = quad.x - quad.y * VGA x, z = quad.z - quad.w * row
uniform float texScale;           // texels per world unit (320 / 256)
uniform vec2 vga;                 // VGA units per output pixel
uniform vec3 black;               // where a ray misses the plane (index 0)

// Layer colour at VGA position p; `line` = 0 for the even-line layer, 1 for the odd one.
vec3 layer(vec2 p, float line, vec3 E, vec3 X, vec3 Z, sampler2D tex, float period) {
    float k = (p.y - line) * 0.5;                        // plane row, continuous
    vec3 Q = (quad.x - quad.y * p.x) * X + (quad.z - quad.w * k) * Z;
    float den = E.z - Q.z;
    float g = den * sign(E.z);                           // > 0: the ray meets the plane in front of the eye
    float cover = E.z == 0.0 ? 0.0 : clamp(g / max(fwidth(g), 1e-6) + 0.5, 0.0, 1.0);
    float t = g > 0.0 ? min(E.z / den, 1e4) : 1e4;       // beyond the horizon: the far plane, for the edge pixels
    vec3 H = E + t * (Q - E);
    vec3 c = texture(tex, vec2(H.y, H.x) * (texScale / period)).rgb;
    return mix(black, c, cover);
}

// Share of the output pixel at VGA y that lies on even lines (box filter of the line interlace).
float evenShare(float y, float h) {
    float a = y - 0.5 * h, b = y + 0.5 * h;
    float fa = floor(a * 0.5) + min(fract(a * 0.5) * 2.0, 1.0);
    float fb = floor(b * 0.5) + min(fract(b * 0.5) * 2.0, 1.0);
    return clamp((fb - fa) / h, 0.0, 1.0);
}

void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);   // VGA coordinates, y down
    vec3 a = layer(p, 0.0, eyeA, axA, azA, texA, periodA);
    vec3 b = layer(p, 1.0, eyeB, axB, azB, texB, periodB);
    o = vec4(mix(b, a, evenShare(p.y, vga.y)), 1.0);
}
)";

// The renderer: two layers on alternate lines.
struct Renderer {
    Shader sh;
    Geometry geo;
    Vec3 black;
    void init(Ctx& c, Vec3 indexZero) {
        geo.read(*c.exe);
        black = indexZero;
        sh.build(kFullscreenVS, kPlaneFS, "planes");
    }
    void draw(Ctx& c, const Texture& ta, float periodA, const Camera& A, const Texture& tb, float periodB,
              const Camera& B) {
        setBlend(Blend::None);
        sh.use();
        ta.bind(0);
        tb.bind(1);
        sh.set("texA", 0);
        sh.set("texB", 1);
        sh.set("periodA", periodA);
        sh.set("periodB", periodB);
        sh.set("eyeA", A.eye);
        sh.set("axA", A.ax);
        sh.set("azA", A.az);
        sh.set("eyeB", B.eye);
        sh.set("axB", B.ax);
        sh.set("azB", B.az);
        sh.set("quad", geo.cx, 2 * geo.cx / geo.pixels, geo.cz, 2 * geo.cz / geo.rows);
        sh.set("texScale", geo.pixels / 256.0f);   // u0 = 320 uL (1/256 units), texel = u0 >> 16
        int w = c.out ? c.out->w : c.w, h = c.out ? c.out->h : c.h;
        sh.set("vga", 320.0f / float(w), 200.0f / float(h));
        sh.set("black", black);
        drawFullscreen();
    }
};

}  // namespace planes

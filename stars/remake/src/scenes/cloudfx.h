// The cloud effect of STARS (stars/re/notes-parts3d-a.md section 2), shared by the cloud part and the bee (whose
// background, file 27, is a still of it: section 3.6). Ports the tunnel table 0x1259c / 0x124eb, the draw 0x125f4
// and the speed sum of the PIT callback 0x1278e.
//
// Two layers of the 4-bit texture file 30 (256 x 256, row = angle, column = depth) are mapped on a tunnel and
// combined through the 16x16 palette file 31 (colour c1 + 16 c2). Evaluated exactly instead of the original's
// shortcuts:
//   - angle and depth per output pixel from the continuous formulas of 2.2 (angle = 256 / 2 pi atan2(y - 100,
//     160 - x), depth = 5120 / (r + 18), r in 320x200 pixel units: circles in pixel units, ellipses at 4:3) instead
//     of the 64,000-word table of integer rays (the 2x2 centre the table never writes is gone);
//   - both layers are read bilinearly as the original's 65,536-byte strip (address = row * 256 + column, so the
//     depth wraps into the next row: the one-row step where depth + offset crosses a multiple of 256 stays), and
//     where the texture is minified (near the centre) through mipmaps; layer 2 is read per pixel at its full offset
//     instead of once per pixel pair with the depth rounded to even;
//   - the colour is looked up bilinearly in the 16x16 table with the two filtered layer values (0..15), so the
//     8-bit index c1 + 16 c2 becomes a continuous 2-D ramp;
//   - the offsets follow the speed sum continuously (whole ticks as the callback adds them, the tick in progress
//     interpolated).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "pictures.h"

namespace cloudfx {

// Texel offsets of the two layers: depth (column) and angle (row). The texel at (depth + d, angle + a) of the strip.
struct Offsets {
    double d1 = 0, a1 = 0, d2 = 0, a2 = 0;
};

// Callback 0x1278e: the speed sum S / 1024 in texels after n ticks. Each tick adds 0x400 up to tick 292, then
// 0x400 - (0x400 * (k - 292)) / 0xe4 (falling to 0 at tick 520: the part's phase 0x126fa switches the deceleration on
// at n = 292); the tick in progress is interpolated.
inline double speedSum(double n) {
    if (n <= 0) return 0;
    auto step = [](int k) {   // what tick k (1, 2, ...) adds, in 1/1024 texel
        if (k <= 0x124) return 0x400;
        return std::max(0, 0x400 - (0x400 * (k - 0x124)) / 0xe4);
    };
    int whole = int(std::min(std::floor(n), 1000.0));
    long sum = 0;
    for (int k = 1; k <= whole; k++) sum += step(k);
    return (double(sum) + (n - whole) * step(whole + 1)) / 1024.0;
}

// 0x125f4's offsets as functions of the speed sum s (texels): layer 1 si (depth 0x14000 + S, angle (0x23000 + S) / 2),
// layer 2 di (depth -2 S, angle 2 (-0xa000 - S) / 2), from the initial dwords at 0x1269e..0x126aa (read from the exe).
inline Offsets partOffsets(const StarsExe& exe, double s) {
    double o1 = exe.s32(0x1269e) / 1024.0, o2 = exe.s32(0x126a2) / 1024.0;
    double o6 = exe.s32(0x126a6) / 1024.0, oa = exe.s32(0x126aa) / 1024.0;
    return {o1 + s, (o2 + s) / 2, 2 * (o6 - s), oa - s};
}

inline const char* kCloudFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;       // file 30: levels 0..15, 256 x 256 (column = depth, row = angle), R16F, mipmapped, repeat
uniform sampler2D colours;   // file 31 as 16 x 16 (x = layer 1, y = layer 2), linear
uniform vec4 off;            // layer 1 (depth, angle), layer 2 (depth, angle) in texels
uniform vec2 px;             // VGA units per output pixel
uniform vec3 mul, add;

const float kTau = 6.283185307179586;

// texel a of the 65,536-byte strip (0x125f4 adds 16-bit addresses: the depth carries into the angle)
float texel(int a) {
    a = (a + 0x1000000) & 0xffff;
    return texelFetch(tex, ivec2(a & 255, a >> 8), 0).r;
}

// The layer value at (depth u, angle v) in texels; g = the footprint of an output pixel (du, dv per x and y).
float layer(vec2 q, vec2 gx, vec2 gy) {
    if (max(length(gx), length(gy)) < 1.0) {
        vec2 s = q - 0.5;
        vec2 f = fract(s);
        ivec2 i = ivec2(floor(s));
        int a = i.y * 256 + i.x;
        return mix(mix(texel(a), texel(a + 1), f.x), mix(texel(a + 256), texel(a + 257), f.x), f.y);
    }
    // minified: the strip's one-row carry at depth 256 k lies inside the filter footprint
    float k = floor(q.x / 256.0);
    vec2 w = vec2(q.x - 256.0 * k, q.y + k);
    return textureGrad(tex, w / 256.0, gx / 256.0, gy / 256.0).r;
}

void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec2 d = p - vec2(160.0, 100.0);
    float r = max(length(d), 1e-3);
    float angle = atan(d.y, -d.x) * (256.0 / kTau);   // 0 at the left, 64 at the bottom
    float depth = 5120.0 / (r + 18.0);
    // derivatives per VGA unit, then per output pixel
    float dr = -5120.0 / ((r + 18.0) * (r + 18.0));
    vec2 gu = dr * d / r;                               // grad depth
    vec2 ga = (256.0 / kTau) * vec2(d.y, -d.x) / (r * r);   // grad angle
    vec2 gx = vec2(gu.x, ga.x) * px.x, gy = vec2(gu.y, ga.y) * px.y;
    float c1 = layer(vec2(depth, angle) + off.xy, gx, gy);
    float c2 = layer(vec2(depth, angle) + off.zw, gx, gy);
    vec3 c = texture(colours, (vec2(c1, c2) + 0.5) / 16.0).rgb;
    o = vec4(clamp(c * mul + add, 0.0, 1.0), 1.0);
}
)";

class CloudFx {
public:
    CloudFx() = default;
    CloudFx(const CloudFx&) = delete;
    CloudFx& operator=(const CloudFx&) = delete;
    ~CloudFx() {
        tex_.destroy();
        colours_.destroy();
        if (shader_.id()) gl::DeleteProgram(shader_.id());
    }

    // Files 30 (texture) and 31 (the 16x16 palette, 6-bit).
    void init(const StarsExe& exe) {
        shader_.build(kFullscreenVS, kCloudFS, "cloud");
        const std::vector<uint8_t>& f30 = exe.file(30);
        std::vector<float> lv(65536);
        for (size_t i = 0; i < lv.size(); i++) lv[i] = float(f30[i] & 15);
        tex_.createRaw(256, 256, GL_R16F, GL_RED, GL_FLOAT, lv.data(), true, true, true);
        Palette pal = vgaPalette(exe.fileSpan(31));
        std::vector<float> rgb(256 * 3);
        for (size_t i = 0; i < 256; i++) {
            rgb[i * 3 + 0] = pal[i].x;
            rgb[i * 3 + 1] = pal[i].y;
            rgb[i * 3 + 2] = pal[i].z;
        }
        colours_.createRaw(16, 16, GL_RGBA16F, GL_RGB, GL_FLOAT, rgb.data(), true, false, false);
    }

    // Draws the effect over the bound target (the whole frame): colour = clamp(file 31 colour * mul + add).
    void draw(Ctx& c, const Offsets& o, Vec3 mul, Vec3 add = Vec3(0, 0, 0)) const {
        setBlend(Blend::None);
        shader_.use();
        tex_.bind(0);
        colours_.bind(1);
        shader_.set("tex", 0);
        shader_.set("colours", 1);
        double d1 = o.d1, a1 = o.a1, d2 = o.d2, a2 = o.a2;
        normalise(d1, a1);
        normalise(d2, a2);
        shader_.set("off", float(d1), float(a1), float(d2), float(a2));
        shader_.set("px", 320.0f / float(c.w), 200.0f / float(c.h));
        shader_.set("mul", mul);
        shader_.set("add", add);
        drawFullscreen();
    }

private:
    // An offset matters as a strip address (a * 256 + d, modulo 65,536): move whole rows of the depth into the angle
    // and wrap both to 0..256 (float precision in the shader).
    static void normalise(double& d, double& a) {
        double rows = std::floor(d / 256.0);
        d -= 256.0 * rows;
        a += rows;
        a -= 256.0 * std::floor(a / 256.0);
    }

    Shader shader_;
    Texture tex_, colours_;
};

}  // namespace cloudfx

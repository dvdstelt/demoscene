// Gaussian replacement of the four-neighbour blur 0xf47 / 0xf90 (paper/re/notes-init.md §8.11). See blur.h.
#include "paper/blur.h"

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "paper/gpu.h"

namespace paper {
namespace {

const char* kFetchGLSL = R"(
uniform int edge;   // 0 zero, 1 clamp, 2 wrap
vec4 fetch(sampler2D t, ivec2 p) {
    ivec2 s = textureSize(t, 0);
    if (edge == 2) p = ((p % s) + s) % s;
    else if (edge == 1) p = clamp(p, ivec2(0), s - 1);
    else if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, s))) return vec4(0.0);
    return texelFetch(t, p, 0);
}
)";

const char* kDownFS = R"(
uniform sampler2D src;
uniform ivec2 f;
out vec4 o;
void main() {
    ivec2 b = ivec2(gl_FragCoord.xy) * f;
    vec4 s = vec4(0.0);
    for (int y = 0; y < f.y; y++)
        for (int x = 0; x < f.x; x++) s += fetch(src, b + ivec2(x, y));
    o = s / float(f.x * f.y);
}
)";

const char* kGaussFS = R"(
uniform sampler2D src;
uniform ivec2 dir;
uniform float sigma;
uniform int radius;
out vec4 o;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 s = vec4(0.0);
    float ws = 0.0;
    for (int i = -radius; i <= radius; i++) {
        float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        s += w * fetch(src, p + i * dir);
        ws += w;
    }
    o = s / ws;
}
)";

const char* kUpFS = R"(
uniform sampler2D src;
uniform vec2 f;
out vec4 o;
void main() {
    vec2 q = gl_FragCoord.xy / f - 0.5;   // low-res texel-centre coordinates
    vec2 i = floor(q), a = q - i;
    ivec2 b = ivec2(i);
    o = mix(mix(fetch(src, b), fetch(src, b + ivec2(1, 0)), a.x),
            mix(fetch(src, b + ivec2(0, 1)), fetch(src, b + ivec2(1, 1)), a.x), a.y);
}
)";

struct BlurShaders {
    Shader down, gauss, up;
    explicit BlurShaders(Ctx&) {
        std::string head = kFetchGLSL;
        down.build(kFullscreenVS, head + kDownFS, "paper blur down");
        gauss.build(kFullscreenVS, head + kGaussFS, "paper blur gauss");
        up.build(kFullscreenVS, head + kUpFS, "paper blur up");
    }
};

void pass(const Shader& s, GLuint src, Target& dst, Edge edge) {
    dst.bind();
    s.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, src);
    s.set("src", 0);
    s.set("edge", int(edge));
    drawFullscreen();
}

void gauss(const BlurShaders& sh, GLuint src, Target& dst, Edge edge, int dx, int dy, double sigma) {
    sh.gauss.use();
    gl::Uniform2i(sh.gauss.loc("dir"), dx, dy);
    sh.gauss.set("sigma", float(std::max(sigma, 1e-3)));
    sh.gauss.set("radius", sigma < 1e-3 ? 0 : int(std::ceil(3.0 * sigma)));
    pass(sh.gauss, src, dst, edge);
}

// Downsampling factor for a sigma (target pixels): keeps the Gaussian radius below ~15 taps.
int factorFor(double sigma) { return sigma < 4.0 ? 1 : std::max(1, int(sigma / 4.0)); }

}  // namespace

double passesSigma(double passes) { return std::sqrt(0.5 * std::max(0.0, passes)); }

void blur(Ctx& c, Target& t, double passes, Edge edge, double origW, double origH, GLenum fmt) {
    double s = passesSigma(passes);
    blurSigma(c, t, s * t.w / origW, s * t.h / origH, edge, fmt);
}

void blurSigma(Ctx& c, Target& t, double sx, double sy, Edge edge, GLenum fmt) {
    if (sx <= 0 && sy <= 0) return;
    const BlurShaders& sh = c.shared<BlurShaders>();
    GlState saved;
    gl::Disable(GL_BLEND);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_STENCIL_TEST);
    int fx = factorFor(sx), fy = factorFor(sy);
    if (fx == 1 && fy == 1) {
        auto tmp = c.tempSized(t.w, t.h, false, fmt);
        gauss(sh, t.tex, *tmp, edge, 1, 0, sx);
        gauss(sh, tmp->tex, t, edge, 0, 1, sy);
    } else {
        // box down by (fx, fy), Gaussian at low resolution, bilinear up; the resampling adds about
        // (f^2 - 1)/12 + f^2/6 px^2 of variance, which the low-resolution Gaussian leaves out
        int lw = (t.w + fx - 1) / fx, lh = (t.h + fy - 1) / fy;
        auto a = c.tempSized(lw, lh, false, fmt), b = c.tempSized(lw, lh, false, fmt);
        sh.down.use();
        gl::Uniform2i(sh.down.loc("f"), fx, fy);
        pass(sh.down, t.tex, *a, edge);
        auto lowSigma = [](double s, int f) {
            double v = s * s - (f * f - 1) / 12.0 - f * f / 6.0;
            return std::sqrt(std::max(0.0, v)) / f;
        };
        gauss(sh, a->tex, *b, edge, 1, 0, lowSigma(sx, fx));
        gauss(sh, b->tex, *a, edge, 0, 1, lowSigma(sy, fy));
        sh.up.use();
        sh.up.set("f", float(fx), float(fy));
        pass(sh.up, a->tex, t, edge);
    }
}

}  // namespace paper

// Page peel 0x2b55 / 0x2b20 / 0x2b6c / 0x2ca2 with the parabolas of 0x2ad0, evaluated per output row
// (paper/re/notes-scene3d.md §9). See peel.h for the geometry and the quirk decisions.
#include "paper/peel.h"

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "paper/common.h"
#include "paper/textures.h"

namespace paper {

int peelProgressAt(int tick, int firstTick) {
    int k = tick - firstTick + 1;  // 0x2b20 calls since the reset
    if (k <= 0) return 0;
    if (k <= 64) return 4 * k;
    return std::min(700, 256 + 7 * (k - 64));
}

double peelProgress(double tick, int firstTick) {
    double f = std::floor(tick);
    int t = int(f);
    double a = peelProgressAt(t, firstTick), b = peelProgressAt(t + 1, firstTick);
    return a + (b - a) * (tick - f);
}

// parabola through (p0, p1, 0) at s = 0, 1, 2:  p0 + (B s + A s^2) / 2
static double para(double p0, double p1, double s) {
    double B = 4 * p1 - 3 * p0, A = p0 - 2 * p1;
    return p0 + 0.5 * (B * s + A * s * s);
}

double PeelCurves::a(double s) const { return para(a0, a1, s); }
double PeelCurves::b(double s) const { return para(b0, b1, s); }
double PeelCurves::da(double s) const { return 0.5 * ((4 * a1 - 3 * a0) + 2 * (a0 - 2 * a1) * s); }

PeelCurves peelCurves(double p) {
    PeelCurves c;
    c.a0 = 200.0 * cosTurn(p);
    c.a1 = 100.0 * cosTurn(p / 2);
    c.b0 = -1600.0 * sinTurn(p);
    c.b1 = -800.0 * sinTurn(p / 2);
    return c;
}

PeelHit peelRow(const PeelCurves& c, double y) {
    double B = 4 * c.a1 - 3 * c.a0, A = c.a0 - 2 * c.a1;
    // 0.5 A s^2 + 0.5 B s + (a0 - y) = 0
    double qa = 0.5 * A, qb = 0.5 * B, qc = c.a0 - y;
    double roots[2];
    int n = 0;
    if (std::fabs(qa) < 1e-9) {
        if (std::fabs(qb) > 1e-12) roots[n++] = -qc / qb;
    } else {
        double d = qb * qb - 4 * qa * qc;
        if (d >= 0) {
            double r = std::sqrt(d);
            roots[n++] = (-qb - r) / (2 * qa);
            roots[n++] = (-qb + r) / (2 * qa);
        }
    }
    PeelHit best;
    best.s = -1;
    for (int i = 0; i < n; i++) {
        double s = roots[i];
        if (s < 0 || s > 2 || s <= best.s) continue;
        bool back = c.da(s) >= 0;
        if (!back) {
            // front: visible only at the running minimum of a over [0, s]
            double m = c.a(0);
            double v = qa != 0 ? -qb / (2 * qa) : -1;
            if (qa > 0 && v > 0 && v < s) m = std::min(m, c.a(v));
            if (y > m + 1e-9) continue;
        }
        best.kind = back ? 2 : 1;
        best.s = s;
    }
    if (best.s < 0) return PeelHit{};
    best.srcRow = 200 - 100 * best.s;
    best.sc = 1 + c.b(best.s) / 2048.0;
    return best;
}

namespace {

const char* kPeelVS = R"(
out vec2 vga;
out vec2 uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    vga = vec2(p.x * 320.0, (1.0 - p.y) * 200.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kPeelFS = R"(
in vec2 vga;
in vec2 uv;
out vec4 o;
uniform sampler2D oldTex, newTex, grain;
uniform vec4 curves;      // a0, a1, b0, b1
uniform float rowsPerPx;  // VGA rows per output pixel
float para(float p0, float p1, float s) { return p0 + 0.5 * ((4.0 * p1 - 3.0 * p0) * s + (p0 - 2.0 * p1) * s * s); }
// kind 0 new picture, 1 front of the old picture, 2 back of the page
int classify(float y, out float sOut) {
    float a0 = curves.x, a1 = curves.y;
    float qa = 0.5 * (a0 - 2.0 * a1), qb = 0.5 * (4.0 * a1 - 3.0 * a0), qc = a0 - y;
    float r[2];
    int n = 0;
    if (abs(qa) < 1e-6) {
        if (abs(qb) > 1e-9) { r[0] = -qc / qb; n = 1; }
    } else {
        float d = qb * qb - 4.0 * qa * qc;
        if (d >= 0.0) {
            float q = -0.5 * (qb + (qb >= 0.0 ? 1.0 : -1.0) * sqrt(d));   // stable roots
            r[0] = q / qa;
            r[1] = abs(q) > 1e-9 ? qc / q : r[0];
            n = 2;
        }
    }
    int kind = 0;
    float best = -1.0;
    for (int i = 0; i < n; i++) {
        float s = r[i];
        if (s < 0.0 || s > 2.0 || s <= best) continue;
        bool back = qb + 2.0 * qa * s >= 0.0;
        if (!back) {
            float m = a0;
            float v = -qb / (2.0 * qa);
            if (qa > 0.0 && v > 0.0 && v < s) m = min(m, para(a0, a1, v));
            if (y > m + 1e-4) continue;
        }
        kind = back ? 2 : 1;
        best = s;
    }
    sOut = best;
    return kind;
}
vec3 shade(int kind, float s, vec2 pix) {
    if (kind == 0) return texture(newTex, pix).rgb;
    float sc = 1.0 + para(curves.z, curves.w, s) / 2048.0;
    vec2 src = vec2(160.0 + (vga.x - 160.0) * sc, 200.0 - 100.0 * s);
    if (kind == 2) return pageBack(grain, src);
    src.x = clamp(src.x, 0.0, 320.0);
    return texBicubic(oldTex, vec2(src.x / 320.0, 1.0 - src.y / 200.0)).rgb;
}
void main() {
    float s;
    int k = classify(vga.y, s);
    float ss[4];
    int kk[4];
    bool same = true;
    for (int i = 0; i < 4; i++) {
        kk[i] = classify(vga.y + (float(i) - 1.5) * 0.25 * rowsPerPx, ss[i]);
        same = same && kk[i] == k;
    }
    vec3 c;
    if (same) c = shade(k, s, uv);
    else {
        c = vec3(0.0);
        for (int i = 0; i < 4; i++) c += shade(kk[i], ss[i], uv);
        c *= 0.25;
    }
    o = vec4(c, 1.0);
}
)";

}  // namespace

Peel::Peel(Ctx&) {
    shader_.build(kPeelVS, std::string(kTexturesGLSL) + kPeelFS, "paper peel");
}

void Peel::draw(Ctx& c, GLuint oldTex, GLuint newTex, double p) {
    CoreTextures& tx = c.shared<CoreTextures>();
    PeelCurves pc = peelCurves(p);
    GLint vp[4] = {0, 0, 1, 1};
    gl::GetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
    gl::Disable(GL_BLEND);
    shader_.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, oldTex);
    gl::ActiveTexture(GL_TEXTURE0 + 1);
    gl::BindTexture(GL_TEXTURE_2D, newTex);
    tx.grain.bind(2);
    shader_.set("oldTex", 0);
    shader_.set("newTex", 1);
    shader_.set("grain", 2);
    shader_.set("curves", float(pc.a0), float(pc.a1), float(pc.b0), float(pc.b1));
    shader_.set("rowsPerPx", 200.0f / float(std::max(1, vp[3])));
    drawFullscreen();
    gl::ActiveTexture(GL_TEXTURE0);
}

}  // namespace paper

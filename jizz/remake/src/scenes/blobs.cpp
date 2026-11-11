// The gold metaballs, part 0x10fda (11:00..15:00, jizz/re/notes-parts3d-b.md section 5): six balls on the paths of
// 0x7aae inside Nix's engine scene 4 (script 0x159cb: a sphere with map5, seen from inside along camera path 4),
// drawn with the colour tables 0x1a336(-50) at gain 0x132 (0x11085, every frame), negative during event 0x1400
// (14:00..14:10, [0x1a478] = 0xff), with the timer fades from white 0x17979(0x325aa0) at the start and
// 0x17979(0x36ee80) at 13:00 (event 0x1301), 0x17999 every frame.
//
// The original polygonises the field every frame (0x769f: surface-following marching cubes 0x77ad / 0x6e74 on a
// 64-unit grid, field 0x7a4e from the 1/sqrt table 0x662f, normals and texture coordinates 0x79a8). The remake ray
// marches the surface that grid approximates, sum_i 1 / |p - c_i| = 1 / 84, per pixel on the GPU (march() below:
// certified steps, so thin necks between balls are not skipped), with the normal sum (p - c_i) / |p - c_i|^3 of
// 0x79a8 and its texture coordinates 128 n.xy texels (map10, slot [0x6b6c]) per pixel, read with derivatives from
// the field's Hessian, shaded with the engine's pixel formula (kEngineGLSL). Silhouettes are anti-aliased with the
// coverage of a straight edge from their distance to the pixel centre. The blobs always lie inside the dome (the
// rays end at its inner radius), so they are drawn over the engine's picture of it.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "jizz/core.h"
#include "jizz/engine.h"
#include "jizz/textures.h"

namespace {

using namespace jizz;

constexpr int kStart = 0x1100;             // event 0x1100 at 11:00
constexpr int kEventFade = 0x1301;         // 13:00: fade from white again (the part sets the event to 0x1300)
constexpr int kEventNegative = 0x1400;     // 14:00..14:10: [0x1a478] = 0xff
constexpr uint32_t kFadeStart = 0x325aa0;  // 0x11030: 1.12 s
constexpr uint32_t kFadeEvent = 0x36ee80;  // 0x1104b: 1.02 s
constexpr int kDetail = 6;                 // dome segments per original segment
constexpr int kBalls = 6;

const char* kBlobFS = R"(
uniform vec3 uBall[6];
uniform float uBound[6];  // radii of spheres about the balls whose union contains the surface
uniform vec3 uEye, uRight, uUp, uFwd;
uniform float uFocal;     // F in pixels of the 320-wide frame
uniform vec2 uSize;       // output size in pixels
uniform float uRadius;    // the surface is sum 1 / |p - c_i| = 1 / uRadius
uniform vec3 uDomeC;      // the dome's centre and inner radius: rays end there
uniform float uDomeR;
uniform vec3 uESF;
uniform sampler2D uMap;   // the blob's texture slot
out vec4 o;

// view ray through the window position f (pixels, GL orientation); the fwd component is 1 (0x19118)
vec3 rayAt(vec2 f) {
    vec2 q = vec2(f.x / uSize.x * 320.0, (1.0 - f.y / uSize.y) * 200.0);
    return uFwd + uRight * ((q.x - 160.0) / uFocal) + uUp * ((100.0 - q.y) / uFocal);
}

float domeExit(vec3 ro, vec3 rd) {
    vec3 oc = ro - uDomeC;
    float b = dot(oc, rd), c = dot(oc, oc) - uDomeR * uDomeR;
    return -b + sqrt(max(b * b - c, 0.0));
}

// the part of the ray inside the union of the bounding spheres (uBall[i], uBound[i]): [x, y], empty if x > y
vec2 bounds(vec3 ro, vec3 rd) {
    vec2 r = vec2(1e20, -1e20);
    for (int i = 0; i < 6; i++) {
        vec3 oc = ro - uBall[i];
        float b = dot(oc, rd), h = b * b - dot(oc, oc) + uBound[i] * uBound[i];
        if (h > 0.0) {
            h = sqrt(h);
            r = vec2(min(r.x, -b - h), max(r.y, -b + h));
        }
    }
    return vec2(max(r.x, 0.0), min(r.y, domeExit(ro, rd)));
}

// The field along the current ray: ball i is at p(t) - c_i = rd (t - gB[i]) - gQ[i] (gQ[i] perpendicular to rd,
// gA[i] = |gQ[i]|^2).
float gA[6], gB[6];
vec3 gQ[6];

void setRay(vec3 ro, vec3 rd) {
    for (int i = 0; i < 6; i++) {
        vec3 oc = uBall[i] - ro;
        gB[i] = dot(oc, rd);
        gQ[i] = oc - rd * gB[i];
        gA[i] = dot(gQ[i], gQ[i]);
    }
}

// At t: g = R sum 1/d_i - 1 (> 0 inside), its derivative along the ray, and r, a lower bound of the distance to
// the surface (outside), the larger of two:
//   d_min (1 - R S): within d of p, S <= sum 1/(d_i - d) <= S d_min / (d_min - d) <= 1/R;
//   the root x of S + |grad S| x + 8 x^2 sum 1/d_i^3 = 1/R, capped at d_min / 2: within x <= d_min / 2 every
//   second directional derivative of 1/d_i is at most 2 / (d_i / 2)^3 (near the surface this is about the true
//   distance, also where several balls meet and the first bound is poor).
float field(float t, out float dg, out float r) {
    float s = 0.0, m = 0.0, su = 0.0, h = 0.0;
    vec3 qv = vec3(0.0);
    for (int i = 0; i < 6; i++) {
        float u = t - gB[i], w = inversesqrt(gA[i] + u * u), w3 = w * w * w;
        s += w;
        m = max(m, w);
        su += u * w3;
        qv += gQ[i] * w3;
        h += w3;
    }
    dg = -uRadius * su;
    float c = 1.0 / uRadius - s;
    r = uRadius * c / m;
    if (c > 0.0) {
        float G = sqrt(su * su + dot(qv, qv)), H = 16.0 * h;
        r = max(r, min(2.0 * c / (G + sqrt(G * G + 2.0 * H * c)), 0.5 / m));
    }
    return uRadius * s - 1.0;
}

// An upper bound of S'' on [t, t + x]: per ball (2 u^2 - a) / (a + u^2)^(5/2), its largest |u| in the numerator
// and its smallest in the denominator (negative parts, near the closest approach, count as 0).
float curvature(float t, float x) {
    float m = 0.0;
    for (int i = 0; i < 6; i++) {
        float u0 = t - gB[i], u1 = u0 + x;
        float umin = u0 <= 0.0 && u1 >= 0.0 ? 0.0 : min(abs(u0), abs(u1));
        float w = inversesqrt(gA[i] + umin * umin), w2 = w * w;
        m += max(2.0 * max(u0 * u0, u1 * u1) - gA[i], 0.0) * w2 * w2 * w;
    }
    return m;
}

// no point after t can be inside: the balls ahead taken at their closest approach, the others at t
bool cannotHit(float t) {
    float s = 0.0;
    for (int i = 0; i < 6; i++) {
        float u = max(t - gB[i], 0.0);
        s += inversesqrt(gA[i] + u * u);
    }
    return uRadius * s < 1.0;
}

// the root in [lo, hi] (g(lo) <= 0 < g(hi)): Newton from hi, bisection when it leaves the bracket
float refine(float lo, float hi, float eps) {
    float t = hi;
    for (int k = 0; k < 8; k++) {
        float dg, r;
        float g = field(t, dg, r);
        if (g > 0.0) hi = t; else lo = t;
        if (hi - lo < eps) break;
        float tn = t - g / dg;
        t = (tn > lo && tn < hi) ? tn : 0.5 * (lo + hi);
    }
    return t;
}

// Where S has a maximum along the ray in [a, b] (dS/dt from + to -): regula falsi on dS/dt. Returns the bound
// there (about the distance of the silhouette the ray passes) and its position.
float closest(float a, float b, float da, float db, out float tm) {
    float dm, rm;
    tm = b;
    for (int j = 0; j < 3; j++) {
        tm = a + (b - a) * da / (da - db);
        field(tm, dm, rm);
        if (dm > 0.0) { a = tm; da = dm; } else { b = tm; db = dm; }
    }
    field(tm, dm, rm);
    return rm;
}

// Distance along rd to the surface, or -1; pass = (distance / t) of the nearest silhouette the ray passed without
// hitting it (where S has a maximum along the ray, within two pixels of the surface), tPass = where. Each step is
// the larger of two that cannot cross the surface: the bound of field() (a sphere about the point) and the root of
// S + S' x + M x^2 / 2 = 1/R along the ray, M = curvature() over the Newton step (= the Newton step where S is
// concave: grazing rays and oblique approaches). A Newton step a little past the root brackets it where S is
// convex (then Newton / bisection); a receding ray stops as soon as nothing ahead can reach the surface.
float march(vec3 ro, vec3 rd, float pix, out float pass, out float tPass) {
    pass = 1e20;
    tPass = 0.0;
    vec2 iv = bounds(ro, rd);
    if (iv.x > iv.y) return -1.0;
    setRay(ro, rd);
    float t = iv.x, tPrev = t, dgPrev = 0.0, rPrev = 1e20;
    for (int k = 0; k < 96; k++) {
        float dg, r;
        float g = field(t, dg, r);
        if (r < 0.02 * pix * t) return t;
        if (dgPrev > 0.0 && dg <= 0.0 && min(rPrev / tPrev, r / t) < 2.0 * pix) {   // passed a silhouette closely
            float tm, rm = closest(tPrev, t, dgPrev, dg, tm);
            if (rm / tm < pass) {
                pass = rm / tm;
                tPass = tm;
            }
        }
        if (dg <= 0.0) {
            if (cannotHit(t)) return -1.0;
        } else if (r < 8.0 * pix * t) {
            float tn = t - g / dg;
            if (tn < t + 8.0 * r) {
                float tp = tn + 0.02 * (tn - t) + 0.02 * pix * t, dn, rn;
                if (field(tp, dn, rn) > 0.0) return refine(t + r, tp, 0.01 * pix * t);
            }
        }
        float c = -g / uRadius, sd = dg / uRadius;   // 1/R - S, S'
        float x1 = sd > 0.0 ? c / sd : 64.0 * r;
        float m = curvature(t, x1);
        float x2 = (sd > 0.0 || m > 0.0) ? 2.0 * c / (sd + sqrt(sd * sd + 2.0 * m * c)) : x1;
        tPrev = t;
        dgPrev = dg;
        rPrev = r;
        t += max(r, min(x1, x2));
        if (t > iv.y) return -1.0;
    }
    if (rPrev / tPrev < pass) {  // step cap (not seen in tests): count it as a pass where it stopped
        pass = rPrev / tPrev;
        tPass = tPrev;
    }
    return -1.0;
}

struct Surf {
    vec3 n;        // unit normal (0x79a8)
    vec2 uv;       // texture coordinates in repeats: 128 n.xy texels
    vec2 dx, dy;   // their derivatives per output pixel
    float sil;     // distance to the silhouette in output pixels (linear estimate)
    vec2 toEdge;   // screen direction towards the silhouette (GL window orientation)
};

Surf surface(vec3 ro, vec3 D, float t) {
    float L = length(D);
    vec3 rd = D / L, p = ro + rd * t;
    vec3 a = vec3(0.0);
    mat3 J = mat3(0.0);   // d a / d p
    for (int i = 0; i < 6; i++) {
        vec3 r = p - uBall[i];
        float d2 = dot(r, r), inv3 = inversesqrt(d2) / d2, inv5 = inv3 / d2;
        a += r * inv3;
        J += mat3(inv3) - outerProduct(r, r) * (3.0 * inv5);
    }
    float la = length(a);
    Surf s;
    s.n = a / la;
    // the hit point moves with the ray: dp = (t / L) (dD - D (n.dD) / (n.D))
    vec3 dDx = uRight * (320.0 / (uSize.x * uFocal)), dDy = uUp * (200.0 / (uSize.y * uFocal));
    float nD = dot(s.n, D);
    nD = nD < 0.0 ? min(nD, -1e-4 * L) : max(nD, 1e-4 * L);
    float k = t / L;
    vec3 dpx = k * (dDx - D * (dot(s.n, dDx) / nD)), dpy = k * (dDy - D * (dot(s.n, dDy) / nD));
    vec3 dnx = J * dpx, dny = J * dpy;
    dnx = (dnx - s.n * dot(s.n, dnx)) / la;
    dny = (dny - s.n * dot(s.n, dny)) / la;
    s.uv = 0.5 * s.n.xy;
    s.dx = 0.5 * dnx.xy;
    s.dy = 0.5 * dny.xy;
    float lx = length(s.dx), ly = length(s.dy);
    if (lx > 1.0) s.dx /= lx;
    if (ly > 1.0) s.dy /= ly;
    // silhouette where n.rd = 0
    float g = dot(s.n, rd);
    float gx = dot(dnx, rd) + dot(s.n, dDx - rd * dot(rd, dDx)) / L;
    float gy = dot(dny, rd) + dot(s.n, dDy - rd * dot(rd, dDy)) / L;
    s.sil = abs(g) / max(length(vec2(gx, gy)), 1e-12);
    s.toEdge = normalize(vec2(gx, gy) * -sign(g) + vec2(1e-12));
    return s;
}

vec3 shade(Surf s) {
    vec3 texel = textureGrad(uMap, s.uv, s.dx, s.dy).rgb;
    return engineColour(texel, s.n, uRight, uUp, uESF);
}

// Anti-aliasing with the coverage of a straight edge (box filter: 0.5 + the signed distance of the silhouette from
// the pixel centre, in pixels), up to two layers: the surface the ray hits, covering 0.5 + its distance inside its
// own silhouette, over what a ray a pixel beyond that silhouette finds (another blob or the dome); and in front of
// it a silhouette the ray passed within half a pixel (0.5 - the distance), shaded where the ray passed it.
void main() {
    vec2 f = gl_FragCoord.xy;
    float pix = 320.0 / (uSize.x * uFocal);   // angle of one output pixel (the wider axis)
    vec3 D = rayAt(f);
    float pass, tPass;
    float t = march(uEye, normalize(D), pix, pass, tPass);
    vec4 c = vec4(0.0);   // premultiplied
    if (t >= 0.0) {
        Surf s = surface(uEye, D, t);
        // n.rd ~ sqrt(distance to the silhouette): its linear estimate is twice the distance (checked against
        // renders at 4x the resolution: mean coverage error 9 / 255 along the edges)
        float inside = 0.5 * s.sil;
        vec3 col = shade(s);
        c = vec4(col, 1.0);
        if (inside < 0.5) {   // own silhouette within half a pixel: a ray beyond it finds what lies behind
            vec3 Db = rayAt(f + s.toEdge * (inside + 1.0));
            float pb, tpb;
            float tb = march(uEye, normalize(Db), pix, pb, tpb);
            float cov = 0.5 + inside;
            c = vec4(col, 1.0) * cov;
            if (tb >= 0.0) c += vec4(shade(surface(uEye, Db, tb)), 1.0) * (1.0 - cov);
        }
    }
    float passPx = pass / pix;   // the closest pass of a silhouette before the end of the ray, in pixels
    if (passPx < 0.5) {
        float cov = 0.5 - passPx;
        c = vec4(shade(surface(uEye, D, tPass)), 1.0) * cov + c * (1.0 - cov);
    }
    if (c.a <= 0.0) discard;
    o = c;
}
)";

class BlobsScene : public Scene {
public:
    void init(Ctx& c) override {
        jizz::prepare(c);
        const JizzExe& exe = *c.exe;
        tex_ = c.shared<Textures>().slotIds(true);  // slot 1 = map5 since the tunnel part (0x169f3..0x16a0c)
        dome_.build(exe, 4, kDetail);
        c.shared<Engine>();
        for (int i = 0; i < kBalls; i++) {  // 0x7aae's speeds
            k1_[i] = exe.s32(0x78a0 + 4 * uint32_t(i));
            k2_[i] = exe.s32(0x78d0 + 4 * uint32_t(i));
            k3_[i] = exe.s32(0x7900 + 4 * uint32_t(i));
        }
        // 0x7a4e: field = (21 * sum g(d^2)) >> 7, inside at >= 128, with 0x662f's g = 16 RSQ[0] / d
        // (RSQ[0] = 4096 at 0x6716): sum 1/d >= 128 * 128 / (21 * 16 * RSQ[0]) = 1 / 84
        radius_ = float(21.0 * 16.0 * double(exe.s32(0x6716)) / (128.0 * 128.0));
        domeLimits();
        std::string fs = std::string(kEngineGLSL) + kBlobFS;
        if (!shader_.build(kFullscreenVS, fs, "blobs")) SDL_Log("blobs: shader failed");
        slot_ = exe.u8(0x6b6c);  // 0x769f's face texture slot (13: map10)
    }

    void render(Ctx& c, double t) override {
        const double t0 = c.positionTime(kStart);
        double since = 0;
        const int ev = c.event(t, &since);
        const double clocks = c.timer(t) - c.timer(t0);  // xmpGetTimer + [0x6542]
        EngineCamera cam = dome_.camera(clocks, false);
        EngineShading sh;
        sh.tex = tex_;
        sh.E = -50;  // 0x11085: [0x1a225] = 0x132; 0x1f5c5(-50)
        sh.S = 0x132;
        sh.F = ev == kEventNegative ? 255.0f : 0.0f;

        c.clear(kBlack);  // never visible: the dome covers the frame
        c.shared<Engine>().render(c, dome_, cam, EngineViewport::full(), sh);
        drawBlobs(c, cam, sh, clocks);

        timerFade(c, t, t0, kFadeStart);
        if (ev == kEventFade) timerFade(c, t, since, kFadeEvent);
    }

private:
    // 0x7aae with the continuous timer: t = clocks * 710000 / 2^32 steps, angles 3 k t / 32 1024ths of a turn
    void balls(double clocks, float* out) const {
        const double steps = clocks * 710000.0 / 4294967296.0, turn = 2.0 * 3.14159265358979323846 / 1024.0;
        for (int i = 0; i < kBalls; i++) {
            double a = 3.0 * k1_[i] * steps / 32.0 * turn, b = 3.0 * k2_[i] * steps / 32.0 * turn,
                   cc = 3.0 * k3_[i] * steps / 32.0 * turn;
            double r = 490.0 + 256.0 * std::sin(cc);       // (SIN[c] >> 2) + 490
            out[3 * i + 0] = float(r * std::sin(a));        // (SIN[a] r) >> 10
            out[3 * i + 1] = float(0.5 * r * std::cos(a));  // (SIN[a + 256] r) >> 11
            out[3 * i + 2] = float(512.0 * std::sin(b));    // SIN[b] >> 1
        }
    }

    // Radii R_i such that the spheres (c_i, R_i) together contain the surface: a point whose nearest ball i is at
    // distance rho has d_j >= max(rho, D_ij - rho) for the others, so sum 1/d <= 1/rho + sum_j 1/max(rho, D_ij - rho);
    // R_i is the top of the highest interval [a, a + 1] on which an upper bound of that reaches 1/84.
    void boundRadii(const float* b, float* R) const {
        const double target = 1.0 / radius_;
        for (int i = 0; i < kBalls; i++) {
            double D[kBalls];
            for (int j = 0; j < kBalls; j++)
                D[j] = std::hypot(double(b[3 * j] - b[3 * i]), double(b[3 * j + 1] - b[3 * i + 1]),
                                  double(b[3 * j + 2] - b[3 * i + 2]));
            R[i] = 0;
            for (double a = std::ceil(kBalls * radius_); a >= 0; a -= 1) {  // beyond 6 R even six balls in one
                double h = 1.0 / std::max(a, 1e-3);
                for (int j = 0; j < kBalls; j++)
                    if (j != i) h += 1.0 / std::max(std::max(a, D[j] - a - 1.0), 1e-3);
                if (h >= target) {
                    R[i] = float(a + 2.0);  // the interval's top plus a margin
                    break;
                }
            }
        }
    }

    // the dome's centre and the radius of the largest sphere about it inside all of its faces
    void domeLimits() {
        const EngineGeometry& g = dome_.geometry();
        const EngineObject& o = g.objects[0];
        double cx = 0, cy = 0, cz = 0;
        for (uint32_t i = 0; i < o.vertexCount; i++) {
            const EngineVertex& v = g.vertices[o.firstVertex + i];
            cx += v.p[0];
            cy += v.p[1];
            cz += v.p[2];
        }
        domeC_ = Vec3(float(cx / o.vertexCount), float(cy / o.vertexCount), float(cz / o.vertexCount));
        domeR_ = 1e30f;
        for (uint32_t k = 0; k < o.faceCount; k++) {
            const EngineFace& f = g.faces[o.firstFace + k];
            auto P = [&](uint32_t i) { const float* p = g.vertices[i].p; return Vec3(p[0], p[1], p[2]) - domeC_; };
            Vec3 a = P(f.a), b = P(f.b), cc = P(f.c);
            Vec3 n = (b - a).cross(cc - a);
            if (n.len() < 1e-6f) continue;  // the pinched faces at the poles
            float d = std::fabs(n.norm().dot(a));
            if (d < domeR_) domeR_ = d;
        }
    }

    void drawBlobs(Ctx& c, const EngineCamera& cam, const EngineShading& sh, double clocks) {
        float b[3 * kBalls];
        balls(clocks, b);
        float R[kBalls];
        boundRadii(b, R);
        c.out->bind();
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        setBlend(Blend::Premul);
        shader_.use();
        gl::Uniform3fv(shader_.loc("uBall"), kBalls, b);
        gl::Uniform1fv(shader_.loc("uBound"), kBalls, R);
        shader_.set("uEye", cam.eye);
        shader_.set("uRight", cam.right);
        shader_.set("uUp", cam.up);
        shader_.set("uFwd", cam.fwd);
        shader_.set("uFocal", cam.focal);
        shader_.set("uSize", float(c.out->w), float(c.out->h));
        shader_.set("uRadius", radius_);
        shader_.set("uDomeC", domeC_);
        shader_.set("uDomeR", domeR_);
        shader_.set("uESF", sh.E / 255.0f, sh.S / 256.0f, sh.F / 256.0f);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, slot_ < tex_.size() ? tex_[slot_] : 0);
        shader_.set("uMap", 0);
        drawFullscreen();
        setBlend(Blend::None);
    }

    EngineScene dome_;
    Shader shader_;
    std::array<GLuint, 18> tex_{};
    int k1_[kBalls] = {}, k2_[kBalls] = {}, k3_[kBalls] = {};
    float radius_ = 84.0f;
    Vec3 domeC_;
    float domeR_ = 0;
    size_t slot_ = 13;
};

}  // namespace

REGISTER_SCENE(BlobsScene, "blobs");

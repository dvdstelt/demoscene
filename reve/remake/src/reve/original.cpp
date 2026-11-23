// REVE_ENGINE=original (engine.h, class Original): the original's 3D engine in its own integer arithmetic, drawing the
// 320 x 200 indexed frame exactly as REVE.EXE does, for pixel checks against the capture at --shotsize 320x200 (a port
// of reve/re/models/engine/engine.py, which reproduces the original's frames bit for bit; reve/re/notes-engine.md has
// every routine). Ports the camera 0x21db9c, the object setup and culls 0x21ddf8 / 0x21dfb8 (with their history: the
// outcode bytes 0x21eea8 and the box corners' screen positions persist across objects and frames), the object and
// face sorts, the vertex transform 0x21e54c, the near clip 0x21e864, the lighting 0x21e72c / 0x21e7c8, the polygon
// routines 0x21bd4c / 0x21c6b4 / 0x21cc28 with the screen clippers 0x21b14c / 0x21b42c / 0x21b70c / 0x21b9ec, PART4's
// boxes 0x25f26e and lines 0x21d22c, the AA pass 0x2045fa and FIN's blend 0x2046c7.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "demo.h"
#include "exe.h"
#include "reve/engine.h"

namespace reve3d {

namespace {
int16_t s16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int32_t s32(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
int8_t s8(int64_t v) { return int8_t(uint8_t(uint64_t(v))); }
int64_t idiv(int64_t a, int64_t b) { return b ? a / b : 0; }   // x86 idiv: truncated (b is never 0 where used)

constexpr uint32_t kSine = 0x2176c8;       // 5120 words, 4096 a turn, cosine at +1024
constexpr uint32_t kSqrt = 0x219ec8;       // 0x2175bc writes isqrt(i) for i < 1156; beyond it, the image's bytes
constexpr uint32_t kTextureName = 0x205ceb, kGouraudName = 0x205d18, kAntialiaName = 0x205d28;
}  // namespace

bool originalEngine() {
    static int on = -1;
    if (on < 0) {
        const char* e = SDL_getenv("REVE_ENGINE");
        on = e && std::strcmp(e, "original") == 0;
    }
    return on != 0;
}

struct Original::Impl {
    const World* w = nullptr;
    int16_t sine[5120] = {};
    uint8_t sqrtTab[2048] = {};
    std::vector<uint8_t> tex, gour, T;
    bool part4 = false;
    uint8_t outcodes[8] = {};
    // per object: the box corners' camera and screen positions (+0x7c), matrices, P
    struct Obj {
        int32_t pos[3] = {}, ang[3] = {}, pos16[3] = {};
        int32_t P[3] = {}, R[9] = {}, Cm[9] = {};
        int32_t corner[8][3] = {};
        int16_t sx[8] = {}, sy[8] = {};
    };
    std::vector<Obj> objs;
    int32_t M[9] = {}, Tt[3] = {}, h[3] = {};
    uint8_t* buf = nullptr;

    int S(int a) const { return sine[(a & 0x1ffe) >> 1]; }
    int C(int a) const { return sine[((a & 0x1ffe) >> 1) + 1024]; }
    static int16_t mul(int p, int q) { return s16((int64_t(p) * q) >> 15); }
    int sq(int64_t i) const { return i >= 0 && i < 2048 ? sqrtTab[i] : 0; }

    void camera(const int32_t cam[6]) {
        int a = s32(-int64_t(cam[5])) & 0x1ffe, b = s32(int64_t(cam[3]) + 0x1000) & 0x1ffe,
            c = s32(-int64_t(cam[4])) & 0x1ffe;
        int Sa = S(a), Ca = C(a), Sb = S(b), Cb = C(b), Sc = S(c), Cc = C(c);
        M[0] = s16(-s16(mul(Ca, Sb) + mul(mul(Cb, Sc), Sa)));
        M[3] = s16(mul(Sb, Sa) - mul(mul(Cb, Sc), Ca));
        M[6] = s16(-mul(Cb, Cc));
        M[1] = s16(mul(Ca, Cb) - mul(mul(Sb, Sc), Sa));
        M[4] = s16(-s16(mul(Cb, Sa) + mul(mul(Sb, Sc), Ca)));
        M[7] = s16(-mul(Sb, Cc));
        M[2] = mul(Sa, Cc);
        M[5] = mul(Ca, Cc);
        M[8] = s16(-Sc);
        for (int k = 0; k < 3; k++) h[k] = s32(-int64_t(cam[k])) >> 1;
        for (int r = 0; r < 3; r++) Tt[r] = s32(int64_t(M[3 * r]) * h[0] + int64_t(M[3 * r + 1]) * h[1] + int64_t(M[3 * r + 2]) * h[2]);
    }
    void rotation(int a, int b, int c, int32_t* R) const {
        int Sa = S(a), Ca = C(a), Sb = S(b), Cb = C(b), Sc = S(c), Cc = C(c);
        R[8] = s16(mul(Ca, Cb) - mul(mul(Sa, Sb), Sc));
        R[7] = s16(mul(Sa, Cb) + mul(mul(Ca, Sc), Sb));
        R[6] = s16(-mul(Sb, Cc));
        R[5] = s16(-mul(Sa, Cc));
        R[4] = mul(Cc, Ca);
        R[3] = Sc;
        R[2] = s16(mul(Ca, Sb) + mul(mul(Sa, Sc), Cb));
        R[1] = s16(mul(Sa, Sb) - mul(mul(Ca, Sc), Cb));
        R[0] = mul(Cc, Cb);
    }
    bool fits(int64_t v) const { int64_t hi = v >> 32; return hi == 0 || hi == -1; }

    bool setup(int i) {
        Obj& o = objs[size_t(i)];
        const Object& wo = w->objects[size_t(i)];
        int32_t R[9];
        rotation((o.ang[0] >> 16) & 0x1ffe, (o.ang[1] >> 16) & 0x1ffe, (o.ang[2] >> 16) & 0x1ffe, R);
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++)
                o.Cm[3 * r + k] = s32((int64_t(R[k]) * M[3 * r] + int64_t(R[3 + k]) * M[3 * r + 1] +
                                       int64_t(R[6 + k]) * M[3 * r + 2]) >> 15);
        rotation((-(o.ang[0] >> 16)) & 0x1ffe, (-(o.ang[1] >> 16)) & 0x1ffe, (-(o.ang[2] >> 16)) & 0x1ffe, o.R);
        for (int r = 0; r < 3; r++) {
            int64_t v = int64_t(M[3 * r]) * o.pos[0] + int64_t(M[3 * r + 1]) * o.pos[1] + int64_t(M[3 * r + 2]) * o.pos[2] + Tt[r];
            if (!fits(v)) return false;
            o.P[r] = s32(v);
        }
        if (o.P[2] >= 0x1b580000) return false;
        int k = 0;
        for (int c8 = 0; c8 < 8; c8++) {
            int32_t xyz[3];
            for (int r = 0; r < 3; r++) {
                int64_t v = int64_t(M[3 * r]) * wo.box[c8][0] + int64_t(M[3 * r + 1]) * wo.box[c8][1] +
                            int64_t(M[3 * r + 2]) * wo.box[c8][2] + s32(int64_t(Tt[r]) + o.P[r]);
                v >>= 15;
                if (!fits(v)) return false;
                xyz[r] = s32(v);
            }
            std::memcpy(o.corner[c8], xyz, sizeof xyz);
            if (xyz[2] > 0x80) {
                int64_t sx = idiv(int64_t(xyz[0]) * 256, xyz[2]) + 0xa0, sy = 0x64 - idiv(int64_t(xyz[1]) * 256, xyz[2]);
                o.sx[c8] = s16(sx);
                o.sy[c8] = s16(sy);
                int32_t x32 = s32(sx), y32 = s32(sy);
                int code = (x32 < 0) | (s32(0x140 - int64_t(x32)) < 0) << 1 | (y32 < 0) << 2 | (s32(0xc8 - int64_t(y32)) < 0) << 3;
                if (!code) return true;
                outcodes[k++] = uint8_t(code);
            }
        }
        if (part4) return false;
        for (int j = 1; j < 8; j++)
            if (outcodes[j] != outcodes[0]) return true;
        return false;
    }

    struct V {   // a vertex as the render sees it
        int x, y, z, u, v, n[3];
        int xc, yc, zc, zc32, sx, sy, shade, clip;
    };

    int lightLevel(const int L[3], const int p[3], const int16_t* n, int shift, bool flat) const {
        int d[3];
        for (int k = 0; k < 3; k++) d[k] = s16(int64_t(L[k]) - p[k]);
        int32_t dd = s32(int64_t(d[0]) * d[0] + int64_t(d[1]) * d[1] + int64_t(d[2]) * d[2]);
        int dot = s16(((int64_t(d[0]) * n[0]) >> 16) + ((int64_t(d[1]) * n[1]) >> 16) + ((int64_t(d[2]) * n[2]) >> 16));
        if (dot <= 0) return flat ? 15 : 15;
        int64_t q = idiv(int64_t(dot) * dot * 1024, dd) & 0xffff;
        int al = s8(sq(q) - sq(int64_t(uint32_t(dd) >> shift)));
        if (flat) return (al < 0 || al > 15) ? 15 : 15 - al;
        if (al < 0) return 15;
        if (al > 15) return 0;
        return 15 - al;
    }

    // ---- the polygon routines
    struct P5 { int x, y, u, v, s; };
    std::vector<P5> clip1(const std::vector<P5>& pts, int axis, int bound, bool keepLe, int flags) const {
        auto outside = [&](const P5& p) { int c = axis ? p.y : p.x; return keepLe ? c > bound : c < bound; };
        std::vector<P5> out;
        P5 prev = pts.back();
        bool pout = outside(prev);
        for (const P5& cur : pts) {
            bool cout = outside(cur);
            if (cout != pout) {
                int pa = axis ? prev.y : prev.x, ca = axis ? cur.y : cur.x;
                int po = axis ? prev.x : prev.y, co = axis ? cur.x : cur.y;
                int64_t num = int64_t(bound) - pa, den = int64_t(ca) - pa;
                P5 q{0, 0, 0, 0, 0};
                int32_t val = s32(idiv(int64_t(s32((int64_t(co) - po) * num)) * 65536, den) + (int64_t(po) << 16));
                int other = s16(val >> 16);
                if (axis) { q.y = bound; q.x = other; } else { q.x = bound; q.y = other; }
                if (flags & 2) {
                    int32_t sv = s32(idiv(int64_t(s32((int64_t(cur.s) - prev.s) * num)) * 65536, den) + (int64_t(prev.s) << 16));
                    q.s = (sv >> 16) & 0xffff;
                }
                if (flags & 4) {
                    int pu[2] = {prev.u, prev.v}, cu[2] = {cur.u, cur.v}, r[2];
                    for (int k = 0; k < 2; k++) {
                        int64_t sl = idiv(s32((int64_t(cu[k]) - pu[k]) * 256), s16(den));
                        r[k] = int((int64_t(s32(sl * s16(num)) >> 8) + pu[k]) & 0xffff);
                    }
                    q.u = r[0];
                    q.v = r[1];
                }
                out.push_back(q);
            }
            if (!cout) out.push_back(cur);
            prev = cur;
            pout = cout;
        }
        return out;
    }
    bool clipped(std::vector<P5>& pts, int flags) const {
        for (;;) {
            if (pts.empty()) return false;
            int mnx = pts[0].x, mxx = mnx, mny = pts[0].y, mxy = mny;
            for (const P5& p : pts) {
                mnx = std::min(mnx, p.x), mxx = std::max(mxx, p.x), mny = std::min(mny, p.y), mxy = std::max(mxy, p.y);
            }
            if (mny >= mxy || mny > 199 || mxy < 0 || mnx > 319 || mxx < 0) return false;
            if (mnx < 0) pts = clip1(pts, 0, 0, false, flags);
            else if (mxx > 320) pts = clip1(pts, 0, 320, true, flags);
            else if (mny < 0) pts = clip1(pts, 1, 0, false, flags);
            else if (mxy > 199) pts = clip1(pts, 1, 199, true, flags);
            else return true;
        }
    }
    using Span = std::function<void(int y, int32_t xl, int32_t xr, int32_t ul, int32_t vl, int32_t sl, const int64_t* g)>;
    void walk(const std::vector<P5>& pts, const Span& span, int flags) const {
        const int n = int(pts.size());
        int top = 0;
        for (int i = 1; i < n; i++)
            if (pts[size_t(i)].y < pts[size_t(top)].y) top = i;
        int y = pts[size_t(top)].y, L = top, R = top, Ln = (top + 1) % n, Rn = (top - 1 + n) % n, need = 3;
        int32_t xl = 0, xr = 0, sxl = 0, sxr = 0, ul = 0, vl = 0, sl = 0, sul = 0, svl = 0, ssl = 0, sur = 0, svr = 0, ssr = 0;
        const bool tex = flags & 4, sh = flags & 2;
        for (;;) {
            if (need & 1) {
                const P5 *a, *b;
                for (;;) {
                    a = &pts[size_t(L)];
                    b = &pts[size_t(Ln)];
                    L = Ln;
                    Ln = (Ln + 1) % n;
                    int dy = s16(b->y - a->y);
                    if (dy == 0) continue;
                    if (dy < 0) return;
                    break;
                }
                int dy = s16(b->y - a->y);
                xl = s32((int64_t(a->x) << 16) + 0x8000);
                sxl = s32(idiv(s32(int64_t(s16(b->x - a->x)) << 16), dy) | 1);
                if (tex) {
                    ul = a->u << 8;
                    sul = s32(idiv(s32((int64_t(b->u) << 8) - ul), dy));
                    vl = a->v << 8;
                    svl = s32(idiv(s32((int64_t(b->v) << 8) - vl), dy));
                }
                if (sh) {
                    sl = s32(int64_t(a->s) << 16);
                    ssl = s32(idiv(s32((int64_t(b->s) << 16) - (int64_t(a->s) << 16)), dy));
                }
            }
            if (need & 2) {
                const P5 *a, *b;
                for (;;) {
                    a = &pts[size_t(R)];
                    b = &pts[size_t(Rn)];
                    R = Rn;
                    Rn = (Rn - 1 + n) % n;
                    int dy = s16(b->y - a->y);
                    if (dy == 0) continue;
                    if (dy < 0) return;
                    break;
                }
                int dy = s16(b->y - a->y);
                xr = s32((int64_t(a->x) << 16) + 0x8000);
                sxr = s32(idiv(s32(int64_t(s16(b->x - a->x)) << 16), dy) | 1);
                if (tex) {
                    sur = s32(idiv(s32((int64_t(b->u) - a->u) << 8), dy));
                    svr = s32(idiv(s32((int64_t(b->v) - a->v) << 8), dy));
                }
                if (sh) ssr = s32(idiv(s32((int64_t(b->s) - a->s) << 16), dy));
            }
            int yl = pts[size_t(L)].y, yr = pts[size_t(R)].y, cnt;
            if (yl < yr) cnt = s16(yl - y), need = 1;
            else if (yl > yr) cnt = s16(yr - y), need = 2;
            else cnt = s16(yl - y), need = 3;
            int64_t g[3] = {0, 0, 0};
            if (tex) {
                int64_t den = s32(int64_t(sxr) - sxl);
                if (!den) den = 0x7fff;
                g[0] = idiv(int64_t(s32(int64_t(sur) - sul)) * 65536, den);
                g[1] = idiv(int64_t(s32(int64_t(svr) - svl)) * 65536, den);
                g[2] = sh ? idiv(int64_t(s32(int64_t(ssr) - ssl)) * 65536, den) : 0;
            }
            for (int k = 0; k <= cnt; k++) {
                span(y + k, xl, xr, ul, vl, sl, g);
                if (k < cnt) {
                    xl = s32(int64_t(xl) + sxl);
                    xr = s32(int64_t(xr) + sxr);
                    ul = s32(int64_t(ul) + sul);
                    vl = s32(int64_t(vl) + svl);
                    sl = s32(int64_t(sl) + ssl);
                }
            }
            y += cnt;
        }
    }
    void polyFlat(std::vector<P5> pts, int colour) const {
        if (!clipped(pts, 1)) return;
        walk(pts, [&](int y, int32_t xl, int32_t xr, int32_t, int32_t, int32_t, const int64_t*) {
            uint32_t a = uint32_t(xl) >> 16, b = uint32_t(xr) >> 16;
            if (b > a && y >= 0 && y < 200 && b <= 320) std::memset(buf + y * 320 + a, colour, b - a);
        }, 1);
    }
    void polyTex(std::vector<P5> pts, int base, bool gouraud) const {
        const int flags = gouraud ? 7 : 5;
        if (!clipped(pts, flags)) return;
        const int mu = 0x3f, mv = 0x3f;   // [0x21b148]
        Span span = [&](int y, int32_t xl, int32_t xr, int32_t ul, int32_t vl, int32_t sl, const int64_t* g) {
            uint32_t a = uint32_t(xl) >> 16, b = uint32_t(xr) >> 16;
            if (b <= a || y < 0 || y >= 200 || b > 320) return;
            const int64_t gu = g[0], gv = g[1], gs = g[2];
            const int64_t c = (-int64_t(xl)) & 0xffff;
            int64_t u = ul + ((gu * c) >> 16), v = vl + ((gv * c) >> 16);
            uint8_t* o = buf + y * 320 + a;
            if (!gouraud) {
                for (uint32_t i = 0; i < b - a; i++) {
                    o[i] = tex[size_t(base + (((v >> 16) & mv) << 8) + ((u >> 16) & mu)) & 0xffff];
                    u += gu;
                    v += gv;
                }
                return;
            }
            uint64_t vf = uint64_t(v) & 0xffff, vi = uint64_t(v >> 16) & 0xff;
            uint64_t sfrac = uint32_t(sl) & 0xffff, sint = (uint32_t(sl) >> 16) & 0xffff;
            const uint64_t gsf = uint64_t(gs) & 0xffff, gsi = uint64_t(gs >> 16) & 0xff;
            const uint64_t gvf = uint64_t(gv) & 0xffff, gvi = uint64_t(gv >> 16) & 0xff;
            for (uint32_t i = 0; i < b - a; i++) {
                int t = tex[size_t(base + int((vi & mv) << 8) + int((u >> 16) & mu)) & 0xffff];
                size_t gi = size_t(sint & 0xff) * 256 + t;
                o[i] = gi < gour.size() ? gour[gi] : 0;
                u += gu;
                sfrac += gsf;
                uint64_t cs = sfrac >> 16;
                sfrac &= 0xffff;
                uint64_t edx = ((vf << 16) | sint) + ((gvf << 16) | gsi) + cs;
                uint64_t cv = edx >> 32;
                edx &= 0xffffffff;
                vf = edx >> 16;
                sint = edx & 0xffff;
                vi = (vi + gvi + cv) & 0xff;
            }
        };
        for (;;) {
            std::vector<P5> tri(pts.begin(), pts.begin() + 3);
            if (!(tri[1].y == tri[0].y && tri[2].y == tri[0].y)) walk(tri, span, flags);
            if (pts.size() == 3) return;
            pts.erase(pts.begin() + 1);
        }
    }

    void line(int x0, int y0, int x1, int y1, int colour) const {
        for (;;) {
            int64_t dx = (x1 - x0) ? (x1 - x0) : 1, dy = (y1 - y0) ? (y1 - y0) : 1;
            if (x0 < 0) { if (x1 <= 0) return; y0 -= s32(idiv(s32(dy << 16), dx) * x0) >> 16; x0 = 0; }
            else if (x0 >= 320) { if (x1 >= 320) return; y0 -= s32(idiv(s32(dy << 16), dx) * (x0 - 319)) >> 16; x0 = 319; }
            else if (x1 < 0) { y1 -= s32(idiv(s32(dy << 16), dx) * x1) >> 16; x1 = 0; }
            else if (x1 >= 320) { y1 -= s32(idiv(s32(dy << 16), dx) * (x1 - 319)) >> 16; x1 = 319; }
            else if (y0 < 0) {
                if (y1 <= 0) return;
                x0 -= s32((idiv(s32(dx << 16), dy) * y0) >> 16);
                if (x0 < 0 || x0 >= 320) return;
                y0 = 0;
            } else if (y0 >= 200) {
                if (y1 >= 200) return;
                x0 -= s32((idiv(s32(dx << 16), dy) * (y0 - 199)) >> 16);
                if (x0 < 0 || x0 >= 320) return;
                y0 = 199;
            } else if (y1 < 0) {
                x1 -= s32((idiv(s32(dx << 16), dy) * y1) >> 16);
                if (x1 < 0 || x1 >= 320) return;
                y1 = 0;
            } else if (y1 >= 200) {
                x1 -= s32((idiv(s32(dx << 16), dy) * (y1 - 199)) >> 16);
                if (x1 < 0 || x1 >= 320) return;
                y1 = 199;
            } else break;
        }
        int ax = std::abs(x1 - x0), sx = x1 >= x0 ? 1 : -1, ay = std::abs(y1 - y0), sy = y1 >= y0 ? 320 : -320;
        int p = y0 * 320 + x0;
        auto plot = [&] { if (p >= 0 && p < 64000) buf[p] = uint8_t(colour); };
        if (ax > ay) {
            for (int e = ax >> 1, n = ax;;) {
                plot();
                p += sx;
                e += ay;
                if (e >= ax) { e -= ax; p += sy; }
                if (--n <= 0) break;
            }
        } else {
            for (int e = ay >> 1, n = ay;;) {
                plot();
                p += sy;
                e += ax;
                if (e >= ay) { e -= ay; p += sx; }
                if (--n <= 0) break;
            }
        }
    }

    void drawObject(int i) {
        const Object& wo = w->objects[size_t(i)];
        Obj& o = objs[size_t(i)];
        int L[3];
        {
            int32_t e[3];
            for (int k = 0; k < 3; k++) e[k] = s32(-2 * int64_t(h[k]) - o.pos[k]);
            for (int r = 0; r < 3; r++)
                L[r] = s16(s32(int64_t(o.R[3 * r]) * e[0] + int64_t(o.R[3 * r + 1]) * e[1] + int64_t(o.R[3 * r + 2]) * e[2]) >> 15);
        }
        // vertices 0x21e54c (plus room for the near clip's new vertices)
        std::vector<V> vs(wo.verts.size());
        vs.reserve(wo.verts.size() + 4 * wo.faces.size() + 8);
        int32_t t[3];
        for (int r = 0; r < 3; r++) t[r] = s32(int64_t(o.P[r]) + Tt[r]);
        for (size_t j = 0; j < vs.size(); j++) {
            const Vertex& p = wo.verts[j];
            V& v = vs[j];
            v.x = p.x, v.y = p.y, v.z = p.z, v.u = p.u, v.v = p.v;
            for (int k = 0; k < 3; k++) v.n[k] = p.n[k];
            v.shade = 0xffff;
            int32_t X = s32(int64_t(s32(int64_t(v.x) * o.Cm[0])) + s32(int64_t(v.y) * o.Cm[1]) + t[0] + s32(int64_t(v.z) * o.Cm[2])) >> 15;
            int32_t Y = s32(int64_t(s32(int64_t(v.x) * o.Cm[3])) + s32(int64_t(v.y) * o.Cm[4]) + t[1] + s32(int64_t(v.z) * o.Cm[5])) >> 15;
            int32_t Z = s32(int64_t(s32(int64_t(v.x) * o.Cm[6])) + s32(int64_t(v.y) * o.Cm[7]) + t[2] + s32(int64_t(v.z) * o.Cm[8])) >> 15;
            v.xc = s16(X), v.yc = s16(Y), v.zc = s16(Z), v.zc32 = Z;
            if (Z > 0x80) {
                v.sx = s16(idiv(int64_t(v.xc) * 256, Z) + 0xa0);
                v.sy = s16(0x64 - idiv(int64_t(v.yc) * 256, Z));
                v.clip = 0;
            } else {
                v.clip = 1;
            }
        }
        struct F {
            const Face* f;
            int a, b, c;
        };
        auto front = [&](int a, int b, int c) {
            const V &A = vs[size_t(a)], &B = vs[size_t(b)], &Cc = vs[size_t(c)];
            int64_t p1 = int64_t(s16(B.sx - A.sx)) * s16(Cc.sy - B.sy), p2 = int64_t(s16(B.sy - A.sy)) * s16(Cc.sx - B.sx);
            return s32(p1 - p2) < 0;
        };
        std::vector<std::pair<int32_t, F>> lst;
        std::vector<F> clippedFaces;
        int n = 0;
        for (const Face& f : wo.faces) {
            int a = f.v[0], b = f.v[1], c = f.v[2];
            if (vs[size_t(a)].clip || vs[size_t(b)].clip || vs[size_t(c)].clip) {
                // 0x21e864: the near clip; new vertices keep u = v = 0 and shade 0
                std::vector<int> out;
                int prev = c;
                for (int cur : {a, b, c}) {
                    if ((vs[size_t(prev)].clip ^ vs[size_t(cur)].clip) & 1) {
                        V nv = vs[size_t(cur)];
                        const V &P = vs[size_t(prev)], &Cu = vs[size_t(cur)];
                        nv.u = nv.v = 0;
                        nv.shade = 0;
                        int64_t tt = idiv(s32(int64_t(-(Cu.zc - 0x80)) * 65536), s32(int64_t(P.zc) - Cu.zc));
                        int32_t x = s32((int64_t(P.xc - Cu.xc) * tt) >> 16) + Cu.xc;
                        nv.sx = s16(2 * int64_t(x) + 0xa0);
                        int32_t yv = s32((int64_t(P.yc - Cu.yc) * tt) >> 16) + Cu.yc;
                        nv.sy = s16(-2 * int64_t(yv) + 0x64);
                        nv.clip = 0;
                        vs.push_back(nv);
                        out.push_back(int(vs.size()) - 1);
                    }
                    prev = cur;
                    if (!(vs[size_t(cur)].clip & 1)) out.push_back(cur);
                }
                for (size_t k = 1; k + 1 < out.size(); k++) clippedFaces.push_back({&f, out[0], out[k], out[k + 1]});
                continue;
            }
            if (front(a, b, c)) lst.push_back({s32((int64_t(vs[size_t(a)].zc & 0xffff) << 16) + n++), {&f, a, b, c}});
        }
        for (const F& f : clippedFaces)
            if (front(f.a, f.b, f.c)) lst.push_back({s32((int64_t(vs[size_t(f.a)].zc & 0xffff) << 16) + n++), f});
        std::stable_sort(lst.begin(), lst.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (auto& [key, f] : lst) {
            const Face& fc = *f.f;
            const int idx[3] = {f.a, f.b, f.c};
            if (fc.type <= 1) {
                static const int zero[3] = {0, 0, 0};
                int lev = lightLevel(L, zero, fc.n, 18, true);
                std::vector<P5> pts;
                for (int k : idx) pts.push_back({vs[size_t(k)].sx, vs[size_t(k)].sy, 0, 0, 0});
                polyFlat(pts, (fc.colour + lev) & 0xff);
                continue;
            }
            const bool shaded = (fc.type >= 2 && fc.type <= 5) || (fc.type >= 8 && fc.type <= 11);
            if (shaded)
                for (int k : idx) {
                    V& v = vs[size_t(k)];
                    if (v.shade == 0xffff) {
                        const int p[3] = {v.x, v.y, v.z};
                        const int16_t nn[3] = {int16_t(v.n[0]), int16_t(v.n[1]), int16_t(v.n[2])};
                        v.shade = lightLevel(L, p, nn, 20, false);
                    }
                }
            const int base = ((fc.tex & 0xf0) << 8) + ((fc.tex << 4) & 0xff);
            std::vector<P5> pts;
            for (int k : idx) {
                const V& v = vs[size_t(k)];
                pts.push_back({v.sx, v.sy, v.u & 0xffff, v.v & 0xffff, fc.type == 8 ? (v.shade & 0xffff) : 0});
            }
            if (fc.type == 6 || fc.type == 7) polyTex(pts, base, false);
            else if (fc.type == 8) polyTex(pts, base, true);
        }
    }

    void boxes() {
        static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {1, 3}, {0, 2}, {4, 5}, {6, 7},
                                              {5, 7}, {4, 6}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (size_t i = 0; i < objs.size(); i++) {
            Obj& o = objs[i];
            const Object& wo = w->objects[i];
            int32_t t[3];
            for (int r = 0; r < 3; r++) t[r] = s32(int64_t(Tt[r]) + o.P[r]);
            bool last = false;
            for (int c8 = 0; c8 < 8; c8++) {
                int32_t xyz[3];
                for (int r = 0; r < 3; r++)
                    xyz[r] = s32((int64_t(o.Cm[3 * r]) * wo.box[c8][0] + int64_t(o.Cm[3 * r + 1]) * wo.box[c8][1] +
                                  int64_t(o.Cm[3 * r + 2]) * wo.box[c8][2] + t[r]) >> 15);
                last = xyz[2] > 0x80;
                if (last) {
                    o.sx[c8] = s16(idiv(int64_t(xyz[0]) * 256, xyz[2]) + 0xa0);
                    o.sy[c8] = s16(0x64 - idiv(int64_t(xyz[1]) * 256, xyz[2]));
                }
            }
            if (last)
                for (const auto& e : kEdges) line(o.sx[e[0]], o.sy[e[0]], o.sx[e[1]], o.sy[e[1]], 0x80);
        }
    }
};

Original::Original() : d_(std::make_unique<Impl>()) {}
Original::~Original() = default;

void Original::init(const ReveExe& e, const World& w) {
    Impl& d = *d_;
    d.w = &w;
    for (int i = 0; i < 5120; i++) d.sine[i] = e.s16(kSine + 2 * uint32_t(i));
    for (int i = 0; i < 2048; i++) d.sqrtTab[i] = i < 1156 ? uint8_t(std::sqrt(double(i))) : e.u8(kSqrt + uint32_t(i));
    for (int i = 0; i < 1156; i++)
        while (int(d.sqrtTab[i]) * d.sqrtTab[i] > i) d.sqrtTab[i]--;
    RawImage t = rawImage(e, e.cstr(kTextureName));
    d.tex = t.px;
    d.tex.resize(65536);
    for (int i = 0; i < 256; i++) pal[i] = t.color(i);
    auto g = e.file(e.cstr(kGouraudName));
    d.gour.assign(g.begin(), g.end());
    auto T = e.file(e.cstr(kAntialiaName));
    d.T.assign(T.begin(), T.end());
    d.T.resize(65536);
    d.objs.assign(w.objects.size(), {});
}

void Original::render(const Anim& a, long n, bool part4, uint8_t* buf) {
    Impl& d = *d_;
    d.part4 = part4;
    d.buf = buf;
    int32_t cam[6];
    std::vector<std::array<int32_t, 6>> obj;
    a.intState(n, cam, obj);
    d.camera(cam);
    std::vector<std::pair<int32_t, int>> vis;
    for (size_t i = 0; i < d.objs.size(); i++) {
        Impl::Obj& o = d.objs[i];
        for (int k = 0; k < 3; k++) {
            o.pos16[k] = obj[i][size_t(k)];
            o.ang[k] = obj[i][size_t(3 + k)];
            o.pos[k] = o.pos16[k] >> 16;
        }
        if (d.setup(int(i))) vis.push_back({s32((int64_t(o.P[2]) & ~int64_t(0xffff)) + int64_t(vis.size())), int(i)});
    }
    std::stable_sort(vis.begin(), vis.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    for (auto& [key, i] : vis) d.drawObject(i);
    if (part4) d.boxes();
}

void Original::aa(const uint8_t* s, uint8_t* dst) const {
    const uint8_t* T = d_->T.data();
    for (int y = 1; y < 199; y++)
        for (int x = 4; x < 316; x++) {
            const int o = y * 320 + x;
            const int c = s[o], l = s[o - 1], u = s[o - 320], dn = s[o + 320];
            dst[o] = (x & 1) ? T[T[dn * 256 + u] * 256 + T[l * 256 + c]] : T[T[u * 256 + dn] * 256 + T[c * 256 + l]];
        }
}

void Original::blend(const uint8_t* src, uint8_t* screen) const {
    const uint8_t* T = d_->T.data();
    for (int i = 0; i < 64000; i++) screen[i] = T[src[i] * 256 + screen[i]];
}

void Original::show(Ctx& c, const uint8_t* idx) {
    std::vector<uint8_t> rgba(64000 * 4);
    for (int i = 0; i < 64000; i++) {
        const Vec3& v = pal[idx[i]];
        rgba[size_t(i) * 4] = uint8_t(std::lround(v.x * 255));
        rgba[size_t(i) * 4 + 1] = uint8_t(std::lround(v.y * 255));
        rgba[size_t(i) * 4 + 2] = uint8_t(std::lround(v.z * 255));
        rgba[size_t(i) * 4 + 3] = 255;
    }
    if (!screenTex_.id) screenTex_.createRaw(320, 200, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data(), false, false, false);
    else screenTex_.update(rgba.data());
    c.drawTexture(screenTex_.id, 0, 0, 320, 200);
}

Original& Scene3D::original(Ctx& c) {
    if (!orig_) {
        orig_ = std::make_shared<Original>();
        orig_->init(*c.exe, world_);
    }
    return *orig_;
}

void Scene3D::renderOriginal(Ctx& c, double n, uint8_t* buf) {
    std::memset(buf, 0, 64000);
    original(c).render(anim_, long(std::floor(std::max(n, 0.0))), part4_, buf);
}

}  // namespace reve3d

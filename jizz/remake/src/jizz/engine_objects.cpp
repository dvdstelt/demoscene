// The vector engine's object scripts: 0x15bb0 (scene set-up: header floats) and the interpreter 0x14394 with all
// sixteen opcodes: 0x1488d sphere, 0x14b8a cylinder / cone, 0x14e7c torus, 0x15174 halve, 0x151e9 translate,
// 0x15217 twist, 0x15316 / 0x1535f / 0x153a5 rotations (0x1545d matrix * vector), 0x152a0 duplicate, 0x15276
// merge, 0x1a089 vertex normals, plus the texture-seam fix 0x14651 and 0x14810 (face attributes).
// See jizz/re/notes-engine.md section 3. The scripts, the generator constants and the cylinder's helper vector are
// read from JIZZ.EXE.
//
// detail 1 reproduces the original bit for bit: every operation is rounded to float32 in the original's order (the
// x87 runs at 24-bit precision; fsin / fcos results are extended, here double, rounded by the operation that uses
// them), like jizz/re/models/engine/model_objects.py. detail k > 1 evaluates the same parametric shapes with k
// times as many segments in each direction.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "exe.h"
#include "jizz/engine.h"

namespace jizz {
namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr uint32_t kScripts[5] = {0x157a4, 0x157fa, 0x15945, 0x15973, 0x159cb};

// x87 operations at 24-bit precision: exact operands (floats or extended transcendental results), one rounding
float mul(double a, double b) { return float(a * b); }
float add(double a, double b) { return float(a + b); }
float sub(double a, double b) { return float(a - b); }
float dvd(double a, double b) { return float(a / b); }

struct V3 {
    float x[3];
    float& operator[](int i) { return x[i]; }
    float operator[](int i) const { return x[i]; }
};

V3 cross(const V3& a, const V3& b) {  // 0x1542e: a x b
    return {{sub(mul(a[1], b[2]), mul(a[2], b[1])), sub(mul(a[2], b[0]), mul(a[0], b[2])),
             sub(mul(a[0], b[1]), mul(a[1], b[0]))}};
}

V3 normalize(const V3& v) {  // 0x15401
    float s = dvd(1.0, float(std::sqrt(double(add(mul(v[0], v[0]), add(mul(v[1], v[1]), mul(v[2], v[2])))))));
    return {{mul(s, v[0]), mul(s, v[1]), mul(s, v[2])}};
}

// The interpreter's state and the engine buffers while a script runs. Normals and texture coordinates are kept
// in the original's units (normal length 2^23, u / v in texels * 65536) until the end.
struct Builder {
    struct Vtx {
        V3 p, n;
        float u, v;
    };
    struct Face {
        uint32_t a, b, c;
        uint8_t type, tex;
        bool own;
        float uv[6];
    };
    const JizzExe& exe;
    int k;  // detail
    std::vector<Vtx> V;
    std::vector<Face> F;
    std::vector<EngineObject> objs;
    uint8_t tex = 1, ftype = 3;  // 0x14567: texture 1, face type 3, repeat 1 x 1
    float us = 1, vs = 1;
    float kU, kV, k65536, k256;  // 128/pi (0x15718), 256/pi (0x15714), 65536 (0x1570c), 256 (0x15720)
    V3 K;                        // the cylinder's helper vector (0x15790)

    Builder(const JizzExe& e, int detail) : exe(e), k(detail) {
        kU = exe.f32(0x15718);
        kV = exe.f32(0x15714);
        k65536 = exe.f32(0x1570c);
        k256 = exe.f32(0x15720);
        for (int i = 0; i < 3; i++) K[i] = exe.f32(0x15790 + 4 * i);
    }

    uint32_t newObject(uint32_t nv, uint32_t nf, int kind, int n, int m) {  // 0x145e0
        objs.push_back({uint32_t(F.size()), nf, uint32_t(V.size()), nv, 0, kind, n, m, k});
        return uint32_t(V.size());
    }
    void vertex(float x, float y, float z, float u, float v) { V.push_back({{{x, y, z}}, {{0, 0, 0}}, u, v}); }

    void face(uint32_t a, uint32_t b, uint32_t c, bool seam) {  // 0x14810 (+ 0x14651 when seam)
        Face f{a, b, c, ftype, tex, false, {}};
        if (seam) seamFix(f);
        F.push_back(f);
    }

    // 0x14651: (u, v) of the corners mod 2^24 (fprem keeps the sign); an edge whose two ends differ by half the
    // texture or more moves the second end by 2^24; if anything moved the face keeps its own table.
    void seamFix(Face& f) {
        const double W = 16777216.0;
        float uv[6];
        const uint32_t c[3] = {f.a, f.b, f.c};
        for (int i = 0; i < 3; i++) {
            uv[2 * i] = float(std::fmod(double(V[c[i]].u), W));
            uv[2 * i + 1] = float(std::fmod(double(V[c[i]].v), W));
        }
        static const int pairs[6][2] = {{0, 2}, {2, 4}, {4, 0}, {1, 3}, {3, 5}, {5, 1}};
        bool moved = false;
        for (const auto& p : pairs) {
            float d = sub(uv[p[0]], uv[p[1]]);
            if (std::fabs(std::nearbyint(double(d))) >= 8388608.0) {
                uv[p[1]] = add(uv[p[1]], d < 0 ? -W : W);
                moved = true;
            }
        }
        if (moved) {
            f.own = true;
            for (int i = 0; i < 6; i++) f.uv[i] = uv[i];
        }
    }

    // 0x1488d (op 0): M rings, S = 2M segments; y is the polar axis. r = R (1 + A1 sin(F1 ph) A2 sin(F2 th)).
    void sphere(int M0, V3 c, float R, float A1, float F1, float A2, float F2) {
        const int M = M0 * k, S = 2 * M;
        uint32_t v0 = newObject(uint32_t((M + 1) * S), uint32_t(2 * (M - 1) * S), 1, S, M);
        float dphi = dvd(float(kPiD + kPiD), S), dth = dvd(kPiD, M);
        float th = 0;
        for (int j = 0; j <= M; j++) {
            float ph = 0;
            for (int i = 0; i < S; i++) {
                float a = mul(std::sin(double(mul(ph, F1))), A1);
                float b = mul(std::sin(double(mul(th, F2))), A2);
                float r = mul(add(mul(a, b), 1.0), R);
                float x = add(mul(mul(std::sin(double(th)), std::cos(double(ph))), r), c[0]);
                float y = add(mul(std::cos(double(th)), r), c[1]);
                float z = add(mul(mul(std::sin(double(th)), std::sin(double(ph))), r), c[2]);
                vertex(x, y, z, mul(mul(mul(ph, kU), k65536), us), mul(mul(mul(th, kV), k65536), vs));
                ph = add(ph, dphi);
            }
            th = add(th, dth);
        }
        for (int rb = 0; rb < S * M; rb += S) {
            int nb = rb + S;
            for (int i = 0; i < S; i++) {
                int i1 = (i + 1) % S;
                if (nb < S * M) face(v0 + rb + i, v0 + nb + i1, v0 + nb + i, i1 == 0);  // not towards the last pole
                if (rb != 0) face(v0 + rb + i, v0 + rb + i1, v0 + nb + i1, i1 == 0);   // not from the first pole
            }
        }
    }

    // 0x14b8a (op 4): N segments around, M rows along the axis A from C, radius R0 -> R1.
    void cylinder(int N0, int M0, V3 C, V3 A, float R0, float R1) {
        const int N = N0 * k, M = M0 * k;
        uint32_t v0 = newObject(uint32_t((M + 1) * N), uint32_t(2 * M * N), 2, N, M);
        V3 U = normalize(cross(A, K)), W = normalize(cross(U, A));
        float dphi = dvd(float(kPiD + kPiD), N), ds = dvd(1.0, M);
        float s = 0;
        for (int j = 0; j <= M; j++) {
            float ph = 0;
            float r = add(mul(sub(R1, R0), s), R0);
            for (int i = 0; i < N; i++) {
                float p[3];
                for (int q = 0; q < 3; q++)
                    p[q] = add(add(C[q], mul(s, A[q])),
                               mul(add(mul(std::cos(double(ph)), U[q]), mul(std::sin(double(ph)), W[q])), r));
                vertex(p[0], p[1], p[2], mul(mul(mul(ph, kU), k65536), us), mul(mul(mul(s, k256), k65536), vs));
                ph = add(ph, dphi);
            }
            s = add(s, ds);
        }
        for (int rb = 0; rb < N * M; rb += N) {
            int nb = rb + N;
            for (int i = 0; i < N; i++) {
                int i1 = (i + 1) % N;
                face(v0 + rb + i, v0 + nb + i1, v0 + rb + i1, i1 == 0);
                face(v0 + rb + i, v0 + nb + i, v0 + nb + i1, i1 == 0);
            }
        }
    }

    // 0x14e7c (op 14): N segments around the tube, M around the ring; the cross-section turns `tw` times per lap.
    void torus(int N0, int M0, float R, float r, float A1, float F1, float A2, float F2, float tw) {
        const int N = N0 * k, M = M0 * k;
        uint32_t v0 = newObject(uint32_t(N * M), uint32_t(2 * N * M), 3, N, M);
        float twopi = float(kPiD + kPiD);
        float dphi = dvd(twopi, N), dth = dvd(twopi, M);
        float th = 0;
        for (int j = 0; j < M; j++) {
            float ph = 0;
            for (int i = 0; i < N; i++) {
                float a = add(ph, mul(th, tw));
                float rr = add(add(r, mul(std::sin(double(mul(a, F1))), A1)), mul(std::sin(double(mul(th, F2))), A2));
                float rad = add(mul(std::cos(double(a)), rr), R);
                vertex(mul(std::cos(double(th)), rad), mul(std::sin(double(a)), rr), mul(std::sin(double(th)), rad),
                       mul(mul(mul(ph, kU), k65536), us), mul(mul(mul(th, kU), k65536), vs));
                ph = add(ph, dphi);
            }
            th = add(th, dth);
        }
        const int NM = N * M;
        for (int j = 0; j < M; j++) {
            int rb = j * N, nb = (rb + N) % NM;
            for (int i = 0; i < N; i++) {
                int i1 = (i + 1) % N;
                bool seam = (i1 & nb) == 0;  // the original's cheap "i1 == 0 || nb == 0" (extra calls change nothing)
                face(v0 + rb + i, v0 + rb + i1, v0 + nb + i1, seam);
                face(v0 + rb + i, v0 + nb + i1, v0 + nb + i, seam);
            }
        }
    }

    // 0x15174 (op 15): keeps every other quad (two faces) of the last object: faces 0, 1, 4, 5, ... For a finer
    // torus / cylinder the original's quad (row / k, column / k) decides, so the strips keep their width.
    void halve() {
        if (objs.empty()) return;
        EngineObject& o = objs.back();
        std::vector<Face> keep;
        for (uint32_t q = 0; q < o.faceCount; q++) {
            uint32_t quad = q >> 1;
            if ((o.kind == 2 || o.kind == 3) && o.detail > 1) {
                uint32_t n = uint32_t(o.n), d = uint32_t(o.detail);
                quad = (quad / n / d) * (n / d) + (quad % n) / d;
            }
            if (quad % 2 == 0) keep.push_back(F[o.firstFace + q]);
        }
        keep.resize(std::min<size_t>(keep.size(), o.faceCount >> 1));
        F.erase(F.begin() + o.firstFace, F.begin() + o.firstFace + o.faceCount);
        F.insert(F.begin() + o.firstFace, keep.begin(), keep.end());
        o.faceCount = uint32_t(keep.size());
    }

    // 0x1545d on every vertex of an object: p'[r] = p[0] m[r][0] + (p[1] m[r][1] + p[2] m[r][2])
    static V3 apply(const V3& p, const float m[3][3]) {
        V3 o;
        for (int r = 0; r < 3; r++) o[r] = add(mul(p[0], m[r][0]), add(mul(p[1], m[r][1]), mul(p[2], m[r][2])));
        return o;
    }
    EngineObject* object(int i) { return i >= 0 && i < int(objs.size()) ? &objs[size_t(i)] : nullptr; }

    void translate(int o, V3 d) {  // 0x151e9 (op 5)
        if (EngineObject* ob = object(o))
            for (uint32_t i = 0; i < ob->vertexCount; i++) {
                V3& p = V[ob->firstVertex + i].p;
                for (int q = 0; q < 3; q++) p[q] = add(p[q], d[q]);
            }
    }
    void twist(int o, float kk) {  // 0x15217 (op 6): rotation about z by y * kk
        if (EngineObject* ob = object(o))
            for (uint32_t i = 0; i < ob->vertexCount; i++) {
                V3& p = V[ob->firstVertex + i].p;
                float a = mul(p[1], kk);
                float c = float(std::cos(double(a))), s = float(std::sin(double(a)));
                const float m[3][3] = {{c, -s, 0}, {s, c, 0}, {0, 0, 1}};
                p = apply(p, m);
            }
    }
    void rotate(int o, int axis, float a) {  // 0x15316 / 0x1535f / 0x153a5 (ops 8, 9, 10)
        float c = float(std::cos(double(a))), s = float(std::sin(double(a)));
        const float mx[3][3] = {{1, 0, 0}, {0, c, -s}, {0, s, c}};
        const float my[3][3] = {{c, 0, s}, {0, 1, 0}, {-s, 0, c}};
        const float mz[3][3] = {{c, -s, 0}, {s, c, 0}, {0, 0, 1}};
        const float(*m)[3] = axis == 0 ? mx : axis == 1 ? my : mz;
        if (EngineObject* ob = object(o))
            for (uint32_t i = 0; i < ob->vertexCount; i++) V[ob->firstVertex + i].p = apply(V[ob->firstVertex + i].p, m);
    }
    void duplicate(int o) {  // 0x152a0 (op 11): copy; the faces get the current texture
        EngineObject* ob = object(o);
        if (!ob) return;
        EngineObject src = *ob;
        uint32_t v0 = newObject(src.vertexCount, src.faceCount, src.kind, src.n, src.m);
        objs.back().detail = src.detail;
        for (uint32_t i = 0; i < src.vertexCount; i++) V.push_back(V[src.firstVertex + i]);
        uint32_t d = v0 - src.firstVertex;
        for (uint32_t i = 0; i < src.faceCount; i++) {
            Face f = F[src.firstFace + i];
            f.a += d;
            f.b += d;
            f.c += d;
            f.tex = tex;
            F.push_back(f);
        }
    }
    void merge() {  // 0x15276 (op 7): the last object becomes part of the one before
        if (objs.size() < 2) return;
        EngineObject b = objs.back();
        objs.pop_back();
        objs.back().vertexCount += b.vertexCount;
        objs.back().faceCount += b.faceCount;
        objs.back().kind = 0;
    }

    // 0x1a089 (op 12; runs at 64-bit precision): per object, area-weighted face normals summed into the corners,
    // then every vertex normal scaled to length 2^23.
    void normals() {
        for (const EngineObject& o : objs) {
            if (!o.faceCount || !o.vertexCount) continue;
            for (uint32_t q = 0; q < o.faceCount; q++) {
                const Face& f = F[o.firstFace + q];
                Vtx &A = V[f.a], &B = V[f.b], &C = V[f.c];
                float d1[3], d2[3];
                for (int i = 0; i < 3; i++) {
                    d1[i] = float(double(B.p[i]) - double(A.p[i]));
                    d2[i] = float(double(C.p[i]) - double(A.p[i]));
                }
                double n[3] = {double(d1[1]) * d2[2] - double(d1[2]) * d2[1], double(d1[2]) * d2[0] - double(d1[0]) * d2[2],
                               double(d1[0]) * d2[1] - double(d1[1]) * d2[0]};
                for (Vtx* X : {&A, &B, &C})
                    for (int i = 0; i < 3; i++) X->n[i] = float(double(X->n[i]) + n[i]);
            }
            for (uint32_t q = 0; q < o.vertexCount; q++) {
                V3& n = V[o.firstVertex + q].n;
                double L = std::sqrt((double(n[0]) * n[0] + double(n[1]) * n[1]) + double(n[2]) * n[2]);
                if (L == 0) continue;
                double s = 8388608.0 / L;
                for (int i = 0; i < 3; i++) n[i] = float(double(n[i]) * s);
            }
        }
    }

    // Finer meshes only: the S coincident vertices of each sphere pole get one normal (their sum), instead of the
    // normal of their own column's faces.
    void poleNormals() {
        for (const EngineObject& o : objs) {
            if (o.kind != 1 || o.detail <= 1) continue;
            for (int ring : {0, o.m}) {
                double s[3] = {0, 0, 0};
                uint32_t first = o.firstVertex + uint32_t(ring * o.n);
                for (int i = 0; i < o.n; i++)
                    for (int q = 0; q < 3; q++) s[q] += V[first + uint32_t(i)].n[q];
                double L = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
                if (L == 0) continue;
                for (int i = 0; i < o.n; i++)
                    for (int q = 0; q < 3; q++) V[first + uint32_t(i)].n[q] = float(s[q] * (8388608.0 / L));
            }
        }
    }

    // 0x14394: the byte code after the header; opcode 3 or > 15 ends it
    void run(uint32_t p) {
        auto f = [&](uint32_t a) { return exe.f32(a); };
        for (int guard = 0; guard < 4096; guard++) {
            uint8_t op = exe.u8(p), b = exe.u8(p + 1);
            if (op == 3 || op > 15) return;
            switch (op) {
                case 0:
                    sphere(b, {{f(p + 2), f(p + 6), f(p + 10)}}, f(p + 14), f(p + 18), f(p + 22), f(p + 26), f(p + 30));
                    p += 34;
                    break;
                case 1: tex = b; p += 2; break;
                case 2: us = float(b); vs = float(exe.u8(p + 2)); p += 3; break;
                case 4:
                    cylinder(b, exe.u8(p + 2), {{f(p + 3), f(p + 7), f(p + 11)}}, {{f(p + 15), f(p + 19), f(p + 23)}},
                             f(p + 27), f(p + 31));
                    p += 35;
                    break;
                case 5: translate(b, {{f(p + 2), f(p + 6), f(p + 10)}}); p += 14; break;
                case 6: twist(b, f(p + 2)); p += 6; break;
                case 7: merge(); p += 1; break;
                case 8: case 9: case 10: rotate(b, op - 8, f(p + 2)); p += 6; break;
                case 11: duplicate(b); p += 2; break;
                case 12: normals(); p += 1; break;
                case 13: ftype = b; p += 2; break;
                case 14:
                    torus(b, exe.u8(p + 2), f(p + 3), f(p + 7), f(p + 11), f(p + 15), f(p + 19), f(p + 23), f(p + 27));
                    p += 31;
                    break;
                case 15: halve(); p += 1; break;
            }
        }
        throw std::runtime_error("engine script does not end");
    }
};

}  // namespace

uint32_t engineScriptAddress(int scene) {
    if (scene < 0 || scene > 4) throw std::out_of_range("engine scene index");
    return kScripts[scene];
}

EngineGeometry buildEngineGeometry(const JizzExe& exe, int scene, int detail) {
    uint32_t s = engineScriptAddress(scene);
    Builder b(exe, std::max(1, detail));
    EngineGeometry g;
    g.timeScale = float(double(exe.f32(s)) * exe.f64(0x15be7));  // 0x15bb0: header * 1e-9, stored as float
    g.focal = exe.f32(s + 4);
    b.run(s + 8);
    b.poleNormals();
    g.objects = b.objs;
    g.vertices.reserve(b.V.size());
    const float n2 = 1.0f / 8388608.0f, t2 = 1.0f / 65536.0f;  // powers of two: exact
    for (const auto& v : b.V)
        g.vertices.push_back({{v.p[0], v.p[1], v.p[2]}, {v.n[0] * n2, v.n[1] * n2, v.n[2] * n2}, v.u * t2, v.v * t2});
    g.faces.reserve(b.F.size());
    for (const auto& f : b.F) {
        EngineFace e{f.a, f.b, f.c, f.type, f.tex, f.own, {}};
        for (int i = 0; i < 6; i++) e.uv[i] = f.uv[i] * t2;
        g.faces.push_back(e);
    }
    return g;
}

}  // namespace jizz

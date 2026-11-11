// 3D math of the original engine (0x0ae2, 0x0b1f, 0x0baa, 0x0bed, 0x0c30): exact 16.16 and double forms.
#include "math3d.h"

#include "common.h"

namespace paper {

namespace {
// 32-bit wrapping arithmetic as the original's imul/add
inline int32_t wrap(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
inline int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
inline int32_t add32(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
// imul + shld/shrd by 16: bits 16..47 of the 64-bit product (= floor(a*b / 65536) mod 2^32)
inline int32_t mulShr16(int32_t a, int32_t b) { return wrap((int64_t(a) * int64_t(b)) >> 16); }
}  // namespace

// ---------------------------------------------------------------- integer
IMat3 imatMul(const IMat3& a, const IMat3& b) {
    IMat3 o;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int32_t s = 0;
            for (int k = 0; k < 3; k++) s = add32(s, mulShr16(a(r, k), b(k, c)));
            o(r, c) = s;
        }
    return o;
}

IMat3 irotXY(int a, int b) {
    const int32_t cb = cosTab(b), sb = sinTab(b), ca = cosTab(a), sa = sinTab(a);
    IMat3 m;
    m.m[0] = cb;
    m.m[1] = 0;
    m.m[2] = -sb;
    m.m[3] = mulShr16(-sb, sa);
    m.m[4] = ca;
    m.m[5] = -mulShr16(cb, sa);
    m.m[6] = mulShr16(sb, ca);
    m.m[7] = sa;
    m.m[8] = mulShr16(cb, ca);
    return m;
}

IMat3 irotX(int a) {
    const int32_t s = sinTab(a), c = cosTab(a);
    IMat3 m;
    m.m[0] = 65536; m.m[1] = 0; m.m[2] = 0;
    m.m[3] = 0;     m.m[4] = c; m.m[5] = -s;
    m.m[6] = 0;     m.m[7] = s; m.m[8] = c;
    return m;
}

IMat3 irotY(int a) {
    const int32_t s = sinTab(a), c = cosTab(a);
    IMat3 m;
    m.m[0] = c;  m.m[1] = 0;     m.m[2] = s;
    m.m[3] = 0;  m.m[4] = 65536; m.m[5] = 0;
    m.m[6] = -s; m.m[7] = 0;     m.m[8] = c;
    return m;
}

IMat3 irotZ(int a) {
    const int32_t s = sinTab(a), c = cosTab(a);
    IMat3 m;
    m.m[0] = c; m.m[1] = -s; m.m[2] = 0;
    m.m[3] = s; m.m[4] = c;  m.m[5] = 0;
    m.m[6] = 0; m.m[7] = 0;  m.m[8] = 65536;
    return m;
}

IMat3 itranspose(const IMat3& a) {
    IMat3 o;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) o(r, c) = a(c, r);
    return o;
}

IVec3 imulVec(const IMat3& m, const IVec3& v) {
    IVec3 o;
    for (int r = 0; r < 3; r++) o[r] = add32(add32(mul32(m(r, 0), v.x), mul32(m(r, 1), v.y)), mul32(m(r, 2), v.z));
    return o;
}

IVec3 imulTVecShr16(const IMat3& m, const IVec3& v) {
    IVec3 o;
    for (int c = 0; c < 3; c++)
        o[c] = add32(add32(mul32(m(0, c), v.x), mul32(m(1, c), v.y)), mul32(m(2, c), v.z)) >> 16;
    return o;
}

// ---------------------------------------------------------------- double
Mat3d Mat3d::operator*(const Mat3d& o) const {
    Mat3d r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r(i, j) = (*this)(i, 0) * o(0, j) + (*this)(i, 1) * o(1, j) + (*this)(i, 2) * o(2, j);
    return r;
}

Vec3d Mat3d::operator*(const Vec3d& v) const {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
}

Mat3d Mat3d::transposed() const {
    Mat3d r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r(i, j) = (*this)(j, i);
    return r;
}

Mat3d toMat3d(const IMat3& a) {
    Mat3d r;
    for (int i = 0; i < 9; i++) r.m[i] = a.m[i] / 65536.0;
    return r;
}

Mat3d rotXY(double a, double b) {
    const double cb = cosTurn(b), sb = sinTurn(b), ca = cosTurn(a), sa = sinTurn(a);
    Mat3d m;
    m.m[0] = cb;       m.m[1] = 0;  m.m[2] = -sb;
    m.m[3] = -sb * sa; m.m[4] = ca; m.m[5] = -cb * sa;
    m.m[6] = sb * ca;  m.m[7] = sa; m.m[8] = cb * ca;
    return m;
}

Mat3d rotX(double a) {
    const double s = sinTurn(a), c = cosTurn(a);
    Mat3d m;
    m.m[4] = c; m.m[5] = -s;
    m.m[7] = s; m.m[8] = c;
    return m;
}

Mat3d rotY(double a) {
    const double s = sinTurn(a), c = cosTurn(a);
    Mat3d m;
    m.m[0] = c;  m.m[2] = s;
    m.m[6] = -s; m.m[8] = c;
    return m;
}

Mat3d rotZ(double a) {
    const double s = sinTurn(a), c = cosTurn(a);
    Mat3d m;
    m.m[0] = c; m.m[1] = -s;
    m.m[3] = s; m.m[4] = c;
    return m;
}

Mat3d lerp(const Mat3d& a, const Mat3d& b, double t) {
    Mat3d r;
    for (int i = 0; i < 9; i++) r.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
    return r;
}

}  // namespace paper

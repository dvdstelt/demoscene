// 3D math of the original engine: 3x3 16.16 matrices and their double-precision counterparts.
//
// Ports (paper/re/notes-init.md §4, notes-sketch.md §6.2, notes-planes.md §4.3):
//   0x0ae2  edi = A * B           out[r][c] = sum_k (A[r][k] * B[k][c]) >> 16, each 64-bit product floored, 32-bit sum
//   0x0b1f  rotXY(a = eax, b = ebx) = Rx(a) * Ry'(b) with Ry'(b) = [cb 0 -sb; 0 1 0; sb 0 cb]
//           = [ cb, 0, -sb ;  (-sb*sa)>>16, ca, -((cb*sa)>>16) ;  (sb*ca)>>16, sa, (cb*ca)>>16 ]
//           The scene camera and the pencil call it with a = pitch, b = yaw (rotYawPitch below).
//   0x0baa  Rx(a) = [1 0 0; 0 c -s; 0 s c]
//   0x0bed  Ry(a) = [c 0 s; 0 1 0; -s 0 c]      (NOTE: the opposite sign convention to the Ry' inside 0x0b1f)
//   0x0c30  Rz(a) = [c -s 0; s c 0; 0 0 1]
// Matrices are row-major, m[3*r + c]. Angles are in 1/1024 turn (the sine table 0x22479 has 1024 entries).
//
// Two forms of every function:
//   IMat3 (int32 16.16, sine table, the original's truncations): for the per-tick state stepping where the
//         original's integer results decide paths (teal flight path, flock steering, camera spline).
//   Mat3d (double, sin/cos at real angles): for rendering at fractional ticks (fractional angles welcome).
#pragma once
#include <cmath>
#include <cstdint>

#include "render.h"

namespace paper {

// A point in VGA coordinates (x 0..320, y 0..200, continuous; the original's pixel (X, Y) covers [X, X+1) x [Y, Y+1)).
struct Vec2f {
    float x = 0, y = 0;
};

// ---------------------------------------------------------------- integer (exact) forms
struct IVec3 {
    int32_t x = 0, y = 0, z = 0;
    IVec3() = default;
    IVec3(int32_t a, int32_t b, int32_t c) : x(a), y(b), z(c) {}
    int32_t operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    int32_t& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    bool operator==(const IVec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct IMat3 {
    int32_t m[9] = {65536, 0, 0, 0, 65536, 0, 0, 0, 65536};  // identity (1.0 = 0x10000)
    int32_t operator()(int r, int c) const { return m[3 * r + c]; }
    int32_t& operator()(int r, int c) { return m[3 * r + c]; }
    bool operator==(const IMat3& o) const {
        for (int i = 0; i < 9; i++)
            if (m[i] != o.m[i]) return false;
        return true;
    }
};

IMat3 imatMul(const IMat3& a, const IMat3& b);  // 0x0ae2
IMat3 irotXY(int a, int b);                     // 0x0b1f (eax = a, ebx = b)
inline IMat3 irotYawPitch(int yaw, int pitch) { return irotXY(pitch, yaw); }  // scene camera / pencil use
IMat3 irotX(int a);                             // 0x0baa
IMat3 irotY(int a);                             // 0x0bed
IMat3 irotZ(int a);                             // 0x0c30
IMat3 itranspose(const IMat3& a);
// m * v with 32-bit wrapping sums and no shift (the original's "16.16 times integer" products, e.g. the camera-space
// translation of 0x3fb3: result is 16.16).
IVec3 imulVec(const IMat3& m, const IVec3& v);
// m^T * v, summed in 32 bits, then >> 16 (floor): the object-space light vector of 0x3fb3 before normalisation.
// The 32-bit wrap is the teal part's light overflow (notes-planes §6.3).
IVec3 imulTVecShr16(const IMat3& m, const IVec3& v);

// ---------------------------------------------------------------- double forms
struct Vec3d {
    double x = 0, y = 0, z = 0;
    Vec3d() = default;
    Vec3d(double a, double b, double c) : x(a), y(b), z(c) {}
    explicit Vec3d(const IVec3& v) : x(v.x), y(v.y), z(v.z) {}
    explicit Vec3d(const Vec3& v) : x(v.x), y(v.y), z(v.z) {}
    Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3d operator-() const { return {-x, -y, -z}; }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3d cross(const Vec3d& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double len() const { return std::sqrt(dot(*this)); }
    Vec3d norm() const { double l = len(); return l > 0 ? *this * (1.0 / l) : *this; }
    Vec3 f() const { return Vec3(float(x), float(y), float(z)); }
};

struct Mat3d {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double operator()(int r, int c) const { return m[3 * r + c]; }
    double& operator()(int r, int c) { return m[3 * r + c]; }
    Mat3d operator*(const Mat3d& o) const;
    Vec3d operator*(const Vec3d& v) const;
    Mat3d transposed() const;
    Vec3d row(int r) const { return {m[3 * r], m[3 * r + 1], m[3 * r + 2]}; }
};

Mat3d toMat3d(const IMat3& a);  // 16.16 -> double (/65536)
Mat3d rotXY(double a, double b);  // continuous 0x0b1f: Rx(a) * Ry'(b)
inline Mat3d rotYawPitch(double yaw, double pitch) { return rotXY(pitch, yaw); }
Mat3d rotX(double a);             // continuous 0x0baa
Mat3d rotY(double a);             // continuous 0x0bed
Mat3d rotZ(double a);             // continuous 0x0c30
// Element-wise linear interpolation of two matrices (for interpolating per-tick 16.16 states at a fractional tick;
// the original's matrices are rotation matrices to 1/65536, a lerp between neighbouring ticks stays one to ~1e-4).
Mat3d lerp(const Mat3d& a, const Mat3d& b, double t);
inline Vec3d lerp(const Vec3d& a, const Vec3d& b, double t) { return a + (b - a) * t; }

}  // namespace paper

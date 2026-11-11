// The vector engine's camera: 0x15bef (engine time from the timer), 0x159fb / 0x15a59 (eye and target on the
// scene's closed path, table 0x15afc), 0x15c40 (roll wobble), 0x154bc (look-at with roll, + 0x15401 normalize),
// and the projection of 0x19118 / 0x19022 / 0x18ea1 as GL matrices for a full-screen or inset viewport.
// See jizz/re/notes-engine.md section 4 and 5.2. The path table, the wobble frequencies and the script headers
// (time scale, focal length F) are read from JIZZ.EXE. Evaluated in double precision with the continuous timer.
#include <cmath>

#include "exe.h"
#include "jizz/engine.h"

namespace jizz {
namespace {

constexpr double kPiD = 3.14159265358979323846;

struct D3 {
    double x, y, z;
};

}  // namespace

void EngineCameraPath::load(const JizzExe& exe, int s) {
    scene = s;
    uint32_t script = engineScriptAddress(s);
    for (int i = 0; i < 9; i++) p[i] = exe.f32(0x15afc + 36 * uint32_t(s) + 4 * uint32_t(i));
    for (int i = 0; i < 3; i++) wobble[i] = exe.f32(0x15c7f + 4 * uint32_t(i));
    focal = exe.f32(script + 4);
    timeScale = float(double(exe.f32(script)) * exe.f64(0x15be7));
    pitchBase = exe.f32(0x15710);
}

EngineCamera EngineCameraPath::at(double t, bool wob) const {
    const double Rh = p[0], Rv = p[1], s2 = p[2], dt = p[3], a = p[4], b = p[5], c = p[6], d = p[7], rs = p[8];
    // 0x15a59: phi = t a + c sin(t a), theta = d sin(t b); the roll is (time of the last call) * rs
    auto path = [&](double tt, double s) {
        double ta = tt * a, phi = ta + c * std::sin(ta), th = d * std::sin(tt * b);
        return D3{std::cos(th) * std::cos(phi) * Rh * s, std::sin(th) * Rv * s, std::cos(th) * std::sin(phi) * Rh * s};
    };
    D3 E = path(t, 1.0), T = path(t + dt, s2);
    double roll = (t + dt) * rs;
    if (wob)  // 0x15c40: the last term has no pi
        roll = roll + kPiD * std::sin(wobble[0] * t) + kPiD * std::sin(wobble[1] * t) + std::sin(wobble[2] * t);

    // 0x154bc: yaw / pitch of the view direction, then the roll about it
    D3 dir{T.x - E.x, T.y - E.y, T.z - E.z};
    double l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    dir = {dir.x / l, dir.y / l, dir.z / l};
    D3 D{-focal * dir.x, -focal * dir.y, -focal * dir.z};  // eye - (eye + F dir)
    double yaw = kPiD - std::atan2(D.x, D.z);
    double pitch = double(pitchBase) - std::atan2(-std::sqrt(D.x * D.x + D.z * D.z), D.y);
    double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    D3 right{cy, 0, sy}, up{sp * sy, cp, -(cy * sp)}, fwd{-(sy * cp), sp, cy * cp};
    double cr = std::cos(roll), sr = std::sin(roll);
    D3 r2{right.x * cr - up.x * sr, right.y * cr - up.y * sr, right.z * cr - up.z * sr};
    D3 u2{right.x * sr + up.x * cr, right.y * sr + up.y * cr, right.z * sr + up.z * cr};

    EngineCamera cam;
    cam.t = t;
    auto v = [](const D3& q) { return Vec3(float(q.x), float(q.y), float(q.z)); };
    cam.eye = v(E);
    cam.target = v(T);
    cam.roll = float(roll);
    cam.right = v(r2);
    cam.up = v(u2);
    cam.fwd = v(fwd);
    cam.focal = focal;
    return cam;
}

Mat4 EngineCamera::view() const {
    Mat4 m = Mat4::identity();
    m.m[0] = right.x; m.m[4] = right.y; m.m[8] = right.z;   m.m[12] = -right.dot(eye);
    m.m[1] = up.x;    m.m[5] = up.y;    m.m[9] = up.z;      m.m[13] = -up.dot(eye);
    m.m[2] = -fwd.x;  m.m[6] = -fwd.y;  m.m[10] = -fwd.z;   m.m[14] = fwd.dot(eye);
    return m;
}

Mat4 EngineCamera::projection(const EngineViewport& vp, float n, float f) const {
    // x_vga = cx + (w/320) F x/z, y_vga = cy - (h/200) F y/z; x_ndc = x_vga/160 - 1, y_ndc = 1 - y_vga/100;
    // GL eye space has z_eye = -z.
    Mat4 m{};
    m.m[0] = vp.w * focal / (320.0f * 160.0f);
    m.m[8] = -(vp.cx / 160.0f - 1.0f);
    m.m[5] = vp.h * focal / (200.0f * 100.0f);
    m.m[9] = -(1.0f - vp.cy / 100.0f);
    m.m[10] = -(f + n) / (f - n);
    m.m[14] = -2.0f * f * n / (f - n);
    m.m[11] = -1.0f;
    return m;
}

Vec3 EngineCamera::toCamera(Vec3 w) const {
    Vec3 d = w - eye;
    return {right.dot(d), up.dot(d), fwd.dot(d)};
}

bool EngineCamera::toVGA(Vec3 w, const EngineViewport& vp, float* x, float* y) const {
    Vec3 c = toCamera(w);
    if (c.z <= 0) return false;
    *x = vp.cx + vp.w / 320.0f * focal * c.x / c.z;
    *y = vp.cy - vp.h / 200.0f * focal * c.y / c.z;
    return true;
}

}  // namespace jizz

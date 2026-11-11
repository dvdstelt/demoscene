// Warp grid 0x2dc8 / 0x2ad0 / 0x2db3 and the texture mapper 0x3095, evaluated per pixel. See warp.h.
#include "paper/warp.h"

#include <cmath>

#include "demo.h"
#include "paper/common.h"

namespace paper {

std::vector<int32_t> quadratic2ad0(int32_t p0, int32_t p1, int32_t p2, int n) {
    int32_t step = recipTab(n) >> 7;
    int32_t B = 4 * p1 - p2 - 3 * p0;
    int32_t A = p0 - 2 * p1 + p2;
    std::vector<int32_t> out(size_t(n) + 1);
    for (int k = 0; k <= n; k++) {
        uint32_t t = uint32_t(k * step);
        int32_t bt = int32_t(uint32_t(B) * t);         // 32-bit imul, as the original
        int32_t at = int32_t(uint32_t(A) * t * t);
        out[k] = (((bt + (at >> 8)) >> 8) + 2 * p0) >> 1;
    }
    return out;
}

std::vector<WarpVertex> warpGridExact(const WarpField& f, WarpShift s) {
    std::vector<int32_t> rx[3], ry[3];
    for (int R = 0; R < 3; R++) {
        rx[R] = quadratic2ad0(int32_t(f.x[R][0]), int32_t(f.x[R][1]), int32_t(f.x[R][2]), 40);
        ry[R] = quadratic2ad0(int32_t(f.y[R][0]), int32_t(f.y[R][1]), int32_t(f.y[R][2]), 40);
    }
    std::vector<WarpVertex> g(41 * 26);
    for (int j = 0; j <= 40; j++) {
        std::vector<int32_t> cx = quadratic2ad0(rx[0][j], rx[1][j], rx[2][j], 25);
        std::vector<int32_t> cy = quadratic2ad0(ry[0][j], ry[1][j], ry[2][j], 25);
        for (int i = 0; i <= 25; i++) {
            g[i * 41 + j].u = int32_t(uint32_t(cy[i]) << s.row);
            g[i * 41 + j].v = int32_t(uint32_t(cx[i]) << s.col);
        }
    }
    return g;
}

static void lagrange(double u, double l[3]) {
    l[0] = (u - 1) * (u - 2) * 0.5;
    l[1] = u * (2 - u);
    l[2] = u * (u - 1) * 0.5;
}

void warpTexelAt(const WarpField& f, WarpShift s, double vx, double vy, double& col, double& row) {
    double lx[3], ly[3];
    lagrange(3.0 * (vx - 0.5) / 512.0, lx);
    lagrange(5.0 * (vy - 0.5) / 512.0, ly);
    double X = 0, Y = 0;
    for (int R = 0; R < 3; R++)
        for (int K = 0; K < 3; K++) {
            X += ly[R] * lx[K] * f.x[R][K];
            Y += ly[R] * lx[K] * f.y[R][K];
        }
    col = X * double(1 << s.col) / 256.0;
    row = Y * double(1 << s.row) / 256.0;
}

const char* kWarpGLSL = R"(
// ---- paper warp field 0x2dc8 (src/paper/warp.h)
uniform vec2 uWarpCP[9];   // control points (X, Y), index row * 3 + column
uniform vec2 uWarpScale;   // (2^shiftCol, 2^shiftRow) / 256
vec3 warpLagrange(float u) { return vec3((u - 1.0) * (u - 2.0) * 0.5, u * (2.0 - u), u * (u - 1.0) * 0.5); }
vec2 warpXY(vec2 vga) {
    vec3 lx = warpLagrange(3.0 * (vga.x - 0.5) / 512.0);
    vec3 ly = warpLagrange(5.0 * (vga.y - 0.5) / 512.0);
    vec2 r0 = lx.x * uWarpCP[0] + lx.y * uWarpCP[1] + lx.z * uWarpCP[2];
    vec2 r1 = lx.x * uWarpCP[3] + lx.y * uWarpCP[4] + lx.z * uWarpCP[5];
    vec2 r2 = lx.x * uWarpCP[6] + lx.y * uWarpCP[7] + lx.z * uWarpCP[8];
    return ly.x * r0 + ly.y * r1 + ly.z * r2;
}
vec2 warpTexel(vec2 vga) { return warpXY(vga) * uWarpScale; }
)";

void setWarpUniforms(const Shader& sh, const WarpField& f, WarpShift s) {
    float cp[18];
    for (int R = 0; R < 3; R++)
        for (int K = 0; K < 3; K++) {
            cp[2 * (R * 3 + K)] = float(f.x[R][K]);
            cp[2 * (R * 3 + K) + 1] = float(f.y[R][K]);
        }
    gl::Uniform2fv(sh.loc("uWarpCP"), 9, cp);
    sh.set("uWarpScale", float(1 << s.col) / 256.0f, float(1 << s.row) / 256.0f);
}

namespace {
const char* kWarpVS = R"(
out vec2 vga;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vga = vec2(p.x * 320.0, (1.0 - p.y) * 200.0);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kTexturedFS = R"(
in vec2 vga;
out vec4 o;
uniform sampler2D tex;
uniform float mul;
void main() {
    float v = texture(tex, warpTexel(vga) / 256.0).r * mul;
    o = vec4(vec3(v), 1.0);
}
)";
const char* kFieldFS = R"(
in vec2 vga;
out vec4 o;
uniform float l;
void main() { o = vec4(warpTexel(vga), l, 1.0); }
)";
}  // namespace

Warp::Warp(Ctx&) {
    textured_.build(kWarpVS, std::string(kWarpGLSL) + kTexturedFS, "paper warp textured");
    field_.build(kWarpVS, std::string(kWarpGLSL) + kFieldFS, "paper warp field");
}

void Warp::drawTextured(Ctx&, const WarpField& f, WarpShift s, GLuint tex, float mul) {
    textured_.use();
    setWarpUniforms(textured_, f, s);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    textured_.set("tex", 0);
    textured_.set("mul", mul);
    drawFullscreen();
}

void Warp::drawField(Ctx&, const WarpField& f, WarpShift s, float l) {
    field_.use();
    setWarpUniforms(field_, f, s);
    field_.set("l", l);
    drawFullscreen();
}

}  // namespace paper

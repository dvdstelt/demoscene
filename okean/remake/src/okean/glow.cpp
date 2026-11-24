// The intro's way of drawing (glow.h): the sine table, palettes, kernels, and the canvas of noise, blobs and lines.
#include "okean/glow.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "exe.h"

namespace okean {

namespace {
constexpr uint32_t kSine = 0x24a3c;

// Blobs: one instanced quad per blob, covering the kernel's bilinear surface: the kernel's 8x8 values sit at the
// pixel centres (x + c + 0.5, y + r + 0.5), with a ring of zeros around them, so the surface runs from x - 0.5 to
// x + 8.5 and its integral is the kernel's sum.
const char* kBlobVS = R"(
layout(location = 0) in vec3 b;   // top-left pixel x, y; weight
out vec2 uv;
out float w;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = vec2(0.05) + 0.9 * c;               // texel centres 0.5 .. 9.5 of the 10x10 kernel texture
    w = b.z;
    vec2 p = b.xy - vec2(0.5) + 9.0 * c;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
const char* kBlobFS = R"(
in vec2 uv;
in float w;
out vec4 o;
uniform sampler2D kern;
void main() { o = vec4(texture(kern, uv).r * w); }
)";
const char* kNoiseFS = R"(
in vec2 uv;
out vec4 o;
uniform float levels;
uniform float frame;
uniform float grain;
void main() {
    vec2 cell = floor(gl_FragCoord.xy / grain);
    float h = hash12(cell + vec2(frame * 7.13, frame * 3.71));
    o = vec4(min(floor(h * levels), levels - 1.0));
}
)";
const char* kFlatVS = R"(
uniform vec4 rect;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x / 160.0 - 1.0, 1.0 - p.y / 100.0, 0.0, 1.0);
}
)";
const char* kFlatFS = R"(
out vec4 o;
uniform float v;
void main() { o = vec4(v); }
)";
const char* kResolveFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D buf;
uniform sampler2D pal;
void main() {
    float i = clamp(texture(buf, uv).r, 0.0, 255.0);
    o = vec4(texture(pal, vec2((i + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
)";
}  // namespace

// ---------------------------------------------------------------- the sine table
Sine::Sine(const OkeanExe& exe) {
    for (int i = 0; i <= 1024; i++) t_[size_t(i)] = exe.s32(kSine + 4u * uint32_t(i));
    // the first part's set-up 0x2c26f: 1025..2048 = 1024..1, 2049..4096 = -(0..2047), 4097..5120 = 0..1023
    for (int j = 0; j < 1024; j++) t_[size_t(1025 + j)] = t_[size_t(1024 - j)];
    for (int j = 0; j < 2048; j++) t_[size_t(2049 + j)] = -t_[size_t(j)];
    for (int j = 0; j < 1024; j++) t_[size_t(4097 + j)] = t_[size_t(j)];
}

double Sine::s(double a) { return 1022.0 * std::sin(2.0 * M_PI * a / 4096.0); }
double Sine::c(double a) { return 1022.0 * std::cos(2.0 * M_PI * a / 4096.0); }

// ---------------------------------------------------------------- palettes and kernels
std::array<Vec3, 256> palette(const OkeanExe& exe, uint32_t addr) {
    std::array<Vec3, 256> p{};
    for (uint32_t i = 0; i < 256; i++) {
        auto c = [&](uint32_t k) { return float(std::min<int>(exe.u8(addr + i * 3 + k), 63)) / 63.0f; };
        p[i] = Vec3(c(0), c(1), c(2));
    }
    return p;
}

std::array<Vec3, 256> scaled(const std::array<Vec3, 256>& pal, double num, double den) {
    std::array<Vec3, 256> p{};
    float f = float(std::clamp(num / den, 0.0, 1.0));
    for (size_t i = 0; i < 256; i++) p[i] = pal[i] * f;
    return p;
}

std::array<uint8_t, 64> kernel(const OkeanExe& exe, uint32_t addr) {
    std::array<uint8_t, 64> k{};
    uint32_t p = addr;
    for (int i = 0; i < 16; i++) {
        uint8_t op = exe.u8(p), m = exe.u8(p + 1);
        if (op != 0x81 || (m & 0x3f) != 0x07) throw std::runtime_error("okean::kernel: not the blob routine");
        int32_t d = 0;
        if ((m >> 6) == 1) { d = exe.s8(p + 2); p += 3; }
        else if ((m >> 6) == 2) { d = exe.s32(p + 2); p += 6; }
        else p += 2;
        int y = d / 320, x = d % 320;
        if (d < 0 || y > 7 || x > 4) throw std::runtime_error("okean::kernel: offset outside the 8x8 blob");
        for (int b = 0; b < 4; b++) k[size_t(y * 8 + x + b)] = uint8_t(k[size_t(y * 8 + x + b)] + exe.u8(p + uint32_t(b)));
        p += 4;
    }
    return k;
}

// ---------------------------------------------------------------- the canvas
Canvas::Canvas(Ctx&) {
    blob_.build(kBlobVS, kBlobFS, "okean blob");
    noise_.build(kFullscreenVS, kNoiseFS, "okean noise");
    flat_.build(kFlatVS, kFlatFS, "okean flat");
    resolve_.build(kFullscreenVS, kResolveFS, "okean resolve");
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    gl::VertexAttribDivisor(0, 1);
    gl::BindVertexArray(0);
    float zero[256 * 4] = {};
    pal_.createRaw(256, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, zero, true, false, false);
}

void Canvas::begin(Ctx& c) {
    buf_ = c.temp(false, 1.0f, GL_R16F);
    buf_->bind();
    gl::ClearColor(0, 0, 0, 0);
    gl::Clear(GL_COLOR_BUFFER_BIT);
    blobs_.clear();
}

void Canvas::noise(Ctx& c, int levels, uint32_t frame) {
    setBlend(Blend::None);
    noise_.use();
    noise_.set("levels", float(levels));
    noise_.set("frame", float(frame % 65536u));
    // the grain: a third of an original pixel (frame height / 600), at least one output pixel
    // (okean/remake/README.md "Noise")
    noise_.set("grain", std::max(1.0f, float(c.h) / 600.0f));
    drawFullscreen();
}

void Canvas::setKernel(const std::array<uint8_t, 64>& k) {
    float v[100] = {};
    for (int r = 0; r < 8; r++)
        for (int x = 0; x < 8; x++) v[(r + 1) * 10 + x + 1] = float(k[size_t(r * 8 + x)]);
    kernel_.createRaw(10, 10, GL_R16F, GL_RED, GL_FLOAT, v, true, false, false);
}

void Canvas::blob(float x, float y, float w) {
    blobs_.push_back(x);
    blobs_.push_back(y);
    blobs_.push_back(w);
}

void Canvas::line(float x1, float y1, float x2, float y2, float w, float step) {
    float dx = x2 - x1, dy = y2 - y1;
    int n = int(std::max(std::fabs(dx), std::fabs(dy)) / step + 0.5f);
    if (n <= 0) return;
    float sx = dx / float(n), sy = dy / float(n);
    for (int i = 0; i < n; i++) blob(x1 + sx * float(i), y1 + sy * float(i), w);
}

void Canvas::flush(Ctx&) {
    if (blobs_.empty()) return;
    buf_->bind();
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE);
    gl::Enable(GL_BLEND);
    blob_.use();
    kernel_.bind(0);
    blob_.set("kern", 0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(blobs_.size() * sizeof(float)), blobs_.data(), GL_STREAM_DRAW);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(blobs_.size() / 3));
    gl::BindVertexArray(0);
    blobs_.clear();
    setBlend(Blend::None);
}

void Canvas::add(Ctx&, float v, float x, float y, float w, float h) {
    buf_->bind();
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE);
    gl::Enable(GL_BLEND);
    flat_.use();
    flat_.set("rect", x, y, w, h);
    flat_.set("v", v);
    drawQuad();
    setBlend(Blend::None);
}

void Canvas::resolve(Ctx& c, const std::array<Vec3, 256>& pal, bool ramp) {
    float px[256 * 4];
    std::array<Vec3, 256> p = pal;
    if (ramp) {
        // the white end: entries past the last one below white are white (the grey 254 is the font's)
        size_t last = 0;
        for (size_t i = 0; i < 256; i++)
            if (i != 254 && (p[i].x < 1.0f || p[i].y < 1.0f || p[i].z < 1.0f)) last = i;
        Vec3 top = p[255];
        for (size_t i = last + 1; i < 256; i++) p[i] = top;
    }
    for (size_t i = 0; i < 256; i++) {
        px[i * 4] = p[i].x;
        px[i * 4 + 1] = p[i].y;
        px[i * 4 + 2] = p[i].z;
        px[i * 4 + 3] = 1.0f;
    }
    gl::BindTexture(GL_TEXTURE_2D, pal_.id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, px);
    c.out->bind();
    setBlend(Blend::None);
    resolve_.use();
    buf_->bindTex(0);
    pal_.bind(1);
    resolve_.set("buf", 0);
    resolve_.set("pal", 1);
    drawFullscreen();
}

}  // namespace okean

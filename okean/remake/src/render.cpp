// Small OpenGL helpers (render.h): matrices, shaders, textures, render targets, meshes and the GLSL helpers
// prepended to every fragment shader. Shared with the Paper, JIZZ and STARS remakes.
#include "render.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

Mat4 Mat4::identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r{};
    float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}

Mat4 Mat4::ortho(float l, float r_, float b, float t, float n, float f) {
    Mat4 r = identity();
    r.m[0] = 2.0f / (r_ - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}

Mat4 Mat4::lookAt(Vec3 eye, Vec3 at, Vec3 up) {
    Vec3 f = (at - eye).norm();
    Vec3 s = f.cross(up).norm();
    Vec3 u = s.cross(f);
    Mat4 r = identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -s.dot(eye);
    r.m[13] = -u.dot(eye);
    r.m[14] = f.dot(eye);
    return r;
}

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

Mat4 Mat4::scale(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

Mat4 Mat4::rotX(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

Mat4 Mat4::rotY(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

Mat4 Mat4::rotZ(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

Mat4 Mat4::rotAxis(Vec3 a, float ang) {
    a = a.norm();
    float c = std::cos(ang), s = std::sin(ang), t = 1 - c;
    Mat4 r = identity();
    r.m[0] = t * a.x * a.x + c;       r.m[4] = t * a.x * a.y - s * a.z; r.m[8] = t * a.x * a.z + s * a.y;
    r.m[1] = t * a.x * a.y + s * a.z; r.m[5] = t * a.y * a.y + c;       r.m[9] = t * a.y * a.z - s * a.x;
    r.m[2] = t * a.x * a.z - s * a.y; r.m[6] = t * a.y * a.z + s * a.x; r.m[10] = t * a.z * a.z + c;
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * o.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

Vec3 Mat4::transformPoint(Vec3 p) const {
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    return w != 0 ? Vec3(x / w, y / w, z / w) : Vec3(x, y, z);
}

// ---------------------------------------------------------------- shaders
static GLuint compile(GLenum type, const std::string& src, const char* name) {
    GLuint s = gl::CreateShader(type);
    const char* p = src.c_str();
    gl::ShaderSource(s, 1, &p, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl::GetShaderInfoLog(s, sizeof(log), nullptr, log);
        SDL_Log("shader %s (%s) failed:\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log);
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::build(const std::string& vs, const std::string& fs, const char* name) {
    std::string head = "#version 330 core\n";
    GLuint v = compile(GL_VERTEX_SHADER, head + vs, name);
    GLuint f = compile(GL_FRAGMENT_SHADER, head + kCommonGLSL + fs, name);
    if (!v || !f) return false;
    prog_ = gl::CreateProgram();
    gl::AttachShader(prog_, v);
    gl::AttachShader(prog_, f);
    gl::LinkProgram(prog_);
    GLint ok = 0;
    gl::GetProgramiv(prog_, GL_LINK_STATUS, &ok);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    if (!ok) {
        char log[4096];
        gl::GetProgramInfoLog(prog_, sizeof(log), nullptr, log);
        SDL_Log("program %s link failed:\n%s", name, log);
        return false;
    }
    return true;
}

void Shader::use() const { gl::UseProgram(prog_); }

GLint Shader::loc(const char* name) const {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    GLint l = gl::GetUniformLocation(prog_, name);
    cache_[name] = l;
    return l;
}

// ---------------------------------------------------------------- textures
void Texture::create(const Image& im, bool repeat, bool mipmap) {
    w = im.w;
    h = im.h;
    if (!id) gl::GenTextures(1, &id);
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba.data());
    if (mipmap) gl::GenerateMipmap(GL_TEXTURE_2D);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    gl::GetError();  // anisotropy is optional
    setRepeat(repeat);
}

void Texture::createEmpty(int w_, int h_, GLenum fmt, bool repeat) {
    w = w_;
    h = h_;
    if (!id) gl::GenTextures(1, &id);
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    setRepeat(repeat);
}

void Texture::createRaw(int w_, int h_, GLenum ifmt, GLenum fmt, GLenum type, const void* data, bool linear,
                        bool repeat, bool mipmap) {
    w = w_;
    h = h_;
    if (!id) gl::GenTextures(1, &id);
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(ifmt), w, h, 0, fmt, type, data);
    if (mipmap) gl::GenerateMipmap(GL_TEXTURE_2D);
    GLint minf = linear ? (mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST;
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    setRepeat(repeat);
}

void Texture::destroy() {
    if (id) gl::DeleteTextures(1, &id);
    id = 0;
    w = h = 0;
}

void Texture::update(const uint8_t* rgba) {
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture::setRepeat(bool repeat) const {
    gl::BindTexture(GL_TEXTURE_2D, id);
    GLint m = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m);
}

void Texture::bind(int unit) const {
    gl::ActiveTexture(GL_TEXTURE0 + unit);
    gl::BindTexture(GL_TEXTURE_2D, id);
}

// ---------------------------------------------------------------- targets
bool Target::create(int w_, int h_, bool withDepth, GLenum fmt) {
    destroy();
    w = w_;
    h = h_;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), w, h, 0, GL_RGBA, fmt == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (withDepth) {
        gl::GenRenderbuffers(1, &depth);
        gl::BindRenderbuffer(GL_RENDERBUFFER, depth);
        gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    }
    bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) SDL_Log("framebuffer %dx%d incomplete", w, h);
    return ok;
}

void Target::destroy() {
    if (fbo) gl::DeleteFramebuffers(1, &fbo);
    if (tex) gl::DeleteTextures(1, &tex);
    if (depth) gl::DeleteRenderbuffers(1, &depth);
    fbo = tex = depth = 0;
}

void Target::bind() const {
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::Viewport(0, 0, w, h);
}

void Target::bindTex(int unit) const {
    gl::ActiveTexture(GL_TEXTURE0 + unit);
    gl::BindTexture(GL_TEXTURE_2D, tex);
}

// ---------------------------------------------------------------- meshes
void GpuMesh::create(const Mesh& m) {
    gl::GenVertexArrays(1, &vao);
    gl::BindVertexArray(vao);
    size_t nv = m.pos.size() / 3;
    std::vector<float> v(nv * 7);
    for (size_t i = 0; i < nv; i++) {
        for (int k = 0; k < 3; k++) v[i * 7 + k] = m.pos[i * 3 + k];
        for (int k = 0; k < 3; k++) v[i * 7 + 3 + k] = m.nrm.empty() ? 0.0f : m.nrm[i * 3 + k];
        v[i * 7 + 6] = m.extra.empty() ? 0.0f : m.extra[i];
    }
    gl::GenBuffers(1, &vbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 28, (void*)12);
    gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 28, (void*)24);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::EnableVertexAttribArray(2);
    gl::GenBuffers(1, &ibo);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.idx.size() * 4), m.idx.data(), GL_STATIC_DRAW);
    count = int(m.idx.size());
    gl::BindVertexArray(0);
}

void GpuMesh::draw() const {
    gl::BindVertexArray(vao);
    gl::DrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

static GLuint g_emptyVao = 0;

void drawFullscreen() {
    if (!g_emptyVao) gl::GenVertexArrays(1, &g_emptyVao);
    gl::BindVertexArray(g_emptyVao);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
}

void drawQuad() {
    if (!g_emptyVao) gl::GenVertexArrays(1, &g_emptyVao);
    gl::BindVertexArray(g_emptyVao);
    gl::DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

const char* kFullscreenVS = R"(
out vec2 uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kCommonGLSL = R"(
const float PI = 3.14159265359;
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
mat2 rot2(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
// Catmull-Rom bicubic sampling (sharper upscaling of the low-res originals)
vec4 texBicubic(sampler2D t, vec2 uv) {
    vec2 sz = vec2(textureSize(t, 0));
    vec2 p = uv * sz - 0.5;
    vec2 f = fract(p);
    vec2 i = floor(p);
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 s0 = w0 + w1, s1 = w2 + w3;
    vec2 t0 = (i - 0.5 + w1 / s0) / sz, t1 = (i + 1.5 + w3 / s1) / sz;
    return (textureLod(t, vec2(t0.x, t0.y), 0.0) * s0.x + textureLod(t, vec2(t1.x, t0.y), 0.0) * s1.x) * s0.y +
           (textureLod(t, vec2(t0.x, t1.y), 0.0) * s0.x + textureLod(t, vec2(t1.x, t1.y), 0.0) * s1.x) * s1.y;
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
// Original pixels at high resolution: each texel is a flat square whose edges are anti-aliased over one output
// pixel ("sharp bilinear"; needs a GL_LINEAR texture). Used for --original-art pictures.
vec4 texSharp(sampler2D t, vec2 uv) {
    vec2 sz = vec2(textureSize(t, 0));
    vec2 p = uv * sz;
    vec2 seam = floor(p + 0.5);
    vec2 d = max(fwidth(p), vec2(1e-5));
    p = seam + clamp((p - seam) / d, -0.5, 0.5);
    return texture(t, p / sz);
}
)";

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float smoothstep(float a, float b, float x) { float t = clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float hash1(float n) { float s = std::sin(n) * 43758.5453f; return s - std::floor(s); }

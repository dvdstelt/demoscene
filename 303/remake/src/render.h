// Small OpenGL helpers: math, shaders, textures, render targets, meshes.
#pragma once
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "assets.h"
#include "gl.h"

constexpr float kPi = 3.14159265358979f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float len() const { return std::sqrt(dot(*this)); }
    Vec3 norm() const { float l = len(); return l > 0 ? *this * (1.0f / l) : *this; }
};

struct Mat4 {
    float m[16];  // column major
    static Mat4 identity();
    static Mat4 perspective(float fovyRad, float aspect, float zn, float zf);
    static Mat4 ortho(float l, float r, float b, float t, float n, float f);
    static Mat4 lookAt(Vec3 eye, Vec3 at, Vec3 up);
    static Mat4 translate(float x, float y, float z);
    static Mat4 scale(float x, float y, float z);
    static Mat4 rotX(float a);
    static Mat4 rotY(float a);
    static Mat4 rotZ(float a);
    static Mat4 rotAxis(Vec3 axis, float a);
    Mat4 operator*(const Mat4& o) const;
    Vec3 transformPoint(Vec3 p) const;
};

class Shader {
public:
    bool build(const std::string& vs, const std::string& fs, const char* name);
    void use() const;
    GLint loc(const char* name) const;
    void set(const char* n, float v) const { gl::Uniform1f(loc(n), v); }
    void set(const char* n, int v) const { gl::Uniform1i(loc(n), v); }
    void set(const char* n, float a, float b) const { gl::Uniform2f(loc(n), a, b); }
    void set(const char* n, float a, float b, float c) const { gl::Uniform3f(loc(n), a, b, c); }
    void set(const char* n, float a, float b, float c, float d) const { gl::Uniform4f(loc(n), a, b, c, d); }
    void set(const char* n, const Vec3& v) const { gl::Uniform3f(loc(n), v.x, v.y, v.z); }
    void set(const char* n, const Mat4& m) const { gl::UniformMatrix4fv(loc(n), 1, GL_FALSE, m.m); }
    GLuint id() const { return prog_; }
private:
    GLuint prog_ = 0;
    mutable std::map<std::string, GLint> cache_;
};

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
    void create(const Image& im, bool repeat = true, bool mipmap = true);
    void createEmpty(int w, int h, GLenum internalFmt = GL_RGBA8, bool repeat = false);
    void update(const uint8_t* rgba);
    void setRepeat(bool repeat) const;
    void bind(int unit) const;
};

struct Target {
    GLuint fbo = 0, tex = 0, depth = 0;
    int w = 0, h = 0;
    bool create(int w, int h, bool withDepth, GLenum fmt = GL_RGBA16F);
    void destroy();
    void bind() const;           // binds FBO and sets viewport
    void bindTex(int unit) const;
};

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    int count = 0;
    void create(const Mesh& m);
    void draw() const;
};

// Fullscreen triangle (vertex shader uses gl_VertexID).
void drawFullscreen();
// Unit quad as a 4-vertex triangle strip (gl_VertexID 0..3).
void drawQuad();

extern const char* kFullscreenVS;  // outputs vec2 uv in 0..1
extern const char* kCommonGLSL;    // helpers shared by fragment shaders

float clamp01(float v);
float smoothstep(float a, float b, float x);
float mix(float a, float b, float t);
float hash1(float n);

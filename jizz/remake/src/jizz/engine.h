// Nix's vector engine (JIZZ.EXE 0x14394..0x1f641) on the GPU: the object scripts and their generators, the camera
// paths, and the texture + highlight-map triangle renderer with the colour tables E / S / F. See
// jizz/re/notes-engine.md (and notes-core.md section 6 for the colour tables).
//
//   engine_objects.cpp  0x15bb0 / 0x14394: the script interpreter and every opcode's generator (0x1488d sphere,
//                       0x14b8a cylinder, 0x14e7c torus, 0x15174 halve, 0x151e9..0x153a5 transforms, 0x152a0
//                       duplicate, 0x15276 merge, 0x1a089 normals, 0x14651 texture-seam fix), at the original's
//                       resolution (bit-exact) or with more segments of the same shapes
//   engine_camera.cpp   0x15bef / 0x159fb / 0x15a59 paths, 0x15c40 roll wobble, 0x154bc look-at, 0x19118 projection
//   engine.cpp          0x1f5d1 renderer: depth buffer + MSAA instead of the radix-sorted painter's algorithm,
//                       perspective-correct filtered texture reads, the highlight map 0x6592 evaluated per pixel,
//                       the colour tables 0x1a336 as float maths, near-plane clipping, inset viewports
//
// Usage from a part (all coordinates of viewports in VGA units, 320x200 over the whole 4:3 frame):
//
//   jizz::EngineScene scene;  scene.build(*ctx.exe, 2);                    // init(): the tunnel's script 0x15945
//   // render(): clocks = xmpGetTimer + [0x6542], i.e. PIT clocks since the part's start (+1,000,000 in 0x15d20)
//   jizz::EngineCamera cam = scene.camera(ctx.timer(t) - ctx.timer(start), /*wobble [0x15c3f]*/ true);
//   jizz::EngineShading sh;  sh.tex[1] = map5;  sh.E = 0;  sh.S = 256;  sh.F = 0;   // slot -> GL texture, 0x1a336
//   ctx.shared<jizz::Engine>().render(ctx, scene, cam, jizz::EngineViewport::rect(90, 77, 170, 95), sh);
//
// or, with a mesh the part rebuilds every frame (the metaballs):
//   auto& eng = ctx.shared<jizz::Engine>();
//   eng.begin(ctx, cam, jizz::EngineViewport::full(), sh);  eng.draw(scene);  eng.draw(blobMesh);  eng.end(ctx);
//
// Nothing here keeps state between frames: a frame depends only on the clock value, the visibility and the
// shading the part passes. Checked against render dumps of the original with tools/enginetest.
#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "demo.h"
#include "render.h"

class JizzExe;

namespace jizz {

// ---------------------------------------------------------------- geometry (CPU side)

struct EngineVertex {
    float p[3];  // position: object = world space (the engine has no per-object transform)
    float n[3];  // smooth vertex normal, unit length (the original stores it with length 2^23)
    float u, v;  // texture coordinates in texels (the original's 16.16 values / 65536); repeat every 256
};

struct EngineFace {
    uint32_t a, b, c;  // corner vertex indices; front faces are clockwise on the screen (y down)
    uint8_t type;      // draw type [0x14881]: 1..3 draw (texture + highlight), anything else draws nothing
    uint8_t tex;       // texture slot 1..17 of the table at 0x654a
    bool ownUV;        // across a texture seam (0x14651, face flag 0x80): uv[] replaces the vertices' (u, v)
    float uv[6];       // uA vA uB vB uC vC in texels
};

struct EngineObject {
    uint32_t firstFace, faceCount, firstVertex, vertexCount;
    uint32_t flags;  // +0x4c: 0x40 = not drawn (the ribbon part hides its dome with 0x43); EngineScene::setVisible
    // generator layout, for halve (op 15) and the pole normals: kind 0 = other, 1 = sphere, 2 = cylinder, 3 = torus;
    // n = segments around (S, N), m = rows (M); detail = segment multiplier used
    int kind, n, m, detail;
};

struct EngineGeometry {
    float timeScale = 0;  // header float 0 * 1e-9 ([0x1579c]): engine time units per PIT clock
    float focal = 0;      // header float 1 ([0x154b4]): F, focal length in pixels of the 320-wide frame
    std::vector<EngineVertex> vertices;
    std::vector<EngineFace> faces;
    std::vector<EngineObject> objects;
};

// Runs the object script of scene `scene` (0..4: 0x157a4 ribbons, 0x157fa tube, 0x15945 tunnel, 0x15973 spiky
// object, 0x159cb metaball dome; the scene index is also the camera path's row) from JIZZ.EXE.
// detail = 1: the original's vertices and faces, bit-exact (float32 like the x87 at 24-bit precision).
// detail = k > 1: every generator makes k times as many segments in each direction (same parametrisation, same
// texture coordinates; "halve" keeps the original's strips), and the coincident vertices at the sphere poles
// share one normal (the original gives each pole vertex the normal of its own column's faces).
EngineGeometry buildEngineGeometry(const JizzExe& exe, int scene, int detail = 1);
// The address of scene `scene`'s script header (two floats, then the byte code).
uint32_t engineScriptAddress(int scene);

// ---------------------------------------------------------------- camera

// A viewport in VGA coordinates (x 0..320, y 0..200 over the whole 4:3 frame) and the projection centre in it.
// The original renders an inset of W x H pixels at (x, y) with the full-screen view scaled to it (0x19118).
struct EngineViewport {
    float x = 0, y = 0, w = 320, h = 200;
    float cx = 160, cy = 100;  // projection centre (VGA coordinates)
    static EngineViewport rect(float x, float y, float w, float h) { return {x, y, w, h, x + w * 0.5f, y + h * 0.5f}; }
    static EngineViewport full() { return rect(0, 0, 320, 200); }
};

struct EngineCamera {
    double t = 0;          // engine time ([0x159f7])
    Vec3 eye, target;      // 0x15a59 path points ([0x1549c], [0x154a8] before the look-at)
    float roll = 0;        // [0x154b8], wobble included
    Vec3 right, up, fwd;   // world-space camera axes after the roll: rows of the matrix 0x154bc writes
    float focal = 0;       // F
    // world -> GL eye space (x right, y up, looking down -z)
    Mat4 view() const;
    // GL eye space -> clip space of the whole 4:3 frame, for the viewport: x_vga = cx + (w/320) F x/z,
    // y_vga = cy - (h/200) F y/z (0x18ea1), depth range zNear..zFar (the original drops faces nearer than 16.5)
    Mat4 projection(const EngineViewport& vp, float zNear = 16.0f, float zFar = 20000.0f) const;
    // camera space of a world point: x right, y up, z = depth along the view axis (the original's x', y', z')
    Vec3 toCamera(Vec3 world) const;
    // VGA coordinates of a world point; false when it is not in front of the eye
    bool toVGA(Vec3 world, const EngineViewport& vp, float* x, float* y) const;
};

// The camera path of one scene: the 9 floats of row `scene` of the table at 0x15afc, the wobble constants
// (0x15c7f..) and the scene's F and time scale (script header).
struct EngineCameraPath {
    int scene = 0;
    float p[9] = {};           // Rh, Rv, target scale, target time offset, a, b, c, d, roll speed
    float wobble[3] = {};      // 0.21, 0.62, 0.14
    float focal = 0, timeScale = 0;
    float pitchBase = 0;       // 0x15710 (3 pi / 2)
    void load(const JizzExe& exe, int scene);
    // engine time t = clocks * time scale; clocks = xmpGetTimer + [0x6542] (PIT clocks since the part's start,
    // plus 1,000,000 in the spiky part 0x15d20). The original truncates clocks to an integer; this is continuous.
    double time(double clocks) const { return clocks * double(timeScale); }
    EngineCamera at(double t, bool wobble) const;  // 0x159fb, 0x15c40 (if wobble), 0x154bc
    EngineCamera camera(double clocks, bool wobble) const { return at(time(clocks), wobble); }
};

// ---------------------------------------------------------------- GPU meshes

struct EngineGpuVertex {
    float p[3], n[3], uv[2];  // uv in texture repeats (texels / 256)
    float unit;               // texture unit 0..3 of the draw, + 4: texture coordinates from the normal
};

// A built scene: the static objects of one script, on the GPU.
class EngineScene {
public:
    EngineScene() = default;
    EngineScene(const EngineScene&) = delete;
    EngineScene& operator=(const EngineScene&) = delete;
    // Build scene `scene` (see buildEngineGeometry) and upload it. detail 1 = the original's faces.
    void build(const JizzExe& exe, int scene, int detail = 4);
    void build(const EngineGeometry& g, const EngineCameraPath& path);
    int scene() const { return path_.scene; }
    const EngineCameraPath& path() const { return path_; }
    const EngineGeometry& geometry() const { return geo_; }
    EngineCamera camera(double clocks, bool wobble) const { return path_.camera(clocks, wobble); }
    // Object visibility (object flag 0x40); the ribbon part shows its dome (object 0) from event 201 on.
    void setVisible(int object, bool on);
    bool visible(int object) const;

private:
    friend class Engine;
    EngineGeometry geo_;
    EngineCameraPath path_;
    std::vector<EngineGpuVertex> verts_;                 // 3 per drawn face, objects in order
    std::vector<std::pair<uint32_t, uint32_t>> ranges_;  // per object: first vertex in verts_, vertex count
    std::vector<int> slots_;                             // texture slot of each unit (at most 4)
    GLuint vao_ = 0, vbo_ = 0;
};

// A mesh the part rebuilds every frame (the metaballs, 0x769f), drawn with the same shading.
class EngineMesh {
public:
    EngineMesh() = default;
    EngineMesh(const EngineMesh&) = delete;
    EngineMesh& operator=(const EngineMesh&) = delete;
    // pos, nrm: 3 floats per vertex (world space; normals of any length, renormalised per pixel); uv: 2 floats per
    // vertex in texels (may be null when uvFromNormal); idx: 3 vertex indices per triangle, front faces clockwise
    // on the screen like the generated objects. Set uvFromNormal before the upload. GL objects are not freed
    // (like the framework's meshes and textures, they live as long as the program).
    void upload(const float* pos, const float* nrm, const float* uv, size_t vertexCount, const uint32_t* idx,
                size_t indexCount);
    int texSlot = 13;           // texture slot of every face (the blob mesh: [0x6b6c] = 13)
    bool uvFromNormal = false;  // (u, v) = 128 * normalize(n).xy texels, per pixel (0x79a8's mapping)
    bool cull = true;           // back faces culled (face flag 0x40 clear)

private:
    friend class Engine;
    std::vector<EngineGpuVertex> verts_;
    std::vector<uint32_t> idx_;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
};

// ---------------------------------------------------------------- renderer

struct EngineShading {
    std::array<GLuint, 18> tex{};  // GL texture of each slot of the table at 0x654a (1..17): 256x256 maps, row v at
                                   // t = (v + 0.5) / 256, red = byte 0; GL_REPEAT, linear (mipmapped) filtering.
                                   // Slot 0 (the highlight map 0x220000) is evaluated in the shader.
    float E = 0;    // colour tables 0x1a336: brightness in 8-bit units (eax: -200, -40, -50, 0)
    float S = 256;  // contrast [0x1a225], 8.8 fixed point (0x100 = 1.0; 0x1c8, 0x132)
    float F = 0;    // inversion [0x1a478], 0..255 (255 = negative)
};

// GLSL for parts that shade engine surfaces themselves (e.g. ray marched): the pixel formula of the engine.
//   vec3 engineColour(vec3 texel, vec3 n, vec3 right, vec3 up, vec3 esf)
// texel: the texture's colour (0..1), n: the surface normal (any length), right / up: EngineCamera::right / up,
// esf = (E / 255, S / 256, F / 256).
extern const char* kEngineGLSL;

class Engine {
public:
    Engine();
    explicit Engine(Ctx&) : Engine() {}  // for ctx.shared<jizz::Engine>()
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Draw a scene into `out` (the part's frame, ctx.out), over what is there: anti-aliased edges are blended onto
    // it. Leaves `out` bound, blending, depth test, culling and scissor off.
    void render(Target& out, const EngineScene& s, const EngineCamera& cam, const EngineViewport& vp,
                const EngineShading& sh) {
        begin(out, cam, vp, sh);
        draw(s);
        end();
    }
    void render(Ctx& c, const EngineScene& s, const EngineCamera& cam, const EngineViewport& vp, const EngineShading& sh) {
        render(*c.out, s, cam, vp, sh);
    }
    // Several scenes / meshes in one depth buffer: begin, draw..., end (composites onto `out`).
    void begin(Target& out, const EngineCamera& cam, const EngineViewport& vp, const EngineShading& sh);
    void begin(Ctx& c, const EngineCamera& cam, const EngineViewport& vp, const EngineShading& sh) {
        begin(*c.out, cam, vp, sh);
    }
    void draw(const EngineScene& s);
    void draw(const EngineMesh& m);
    void end();
    void end(Ctx&) { end(); }

    // MSAA samples (clamped to GL_MAX_SAMPLES); 4 by default.
    int samples = 4;
    // For checking against the original only: painter = no depth buffer, faces sorted far to near by the
    // original's key (0x19f78: the rounded sum of the three corner depths, low 16 bits, 0x1966e's order) across all
    // draws of the frame; dropNear = drop faces with a corner nearer than 16.5 (0x18ea1 / 0x19f78) instead of
    // clipping them at the near plane; original = the original's pixel shortcuts (screen-linear interpolation,
    // nearest texels, highlight coordinates interpolated per vertex, integer colour tables, 5:6:5).
    struct Debug {
        bool painter = false;
        bool dropNear = false;
        bool original = false;
    } debug;

private:
    struct PainterFace {
        int key;
        EngineGpuVertex v[3];
    };
    void ensureTargets(int w, int h);
    int unitFor(int slot);
    void bindUnits(const std::vector<int>& slots);
    void addPainterFaces(const std::vector<EngineGpuVertex>& verts, const uint32_t* idx, uint32_t first,
                         uint32_t count, const std::vector<int>& slots);
    void setDrawState(bool cull);

    Shader shader_, composite_;
    Texture dummy_;  // bound for slots without a texture
    GLuint msFbo_ = 0, msColour_ = 0, msDepth_ = 0;
    int msW_ = 0, msH_ = 0, msSamples_ = 0;
    Target resolved_;  // the samples averaged: premultiplied colour, alpha = coverage
    GLuint painterVao_ = 0, painterVbo_ = 0;
    // frame state between begin and end
    Target* out_ = nullptr;
    EngineShading sh_;
    EngineCamera cam_;
    EngineViewport vp_;
    std::vector<int> frameSlots_;
    std::vector<PainterFace> painter_;
};

}  // namespace jizz

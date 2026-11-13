// The 3D engine of parts 7..10 on the GPU (clone/re/notes-engine.md; how the parts call it: notes-parts3d.md
// sections 2.1..2.7):
//
//   engine_geometry.cpp  the CPU side: the matrix routines 0x404c9f (identity), 0x404cd6 (scale), 0x404d5c /
//                        0x404e36 / 0x404f0f (rotations), 0x404feb (translate), 0x4050e9 (transform); the geosphere
//                        generator 0x4174c4 / 0x417420 / 0x4172c8; the vertex normals 0x416b61; the back-face test and
//                        depth key of 0x416929 (with the far cut)
//   engine.cpp           the draw 0x4169f8 (the shade per vertex), the frustum clipper 0x41674b / 0x4166dc /
//                        0x416669 / 0x416472 / 0x416297 and Craft's texture mapper 0x415127 (edges 0x414f2f /
//                        0x4150f7, the bilinear filter, the shade table 0x426b8c) as one shader: the closed form of
//                        notes-engine 10, MSAA, a depth buffer, composited over the part's frame inside the window of
//                        0x414f04
//
// What the original computes and what is evaluated here (notes-engine 11):
//   - Geometry in float as the original (it is float already): camera space x right, y down, z forward, the eye at
//     the origin. Back faces are dropped by the original's test det(v0, v1, v2) < 0, and faces whose depth key
//     trunc(0.2 (z0 + z1 + z2)) is >= 4096 (mean z >= 6826.7) by the far cut of the bucket table: part 8's blob comes
//     in face by face from z = 9400. Faces with a negative key are drawn (the original puts them in bucket 0).
//   - The projection column = 159.5 + 256 (-a) x / z, row = 99.5 + 256 (-b) y / z for the window (a, b) of 0x414f04
//     (a = -0.625: 160 x / z; b = -0.3125: 80 y / z, b = -0.390625: 100 y / z): the original's sample point is the lower
//     right corner of a pixel, so its picture is the exact projection moved up and left by half a pixel (9.2). The
//     pyramid clip |x|, |y| < z becomes the window rectangle columns 0..320, rows 100 -+ 256 (-b) (rows 20..180 or
//     0..200, whole pixels as the original fills them) cut on the screen, no near plane but z = 1 (the original has
//     none; the parts never come near the eye).
//   - Craft's mapper: perspective-correct u, v per pixel (the original divides every 8 pixels and steps affinely
//     between), continuous coordinates (8.8 fixed, V's carry leaking into U), the map's log levels L filtered
//     bilinearly by the GPU with continuous weights (4-bit weights, floored sums), texel centres at integer
//     coordinates (+0.5 texel in GL), GL_REPEAT (the original's 257th row and the U + 1 seam read). The shade s is
//     interpolated linearly on the screen (the original steps it in 16.16, affine on the screen as here); the pixel
//     is min(255, 128 exp(0.011 (L + s - 256))) / 255 per channel instead of the table 0x426b8c (integer index,
//     truncated bytes): the clamp to white stays, it is part of the look. The maps come from cmc::Textures in the
//     Log form, in the style the part chooses (Smooth: round spots at high resolution; Texel: the original's texels).
//     Where the map is minified (part 8's fly-in, small cubes) the original's point samples alias, and average to the
//     mean of the decoded values; the remake filters, with that mean: the log-filtered sample times the ratio of the
//     mipmapped Linear form to its base level (1 where magnified). Mipmaps of the log levels alone would give the
//     geometric mean, darker where the lattices' dark cells are minified (Debug::logMips).
//   - Shades: with normals (parts 7, 8) 37.2 (1 - n_z)^3 per vertex (2437939.2 / 65536 read at 0x41a4c8) from the
//     normalised sum of the face normals around the vertex (0x416b61, area-weighted, from the camera-space positions
//     every frame); per-corner values (parts 9, 10: the faces' +0x3c..) as given, in table units (16.16 / 65536,
//     not truncated). Both times the light [0x427f70].
//   - The painter's order of the 4096 buckets becomes a depth buffer (the 20 checked frames give the same picture,
//     notes-engine 6; part 9's interior faces, which only the sort shows, disappear: notes-parts3d 8).
//     Order::Painter draws the faces in the original's order without the depth test, for comparison.
//   - Edges: the GPU rasteriser with MSAA instead of 16.16 edges with truncated slopes; composited with coverage over
//     what the part drew (premultiplied).
//   - Not kept: the clipper's re-interpolation of the shade at the clip points in camera space (a triangle that
//     crosses the window's edge is shaded affinely on the screen as a whole), the left-clip bug, D == 0, shade-table
//     indices >= 512 (none of them is seen in the capture).
//
// Usage from a part (part 7, notes-parts3d 3; T = c.clock(t), t70 units since the part's origin):
//
//   // init() (the GL context is current):
//   geo = cmc::Geosphere::make(*c.exe, 4);                    // 0x4174c4: 1026 vertices, 2048 faces, radius 300
//   mesh = geo.mesh(cmc::Tex::MapA, c.exe->f64(0x41a4d0));    // 0x416cca: uv = 0.004 (x, y), map A, shade by normals
//   c.shared<cmc::Engine>().prepare(cmc::Tex::MapA);          // builds the map's textures now
//   // render():
//   cmc::Mat34 m = cmc::Mat34::part(A7, float(1.1 * A7), float(0.8 * A7), 4.0f / 3.0f);   // notes-parts3d 2.2
//   cmc::transform(m, geo.verts, mesh.pos);                   // 0x4050e9 (mesh.pos is sized by mesh())
//   for (size_t i = 0; i < mesh.pos.size(); i++) { ...the deformation 0x416e37: mesh.pos[i] *= s; z += 2000... }
//   ...the tile block over the frame (ctx.rect)...
//   auto& eng = c.shared<cmc::Engine>();
//   eng.begin(c, cmc::Window::at(c, t));                      // event 7: full height; 8, 9, 10: rows 20..179
//   eng.draw(mesh, light);                                    // [0x427f70]: min(2 fade, 1) for part 7
//   eng.end();                                                // composited over the tiles
//
// Parts 9 / 10 build their faces per frame: mesh.shade = cmc::Shade::Corners, mesh.map = cmc::Tex::MapC; for every
// cube 8 entries in an object-space vertex list and up to 12 EngineFace{v = {n + a, n + b, n + c}, uv (0.003 (X + Z),
// 0.003 (Y - Z)), shade = 192 + 31 (sin + sin + sin)}; then mesh.pos.resize(src.size()), cmc::transform(m, src,
// mesh.pos) and eng.draw(mesh, 1.2f). tools/enginetest/scenes.cpp has test scenes of the four parts that show the
// calls in context.
//
// The geosphere: level 4 with int16 vertices is the original's. Geosphere::make(exe, 6, true) rounds the silhouette at
// high resolution, but it also resolves the creases of the parts' deformations (the clamps of 0x416e37 / 0x4176d0) in
// the Gouraud shading, which level 4 averages away: the shading of part 7's bumps changes visibly.
//
// Checked with tools/enginetest against the 28 draws dumped from the emulator (clone/re/models/engine; 20 with faces):
// the geosphere identical (1026 vertices, 2048 faces), Mat34::part on the parts' angles within 1.3e-4 of the original's
// vertices, the normals within 1.4e-7, the back-face flags, the far cut (all 1019..1055 front faces of part 8's first
// frames) and the painter's order identical; the frames drawn from the dumped vertices with the original's map bytes
// at its resolution and coverage (one sample, no mipmaps) differ from the original's pages by 0.7..1.5 (part 7),
// 1.2..1.4 (8), 1.7..1.9 (9), 1.6..2.2 (10) levels on average (the closed form of notes-engine 10: 0.6, 1.1, 1.7, 1.9);
// the test scenes against the capture (tools/compare.py) show the same pictures.
//
// The light [0x427f70] (notes-parts3d 2.3): part 7 min(2 fade, 1); part 8 1400 / Z, after the 803 events 0.75, 0.5,
// 0.25; part 9 1.2 (event 9); part 10 1.0, 1.2 / 1.0 at the events 1011 / 1012. The engine keeps no state between
// frames: a frame depends only on the meshes, the light and the window passed.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "clone/textures.h"
#include "demo.h"
#include "render.h"

class CloneExe;

namespace cmc {

struct EngineMesh;

// =============================================================================================== matrices
// A 3 x 4 matrix as the original keeps it: 12 floats, row r = (m[4r], m[4r+1], m[4r+2], m[4r+3]); a point transforms as
// p' = M (x, y, z, 1). The routines change the matrix in place and pre-multiply (the last call is applied last to the
// points), with sin / cos of the float angle stored as floats as the original does (notes-engine 3).
struct Mat34 {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    static Mat34 identity() { return Mat34{}; }               // 0x404c9f
    Mat34& rotX(float a);                                      // 0x404f0f: M = [[1,0,0],[0,c,-s],[0,s,c]] M
    Mat34& rotY(float a);                                      // 0x404e36: M = [[c,0,s],[0,1,0],[-s,0,c]] M
    Mat34& rotZ(float a);                                      // 0x404d5c: M = [[c,-s,0],[s,c,0],[0,0,1]] M
    Mat34& scale(float sx, float sy, float sz);                // 0x404cd6: M = diag(sx, sy, sz) M (translation too)
    Mat34& translate(float tx, float ty, float tz);            // 0x404feb: the translation column += (tx, ty, tz)
    Vec3 apply(const Vec3& p) const;                           // 0x4050e9 for one point
    // The matrix of all four parts (notes-parts3d 2.2): I, Rx(a), Ry(b), Rz(c), scale(1, sy, 1), translate(0, 0, tz).
    // a, b, c as the part passes them (part 7: A7, (float)(1.1 A7), (float)(0.8 A7); the products are doubles).
    static Mat34 part(float a, float b, float c, float sy, float tz = 0);
};
// 0x4050e9: dst[i] = M src[i] (dst.size() >= src.size()).
void transform(const Mat34& m, std::span<const Vec3> src, std::span<Vec3> dst);

// =============================================================================================== the geosphere
// 0x4174c4: the octahedron of 0x427f9c (6 vertices, the low 16 bits of 3 dwords each) / 0x427f84 (8 faces, 3 bytes)
// subdivided `levels` times (0x417420: depth first, the middle triangle last), each new vertex pushed out to radius
// 300 (0x4172c8). exact = false: the original's int16 arithmetic (C truncations; level 4: 1026 vertices, 2048 faces,
// radius 298.5..300.5, identical to the original's); true: the same topology, vertex order and face order with exact
// float positions on the sphere of radius 300 (any level: 4^levels * 8 faces, a rounder silhouette at high
// resolution; the shading changes slightly, notes-engine 4).
struct Geosphere {
    std::vector<Vec3> verts;                     // model positions (the int16 values as floats, or exact)
    std::vector<std::array<uint32_t, 3>> faces;  // a, b, c: (p_a - p_b) x (p_c - p_b) points outwards
    static Geosphere make(const CloneExe& exe, int levels = 4, bool exact = false);
    // 0x416cca / 0x417563: an EngineMesh with these faces, uv = uvScale (x, y) of the model positions per corner
    // (the double 0.004 at 0x41a4d0 / 0x41a554: a planar projection along z, the map 2.4 times across), the map,
    // shaded by normals; pos sized to the vertex count (fill it per frame).
    EngineMesh mesh(Tex map, double uvScale) const;
};

// =============================================================================================== objects
// One face (the record of notes-engine 2.2 without the engine's fields).
struct EngineFace {
    uint32_t v[3] = {0, 0, 0};      // +0x04..: the corners a, b, c (indices into EngineMesh::pos)
    float uv[3][2] = {};            // +0x1c..: u, v per corner (1.0 = one map width: 256 texels; u = column, v = row)
    float shade[3] = {0, 0, 0};     // +0x3c..: shade per corner in table units (the 16.16 value / 65536), Shade::Corners
};
enum class Shade {
    Normals,  // 37.2 (1 - n_z)^3 from per-vertex normals (parts 7, 8: +0x10 != 0)
    Corners,  // the faces' shade values (parts 9, 10: +0x10 = 0)
};
// An object as the parts hand it to the engine: camera-space positions (after 0x4050e9 and the part's own
// deformation; x right, y down, z forward, the eye at the origin), faces, how they are shaded, the map.
struct EngineMesh {
    std::vector<Vec3> pos;
    std::vector<EngineFace> faces;
    Shade shade = Shade::Normals;
    // Shade::Normals: per-vertex normals (camera space, unit length). Left empty, the engine computes them from pos as
    // 0x416b61 does every frame (vertexNormals); a part may supply its own (e.g. analytic ones).
    std::vector<Vec3> normals;
    Tex map = Tex::MapA;                 // +0x38: map A (part 7), B (parts 8, 10), C (part 9)
    const Texture* texture = nullptr;    // instead of map: any texture in the Log form (L / 255 per channel, 0..1),
                                         // repeating once per 1.0 of u, v, texel centres at (i + 0.5) / size
};
// 0x416b61: n[i] = normalised sum over the faces around vertex i of (p0 - p1) x (p2 - p1) (camera space).
void vertexNormals(std::span<const Vec3> pos, std::span<const EngineFace> faces, std::vector<Vec3>& out);

// 0x416929 for one face (camera-space corners): the back-face test and the depth key.
struct FaceSort {
    bool hidden = false;  // det(v0, v1, v2) < 0: turned away from the eye (+0x34)
    int key = 0;          // trunc(0.2 (z0 + z1 + z2)), negative -> 0 (the bucket)
    bool cut = false;     // key >= 4096: not drawn at all (the far cut; mean z >= 6826.7)
    bool drawn() const { return !hidden && !cut; }
};
FaceSort sortFace(const Vec3& v0, const Vec3& v1, const Vec3& v2);
constexpr int kBuckets = 4096;  // 0xf5d3dc: 4096 bucket lists

// =============================================================================================== the window
// 0x414f04(a, b) stores the engine's view limits [0x422534] = a, [0x422538] = b: column = 160 + 256 (-a) x / z,
// row = 100 + 256 (-b) y / z, and the clip pyramid |x|, |y| < z is the rectangle columns 160 -+ 256 (-a), rows
// 100 -+ 256 (-b). The precalc 0x416806 and event 10 set band(), event 7 full() (notes-parts3d 2.6).
struct Window {
    float a = -0.625f, b = -0.3125f;
    static Window band() { return {-0.625f, -0.3125f}; }    // rows 20..179: parts 8, 9, 10 (and before event 7)
    static Window full() { return {-0.625f, -0.390625f}; }  // rows 0..199: part 7
    // The window main has set at music time t: band() before event 7 and from event 10, full() in between.
    static Window at(const Ctx& c, double t);
    float x0() const { return 160.0f + 256.0f * a; }        // VGA rectangle the engine draws
    float x1() const { return 160.0f - 256.0f * a; }
    float y0() const { return 100.0f + 256.0f * b; }
    float y1() const { return 100.0f - 256.0f * b; }
};

// =============================================================================================== the renderer
// The VGA rectangle a target covers (the frame: 0, 0, 320, 200).
struct View {
    float x = 0, y = 0, w = 320, h = 200;
};

enum class Order {
    Depth,    // a depth buffer (default)
    Painter,  // the original's order: buckets far to near, within a bucket the later face first, no depth test
};

class Engine {
public:
    explicit Engine(Ctx& c);   // for c.shared<cmc::Engine>() (reads the shade constant from c.exe)
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    // Start a frame: the meshes drawn until end() share one depth buffer (MSAA) and are composited over what the
    // target holds (the part's background) inside the window.
    void begin(Ctx& c, const Window& w) { begin(*c.out, w, View{}); }
    void begin(Target& out, const Window& w, const View& view = View{});
    // 0x416929 + 0x4169f8 for one object with the light [0x427f70].
    void draw(const EngineMesh& m, float light);
    // Builds the map's textures in the current style now (call it in init(): the smooth style takes some 25 ms per
    // map, which would otherwise fall on the part's first frame).
    void prepare(Tex map);
    void end();

    // The shade of a vertex with normal z component nz, before the light: 37.2 (1 - nz)^3 (0x416ae3).
    float normalShade(float nz) const;
    // VGA position of a camera-space point (with the half-pixel shift; false behind the eye).
    bool toScreen(const Vec3& p, float* x, float* y) const;

    int samples = 4;                    // MSAA samples (clamped to GL_MAX_SAMPLES)
    Order order = Order::Depth;
    TexStyle style = TexStyle::Smooth;  // the maps' style (textures.h); Texel is the closest to the capture
    struct Debug {
        bool logMips = false;           // minified maps averaged in the log form (the mean of L: darker)
    } debug;
    struct Stats {
        int faces = 0, hidden = 0, cut = 0, drawn = 0;
    } stats;                            // of the last draw()

private:
    struct Buffers;
    Buffers& buffers(int w, int h);
    void upload(const std::vector<float>& data);

    Ctx* ctx_ = nullptr;
    double shadeK_ = 37.2;   // 2437939.2 / 65536 (0x41a4c8)
    Shader shader_, composite_;
    std::vector<Buffers*> buffers_;
    Buffers* cur_ = nullptr;
    Target* out_ = nullptr;
    Window win_;
    View view_;
    unsigned vao_ = 0, vbo_ = 0;
    size_t vboSize_ = 0;
    std::vector<float> data_;
    std::vector<Vec3> normals_;
    struct Sorted { int key; uint32_t face; };
    std::vector<Sorted> sorted_;
};

}  // namespace cmc

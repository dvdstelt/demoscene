// SurRender P5 1.02 (Hybrid), the 3D engine INSIDE links, ported to OpenGL (inside/re/notes-engine.md has every
// routine, structure and constant; the addresses below refer to it). The parts call it with their own geometry and
// motion, as they call the original: objects from .sro data or built in code, cameras and point lights as objects
// with a parent hierarchy, materials with textures and tables, a rasterizer per face, billboards and 2D bitmaps.
//
// What the engine computes as the original does (in double precision, without the integer quantisation unless
// Fidelity::exact): world transforms 0x40c2b8 / 0x40c224, object -> camera 0x40c158, the bounding-box test 0x41a5b8,
// the object-space back-face test 0x41a458 (no back-face culling with flag 0x100), the projection 0x41afa0 (centre
// (160, 100) of the 320 x 200 frame with the rasterizers' sampling offset (0.5, 0.4375), scales fov W / 320 and
// fov H / 240), the face list and keys 0x41a9d4, per-vertex point lights 0x41aba4 / 0x41b508 (linear falloff,
// Lambert, clamp 992), the environment mapping of flag 0x1000 (0x41acc4), the near clip at the scene's near plane
// 0x41c0e0 (the same one or two triangles), the painter's order 0x40c384 (key descending, stable in scene-list and
// face order; no depth test: the sort biases decide what is in front), billboards 0x4157d4 / 0x415878 and 2D
// bitmaps 0x415df0. The pixels are drawn by the GPU: each texel's index goes through the tables and the palette, the
// four texels around the sample point are blended in colour, the light level is continuous (the two neighbouring
// table rows blended in colour), u, v and the levels are interpolated perspective-correctly, edges are anti-aliased
// (MSAA, resolved into ctx.out). The rasterizers that read the background (0x41669c, 0x416dc8, the type 2 / 4
// bitmaps and the blended billboards) are drawn with dual-source blending: each table row is fitted in RGB, per
// channel, as out = B * background + S (least squares over the 256 background colours of the palette, leaving out
// entries where the table saturates at white or black past the line; Renderer::fit gives the fit and its error,
// tools/blend_fit.py reports it for every such table of INSIDE), and the fragment outputs S and B.
//
// ---------------------------------------------------------------------------------------------------------- usage
//
//   // init (the part's init routine)
//   sr::Renderer eng;                                      // a member of the part; eng.init(c) first in its init
//   int tex = eng.texture(dat.file(texName), 256, 256);    // 8-bit texture (powers of two up to 512 x 512)
//   int shd = eng.table(dat.file(shdName));                // .shd / .int / .fog table: rows x 256 bytes
//   int pal = eng.palette(inside::palette(dat, palName));  // the part's base palette
//   int mat = eng.material({.texture = tex, .table1 = shd});          // 0x4105e4
//   sr::Scene scene;                                       // 0x410878 defaults: fov 256, near 0x2000, ambient 0
//   scene.fov = 42; scene.near = 0x3000;                   // the original's units (near: camera z x 128)
//   sr::Object camera, obj, light;                         // the part owns its objects; the scene points to them
//   scene.add(camera); scene.camera = &camera;             // 0x411154 (puts it at the head of the list)
//   obj.loadSro(dat.file(sroName), 0x10000, mat);          // 0x410bb0 (scale 16.16), normals 0x410918 / 0x410a70
//   obj.setRaster(sr::rasterAt(0x414e5c));                 // 0x410850: every face's rasterizer
//   obj.flags |= sr::kLit; scene.add(obj);
//   light.flags |= sr::kLight; light.range = 550; light.intensity = 1600; scene.add(light);
//
//   // a frame (the part's frame routine)
//   obj.rot = ...; camera.pos = {0, 0, 100};               // set what the part sets, at refresh r
//   eng.render(c, scene, pal, 0);                          // 0x41127c(scene, 0) + 0x40c6ac: into ctx.out
//   eng.drawBitmap(c, bitmap, 31, 9, pal);                 // 0x415df0
//
// Units are the original's, so the notes' constants can be used as they are: positions in object units (the .sro's
// int16 x scale / 65536), u, v as 12-bit fractions of the texture (4096 = its width or height, whatever its size:
// 16 per texel of a 256 x 256 texture; they wrap) and eu, ev likewise of the 256 x 256 second texture, shades in 1/16
// of a table row (level = shade / 16; lighting clamps at 992), face normals of length 1024 and vertex normals of
// length 819.2 (the lighting and 0x1000 depend on these lengths), the scene's near plane and object biases in camera
// z x 128, angles in radians. Rotations are 3 x 3 row-major matrices acting on column vectors:
// world = rot * local + pos.
//
// Objects (struct Object): a mesh (loadSro, makeGrid, or fill verts / faces and call computeNormals), a camera or a
// light (no geometry), or a billboard (makeBillboard). `parent` makes a hierarchy (0x40c224: the chain of the
// parents' own rot / pos; flags kNoParentRot / kNoParentPos as 0x08 / 0x10). The camera must be in the scene's list
// (its world transform is computed there). A light is any listed object with kLight and without kHidden: range R
// ([0xc8]) and intensity I ([0xcc]); every kLit mesh gets shade = ambient + sum of I (1 - d / R) (n . d) / (512 d)
// per vertex. Vertices of meshes that are not lit keep the shade the part writes (temple's glow, flight's fog in
// shade2). A face has its own material and rasterizer, and flags kFaceHidden / kFaceDoubleSided.
//
// Rasterizers (Raster, rasterAt(original address)), with the table the material gives:
//   Tex          0x412db0  tex[u, v]
//   TexShade     0x414e5c  table1[level][tex[u, v]]
//   TexShadeFog  0x412264  table2[level2][table1[level][tex[u, v]]]
//   TexBlend     0x41669c  table1[tex[u, v]][background]          (fitted blend)
//   ShadeBlend   0x416dc8  table1[level][background]              (fitted blend, no texture)
//   TexEnv       0x4172e0  table1[tex2[eu, ev]][tex[u, v]]
// Billboards: opaque (0x415dbc) or through their table (0x415dc8, fitted blend). Bitmaps (struct Bitmap, made by
// Renderer::bitmap, any size): type 1 copy, 2 table[src][dst], 3 copy where src != key, 4 table where src != key.
//
// Render (Renderer::render) draws into ctx.out (bound): it clears the scene's viewport to a palette index (as the
// parts' 0x41127c) or, with clearIndex < 0, draws over what ctx.out holds (a background drawn before). Everything
// the scene draws is in VGA coordinates of the 320 x 200 frame shown at 4:3. render depends only on the objects'
// state: a part sets that from the refresh and renders, and seeking works.
//
// Fidelity (sr::fidelity(), from the environment variable INSIDE_ENGINE): the remake's settings by
// default; INSIDE_ENGINE=original gives the original's look for pixel checks at --shotsize 320x200: affine
// (noperspective) interpolation, nearest texels and floor(level) rows, the integer arithmetic of the original's CPU
// stages (truncated positions, normals, 1/16-pixel screen positions, integer shades, the fixed-point clipper) and no
// MSAA; table bitmaps (types 2, 4) then look the background's index up from a copy of the frame (the palette
// colour nearest to it) and the table exactly. Single switches: INSIDE_ENGINE=affine,nearest,exact,msaa=N
// (comma-separated). tools/engine_check.py compares such shots with the capture (the logo: 99.97 % of the 3D pixels
// and 99.995 % of the bitmaps' pixels identical over all 701 records; the rest are one table row or texel off where
// float interpolation and the original's fixed-point gradients round differently).
//
// Differences from the original (besides the evaluation in float, filtering and MSAA):
//   - the blend tables are fitted per row in colour (see above), not looked up from the background's index;
//   - a material with fixed coordinates (fixedUV) gives them to the face it draws only; the original writes them into
//     the face's vertices, where a later face sharing a vertex would also see them (no INSIDE object mixes them);
//   - level and texel rows past a table's end are clamped to its last row (the original reads past the table);
//   - bitmaps are drawn at the float position given (the original truncates it; Fidelity::exact does too);
//   - the scene viewport flags of [0x4c] (keep XS / YS / centre, scale from the viewport) are not modelled (unused).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render.h"

struct Ctx;

namespace inside::sr {

// ---------------------------------------------------------------------------------------------- vectors, matrices
// Doubles, with the original's operation order where it matters for Fidelity::exact (notes-engine.md: 0x414070,
// 0x4144ec).
struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double a, double b, double c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const V3& o) const { return (x * o.x + y * o.y) + z * o.z; }
    V3 cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double len() const;
};

struct M3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static M3 identity() { return {}; }
    M3 operator*(const M3& b) const;   // 0x414070: (a[i][1] b[1][j] + a[i][0] b[0][j]) + a[i][2] b[2][j]
    V3 operator*(const V3& v) const;   // 0x4144ec: (v0 M[i][0] + v1 M[i][1]) + v2 M[i][2]
    M3 transposed() const;
};
M3 rotX(double a);                         // [[1,0,0],[0,c,-s],[0,s,c]]
M3 rotY(double a);                         // [[c,0,s],[0,1,0],[-s,0,c]]
M3 rotZ(double a);                         // [[c,-s,0],[s,c,0],[0,0,1]]
M3 euler(double a, double b, double c);    // 0x4145f4: Rx(a) Ry(b) Rz(c)
M3 rotateX(const M3& m, double a);         // 0x4145c4: m Rx(a)  (0x414270)
M3 rotateY(const M3& m, double a);         // 0x4145ac: m Ry(a)  (0x414344)
M3 rotateZ(const M3& m, double a);         // 0x4145dc / 0x414418: m Rz(a)
M3 look(V3 dir);                           // 0x414cec (rows r0, r1, d; see notes-engine.md for its orientation)
M3 axisAngle(V3 unitAxis, double angle);   // rotation by angle about a unit axis (for continuous forms of S^k)
// The axis and angle of a rotation matrix (angle in [0, pi]).
void toAxisAngle(const M3& r, V3& axis, double& angle);

// ---------------------------------------------------------------------------------------------- flags, rasterizers
enum : uint32_t {
    kHidden = 0x0001,         // the object's pipeline stops; a hidden light is off
    kNoParentRot = 0x0008,    // world rotation = own rotation
    kNoParentPos = 0x0010,    // world position = own position
    kLit = 0x0040,            // Gouraud shade from the scene's lights (0x41aba4)
    kLight = 0x0080,          // this object is a point light (range, intensity)
    kNoCull = 0x0100,         // no back-face culling (every face used)
    kEnvMap = 0x1000,         // u, v from the camera-space normal and the projected position (0x41acc4)
    kNoBBox = 0x2000,         // skip the bounding-box test
};
enum : uint32_t {
    kFaceHidden = 0x001,
    kFaceDoubleSided = 0x100,
};

enum class Raster : uint8_t { Tex, TexShade, TexShadeFog, TexBlend, ShadeBlend, TexEnv };
// The rasterizer of an original draw function address (0x412db0, 0x414e5c, 0x412264, 0x41669c, 0x416dc8,
// 0x4172e0); throws std::invalid_argument for anything else.
Raster rasterAt(uint32_t routine);

// ---------------------------------------------------------------------------------------------- data
struct Material {             // 0x4105e4 (0x44 bytes)
    int texture = -1;         // Renderer::texture handles
    int texture2 = -1;        // the environment map of TexEnv (addressed eu & 255, ev & 255)
    int table1 = -1;          // Renderer::table handles
    int table2 = -1;
    bool fixedUV = false;     // flag bit 0: the face's vertices get uv[] (v0, v1, v2 in face order) when drawn
    double uv[6] = {};        // u0, v0, u1, v1, u2, v2 (texture fractions, as Vertex::u, v)
};

struct Vertex {               // 0x40 bytes in the original
    V3 p;                     // object-space position
    double u = 0, v = 0;      // texture coordinates (4096 = the texture's width / height)
    double eu = 0, ev = 0;    // second coordinates (TexEnv: 16 per texel of its 256 x 256 second texture)
    double shade = 0;         // channel 1 (1/16 levels); lit objects get it from the lights every render
    double shade2 = 0;        // channel 2 (TexShadeFog's level2)
    V3 n;                     // vertex normal, length 819.2
};

struct Face {                 // 0x34 bytes
    int a = 0, b = 0, c = 0;  // vertex indices
    int material = -1;        // Renderer::material handle
    Raster raster = Raster::Tex;
    uint32_t flags = 0;       // kFaceHidden, kFaceDoubleSided
    V3 n;                     // face normal, length 1024
};

struct Sprite {               // a billboard's bitmap (0x415790: [0xd8..0xf4])
    int texture = -1;         // Renderer::texture handle (not wrapped)
    int table = -1;           // blended through this table (0x415dc8) if >= 0, else opaque (0x415dbc)
    int w = 0, h = 0;         // texels
    double scale = 1.0;       // [0xec]: scales the position
    double size = 1.0;        // [0xf0]: units per texel
    double bias = 0;          // [0xf4]: added to the depth key (camera z x 128)
};

struct Object {
    enum class Kind { Plain, Mesh, Billboard } kind = Kind::Plain;
    Object* parent = nullptr;
    V3 pos;                   // in the parent's space
    M3 rot;
    uint32_t flags = 0;
    double bias = 0;          // mesh: sort bias [0xcc], camera z x 128 (subtracted from the face keys)
    double range = 0;         // light: R [0xc8]
    double intensity = 0;     // light: I [0xcc]
    std::vector<Vertex> verts;
    std::vector<Face> faces;
    V3 bmin, bmax;            // bounding box (0x410de8)
    double radius = 0;        // max |vertex|
    Sprite sprite;            // billboards

    void euler(double a, double b, double c) { rot = sr::euler(a, b, c); }   // 0x4108dc
    // 0x410bb0: the .sro image (header, int16 x y z, uint16 u v, faces of three int16 indices) with a 16.16 scale and
    // a material; computes the normals and the bounding box. Like the original it resets the object first (identity
    // rotation, position 0, no parent, flags 0, bias 0). Every face gets raster `Tex` (set it with setRaster).
    void loadSro(std::span<const uint8_t> sro, int32_t scale, int material);
    void setRaster(Raster r);                                   // 0x410850
    void computeNormals();                                      // 0x410918, 0x410a70
    void computeBounds();                                       // 0x410de8
    void translate(V3 d);                                       // 0x41172c (bounds updated)
    void scale(double sx, double sy, double sz);                // 0x4116c8 (bounds not updated, as the original)
    // 0x4114c8: vertex positions, vertex normals and face normals = a + (b - a) t (min of the counts)
    void morph(const Object& a, const Object& b, double t);
    // 0x410f34 + 0x410bb0: an nx x nz grid in the XZ plane, cells sx x sz, u = v = 0
    void makeGrid(int nx, int nz, double sx, double sz, int material);
    void makeBillboard(const Sprite& s);                        // 0x415790
    void lookAt(V3 target);   // 0x41077c: rot = look(target - world position), flag kNoParentRot (call after render
                              // computed the world position, or for an object without parent)

    // computed by render (world transform)
    V3 wpos;
    M3 wrot;
};

struct Scene {                // 0x50 bytes; defaults of 0x410878
    std::vector<Object*> list;          // objects in list order (add() puts an object at the head, as 0x411154)
    Object* camera = nullptr;
    int width = 320, height = 200;      // buffer size (projection scales)
    int x0 = 0, y0 = 0, x1 = 320, y1 = 200;   // viewport (0x410618)
    double fov = 256;                   // [0x3c]
    double ambient = 0;                 // [0x40], shade units
    double near = 0x2000;               // [0x44], camera z x 128
    double far = 0x400000;              // [0x48], units (billboards only)
    void add(Object& o);                // 0x411154: at the head of the list, if not in it
    void remove(Object& o);
    // 0x411e80: a world point's position on the original's screen in pixels, as the original returns it (not
    // truncated; the triangles' pixel (c, r) shows the point (c + 1, r + 15/16) of this screen, so the remake's VGA
    // coordinates of the point are x - 0.5, y - 0.4375); false if near / 256 > camera z. Uses the world transforms
    // of the last render (or call updateTransforms first).
    bool project(V3 world, double& x, double& y) const;
    void updateTransforms();            // 0x40c2b8 for every object in the list
};

struct Bitmap {               // 0x415e50 (0x28 bytes)
    int type = 1;             // 1 copy, 2 table[src][dst], 3 copy where src != key, 4 table where src != key
    int table = -1;           // Renderer::table handle (types 2, 4)
    int key = 0;              // transparent index (types 3, 4)
    int w = 0, h = 0;
    int id = -1;              // the Renderer's texture of the pixels
};

struct Fidelity {
    bool perspective = true;  // false: u, v and levels interpolated affinely in screen space (the original)
    bool filter = true;       // false: the nearest texel and floor(level) table row (the original)
    bool exact = false;       // the original's integer arithmetic in the CPU stages
    int samples = 4;          // MSAA samples (0: none)
    static Fidelity original() { return {false, false, true, 0}; }
};
// The engine's settings, from INSIDE_ENGINE (see the top of this file) at the first call; parts may change them
// before their init (objects loaded in exact mode keep integer positions).
Fidelity& fidelity();

// A table row fitted in colour: out = b * background + s per channel; error in 6-bit units (0..63).
struct RowFit {
    Vec3 b, s;
    float rms = 0, max = 0;
};

struct Gpu;

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init(Ctx& c);        // GL objects (with the context current)

    // Resources (any time after init; uploaded at the next render). Pixels are row-major, `pitch` bytes per row
    // (0: w).
    int texture(std::span<const uint8_t> px, int w, int h, int pitch = 0);
    // A texture of INSIDE.DAT by file name, with its upscale (art/tex_<name>.jpg, pictures.h) when the remake shows
    // its art and the texels are filtered: the shader adds the upscale's detail to the colour it computes from the
    // original's indices, through the gain of the tables it reads (see the top of engine_gl.cpp).
    int textureFile(const std::string& file, int w, int h, int pitch = 0);
    int table(std::span<const uint8_t> t);          // rows = size / 256 (up to 256)
    int palette(const std::array<Vec3, 256>& colours);
    int material(const Material& m);
    Material& materialAt(int handle);
    Bitmap bitmap(std::span<const uint8_t> px, int pitch, int w, int h);   // type 1, key 0
    // A level map's upscale for a table bitmap (types 2, 4) whose pixels are table rows 0..levels (the INSIDE.DAT
    // file's art/tex_<name>.jpg, grey 255 x level / levels, 4 x the bitmap's size), used with the art on and filtered
    // texels: the bitmap is drawn at the upscale's continuous level (the two table rows around it blended; the key
    // row leaves the background as it is).
    void bitmapDetail(const Bitmap& b, const std::string& file, int levels);

    // Upload the textures, tables and upscales now (the end of a part's init) instead of at the first render.
    void prepare();

    // The fit of every row of a table through a palette (what the blended rasterizers draw).
    const std::vector<RowFit>& fit(int table, int palette);

    // 0x41127c (clearIndex >= 0) and 0x40c6ac into ctx.out.
    void render(Ctx& c, Scene& s, int palette, int clearIndex = -1);
    // 0x415df0: the bitmap with its top-left corner at (x, y) in VGA coordinates, one texel per original pixel.
    void drawBitmap(Ctx& c, const Bitmap& b, float x, float y, int palette);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace inside::sr

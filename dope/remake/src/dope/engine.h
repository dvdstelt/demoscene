// The 3D engine of p2, p3, p5, p6 and p7 on the GPU (dope/re/notes-engine.md; how each part uses it: notes-p2
// section 7, notes-p3p5 section 2, notes-p6p7 section 2). One engine in five copies, ported once with the copies'
// differences (notes-engine 12) as options:
//
//   engine_data.cpp  meshes (the format of notes-engine 3.2) and object records (3.1) read from the part images by
//                    address, the per-part options (EnginePart), env maps and bump maps (the texel codes through the
//                    bump tables, 7.4), and the float transform on the CPU (for checks and the painter's order)
//   engine.cpp       the frame build on the GPU: object loop / matrix / vertex transform (p2 354a, 348b, 1d86,
//                    1c60, 1b60; p3 2a5a, 299b, 1df6, 1cd0, 1bb0; p5 2a17, 2958, 1906, 17e0, 16b0; p6 2c9c, 2bdd,
//                    1b56, 1a30, 1930; p7 2eca, 2e0b, 1dd6, 1cb0, 1b90), face set-up and culling (p2 3377, p3 2887,
//                    p5 2844, p6 2ac7, p7 2cf7), the radix sort (p2 3585, p3 2a95, p5 2a52, p6 2cd9, p7 2f05) as a
//                    depth buffer (or the original's order), the fillers texture + env (p2 2881 / 2660, p3 2211 /
//                    1ff0, p5 1d51 / 1b30, p6 1fd1 / 1db0, p7 2201 / 1fe0) and env only (p2 3030 / 2ea0, p5 2500 /
//                    2370, p6 2780 / 25f0, p7 29b0 / 2820) as one shader, MSAA, composited over the part's frame
//
// What the original computes and what is evaluated here (notes-engine 11):
//   - The transform in float with exact sin / cos of the 16-bit angles: R = Rz(-c) Rx(-a) Ry(b) (a, b, c = record
//     +1e, +20, +22 plus the camera slot's), V = R (v + pivot), T = position + camera position (1/128 mesh unit),
//     d = T.z / 128 - V.z, screen x = 160 - f/8 (V.x + T.x/128) / d, screen row = 99 - 3f/32 (V.y + T.y/128) / d
//     (the buffer's row 100 is screen row 99), f per object. No rounding to whole pixels; the GPU rasterises with
//     MSAA. The 0.75 vertical scale and the per-object f stay (part of the look).
//   - Back faces are culled by the winding on the screen (cross product >= 0 drawn, or < 0 with the record's +2c
//     set), in float: the 16-bit products that overflow on faces larger than ~181 px are not reproduced.
//   - A depth buffer instead of the radix sort by face-centroid depth (Order::Depth), or the original's order
//     (Order::Painter: faces of all objects sorted by the sum of their vertices' local depth times f, far first,
//     drawn over each other). The depth compared is d, or with Object::depthScale the original's key: p2's "C"
//     (f = 1300 against the discs' 4000) sorts with its depth times 1300/4000, so it shows between the discs as in
//     the original (notes-p2 7.2).
//   - Env coordinates per pixel from the view-space normal R n interpolated perspective-correctly and not
//     renormalised (the original interpolates the per-vertex coordinates, a linear function of it): column 64 +
//     (R n).x / 512, row 64 + (R n).y / 512 texels of the 128 x 128 map (p6: 64 - .., 64 - ..). Texture words
//     (mesh map coordinates or the object-space normal, with p3's 15-bit wrap) interpolated perspective-correctly.
//   - Maps are read filtered instead of nearest: the env map's colours (indices converted through the part's
//     palette first, or a level map through a palette ramp), and the bump displacement (each texel code decoded
//     through the bump table, then filtered); the texel shift's low-resolution row sampling is kept (p2 reads 64 of
//     the bump map's 128 rows, p5 / p6 32: that is the pattern the viewer sees). Where the bump map is minified the
//     pixel is box-filtered (up to 3 x 3 samples).
//   - Dropped 320x200 / 8-bit artefacts: integer edge stepping, the carries between the packed 16-bit halves, the
//     stale gradients of degenerate triangles, the 8-bit span counter, the missing column 319 of clipped env-only
//     spans, the trivial reject of triangles with all corners left / right of the screen, the top-edge borrow.
//
// Usage from a part (p5's BUMP torus; values in the record's units: positions 1/128 mesh unit, angles 65536 per
// turn; the part evaluates its per-retrace motion at r = c.retraces(t) with dope::Integrator):
//
//   // init() (GL context current):
//   const DopeImage& img = c.exe->part(5);
//   const EnginePart part = EnginePart::of(5);                   // p5's options and addresses
//   torus.load(img, 0x1402);                                     // the mesh at 1402:0000
//   rec = ObjectRecord::read(img, 0x144c);                       // object 1 (f 2500, ...)
//   fur.create(img, 0x1c96, bumpNibbles(), part);                // texture 1c96, offsets (lo - 8, hi - 8)
//   const Picture& env = c.picture("p5-envmaps");                // 256 x 128: gold left (03d7), red right (03df)
//   mat.env[0] = mat.env[2] = EnvMap::picture(env, 0);           // the env table 03d7, 03df, 0bd7, 0bdf
//   mat.env[1] = mat.env[3] = EnvMap::picture(env, 1);
//   mat.tex[1] = &fur;                                           // the texture table 0, 1c96, 0, 0
//   // render():
//   c.drawPicture(c.picture("p5-chequerboard"));                 // the background, restored every frame
//   Object o = Object::from(rec, &torus);
//   o.envAdd = o.texAdd = 1;                                     // the script's [1476] = 0101h (record +2a)
//   mat.bumpScale = float(A / 256);                              // 3099's depth, continuous
//   auto& eng = c.shared<Engine>();
//   eng.begin(c, part, Pose{camX, camY, camZ, camA, camB, camC}); // the camera slot 1412
//   eng.draw(o, mat);
//   eng.end();                                                   // composited over the frame (MSAA, AA edges)
//   c.drawPicture(c.picture("p5-label-bump"), labelDraw(label.position(r + 1)));      // the label, over it
//
// drawFrame() does the frame composition of the last lines (background, objects, label) in one call. p2 draws the
// frame into its own target for the blur: eng.begin(target, EnginePart::of(2), cam); its "C" sets depthScale =
// 1300 / 4000. p6's light map 0451 (shadow mask, morph) is an EnvTexture the part rebuilds per frame; p7's light
// map is a level map in a 256 x 256 texture of the part's: EnvMap::levelMap(tex, 256, 256, 64, 64, 128, &pal, 0,
// 255) with clamp = false. tools/enginetest/scenes.cpp has test scenes of all five parts that show the calls in
// context, and enginetest checks the engine against the bit-exact model (dope/re/models/engine).
//
// Nothing keeps state between frames: a frame depends only on the objects, materials and camera passed. Checked with
// tools/enginetest against the 19 frames of the bit-exact model (screen positions within 2 px of the integer engine,
// mean 0.3; env coordinates within 0.25 texel; texture words exact; culling differs only on faces under 31 px^2 and
// on the 16-bit overflows), and with the test scenes against the capture.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "demo.h"
#include "dope/core.h"
#include "pictures.h"
#include "render.h"

class DopeImage;

namespace dope {

// =============================================================================================== per-part options
// Where the five copies of the engine differ (notes-engine 12). EnginePart::of(n) gives the original's choice for
// part n; the flags that are a remake's choice say so.
struct EnginePart {
    enum class TexSource {
        Mesh,    // the mesh's map coordinates (p2, p5, p6)
        Normal,  // the object-space normal: texture words (hi, lo) = (n.y, n.x) (p3, p7)
    };
    TexSource texSource = TexSource::Mesh;
    // Normal source: the 15-bit wrap sext15(n) = ((n + 16384) mod 32768) - 16384 of 1bb0 / 1b90 (vertices with
    // |n| >= 16384 jump by a whole bump tile against their neighbours: the texture is interpolated "the long way
    // round" across those faces, the streaks of notes-p3p5 3.8). p3 keeps it; for p7 notes-engine 11.3 says keep,
    // notes-p6p7 7 says drop: the part decides (of(7) keeps it).
    bool wrap15 = true;
    int texShift = 9;           // the filler's `shld ebx, ecx, k`: 8 (p3, p7), 9 (p2), 10 (p5, p6)
    // k = 9 / 10 read only every 2nd / 4th row of the bump map (rows 2j / 4j + 1): sample a 64 / 32-row map made of
    // those rows (true: what the original shows, notes-engine 11.3) or all 128 rows continuously (false: notes-p2
    // 7.3's suggestion; the bumps then change shape).
    bool lowResRows = true;
    int rowPhase = 1;           // k = 10: the row bits taken from the high word, hi >> 14 (1 for the meshes that use
                                // a texture: hi = 0x4000..0x7fff); k = 9: hi >> 15 = 0
    bool envNegate = false;     // p6 1a5c `neg edx`: env coordinates 64 - (R n) / 512
    bool pivotAfter = false;    // p5 16b0: the pivot added again after the rotation, unscaled (pivot / 32768 units)
    // Addresses (cs offsets) in the part image: the object list (slot 0 = the camera), the env and texture segment
    // tables (4 words each, image segments: segmentTable()), and the static bump table (-1: built per retrace from a
    // strength, p5 3099 / p6 3913: use bumpNibbles()).
    uint16_t objects = 0, envTable = 0, texTable = 0;
    int bumpTable = -1;
    static EnginePart of(int part);  // 2, 3, 5, 6, 7
};
// The 4-word segment table at cs:off (EnginePart::envTable / texTable): image segments, 0 = none.
std::array<uint16_t, 4> segmentTable(const DopeImage& img, uint16_t off);

// =============================================================================================== object state
// A position and three angles in the object record's units: position +06 / +0a / +0e (1/128 mesh unit), angles
// +1e / +20 / +22 (65536 = one turn; fractional values and any range are fine: the engine takes them mod 65536).
struct Pose {
    double x = 0, y = 0, z = 0;
    double a = 0, b = 0, c = 0;
    Pose operator+(const Pose& o) const { return {x + o.x, y + o.y, z + o.z, a + o.a, b + o.b, c + o.c}; }
};

// An object record (0x3a bytes; notes-engine 3.1) as the part image holds it at cs:addr: the initial values the
// scripts start from (they set f, positions, velocities, the material adds and +02 themselves).
struct ObjectRecord {
    uint16_t addr = 0;          // cs offset of the record
    uint16_t seg = 0;           // +00 mesh segment (0xffff: the camera slot; 0: the end of the list)
    bool off = false;           // +02 != 0: neither drawn nor moved
    int f = 0;                  // +04 projection factor
    Pose pose;                  // +06 .. +0e position, +1e .. +22 angles
    Pose vel;                   // +12 .. +1a velocity, +24 .. +28 angular velocity (per retrace)
    int envAdd = 0, texAdd = 0; // +2a low byte (added to the env selector), high byte (texture selector)
    bool cullFlip = false;      // +2c != 0: the negative screen cross products are drawn
    double pivot[3] = {0, 0, 0};// +2e / +32 / +36 (mesh units, added before the rotation)
    static ObjectRecord read(const DopeImage& img, uint16_t addr);
};
// The list from slot 0 (the camera) at cs:first up to the record whose +00 is 0 (not included).
std::vector<ObjectRecord> readObjectList(const DopeImage& img, uint16_t first);

// A mesh of the engine's format (notes-engine 3.2) read from a part image: S:0000 vertex count, face count, then
// the offsets of the positions (3 x s16), faces (attr, 3 x vertex index * 4), normals (3 x s16, length 27558) and
// map coordinates (low word, high word). Rigid: never changed at run time. The GPU copy is made on the first draw.
class EngineMesh {
public:
    struct Vertex {
        int16_t x = 0, y = 0, z = 0;
        int16_t nx = 0, ny = 0, nz = 0;
        uint16_t lo = 0, hi = 0;   // the texture dword's words (map coordinates)
    };
    struct Face {
        uint16_t attr = 0;         // low byte: env selector, high byte: texture selector (0 in all meshes)
        uint16_t a = 0, b = 0, c = 0;  // vertex indices
    };
    std::vector<Vertex> verts;
    std::vector<Face> faces;
    uint16_t seg = 0;

    EngineMesh() = default;
    EngineMesh(const EngineMesh&) = delete;
    EngineMesh& operator=(const EngineMesh&) = delete;
    ~EngineMesh();
    bool load(const DopeImage& img, uint16_t seg);   // false (and empty) if the header does not fit the image

private:
    friend class Engine;
    void upload() const;
    struct Group { uint16_t attr; int first, count; };   // faces by attr (indices into the index buffer)
    mutable std::vector<Group> groups_;
    mutable unsigned vao_ = 0, vbo_ = 0, ibo_ = 0;
};

// One drawable object: a record's state at the moment drawn.
struct Object {
    const EngineMesh* mesh = nullptr;
    Pose pose;                      // the record's own position and angles (the camera slot's are added)
    double f = 2500;                // +04
    double pivot[3] = {0, 0, 0};    // +2e .. (mesh units)
    bool cullFlip = false;          // +2c
    int envAdd = 0, texAdd = 0;     // +2a: added to the faces' env / texture selectors
    // The depth used for the depth test: d with V.z scaled by this (1: the true distance). p2's "C": 1300 / 4000,
    // the original's sort key relative to the discs' (notes-p2 7.2).
    double depthScale = 1;
    static Object from(const ObjectRecord& r, const EngineMesh* m);
};

// =============================================================================================== maps
// An env map: the 128 x 128 window of 8-bit colours a face's env selector picks (notes-engine 7.1), as a texture
// region. Texel (column u, row v) of the window is at texture coordinates (u0 + (u1 - u0) u / 128, v0 + (v1 - v0)
// v / 128), row 0 at v0. Colour maps are sampled filtered in true colour; level maps (levels != nullptr) give the
// colour of index levelBase + levelScale * value (value = the texture's red, 0..1), interpolated along the palette.
struct EnvMap {
    unsigned tex = 0;
    int texW = 128, texH = 128;    // the texture's size in texels (for the clamp half a texel inside)
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    // Reads outside the window (bump offsets at the silhouette) clamped to its edge (true), or the texture's
    // neighbouring texels (false: what the original reads there, e.g. p7's light map around its centre window; or
    // the other half of p5's 256 x 128 picture, which the original reads at the red map's silhouette).
    bool clamp = true;
    const PaletteTexture* levels = nullptr;
    float levelBase = 0, levelScale = 255;
    // A picture's frame as the map: half = 0 the left, 1 the right 128 x 128 of a 256 x 128 picture (p2-envmaps,
    // p3-phongmap, p5-envmaps), -1 the whole frame (p6-envmap's left half is its frame).
    static EnvMap picture(const Picture& p, int half = -1);
    // A level texture of w x h texels with the window's top-left texel at (x, y) and `size` texels wide.
    static EnvMap levelMap(unsigned tex, int w, int h, float x, float y, float size, const PaletteTexture* pal,
                           float base, float scale);
};

// An env map texture made from indices through a palette (palette first, then filtered: indices are not linear
// values): p2's maps 6538 / 6540, p6's generated right half 0451 (per frame, with the morph's mixed palette), or
// anything a part generates. RGBA16F, mipmapped, clamped. Rebuilding it per frame is cheap (128 x 128).
class EnvTexture {
public:
    Texture tex;
    EnvTexture() = default;
    EnvTexture(const EnvTexture&) = delete;
    EnvTexture& operator=(const EnvTexture&) = delete;
    // w x h indices, `stride` bytes per row (256 for a window of a 256-byte-wide block).
    void set(std::span<const uint8_t> indices, int w, int h, int stride, const Palette& pal);
    // Colours, w x h, rows top to bottom.
    void setColours(std::span<const Vec3> rgb, int w, int h);
    EnvMap map(int half = -1) const;    // as EnvMap::picture
    ~EnvTexture() { tex.destroy(); }
};

// Bump displacement per texel code t (0..255): (column, row) offset of the env lookup in env texels.
using BumpOffsets = std::array<std::array<float, 2>, 256>;
// A 256-word table at cs:off as the filler adds it to the 16-bit env address (p2, p3, p7 0010:0000): word o =
// 256 dy + dx, dx = ((o + 128) & 255) - 128 (the table's quirks kept: p2's D = -7..6 with its two borrow rows,
// p3 / p7's dx = 0 rows one row up).
BumpOffsets bumpTable(const DopeImage& img, uint16_t off);
// The tables p5 (3099) and p6 (3913) rebuild every retrace from a strength s, as continuous offsets per unit
// strength: dx = (t & 15) - 8 (column, low nibble), dy = (t >> 4) - 8 (row, high nibble); multiply by s / 256 with
// Materials::bumpScale (the floor and the +1 row on negative column offsets are byte-packing artefacts, dropped).
BumpOffsets bumpNibbles();

// A bump map: the texture segment's 128 x 128 texel codes (rows of 256 bytes) decoded to their displacement, with
// the part's texel addressing (texShift, lowResRows, rowPhase): RG16F, filtered, mipmapped, repeating.
class BumpMap {
public:
    Texture tex;
    BumpMap() = default;
    BumpMap(const BumpMap&) = delete;
    BumpMap& operator=(const BumpMap&) = delete;
    int rows = 128;                            // rows of the texture: 128, or 64 / 32 with lowResRows
    float colScale = 1.0f / 256, rowScale = 1.0f / 256, rowOffset = 0;   // texels per texture-word unit
    bool create(const DopeImage& img, uint16_t seg, const BumpOffsets& o, const EnginePart& part);
    // codes: at least 127 * stride + 128 bytes.
    bool create(std::span<const uint8_t> codes, int stride, const BumpOffsets& o, const EnginePart& part);
    ~BumpMap() { tex.destroy(); }
};

// A part's materials: the two 4-entry segment tables and the global selector adders (notes-engine 7.1, the
// tables at p2 0010:0400 / 0408 ...), the bump strength and a final colour transform.
struct Materials {
    EnvMap env[4];
    const BumpMap* tex[4] = {nullptr, nullptr, nullptr, nullptr};   // nullptr = segment 0: the env-only filler
    int envAdd = 0, texAdd = 0;     // the global adders (p2 [194f] / [194d], p3 [19be] / [19bc], ...)
    float bumpScale = 1;            // offsets times this (p5 / p6: strength / 256 with bumpNibbles())
    Vec3 mul = Vec3(1, 1, 1), add = Vec3(0, 0, 0);   // colour = clamp(colour * mul + add, 0, 1)
};

// =============================================================================================== the transform
// The float transform (the vertex shader does the same) for one vertex, as the original would see it.
struct ProjectedVertex {
    double x = 0, y = 0;     // VGA coordinates (screen row = the engine's buffer row - 1)
    double d = 0;            // distance in front of the eye (mesh units)
    double vz = 0;           // V.z, the local depth in the object's rotated frame (the sort key is vz * f)
    double eu = 0, ev = 0;   // env column / row (texels of the 128 map)
    double thi = 0, tlo = 0; // texture words (hi, lo) as interpolated
};
// R = Rz(-c) Rx(-a) Ry(b) for the angles (65536 per turn), row-major: v' = R v for a column vector v.
std::array<double, 9> engineRotation(double a, double b, double c);
void projectObject(const EnginePart& part, const Pose& camera, const Object& o, std::vector<ProjectedVertex>& out);

// =============================================================================================== the renderer
// The part of the target the engine draws: the VGA rectangle the target covers and where the engine may draw
// (the original fills buffer rows 1..200 = screen rows 0..199, columns 0..319: the whole screen).
struct View {
    float x = 0, y = 0, w = 320, h = 200;
    float clipX = 0, clipY = 0, clipW = 320, clipH = 200;
};

enum class Order {
    Depth,    // a depth buffer (default)
    Painter,  // the original's order: all faces of the frame sorted by key, far first, no depth test
};

class Engine {
public:
    Engine();
    explicit Engine(Ctx&) : Engine() {}   // for c.shared<dope::Engine>()
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    // Start a frame: the objects drawn until end() share one depth buffer (MSAA) and are composited over what
    // the target holds (the part's background). `camera` is the camera slot (slot 0): its position and angles are
    // added to every object's.
    void begin(Ctx& c, const EnginePart& part, const Pose& camera, const View& view = View{},
               Order order = Order::Depth) {
        begin(*c.out, part, camera, view, order);
    }
    void begin(Target& out, const EnginePart& part, const Pose& camera, const View& view = View{},
               Order order = Order::Depth);
    // The object (348b: matrix, vertices, faces) with the materials its faces select.
    void draw(const Object& o, const Materials& m);
    void end();

    int samples = 4;              // MSAA samples (clamped to GL_MAX_SAMPLES)
    struct Debug {                // for checks against the original (tools/enginetest)
        bool affine = false;      // screen-linear interpolation of the env and texture coordinates, as the fillers
        bool singleSample = false;// one bump read per pixel (no box filter where the bump map is minified)
    } debug;

private:
    struct Buffers;
    struct Pending;
    Buffers& buffers(int w, int h);
    void setObject(const Object& o);
    void setMaterial(const Materials& m, int envSel, int texSel);
    void drawPainter();

    Shader shader_, composite_;
    Texture white_, zero_;
    std::vector<Buffers*> buffers_;
    std::vector<Pending>* pending_ = nullptr;
    Buffers* cur_ = nullptr;
    Target* out_ = nullptr;
    EnginePart part_;
    Pose cam_;
    View view_;
    Order frameOrder_ = Order::Depth;
    unsigned sortIbo_ = 0;
};

// The frame of the engine parts (frame copies p3 2b1c, p5 2adc, p6 2d5c, p7 2f8c; p2 360c before its blend): the
// background picture restored behind the objects (picture rows 0..199 = screen rows 0..199), the objects, then the
// 70 x 44 label at position labelPos (core labelDraw; < 0: none), all times mul (a DAC fade, fadeMul(L)).
// drawObjects() is called between them: eng.begin(...); eng.draw(...); eng.end();
struct FrameLayers {
    const Picture* background = nullptr;
    const Picture* label = nullptr;
    double labelPos = -1;
    Vec3 mul = Vec3(1, 1, 1);
};
template <class F> void drawFrame(Ctx& c, const FrameLayers& l, F&& drawObjects) {
    if (l.background) c.drawPicture(*l.background, PictureDraw{.mul = l.mul});
    drawObjects();
    if (l.label && l.labelPos > 0) {
        PictureDraw d = labelDraw(l.labelPos);
        d.mul = l.mul;
        c.drawPicture(*l.label, d);
    }
}

}  // namespace dope

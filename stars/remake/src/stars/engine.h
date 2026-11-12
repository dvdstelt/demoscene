// BARTi's 3D engine (STARS.EXE 0x7871c..0x79fad and the scanline fillers 0x4d2ee..0x55835) on the GPU, for the 3D
// parts bee, spike ball, wall, star and torso (stars/re/notes-engine.md; how the parts call it: notes-parts3d-a.md
// section 6, notes-parts3d-b.md section 2).
//
//   engine_objects.cpp  0x78c62 `B!` objects, the `WLDD` world of file 28, 0x78fda / 0x78ffe morphing, 0x7984e
//                       rotation matrices, 0x79808 bounding radius, the maps the fillers read (0x7873a's light
//                       curves, textures, bump tables, env maps)
//   engine.cpp          0x789c4 / 0x79483 drawing: transform 0x79b16 and normals 0x79a17 in float on the GPU, the
//                       object test of 0x79483, the face order of 0x78b08 / 0x7898d (or a depth buffer), the pixel
//                       modes 2, 3, 15, 20, 21 (fillers 0x4e710, 0x51593, 0x4f47c, 0x52e96, 0x54548) as one shader
//                       that computes the colour in float, MSAA, rows 1..197 over the part's background (0x78905),
//                       and 256x256 maps (mode 21: the torso's live reflection)
//
// What the original computes and what is evaluated here (notes-engine section 11):
//   - Vertices are transformed and projected in float with the original's matrices (Rx(a) Ry(b) Rz(c) applied to row
//     vectors, exact sin / cos of fractional 12-bit angles instead of the 1023-amplitude table), the eye at z = +F
//     looking along -z, focal length 1024 / 2^shift pixels of the 320x200 frame, centre (160, 100), no aspect
//     correction (on the 4:3 screen the picture is 1.2 times taller, as in mode 13h). The GPU rasterises with MSAA;
//     texture and env coordinates are interpolated perspective-correctly.
//   - Back faces are culled on the screen like 0x78b08. Faces nearer than z = -F (2F from the eye) are clipped per
//     pixel at that plane instead of being dropped whole (Debug::dropNearFaces drops them as the original does).
//     Objects of a scene list are dropped whole like 0x79483 when their bounding sphere is off screen, reaches
//     z >= -F or the centre is farther than 8000 units: that is composition and stays.
//   - The env ("phong") coordinate of each vertex is e = 128 + R n.xy texels (R = 63.75 for modes 3, 15, 20, 21,
//     127.5 for mode 2; 0x4d4b4 / 0x4d4e7 / 0x4da28 in float, without the truncation), interpolated across the face
//     as the fillers do, but perspective-correct; like the fillers' 8-bit differences a face goes the short way round
//     the map (only mode 2's range can wrap). So the zero normals of the bee's wings are the map's centre and
//     morphed normals between keys are shorter, and on coarse meshes a face's coordinates cut across the map nearer
//     its bright centre than its normals' directions would (Debug::Env::Pixel evaluates the env coordinate per pixel
//     from the interpolated normal instead, for comparison). Normals are rotated by the object's angles plus the env
//     angles only, never by the camera (0x78a35 / 0x79a17).
//   - The light maps of 0x7873a (rings of isqrt radius, 16 or 32 levels) are the light curve read at the
//     continuous radius (interpolated between its 128 entries); the level stays continuous and the colour is
//     interpolated between the palette's level rows. The bump offsets are read filtered; the texture's colour is
//     the palette colour of each texel (texture + level), filtered in true colour (or texels as anti-aliased
//     squares: Shading::sharpTexels). Where the texture is minified the pixel is box-filtered (up to 3 x 3 samples).
//   - Env maps that are pictures are sampled filtered in true colour (palette first, or a level map through a
//     palette ramp), mipmapped.
//
// Usage from a part (positions in object units, angles in 1/4096 turn, n = the part's PIT ticks, fractional):
//
//   // init() (the GL context is current):
//   stars::Object3D ball;  ball.load(*c.exe, 18);                          // a `B!` file (0x78c62)
//   stars::LightRamp ramp; ramp.load(*c.exe, 0x6310);                      // 0x7873a's curve
//   stars::IndexMap mat;   mat.create(c.exe->file(16));                    // texture: values added to the index
//   stars::BumpMap bump;   bump.create(c.exe->file(17), -8, 2);            // (b - 8) * 2 env texels (0x6733)
//   pal15 = vgaPalette(c.exe->fileSpan(15));  pal.set(pal15);              // a Palette and a stars::PaletteTexture
//   // render():
//   c.drawPicture(c.picture(19, {.palette = &pal15}), {.y = 1, .h = 197, .sy = 1, .sh = 197, .mul = fade});
//   stars::Camera cam;  cam.F = 80;  cam.shift = 1;  cam.envA = 33 * n;   // [0x57ab6], [0x5594c], [0x55862]
//   stars::Shading sh;  sh.mode = 15;  sh.palette = &pal;  sh.ramp = &ramp;
//   sh.levelMin = 1;  sh.levelMax = 30;                                    // 0x7873a DL 0: 16 * (c >> 2), 0 -> 8
//   sh.tex[0] = &mat;  sh.bump[0] = &bump;  sh.mul = fade;                 // fade = the palette fade as a colour
//   auto& eng = c.shared<stars::Engine>();
//   eng.begin(c, cam);                                                     // into ctx.out, rows 1..197
//   eng.drawScene({{&ball, stars::Pose{x, y, z, 2 * n, 3 * n, 4 * n}}}, sh); // 0x79483: camera, object test
//   eng.end();                                                             // composited over the background
//
// Direct draws (0x789c4: star, torso, mirror; positions are camera space, the original's x.10 / 1024):
//   eng.begin(c, cam);  eng.draw(mirror, pose, shMirror);  eng.layer();  eng.draw(torso, pose2, shTorso);  eng.end();
// Morphing (files 24, 51): stars::Morph m;  m.load(*c.exe, 0x9fa0, 0x9e80, 33);  obj.setShape(m.at(n));
// The bee ([0x582ac] = 1): World w; w.load(*c.exe, 28); sh.order = stars::Order::File; eng.drawScene(list, sh).
// A 256x256 map (mode 21, the torso's reflection): stars::EngineTarget rt;  rt.create(1024);  per frame:
//   rt.fill(c, c.picture(44, {.palette = &pal42}));  eng.begin(c, rt, cam);  eng.draw(inst, pose, sh21);  eng.end();
//   then shMirror.mode = 2, shMirror.env = rt.env() for the mirror on the screen.
// tools/enginetest/scenes.cpp has test scenes for all five 3D parts that show the calls in context.
//
// Nothing keeps state between frames except what the part sets (shapes, edited vertices): a frame depends only on
// the poses and shading passed. Checked with tools/enginetest: the float transform against an integer port of
// 0x7984e / 0x79d25 / 0x79b16 / 0x4d4b4, and test scenes of the spike ball, the wall, the bee, the star and the torso
// against the capture (tools/compare.py).
#pragma once
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "demo.h"
#include "pictures.h"
#include "render.h"
#include "stars/core.h"

class StarsExe;

namespace stars {

// =============================================================================================== poses, matrices
// A position and three angles as the engine's object struct holds them (+0 x, y, z; +0xc a, b, c). Positions in
// object units: world units for scene lists; camera space for direct draws (the original keeps those as x.10:
// divide by 1024). Angles in 1/4096 turn (the original masks them with 0xfff).
struct Pose {
    double x = 0, y = 0, z = 0;
    double a = 0, b = 0, c = 0;
};

// 0x7984e: the rotation of angles (a, b, c) as the engine stores it (rows; v' = v M for a row vector v), with
// exact sin / cos. As a column-vector matrix it is Rx(a) Ry(b) Rz(c) transposed.
struct Mat3d {
    double m[3][3];
    Vec3 apply(const Vec3& v) const;      // v M (row vector)
    Mat3d operator*(const Mat3d& o) const; // this then o: v (this o)
};
Mat3d engineRotation(double a, double b, double c);

// The camera and projection state of a draw: 0x55836 (position, scene lists only), 0x55842 (angles, scene lists
// only), 0x55862 (env angles: added to every object's angles for the normal rotation, which moves the
// reflections), [0x57ab6] F and [0x5594c] shift. The eye is at camera z = +F looking along -z; screen x =
// 160 + f X / (F - Z), y = 100 + f Y / (F - Z), f = 1024 / 2^shift.
struct Camera {
    Pose pose;
    double envA = 0, envB = 0, envC = 0;
    double F = 100;
    int shift = 1;
    double focal() const { return 1024.0 / double(1 << shift); }
};

// =============================================================================================== objects
// One vertex of a `B!` object: position (int16 in the file), normal (int16, length ~255 in the file; here as
// stored), texture coordinates (bytes: texels of a 256x256 map).
struct EngineVertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 0, nz = 0;
    uint8_t u = 0, v = 0;
};
// One face: +0 type (material / per-face mode: modes 20 / 21 draw type 1 with the plain env map, anything else
// bump-mapped), +2 corner vertex indices. Front faces are clockwise on the screen (y down).
struct EngineFace {
    uint16_t type = 0;
    uint16_t a = 0, b = 0, c = 0;
};

class MorphState;

// A `B!` object (files 18, 24, 45, 46, 50, 51, and every object of a `WLDD`): its working vertex set, its faces,
// and for morphing meshes (byte 3 = 0) the key sets. The part may edit `verts` and the face types (the torso moves
// its ring vertices, 0x17342, and sets the ring's faces to type 1, 0x16f46) and then calls touch(). The GPU copy is
// made on the first draw (needs the GL context).
class Object3D {
public:
    Object3D() = default;
    Object3D(const Object3D&) = delete;
    Object3D& operator=(const Object3D&) = delete;
    Object3D(Object3D&& o) noexcept;
    Object3D& operator=(Object3D&& o) noexcept;
    ~Object3D();

    bool load(const StarsExe& exe, int file);       // overlay file n (0x78c62)
    bool parse(std::span<const uint8_t> bytes);     // a `B!` object starting at bytes[0]
    std::vector<EngineVertex> verts;                // the set the engine draws (the file's working set)
    std::vector<EngineFace> faces;
    std::vector<std::vector<EngineVertex>> sets;    // morph key sets (empty for static objects)
    float radius = 0;                               // 0x79808: largest vertex distance from the origin (exact sqrt)

    // The working set = sets[s.a] + (sets[s.b] - sets[s.a]) * s.w for positions and normals (0x78ffe; u, v kept).
    void setShape(const MorphState& s);
    void touch() { dirty_ = true; }                 // re-upload after editing verts or faces
    void computeRadius();

private:
    friend class Engine;
    void upload() const;
    mutable std::vector<float> data_;   // 3 vertices per face: position, normal, uv (unwrapped), type, 1st normal
    mutable bool dirty_ = true;
    mutable GLuint vao_ = 0, vbo_ = 0;
    mutable int count_ = 0;
};

// The `WLDD` world of file 28 (the bee): 68 objects with their records (+0 x, y, z; +0xc a, b, c; +0x12 the
// object's offset in the file). The bee part appends all of them to its scene list.
struct World {
    std::vector<Object3D> objects;
    std::vector<Pose> poses;     // the records' positions and angles (world units; the angles are 0 in file 28)
    bool load(const StarsExe& exe, int file);
};

// =============================================================================================== morphing
// The shape of a morphing mesh at a moment: sets a and b blended by w (0..1).
class MorphState {
public:
    int a = 0, b = 0;
    float w = 0;
};

// The part's animation record (object +0x16: +8 shift, +9 ticks per key, +0xa the key-set list, -1 terminated)
// with the blend weights it copies to 0x782d0 (0x79841: 128 words) and the number of keys it passes to 0x78fda.
// The original steps key and step once per PIT tick (0x78fda) and blends once per drawn frame (0x78ffe) with
// weight W[step] >> shift; at(ticks) blends continuously: between steps the weight is interpolated between W[k]
// and W[k + 1], and in the last tick of a key from W[last] to the next key (weight 1).
struct Morph {
    std::vector<int> list;          // key-set indices (keys + 1 entries used)
    std::vector<double> weights;    // W[i] / 2^shift
    int stepsPerKey = 64, keys = 1;
    bool load(const StarsExe& exe, uint32_t record, uint32_t weightTable, int keys);
    // The shape after `ticks` calls of 0x78fda from key 0, step 0 (fractional: in between).
    MorphState at(double ticks) const;
    // As the original shows it: whole ticks, W[step] >> shift.
    MorphState atTick(long ticks) const;
};

// =============================================================================================== maps
// 0x7873a's light curve: 128 bytes at a part's address (0x6310 spike ball, 0x9df0 wall, 0x16c20 torso), read at
// 127 - r for the radius r of the env coordinate from the map centre (128, 128).
struct LightRamp {
    Texture tex;                    // 128 x 1, R32F
    std::vector<float> values;
    bool load(const StarsExe& exe, uint32_t addr);
    float at(float r) const;        // continuous: interpolated between entries
};

// A 256x256 map of byte values read texel by texel (the texture of mode 15: palette offsets / grey levels). The
// shader reads 4 texels per pixel and filters the colours.
struct IndexMap {
    Texture tex;                    // R8, nearest, repeat
    bool create(std::span<const uint8_t> bytes, int w = 256, int h = 256);
    bool create(const std::vector<uint8_t>& bytes, int w = 256, int h = 256) {
        return create(std::span<const uint8_t>(bytes.data(), bytes.size()), w, h);
    }
};

// The bump table of modes 15 / 20 / 21: per texel the offsets (du, dv) added to the env coordinate, in env texels:
// du = (B[i] + bias) * scale, dv = the same of the byte after it (the right neighbour; at u = 255 the first byte of
// the next row). Spike ball: file 17, bias -8, scale 2 (0x6733); wall: its texture itself, bias 0, scale 1 (0xa0ca
// points the bump selector at the texture); torso: file 43, bias -4, scale 8 (0x16d86) and pairs = true (modes
// 20 / 21 read the bytes at columns 2u and 2u + 1, u's top bit lost: a 128 x 256 table of pairs). Filtered.
struct BumpMap {
    Texture tex;                    // RG16F, linear, repeat; 256 x 256 or 128 x 256 (pairs)
    bool pairs = false;
    bool create(std::span<const uint8_t> bytes, float bias, float scale, bool pairs = false);
    bool create(const std::vector<uint8_t>& bytes, float bias, float scale, bool pairs = false) {
        return create(std::span<const uint8_t>(bytes.data(), bytes.size()), bias, scale, pairs);
    }
};

// An environment map for modes 2 and 3 (and the type-1 faces of modes 20 / 21). Either
//   - a true-colour picture (rgb: an RGBA texture, opaque; GL_REPEAT, mipmapped): makeRgb() from indices through
//     a palette (the star's file 49), a redraw texture, or an EngineTarget (flipV), or
//   - a level map (level: R8, value 0..255) whose value v gives the palette index base + scale * v, looked up
//     continuously in Shading::palette (the bee: file 29 through the gold ramp, base 0, scale 1; the ring: file 29
//     through the red ramp, base 0x60, scale 0.5).
struct EnvMap {
    GLuint rgb = 0;
    bool flipV = false;             // texture row 0 at the bottom (render targets)
    GLuint level = 0;
    float base = 0, scale = 1;
};
// Owns the texture of an EnvMap made from 256x256 bytes (missing bytes: 0).
struct EnvTexture {
    Texture tex;
    bool makeRgb(std::span<const uint8_t> indices, const Palette& pal);   // palette applied, then filtered
    bool makeLevel(std::span<const uint8_t> values);                      // values, filtered
    EnvMap rgb() const { EnvMap e; e.rgb = tex.id; return e; }
    EnvMap level(float base, float scale) const { EnvMap e; e.level = tex.id; e.base = base; e.scale = scale; return e; }
};

// =============================================================================================== shading
// How the pixels of a draw are coloured: the original's mode [0x55948] and what its filler reads. The colour is
// computed in float; `palette` is the DAC palette (rebuild it per frame for crossfades), mul / add an affine
// per-channel map applied last (fades: mul = level; fade from white: mul = L, add = 1 - L; the star's levels).
//   mode 2, 3     env: colour = env(e), e = 128 + R n.xy (R 127.5 / 63.75), mode 2 wrapping (repeat)
//   mode 15       texture + bump-offset light (0x51593): index = base + N L + T(t), L = levelScale * ramp(|e + B(t)
//                 - 128|) clamped to [levelMin, levelMax], T = tex[0] (crossfaded with tex[1] by texMix)
//   mode 20, 21   faces of type 1: env(e) (the ring); other faces: index = base + N L with the bump offsets (pairs);
//                 21 is the same into a 256x256 map (EngineTarget)
// Colour of an index base + N L + m with fractional L (and m): interpolated between the palette entries.
// The band (torso 0x17459): light-map rows bandV0 <= e.v + dv < bandV1 add bandAdd to the index.
//
// The order of the faces (order):
//   Depth   a depth buffer (default): the geometrically right picture. Separate draws are still layered with
//           Engine::layer() where the original's draw order matters.
//   Sorted  the original's painter's order: front faces sorted far to near by 0x78b08's key (z_a + z_b) / 4 +
//           z_c / 2 with the quicksort 0x7898d, drawn over everything drawn before; drawScene orders its objects by
//           their centres like 0x79483.
//   File    faces in file order ([0x582ac] = 1: the bee), objects by their centres. The bee's objects overlap
//           themselves (layered shells): only this order gives the original's look.
enum class Order { Depth, Sorted, File };

struct Shading {
    int mode = 3;
    Order order = Order::Depth;
    const PaletteTexture* palette = nullptr;
    EnvMap env;
    // light (modes 15, 20, 21)
    const LightRamp* ramp = nullptr;
    float levelScale = 0.5f;         // 0x7873a: (c >> 2) << 4 = 16 levels of 2 (DL 0), (c >> 1) << 3 (DL 4), c >> 1 (DL 2)
    float levelMin = 0, levelMax = 31;
    float base = 0, N = 8;            // index = base + N * level + texel
    const IndexMap* tex[2] = {nullptr, nullptr};
    float texMix = 0;
    const BumpMap* bump[2] = {nullptr, nullptr};
    float bumpMix = 0;
    float bandV0 = 0, bandV1 = 0, bandAdd = 0;
    // The texture's and bump table's texels: false = bilinear (the colours of the four texels around the point,
    // each computed as the original does, filtered); true = squares with anti-aliased edges (the pixel-art look).
    bool sharpTexels = false;
    Vec3 mul = Vec3(1, 1, 1), add = Vec3(0, 0, 0);
};

// =============================================================================================== targets and views
// The part of the target the engine draws: the VGA rectangle the target covers and the rectangle it may write.
struct View {
    float x = 0, y = 0, w = 320, h = 200;               // VGA coordinates the target covers
    float clipX = 0, clipY = 1, clipW = 320, clipH = 197; // where the engine draws (VGA coordinates)
    static View screen() { return View{}; }             // the 3D parts' frames: rows 1..197 (0x78905)
    static View fullScreen() { return View{0, 0, 320, 200, 0, 0, 320, 200}; }
    static View map256() { return View{0, 0, 256, 256, 0, 0, 256, 255}; }   // mode 21's buffer: x < 256, y < 255
};

// A 256x256 map rendered by the engine (the torso's reflection, mode 21): the original's buffer at size x size
// texels (texel (x, y) of the original = VGA pixel (x, y) of the 320x200 projection), RGBA16F, mipmapped, repeat.
// Stored with GL's orientation (row 0 of the original at the top: env() sets flipV).
class EngineTarget {
public:
    EngineTarget() = default;
    EngineTarget(const EngineTarget&) = delete;
    EngineTarget& operator=(const EngineTarget&) = delete;
    ~EngineTarget();
    void create(int size);
    void destroy();
    int size() const { return n_; }
    GLuint texture() const { return tex_; }
    // Binds the target (viewport = all of it): ctx.drawPicture with the default rectangle then fills the map.
    void bind() const;
    // The base picture (the torso: file 44 through palette 42): draws p over the whole map, filtered (bilinear, the
    // original's pixels too: it is an env map), colours times mul.
    void fill(Ctx& c, const Picture& p, Vec3 mul = Vec3(1, 1, 1));
    EnvMap env() const { EnvMap e; e.rgb = tex_; e.flipV = true; return e; }

private:
    friend class Engine;
    GLuint fbo_ = 0, tex_ = 0;
    int n_ = 0;
};

// =============================================================================================== the renderer
struct SceneObject {
    const Object3D* object = nullptr;
    Pose pose;
};

class Engine {
public:
    Engine();
    explicit Engine(Ctx&) : Engine() {}   // for ctx.shared<stars::Engine>()
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    // Start a frame: the objects drawn until end() share one depth buffer and are composited over what `out`
    // holds (the part's background), inside the view's clip rectangle. begin(c, cam) = ctx.out, View::screen().
    void begin(Target& out, const View& view, const Camera& cam);
    void begin(Ctx& c, const Camera& cam, const View& view = View::screen()) { begin(*c.out, view, cam); }
    // Into a 256x256 map (View::map256()). end() regenerates its mipmaps and rebinds c.out.
    void begin(Ctx& c, EngineTarget& t, const Camera& cam);

    // 0x789c4 for one object of a scene list (0x79483): pose in world units, seen by the camera (its position and
    // angles); dropped whole when it fails 0x79483's test (visible()). drawScene draws a list.
    void drawInScene(const Object3D& o, const Pose& pose, const Shading& sh);
    void drawScene(std::initializer_list<SceneObject> list, const Shading& sh);
    void drawScene(std::span<const SceneObject> list, const Shading& sh);
    // 0x789c4 called directly ([0x582cd] = 0): pose in camera space (the original's x.10 / 1024), no camera
    // matrix, no object culling.
    void draw(const Object3D& o, const Pose& pose, const Shading& sh);
    // Later draws cover earlier ones whatever their depth (the original's draw order between separate draws:
    // the torso is drawn over the mirror).
    void layer();
    void end();

    // 0x79483's object test: the bounding sphere's projected square on the 320x200 screen, the whole sphere
    // beyond z = -F, the centre nearer than 8000 units.
    bool visible(const Object3D& o, const Pose& pose) const;
    // Camera-space position of a scene-list point (world units), and its VGA position (false behind the eye).
    Vec3 toCamera(const Vec3& world) const;
    bool toScreen(const Vec3& cameraSpace, float* x, float* y) const;

    int samples = 4;   // MSAA samples (clamped to GL_MAX_SAMPLES)
    struct Debug {
        bool dropNearFaces = false;   // drop faces whose depth key (z_a + z_b) / 4 + z_c / 2 >= -F (0x78b08)
        bool noObjectCull = false;    // draw scene objects that 0x79483 drops
        // How the env coordinate is interpolated across a face: Vertex = the vertices' coordinates (0x4d4b4 /
        // 0x4d4e7 / 0x4da28), perspective-correct (default); Affine = the same, screen-linear as the fillers step
        // them; Pixel = from the interpolated normal per pixel (direction renormalised, length interpolated)
        enum class Env { Vertex, Affine, Pixel } env = Env::Vertex;
    } debug;

private:
    struct Buffers;
    void beginOn(GLuint fbo, int w, int h, const View& view, const Camera& cam);
    void drawWith(const Object3D& o, const Mat3d& rot, const Vec3& trans, const Pose& pose, const Shading& sh);
    void drawFaces(const Object3D& o, const Mat3d& rot, const Vec3& trans, Order order);
    void setShading(const Shading& sh);
    Buffers& buffers(int w, int h);

    Shader shader_, composite_;
    Texture white_, zero_;
    std::vector<Buffers*> buffers_;
    // frame state between begin and end
    Buffers* cur_ = nullptr;
    GLuint outFbo_ = 0;
    int outW_ = 0, outH_ = 0;
    View view_;
    Camera cam_;
    Mat3d camRot_{};
    EngineTarget* target_ = nullptr;
    Target* restore_ = nullptr;
    GLuint dropVao_ = 0, dropVbo_ = 0;
};

}  // namespace stars

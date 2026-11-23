// LCA's 3D engine of REVE.EXE (reve/re/notes-engine.md), shared by the 3D entries (scenes credits, p3d1, part2, p3d3,
// part4, fin; reve/re/notes-parts3d.md).
//
// Ports the world loader 0x21d440 / 0x21d538 / 0x21d9ac (3D\*.WLD: objects, faces, vertices, the face and vertex
// normals and the bounding boxes, computed in the original's integers), the animation 0x25f3ae (3D\*.ANI: key 0 and
// per-refresh 16.16 deltas of the camera and the objects; here as continuous motion: the integer state at whole
// steps from prefix sums, blended by the fraction of a refresh), and the render 0x25f5b5 as the original orders it:
// the camera 0x21db9c, per object the setup and cull 0x21ddf8 / 0x21dfb8 (far cull, bounding-box outcodes), the
// object sort, the face sort by the camera depth of each face's first vertex (0x21e9bc, 0x21e6ec), the lighting
// point (0x21e9bc: the camera in object space through the rotation of the negated angles), the lighting 0x21e72c /
// 0x21e7c8 (as a continuous level) and PART4's bounding boxes 0x25f26e.
//
// The GL side (engine_gl.cpp) draws a frame of this in the original's painter's order (objects back to front, each
// object's faces back to front, no depth test: the order is visible, notes-parts3d.md), into a multisampled target:
//   type 6  texture:            the tile's colours (TEXTURE.RAW through its palette), filtered and mip-mapped in colour
//   type 8  texture + Gouraud:  GOURAUD.PAL's colours of the tile at the two levels around the continuous light level,
//                               each filtered in colour, blended by the level's fraction
//   type 1  flat, lit per face: the palette's ramp colour + level, blended between the two neighbouring entries
// Evaluated exactly instead of the original's shortcuts: float transforms and projection, perspective-correct
// texture coordinates, the light level per vertex as the continuous formula of 0x21e7c8 (15 - 16 cos + |d| / 1024),
// interpolated perspective-correctly, MSAA edges, and the near plane z = 128 clipped with interpolated attributes
// (the original's clip gives new vertices u = v = 0 and level 0: dropped). Kept: the original's normals (a vertex
// normal sums only the faces that list the vertex first), flat faces lit at the object's origin, the lighting point
// through the negated angles, the unsigned 16-bit texture coordinates (u / 256 texels, 64-texel repeat in the tile),
// the far cull and the box culls, the painter's order with the original's keys.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render.h"

struct Ctx;
struct Target;
class ReveExe;

namespace reve3d {

class Original;   // REVE_ENGINE=original (below)

// ---------------------------------------------------------------------------------------------- the world (WLD)
struct Vertex {
    int16_t x = 0, y = 0, z = 0;    // object space
    uint16_t u = 0, v = 0;          // 8.8 texels, read unsigned as the rasterizers do
    int16_t n[3] = {0, 0, 0};       // vertex normal, length 32767 (0: no face lists the vertex first)
};
struct Face {
    uint8_t type = 0, colour = 0, tex = 0;
    uint16_t v[3] = {0, 0, 0};      // vertex indices a, b, c
    int16_t n[3] = {0, 0, 0};       // face normal, length 32767
};
struct Object {
    int32_t pos[3] = {0, 0, 0};     // the file's position (objects that key 0 does not place keep it)
    std::vector<Vertex> verts;
    std::vector<Face> faces;
    int16_t box[8][3] = {};         // bounding-box corners (0x21d9ac): x max if k >= 4, y max if k & 2, z max if k & 1
};
struct World {
    std::vector<Object> objects;
    // 0x21d440 / 0x21d538 / 0x21d9ac. False with a message on a malformed file.
    bool load(std::span<const uint8_t> wld, std::string* err);
};

// ---------------------------------------------------------------------------------------------- the animation (ANI)
// The state the render reads: camera x, y, z (world units), yaw, pitch, roll (8192 a turn); per object x, y, z and
// its three angles (8192 a turn; angle 0 about x, 1 about y, 2 about z: R = Ry(a1) Rz(a2) Rx(a0)).
struct State {
    double cam[6] = {};
    std::vector<std::array<double, 6>> obj;
};

class Anim {
public:
    bool load(std::span<const uint8_t> ani, const World& w, std::string* err);
    // Refreshes per loop: refreshes per key x keys + 1 (the step that re-reads key 0).
    long period() const { return long(per_) * nkeys_ + 1; }
    // The state after n steps (n >= 0, fractional: blended between the whole steps around it, except across the
    // step that re-reads key 0, which the original takes at once). n counts from key 0 (the init's call).
    State at(double n) const;
    // The original's integers after n whole steps (modulo the period): camera [0x217474..] (the integer parts),
    // per object its position x 65536 (+0x1c..) and angles x 65536 (+0x10..).
    void intState(long n, int32_t cam[6], std::vector<std::array<int32_t, 6>>& obj) const;

private:
    struct Key {
        int32_t cam[6] = {};
        std::vector<std::pair<int, std::array<int32_t, 6>>> objs;
    };
    // integer state: camera as 32.32 fixed point, objects' positions and angles x 65536
    struct IState {
        int64_t cam[6] = {};
        std::vector<std::array<int64_t, 6>> obj;
    };
    IState exact(long m) const;   // after m steps of a loop, 0 <= m < period()
    State toState(const IState& s) const;
    int per_ = 1, nkeys_ = 0;
    std::vector<Key> keys_;       // keys_[0] = key 0, keys_[k] = delta key k
    std::vector<IState> atKey_;   // the state after k whole delta keys (k = 0..nkeys)
};

// ---------------------------------------------------------------------------------------------- one frame's setup
struct M3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    M3 operator*(const M3& b) const;
    std::array<double, 3> operator*(const std::array<double, 3>& v) const;
};
using V3 = std::array<double, 3>;

// The engine's constants, read from the code by address (notes-engine.md).
struct Constants {
    double nearZ = 128;        // 0x21e5c5: cmp ecx, 0x80 (a vertex is in front when its camera z > 128)
    double cx = 160, cy = 100; // 0x21e5dd: add eax, 0xa0 / 0x21e5fa: add eax, 0x64 (the projection centre)
    double focal = 256;        // 0x21e5d6: sar eax, 8 of x << 16 (x * 256 / z)
    double farZ = 14000;       // 0x21de0b: cmp [edi + 0x30], 0x1b580000 (P.z, 1.15 fixed point: 0x1b580000 / 32768)
    double flatDist = 512, vertexDist = 1024;   // 0x21e799: shr ebp, 0x12 / 0x21e837: shr ebp, 0x14 (sqrt of |d|^2)
    int boxColour = 0x80;      // 0x21d402: mov byte [ebx], 0x80 (PART4's box lines)
    void read(const ReveExe& e);
};

struct ObjectView {
    int index = 0;
    bool drawn = false;
    M3 rot;                   // object space -> camera space: M R
    V3 trans{};               // M (pos - camera)
    V3 light{};               // the lighting point in object space
    std::vector<uint32_t> order;   // the object's faces, back to front (the original's keys)
    bool box = false;         // PART4: the box is drawn (its corner 7 is in front)
    std::array<V3, 8> boxCam{};    // the rotated box's corners in camera space
};
struct Frame {
    M3 camera;                // rows: right, up, forward
    V3 eye{};
    std::vector<ObjectView> objects;   // in file order
    std::vector<int> drawOrder;        // the drawn objects, back to front
};

// The render's CPU part for state s: culling (part4: 0x21dfb8, else 0x21ddf8), the matrices, the lighting points,
// the object and face orders, PART4's boxes.
Frame setupFrame(const World& w, const State& s, const Constants& k, bool part4);

// ---------------------------------------------------------------------------------------------- GL (engine_gl.cpp)
// Textures and shaders shared by the 3D scenes (one per context: c.shared<Gpu>()).
// Two colour sets: [0] each index's palette colour; [1] for the entries that run the AA pass 0x2045fa: the colour the
// pass gives an area of that index, T[T[a][a]][T[a][a]] of GFX\ANTIALIA.PAL. The table does not map every colour to
// itself (beige 81 -> 2 -> 1, white; 82 -> 4 -> 3; 85 -> 40, ...): in the original the pass shifts those colours
// wherever the picture is flat (most of PART4's letters are white, not beige); the remake draws with the shifted
// colours and blurs them in colour.
struct Gpu {
    explicit Gpu(Ctx& c);
    ~Gpu();
    GLuint plain[2] = {}, shade[2] = {};   // texture arrays: 16 tiles; 16 levels x 16 tiles (layer level * 16 + tile)
    Texture palette[2];                    // 256 x 1: the colour of each index
    std::array<Vec3, 256> pal[2];
    // The 4x upscale of TEXTURE.RAW (art/tex_texture.jpg, c.pictures->detail): 16 tiles of 256 x 256 (0: none). The
    // colour stays computed from the indices; the upscale adds its detail: colour + g x (upscale - the original's
    // texels through the palette, plain[0], with the same filter), g = the least-squares gain of the colour set's
    // GOURAUD.PAL row (index 0..15) or of the plain colours (16) over the palette colours, per channel.
    GLuint detail = 0;
    std::array<Vec3, 17> gain[2];
    Shader scene, line, aa, box, weighted;
    Constants k;
    // the multisampled target (the size of the frame)
    GLuint msFbo = 0, msColor = 0;
    int msW = 0, msH = 0, samples = 0, maxSamples = -1;
    bool bindMs(int w, int h);       // binds the multisampled target (false: none available, draw directly)
};

// The format of the targets Scene3D::render draws into (the multisampled one has the same): the colours are the
// palette's 8-bit colours, blended.
constexpr GLenum kTargetFormat = GL_RGBA8;

// A world on the GPU with its animation: what each 3D scene holds.
class Scene3D {
public:
    // Loads the WLD whose name is at `wldName` in the image and its ANI (the name with the extension the inits write,
    // 0x204918: ".ANI"). part4: PART4's cull and boxes.
    // aaColours: the entry runs the AA pass (colour set [1] of Gpu).
    void init(Ctx& c, uint32_t wldName, bool aaColours, bool part4 = false);
    // Draws the state after n steps into `t` (c.temp(false, 1, kTargetFormat): the size of the frame), cleared to index
    // 0's colour first: multisampled (unless msaa is false), resolved into t.
    void render(Ctx& c, double n, Target& t, bool msaa = true);
    const World& world() const { return world_; }
    const Anim& anim() const { return anim_; }
    // REVE_ENGINE=original: the original's frame of the state after floor(n) steps (n >= 0) into buf (cleared here).
    void renderOriginal(Ctx& c, double n, uint8_t* buf);
    Original& original(Ctx& c);

private:
    void upload();
    void drawBoxes(Ctx& c, const Frame& f);
    World world_;
    Anim anim_;
    bool part4_ = false;
    int set_ = 0;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0, lineVao_ = 0, lineVbo_ = 0;
    std::shared_ptr<Original> orig_;
    std::vector<uint32_t> first_;    // per object: its first vertex in the vertex buffer (3 per face)
    std::vector<uint32_t> idx_;
};

// ---------------------------------------------------------------------------------------------- REVE_ENGINE=original
// original.cpp: the original's integer engine, drawing the 320 x 200 indexed frame exactly as REVE.EXE does (a port of
// reve/re/models/engine/engine.py), for pixel checks against the capture: with REVE_ENGINE=original the 3D scenes
// show it (and the entries' table passes: the AA pass with GFX\ANTIALIA.PAL, the credits' indices, FIN's blend) at
// --shotsize 320x200. The culls' history (the outcode bytes, the boxes' corner positions) is kept per scene across the
// frames rendered, as the original keeps it across the frames it draws.
bool originalEngine();
class Original {
public:
    Original();
    ~Original();
    void init(const ReveExe& e, const World& w);
    // The state after n whole steps, drawn into buf (320 x 200 indices, cleared by the caller).
    void render(const Anim& a, long n, bool part4, uint8_t* buf);
    void aa(const uint8_t* src, uint8_t* dst) const;       // 0x2045fa (dst: rows 1..198, columns 4..315)
    void blend(const uint8_t* src, uint8_t* screen) const; // 0x2046c7: screen = T[src][screen]
    void show(Ctx& c, const uint8_t* idx);                 // draws the indices through the palette, pixel for pixel
    std::array<Vec3, 256> pal{};

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    Texture screenTex_;
};

// The AA pass 0x2045fa in colour: (up + down + centre + left) / 4 of src, the taps one original pixel apart and each
// tap a whole pixel of the original (the box average over it; at 320 x 200 the original's kernel exactly; taps of
// single output pixels would make four shifted copies of every sharp edge), into c.out, the whole frame (the
// original's unwritten black border is dropped).
void aaPass(Ctx& c, const Target& src);
// Adds src x weight to the bound target (FIN's trail).
void addWeighted(Ctx& c, const Target& src, float weight);

}  // namespace reve3d

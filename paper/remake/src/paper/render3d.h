// The original's 3D renderer on the GPU: flat-shaded, palette-coloured faces with pencil outlines.
//
// Ports (paper/re/notes-init.md §7.5-7.6, notes-sketch.md §6.3-6.4, notes-scene3d.md §4.3-4.4, notes-planes.md §6.3-6.4):
//   0x3fb3  per object: camera matrix C = Cam * O, light vector in object space (normalised, |L| = 200), vertices
//           to camera space, projection (160 + 224 x/z, 100 + 224 y/z) when z > 400, faces hidden when a vertex has
//           z <= 400 or the screen winding is not clockwise (y down), colour = base + ((n . L) >> 12) (-13..12 on
//           the palette ramp), in mode 2 the faces with add 0x20 become paper (0x3a); silhouette bit k when the
//           neighbour across edge k is hidden; depth bucket ((z0+z1+z2) - 1200) >> shift (shift = byte 0x42ac: 6,
//           the teal part patches 3)
//   0x43df  buckets far to near, LIFO: fill each face (0x1a87/0x1b10), then unless mode 1 its edges that are
//           (silhouette | static outline) [& the sketch reveal mask in mode 2] with the pencil line 0x18b7; in mode 2
//           also the stroke in progress of the face [0x5047]. Mode = byte [0x1ea94]: 0 colour (scene, planes),
//           1 faces only (the pencil), 2 sketch.
//
// Remake:
//   - exact float projection (focal 224 VGA px at (160, 100), the 320x200 anisotropy kept), vertices and faces at
//     sub-pixel positions, culling as the original (any vertex z <= 400 drops the face; screen winding test);
//   - lighting continuous: ramp position base + 12.5 cos(theta) - 0.5 (the -0.5 keeps the mean of the original's
//     floor), the colour interpolated inside the palette ramp of the base colour (clamped to it: the original's
//     rare 8-bit wrap into the neighbouring ramp at -13 is dropped);
//   - a depth buffer instead of the bucket sort *within* a group; the original's group order is kept exactly: each
//     group() is painted over the previous groups (depth cleared between groups), which is what makes the
//     door/window decals cover the house, the flower and the banner cover everything (scene3d's group lists).
//     Within a group the bucket order only differs from a depth buffer where it is wrong, so the bucket shift
//     (0x42ac) has no visible effect here; transformExact/paintersOrder below give the original's order when the
//     exact face list matters (the sketch's stroke numbering 0x5e1b);
//   - edges: the pencil line of pencil.h, drawn after the group's faces with the depth test against the face that
//     draws them (its plane extended past the edge, never behind the edge), so nearer faces of the group hide them
//     as the painter's algorithm does; crease edges are drawn by both faces (darker), as in the original;
//   - antialiasing: the group is rendered into a multisampled target (4x, colour + darkness + depth) and resolved;
//     the faces are composited over the frame by their coverage, the outline darkness goes into a PencilInk layer.
//
// Use per frame (all VGA / world units as the original):
//   PencilInk ink(ctx, frame);                  // background lines (horizon) into ink: covered by the faces
//   Renderer3D& r = ctx.shared<Renderer3D>();
//   r.begin(ctx, frame, view);  r.group({&sun, &path});  r.group({...});  r.end(&ink);
//   ink.addTexture(paper...); ink.line(handwriting...);  ink.resolve(ctx, frame, &palette);
#pragma once
#include <initializer_list>
#include <utility>
#include <vector>

#include "obj3d.h"
#include "pencil.h"

namespace paper {

enum class Mode3D : int { Colour = 0, Pencil = 1, Sketch = 2 };  // [0x1ea94]

struct Camera3D {
    Mat3d rot;   // world -> camera; rows = camera right, down, forward (the original's [0x1e468])
    Vec3d pos;   // [0x1ea9d]
    // The scene camera: 0xb1f(yaw, pitch) at a position (0x5eec: position = spline value >> 10).
    static Camera3D yawPitch(double yaw, double pitch, const Vec3d& pos) { return {rotYawPitch(yaw, pitch), pos}; }
    // VGA position of a world point (x 160 + 224 X/Z, y 100 + 224 Y/Z); false when Z <= 400.
    bool project(const Vec3d& world, Vec2f& vga, double* z = nullptr) const;
};

struct View3D {
    Camera3D cam;
    Vec3d light{-1300, 100, -2500};       // the light point in world coordinates (Objects3D::light, 0x1e48c)
    Mode3D mode = Mode3D::Colour;
    const ShadePalette* palette = nullptr;  // face colours (default palette when null)
    bool exactShade = false;              // the original's integer light levels (needs integer normals): checks only
};

// A pencil line drawn as part of a face (0x43df mode 2: the stroke in progress, pen -> start of the stroke): only
// when that face is drawn, and hidden by the faces in front of it.
struct FaceLine {
    const Object3D* obj = nullptr;
    int face = 0;
    PencilLine line;
};

class Renderer3D {
public:
    static constexpr double kNear = 400.0;     // 0x1900000: a face with a vertex at z <= 400 is not drawn
    static constexpr double kFocal = 224.0;    // 0xe00000 / z = 224 * 65536 / z

    explicit Renderer3D(Ctx& c);
    ~Renderer3D();
    // Start a 3D pass whose result goes onto `frame` at end().
    void begin(Ctx& c, Target& frame, const View3D& view);
    void setView(const View3D& view) { view_ = view; }   // between groups (e.g. another mode)
    // One painter's group (0x5dfc: transform the objects, then draw their faces + edges with depth). Painted over
    // the previous groups of this pass.
    void group(std::initializer_list<const Object3D*> objs, const std::vector<FaceLine>& lines = {});
    void group(const std::vector<const Object3D*>& objs, const std::vector<FaceLine>& lines = {});
    // Composite: faces over the frame (by coverage); outline darkness into `ink` (background darkness already in
    // ink is covered by the faces). Without ink, the outlines are applied to the frame right away.
    void end(PencilInk* ink = nullptr);

    int samples() const { return samples_; }

private:
    struct Msaa;
    struct FaceVert { float x, y, z, r, g, b; };
    void emitObject(const Object3D& o, const std::vector<FaceLine>& lines);
    Msaa& msaaFor(int w, int h);

    Ctx* c_ = nullptr;
    Target* frame_ = nullptr;
    View3D view_;
    Msaa* cur_ = nullptr;
    std::vector<Msaa*> cache_;
    int samples_ = 4;
    Shader faceShader_, compositeShader_, mergeShader_;
    GLuint vao_ = 0, vbo_ = 0;
    std::vector<FaceVert> faces_;
    std::vector<PencilPass> passes_;
    // per object scratch
    std::vector<Vec3d> cam_;
    std::vector<Vec2f> scr_;
    std::vector<uint8_t> vis_;
};

// ---------------------------------------------------------------- the exact integer pipeline
// Integer port of 0x3fb3 for one object, without drawing: the screen positions the original computes (1/32 px), the
// visibility, flags, colour and depth bucket of every face. For logic that depends on the original's integer
// results: the sketch's stroke timing (pen steps between integer endpoints) and its face numbering (0x5e1b).
struct ExactCamera {
    IMat3 rot;            // [0x1e468]
    IVec3 pos;            // [0x1ea9d]
    IVec3 light{-1300, 100, -2500};
    int mode = 0;         // [0x1ea94]
    int bucketShift = 6;  // byte at 0x42ac
};
struct ExactFrame {
    std::vector<int32_t> cz, sx, sy;   // per vertex: camera z (16.16), screen x, y in 1/32 px (valid when cz > 400.0)
    std::vector<uint8_t> flags;        // per face: +0x1d (bit 7 hidden; bits 0..2 silhouette; 3..5 static outline)
    std::vector<uint8_t> colour;       // per face: +0x1f (visible faces)
    std::vector<int> bucket;           // per face: depth bucket, -1 when hidden
    IVec3 lightObj;                    // +0x60
};
// Uses o.ivert, o.face[].n (integer normals) and the integer pose (rot, pos) given here.
void transformExact(const Object3D& o, const IMat3& rot, const IVec3& pos, const ExactCamera& cam, ExactFrame& out);
// 0x43df's drawing order for one group of transformed objects (in transform order): (object, face) pairs, buckets
// 1023 -> 0, the faces of a bucket last-pushed first.
std::vector<std::pair<int, int>> paintersOrder(const std::vector<const ExactFrame*>& group);

}  // namespace paper

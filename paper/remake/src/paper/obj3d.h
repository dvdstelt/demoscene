// The 3D objects of the original, loaded from PAPER.EXE, and their static set-up.
//
// Ports (paper/re/notes-init.md §7.1-§7.4, notes-sketch.md §6.1, notes-scene3d.md §3, §8.1, notes-planes.md §6.1-6.2):
//   0x3cb7  object from a table entry (word format): the 11 scene objects of main's list (table 0x174a3, 12 bytes per
//           entry: u8 nv, u8 nf, i16 ox, oy, oz, u32 data; data = nv x (i16 x, y, z) + offset, nf x (u8 a, b, c, attr));
//           base colour = byte[0x3db8 + (attr >> 4)] - 4, sketch-mode add 0x20, outline flags attr & 7
//   0x39a4  object from compact bytes (u8 nv, u8 nf, nv x (i8 x, y, z) * 8, nf x 4 bytes); base colour =
//           byte[0x3a89 + (attr >> 4)] + al, add 0: the pencil 0x1e353 (al 0), the planes 0x1878b (al 0x1e, flock) and
//           0x187d5 (al 0x20, teal)
//   0x3a8c  the banner cloth: 17 x 11 vertex grid, 320 faces (v, v+1, v+18), (v+18, v+17, v), colour 0x30, add 0,
//           the grid border edges flagged as outlines. Its vertex positions here are the set-up shape (0x3c3e with
//           the all-zero noise table of main's set-up); the per-frame shape (0x3c3e / 0x3b69) is the scene part's:
//           it calls Object3D::setVertices every frame.
//   0x3dc0  face normals: n = (V1 - V0) x (V1 - V2), integer, scaled to length 256 (truncating)
//   0x3eeb  0x3dc0, then per face edge k (v[k] -> v[k+1]) the neighbour face 0x3f37 (first other face in list order that
//           has the reversed edge, else the face itself) and the crease bit (neighbour found and n . n' <= 10000)
//
// Coordinates are the original's world units: x right, y DOWN, z away from the scene's start camera.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "math3d.h"

class PaperExe;
struct Ctx;

namespace paper {

struct Face3D {
    uint16_t v[3] = {0, 0, 0};    // vertex indices                                     (original +0x00/+0x04/+0x08)
    uint16_t nb[3] = {0, 0, 0};   // neighbour across edge k = v[k] -> v[(k+1)%3]; own index if none (+0x0c..+0x14)
    uint8_t base = 0;             // base colour index (palette ramp position + light)  (+0x1e)
    uint8_t add = 0;              // sketch-mode colour add: 0x20 = drawn as paper in mode 2 (+0x1c)
    uint8_t outline = 0;          // static outline bit k per edge: data flag | crease  (+0x1d bits 3..5, >> 3)
    uint8_t sketch = 0;           // mode 2 reveal mask, bit k per edge (the sketch's completed strokes, +0x20 & 7)
    int32_t n[3] = {0, 0, 0};     // integer normal, length ~256 (0x3dc0), of the integer vertices
};

struct Object3D {
    std::string name;
    std::vector<IVec3> ivert;     // integer object-space vertices (what the original's loaders produce)
    std::vector<Vec3d> vert;      // vertices used for rendering (= ivert unless set with setVertices)
    std::vector<Face3D> face;
    std::vector<Vec3d> normal;    // unit face normals of `vert` (lighting), see updateNormals
    // Pose, object -> world: world = rot * v + pos (the original's +0x00 matrix and +0x48 position).
    Mat3d rot;
    Vec3d pos;
    // Object-space direction towards the light, used instead of normalize(rot^T (light - pos)) when set
    // (for the teal part's int32 overflow of that product, notes-planes §6.3; see imulTVecShr16).
    bool lightOverride = false;
    Vec3d light;

    // Per-frame shapes (banner): replace the vertices. The float version recomputes the float normals; the integer
    // version also replaces ivert and (if intNormals) recomputes the integer normals as 0x3dc0 does every frame.
    void setVertices(const std::vector<Vec3d>& v);
    void setVertices(const std::vector<IVec3>& v, bool intNormals = true);
    void updateNormals();        // float normals of vert
    void computeIntNormals();    // 0x3dc0 on ivert
    void setupEdges();           // 0x3eeb on ivert: integer normals, neighbours, crease bits (into Face3D::outline)
    void setSketchAll(uint8_t mask = 7);  // set every face's sketch mask
};

// ---- loaders (static data read from the exe by code32 address)
Object3D loadTableObject(const PaperExe& exe, uint32_t entry);                          // 0x3cb7 + 0x3eeb
Object3D loadCompactObject(const PaperExe& exe, uint32_t data, uint8_t colourOffset);  // 0x39a4 + 0x3eeb
Object3D makeBanner();                                                                  // 0x3a8c (+ set-up shape)

// ---- every object of the intro, loaded once per context: ctx.shared<Objects3D>()
// Parts copy the objects they animate (Object3D is a plain value type) instead of changing these.
struct Objects3D {
    // main's object list 0x5db8 after set-up: 0 sun, 1 path, 2 house, 3 left pole, 4 right pole, 5..7 fir trees,
    // 8 banner (0x3a8c; the third pole that 0x3cb7 built for this slot is never drawn), 9 house windows + door,
    // 10 flower, 11 pencil.
    enum Slot { Sun, Path, House, PoleL, PoleR, Tree5, Tree6, Tree7, Banner, Windows, Flower, Pencil, kSlots };
    static constexpr uint32_t kTable = 0x174a3;     // 0x3cb7 table, 12 bytes per entry
    static constexpr uint32_t kPencilData = 0x1e353;
    static constexpr uint32_t kFlockPlaneData = 0x1878b;
    static constexpr uint32_t kTealPlaneData = 0x187d5;
    static constexpr uint8_t kFlockPlaneColour = 0x1e;  // al of the flock part's 0x39a4 call
    static constexpr uint8_t kTealPlaneColour = 0x20;   // al of the teal part's 0x39a4 call

    Object3D scene[kSlots];
    Object3D thirdPole;       // table entry 8 (0x17503): built by main, then its slot is taken by the banner
    Object3D tealPlane;       // 0x187d5, 56 vertices / 108 faces (two-sided, folded)
    Object3D flockPlane;      // 0x1878b, 8 vertices / 12 faces (two-sided dart)
    Vec3d light;              // the light point (0x1e48c, (-1300, 100, -2500)), world
    IVec3 ilight;

    explicit Objects3D(Ctx& c);
    explicit Objects3D(const PaperExe& exe);
};

// The banner's sag per grid column (0x3c3e: 0x2ad0(0, 90, 0, n = 17), 18 values, the first 17 used; +y = down):
// 0, 19, 37, 52, 64, 74, 82, 87, 89, 89, 87, 82, 75, 65, 53, 38, 21.
std::vector<int32_t> bannerSag();
// The banner vertex index of grid column c (0..16) and row r (0..10): r * 17 + c.
constexpr int bannerVertex(int col, int row) { return row * 17 + col; }

}  // namespace paper

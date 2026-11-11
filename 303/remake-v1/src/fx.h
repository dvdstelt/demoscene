// Shared effect building blocks used by several scenes.
#pragma once
#include <algorithm>

#include "demo.h"

// Music time helpers. The demo is synced to the module; 303's tempo is ~140 BPM (one beat = kBeat seconds).
constexpr float kBeat = 0.4275f;
// Convert a timestamp measured on the reference video capture to music time.
constexpr float V(float videoSeconds) { return (videoSeconds + 1.395f) / 1.00126f; }
inline float VT(float musicSeconds) { return musicSeconds * 1.00126f - 1.395f; }

struct MeshDraw {
    Mat4 model = Mat4::identity();
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    const Texture* env = nullptr;   // sphere environment map
    Vec3 tint{1, 1, 1};
    float exposure = 1.0f;
    float specular = 0.6f;
    float alpha = 1.0f;
    int mode = 0;                   // 0 env map, 1 contour lines (white on black), 2 wireframe-ish glow, 3 flat depth fog
    float param = 0.0f;
    float spikeScale = 1.0f;        // spikey: scales the radial extent of the spikes
    Vec3 fogColor{0, 0, 0};
    float fogNear = 100.0f, fogFar = 200.0f;
    bool cull = true;
    // per-shell twist for the tentacle mesh (vertex group index in 'extra'): radians per shell around z / y,
    // plus a travelling wobble (phase, amount)
    float twistZ = 0, twistY = 0, wobblePhase = 0, wobble = 0;
};

void drawMesh(Ctx& c, const std::string& mesh, const MeshDraw& d);

// Radial (zoom) blur of src around center (frame coords, y down) into the bound target.
void radialBlur(Ctx& c, const Target& src, float cx, float cy, float strength, float gain, float sharpMix = 0.0f);

// Generic "warp a texture" pass: one of several 2D mappings used all over the demo.
struct WarpParams {
    int mode = 0;          // 0 tunnel, 1 funnel bend, 2 plane rotozoom, 3 polar twirl, 4 kaleidoscope, 5 rings, 6 julia flowers
    float cx = 0.5f, cy = 0.5f;
    float time = 0.0f;
    float zoom = 1.0f;
    float angle = 0.0f;
    float strength = 1.0f;
    float bright = 1.0f;
    Vec3 tint{1, 1, 1};
    float fog = 0.0f;      // darken toward the vanishing point
    float lines = 0.0f;    // procedural web / ray overlay
    float seed = 0.0f;
    float alpha = 1.0f;
    float crisp = 0.0f;    // >0: threshold the texture into crisp black/white (anti-aliased)
    float wave = 0.0f;     // sinusoidal domain warp amount
    bool invert = false;
    float contrast = 1.0f; // gamma applied to the texture
    float shapeP = 2.0f;   // tunnel cross-section: 2 = round, higher = squarish
};
void drawWarp(Ctx& c, const Texture& t, const WarpParams& p, float x = 0, float y = 0, float w = 1, float h = 1);

// Ray traced hex corridor (floor + ceiling) with optional gold metaballs and a plasma sphere.
struct WorldParams {
    Vec3 eye{0, 0, 0}, target{0, 0, -1}, up{0, 1, 0};
    float fovy = 1.0f;
    float floorY = -1.0f, ceilY = 1.0f;   // set ceilY very high to disable the ceiling
    float texScale = 0.35f;
    Vec3 tint{1, 1, 1};
    float fogDist = 9.0f;
    Vec3 fogColor{0, 0, 0};
    float bright = 1.0f;
    int balls = 0;                        // metaballs (gold env map)
    Vec3 ball[8];
    float ballR[8] = {};
    int ballStyle = 0;                    // 0 gold env map, 1 wireframe-look
    float sphereR = 0.0f;                 // plasma sphere (0 = none)
    Vec3 sphere{0, 0, 0};
    float sphereGlow = 1.0f;
    float time = 0.0f;
    bool floorOn = true;
    float glintAmount = 0.6f;
    const Texture* floorTex = nullptr;    // default: tex3d#1 (purple hex)
};
void drawWorld(Ctx& c, const WorldParams& w);
Mat4 worldView(const WorldParams& w);
Mat4 worldProj(Ctx& c, const WorldParams& w);
// A textured quad (-1..1 in xy) placed with an MVP matrix, depth tested.
void drawQuad3D(Ctx& c, const Texture& t, const Mat4& mvp, float alpha = 1.0f, Vec3 tint = Vec3(1, 1, 1), bool bicubic = false,
                bool fromTarget = false);
// Wrap a render target's colour buffer as a Texture handle.
Texture asTexture(const Target& t);

// Copy / composite helpers
void copyTarget(Ctx& c, const Target& src, float alpha = 1.0f);
float beatPulse(float t, float period = kBeat, float sharp = 8.0f);  // 1 at each beat, decaying
float easeInOut(float x);

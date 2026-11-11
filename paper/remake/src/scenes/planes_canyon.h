// Shared code of the two paper-plane parts (teal.cpp: one plane over the teal canyon, flock.cpp: 32 planes in the
// blue canyon), paper/re/notes-planes.md §3 and notes-init.md §5.12, §8.7, §8.10.
//
// Ports
//   0x68a6  canyon texture (teal part start): C[i] = 0x8b + ((T[i] >> 5) & 7) + (rand() & 2), T = the init texture
//           0x10d3, one 0xaa2 call per 4 texels (RngPos::kCanyonTex), i.e. a texel "level" L = 11 + q + 2g with
//           q = top 3 bits of T and g a random bit
//   0x2f24  ray grid: the ray through pixel (px, py) is d = (px/8 - 20) r0 + (py/8 - 12.51) r1 + 28 r2 (camera rows),
//           hitting the plane z = +H or z = -H in front: t = (+-H - C.z) / d.z; texture row = -12.51 (C.y + t d.y),
//           column = -12.51 (C.x + t d.x) (8.8 texels), shade row = clamp(8 (1024 - round t), 2048, 8192) >> 8
//   0x3095  8x8-block mapper: SHADE[row][C] = palette ramp 128 + max(0, L - (32 - row)), i.e. a depth fog of one
//           ramp step per 896 world units (32 per unit of t)
//
// Remake (Canyon::draw): the ray and the plane intersection are evaluated per output pixel (no 8x8 blocks), the fog
// is continuous (t/32 + 0.5: the mean of the original's ceil(round(t)/32)) and the ramp colour is interpolated between
// palette entries. The texture is rebuilt with the same construction at high resolution: q is the 3-bit staircase of
// the core's smooth high-resolution 0x10d3 field (box-filtered per pixel, so the contour edges are antialiased at any
// distance), g the original's 65536 random bits (the same realisation) upsampled 4x by Catmull-Rom (smooth grain instead of
// texel squares when close) and mipmapped; both are sampled with trilinear anisotropic filtering. The original pixel (px, py) shows the ray at its top-left corner, so a VGA point (x, y)
// uses the ray of (x - 0.5, y - 0.5): at 320x200 the pixel centres sample exactly the original's rays.
// Development: PLANES_EXACT=1 renders the original's nearest texel and integer fog (for numeric checks at 320x200).
#pragma once
#include <array>

#include "demo.h"
#include "paper/math3d.h"

namespace planes {

// The camera of 0x2f24 / 0x3fb3: rot rows = camera right, down, forward in world coordinates (V, not necessarily
// orthonormal: the original's 16.16 matrices drift), pos = position, H = the planes z = +-H (teal 1000, flock 2000).
struct CanyonCamera {
    paper::Mat3d rot;
    paper::Vec3d pos;
    double H = 1000.0;
};

// Palette entries 128..159 of the canyon: the teal ramp 0x681a (0x158e at the teal start) or the default palette's
// blue ramp (0x15af in the flock part).
std::array<Vec3, 32> tealRamp(const PaperExe& exe);
std::array<Vec3, 32> blueRamp(const PaperExe& exe);

// The canyon texture bits g (0x68a6, 256x256, row-major [row][column]): the +2 of each texel.
std::vector<uint8_t> canyonGrainBits(const PaperExe& exe);

class Canyon {
public:
    explicit Canyon(Ctx& c);  // ctx.shared<Canyon>()
    // Draws the canyon over the whole bound target (VGA frame).
    void draw(Ctx& c, const CanyonCamera& cam, const std::array<Vec3, 32>& ramp);

private:
    Shader shader_;
    static constexpr int kGrainScale = 4;
    static constexpr float kGrainBias = 0.3f, kGrainRange = 1.6f;
    Texture grain_;     // g: R8, (256 kGrainScale)^2, Catmull-Rom upsampled, mipmaps + anisotropic, repeat
    Texture level256_;  // exact levels L / 255 (nearest), for PLANES_EXACT
    float k_[3] = {0, 0, 0};  // the ray constants -20, -12.51 (also the texture scale), 28 (floats 0x23d8..0x23e0)
    bool exact_ = false;
};

// The teal part's plane distance H (float 0x23e4 = 1000; the flock part stores 2000.0 there).
double tealPlaneDistance(const PaperExe& exe);

// ---- interpolation of the per-step 16.16 matrices for display between steps
// M = Q S (polar decomposition, Q a rotation): Q is slerped and the residual S (the original's 16.16 drift of scale
// and shear, a few per cent at most) is interpolated linearly. Exact at t = 0 and t = 1.
paper::Mat3d interpMatrix(const paper::Mat3d& a, const paper::Mat3d& b, double t);

}  // namespace planes

// p4's two effects (dope/re/notes-p4.md), shared by p4.cpp (the scene, its script and the composite):
//   P4Fire (p4_fire.cpp): the FIRE clouds, the feedback 1aab through the polar swirl map 07d9 over the cloud texture
//     1ec1, one step per odd retrace, as a float recursion per output pixel, and the fire colour (palette 0010:15AE)
//     of the blended state, added with the fire weight of the palette mix 1999;
//   P4Land (p4_land.cpp): the FRACTAL scape, the voxel landscape 1ee8 / 1efc as a continuous camera ray-cast per
//     output column and pixel over the height map 2eec and the colour map 3eec, with the background weight.
#pragma once
#include <cstdint>
#include <vector>

#include "demo.h"
#include "dope/core.h"

// The fire state after step m (m = 1..3715: the m-th odd retrace; step 0 = the cleared buffer of 1caa) is kept in
// three buffers of the output's size; render() blends two consecutive steps.
class P4Fire {
public:
    // One fire step: the texture row shift m (pos = -65536 m in 1a20) and the sine phase [1578] after the odd
    // retrace 2m - 1.
    struct Step { int m; int phase; };
    void init(Ctx& c);
    // Brings the buffers to steps n and n + 1 (n >= 0; steps[k] describes step k + 1), stepping forwards from the
    // last state or rebuilding from a cleared buffer kRebuild steps back after any other jump.
    void advance(Ctx& c, int n, const std::vector<Step>& steps);
    // Adds a * fire colour of mix(F_n, F_n+1, frac) to the bound target (the output; additive).
    void draw(int n, double frac, double a);
    // Steps simulated after a jump: (232/256)^150 = 4e-7, so what came before contributes less than 1/1000 of a level.
    static constexpr int kRebuild = 150;

private:
    void step(int k, const Step& s);  // computes F_k from F_k-1 (or from the cleared buffer at base_)
    Target& buf(int k) { return buf_[size_t(((k % 3) + 3) % 3)]; }
    Shader stepShader_, drawShader_;
    Texture cloud_;
    dope::PaletteTexture firePal_;
    Target buf_[3];
    int have_ = -1;   // the highest step in the buffers, -1: none
    int valid_ = 0;   // how many steps up to have_ the buffers hold (at most 3)
    int base_ = 0;    // the step whose state was taken as the cleared buffer by the last rebuild
    int w_ = 0, h_ = 0;
    Vec3 map_;        // the swirl map's formula: v = x + y r, Y weight z
};

class P4Land {
public:
    void init(Ctx& c);
    // Draws the landscape seen from v = vcam texels (P / 8) times `mul` (the background weight) into c.out,
    // replacing what is there.
    void draw(Ctx& c, double vcam, float mul);

private:
    Shader colShader_, scanShader_, drawShader_;
    Texture height_, colour_;
    Target scan_[2];
    int w_ = 0;
    float hmax_ = 0, ucam_ = 0, dnear_ = 0, dfar_ = 0;
    Vec3 sky_;
};

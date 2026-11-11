// Blur 0xf47 / 0xf90 as a GPU Gaussian (paper/re/notes-init.md §8.11).
//
// 0xf90 is one pass of dst = (N + S + W + E) >> 2 (no centre tap): variance 0.5 px^2 per axis. 0xf47 is two passes
// (one "blur call", variance 1.0). Repeated passes approach a Gaussian, so n passes become one Gaussian with
// sigma = sqrt(0.5 n) original pixels per axis, evaluated at the target's own resolution. The original's per-pass
// truncation (>> 2) and the cross kernel's parity artefacts are dropped.
//
//   blur(ctx, target, 2 * calls);                    // a target covering the 320x200 screen
//   blur(ctx, sprite, 2, Edge::Zero, 80, 40);         // a target covering an 80x40 byte buffer
//   blurSigma(ctx, target, sx, sy, ...)               // sigmas in target pixels
//
// Edges: Zero = the buffer is surrounded by zeros (what 0xf47's second pass sees, and the "1996" image), Clamp, or
// Wrap (toroidal, the cloud texture). Large sigmas are blurred at a reduced resolution (box down, Gaussian, bilinear
// up, the extra variance of the resampling subtracted), so the cost stays small at any output size.
// The target is overwritten; `fmt` must be its internal format (temporaries are borrowed with it).
#pragma once
#include "render.h"

struct Ctx;

namespace paper {

enum class Edge { Zero, Clamp, Wrap };

void blur(Ctx& c, Target& t, double passes, Edge edge = Edge::Zero, double origW = 320, double origH = 200,
          GLenum fmt = GL_RGBA16F);
void blurSigma(Ctx& c, Target& t, double sigmaX, double sigmaY, Edge edge = Edge::Zero, GLenum fmt = GL_RGBA16F);
// sigma in original pixels of n four-neighbour passes
double passesSigma(double passes);

}  // namespace paper

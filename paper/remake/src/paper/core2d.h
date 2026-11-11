// The shared 2D core library of the Paper remake, in one include. Each header documents its part and the original
// routines (code32 addresses) it ports; this is the map.
//
//   common.h    Rng 0xaa2 (+ RngPos: the call count at every random generator of the intro, Rng::at(exe, pos)),
//               sine 0x22479, reciprocal 0x1ee19
//   palette.h   default palette 0x169a3, fade upload 0x15c0 (applyLevel), hue generator 0x14dc (huePalette6 exact,
//               hueBase + hueRamp analytic, HueRampTex), shade/multiply tables 0x144b (shadeIndex, mulIndex),
//               PaletteTex, kPaletteGLSL (palLookup, hueRamp, shadeIndex, mulIndex, applyLevel)
//   fade.h      [0x1edb5] per tick (fadeLevel), and the global post pass that applies it (installed automatically:
//               parts draw with the working palette and never apply the level themselves)
//   font.h      font 0xe2e, placement 0x11b0 / 0xe8a / 0xedc (layoutText, layoutBig, FontData::drawBytes*),
//               Font::draw (anti-aliased high-resolution glyphs; Add/Sub/Max into UNORM intensity buffers saturate
//               like the original's byte adds)
//   textures.h  cloud texture 0x10d3 (exact 256 and smooth high-res, tiling), paper grain 0x55e2 (exact bytes and the
//               continuous shade row), CoreTextures, kTexturesGLSL (paperGrain, paperFront, pageBack, greyRamp)
//   osc.h       oscillators 0x2756 / 0x26d0 (OscSet::title / theEnd / greetings / teal, exact and continuous phases),
//               warp control points 0x2dc8 (warpField), teal wave noise 0x67b9 (noise4)
//   warp.h      warp grid 0x2dc8 / 0x2ad0 / 0x3095 per pixel: kWarpGLSL (warpTexel), Warp::drawTextured / drawField,
//               quadratic2ad0, warpGridExact
//   blur.h      0xf47 / 0xf90 as a Gaussian of the same variance: blur(ctx, target, passes, edge, origW, origH, fmt)
//   peel.h      page peel 0x2b55 / 0x2b20 / 0x2b6c / 0x2ca2: peelProgress, Peel::draw(old, new, p)
#pragma once
#include "paper/blur.h"
#include "paper/common.h"
#include "paper/fade.h"
#include "paper/font.h"
#include "paper/osc.h"
#include "paper/palette.h"
#include "paper/peel.h"
#include "paper/textures.h"
#include "paper/warp.h"

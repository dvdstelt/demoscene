# Paper remake — plan

Goal: the same intro, "way sharper": C++20 / SDL3 / OpenGL 3.3 on a copy of the 303 remake's engine, at any
resolution in true colour, with the original's motion, timing, composition and music. Linux and Windows.

## Decisions (defaults)
- **Own project** at `paper/remake`, starting from a copy of the 303 engine (window, GL helpers, render targets,
  scene registry, build files incl. the Windows cross-build). libopenmpt is not needed.
- **Original data is read at run time from the user's PAPER.EXE** (like 303 reads MUSTARD.PAK): a C++ PKLITE 1.15
  unpacker (large model, no extra compression — header word 0x210f) gives the code32 image; every static table
  (font RLE 0x18ed7, palette 0x169a3, cloud texture 0x16ca3, logo art 0x1bf03 / 0x196c3, strokes 0x5073,
  handwriting 0x18a2f, camera keys 0x466f, objects, tune 0x7a9b + samples, random seed 0x9a0, ...) is read
  from it by address. Nothing from the exe is copied into the remake's source. Verified against paper_img.bin.
- **Clock = the 70.0018 Hz tick**, driven by the audio stream. Per-tick state is stepped exactly as the original
  does; continuous quantities (camera spline, oscillator phases, fades, peel) are evaluated at fractional ticks
  for smooth motion at any display rate. The flock (the one per-frame part) steps at 34 Hz, interpolated.
- **Music**: C++ port of player_ref.py (player + GF1 mixer with real volume reads), checked against its register
  stream; output through SDL audio.
- **Fidelity**: keep what is visible in the original (e.g. the plane's inverted shading while the light vector
  overflows, the flock camera bug that shapes its motion, the random realisations — same RNG sequence so the
  flock and jitter match the capture); drop low-resolution artefacts (8x8-block interpolation streaks, stale top
  rows in the peel, integer fade truncation steps). Blurs become Gaussians of the same variance, lines become
  resolution-scaled pencil strokes, 32-step shade ramps become continuous.

## Steps
1. Skeleton (coordinator): copy engine, CMake, timeline by tick ranges (PARTS.md), reference compare tool
   (remake frame vs full.frames at the same tick, side by side).
   In parallel: PKLITE unpacker (agent), music player + GF1 mixer port (agent).
2. Core library `paper/remake/src/paper/` (one agent): RNG 0xaa2 with call-count bookkeeping, sine/reciprocal
   tables, paper grain, cloud texture (hi-res), font, palettes + fade + hue circle 0x14dc, shade/MUL tables as
   continuous functions, object loader/instancing (0x39a4 / 0x3cb7) + transform/light (0x3fb3) + pencil-outlined
   flat faces (0x43df) on the GPU with a depth buffer, pencil lines, warp grid 0x2dc8, canyon ray grid 0x2f24,
   page peel 0x2b6c.
3. Parts in parallel (one agent each, own scene files, verified against the capture):
   title + "the end" + closing logo · pencil sketch + coloured 3D scene + peel · logo page · teal plane + flock ·
   greetings.
4. Integration: full run vs paper_reference.mp4, timing check, README, Windows build.

Notes per scope: notes-init / title / sketch / scene3d / logo / planes / water / music.md.

# AGENTS.md

Instructions for AI coding agents (and humans) working in this repository.

## What this is

*demoscene remakes*: high-resolution re-implementations of MS-DOS demoscene productions in C++20, SDL3 and OpenGL
3.3 core. Each one was reverse engineered from the original executable, and it reads the original files at run
time. The goal for every remake is **the same as the original, but much sharper**: the original's motion, timing,
composition, colours and music, rendered at any resolution in true colour.

```
<demo>/
  README.md        about the original, how to watch/run, which versions exist
  original/        the original release (git-ignored, except its README)
  remake/          the current remake: CMake project, src/, tools/, README.md (build + fidelity details)
  re/              reverse-engineering notes (notes-*.md: pseudo-C with every constant) and helper scripts
  docs/            making-of.md, images/
docs/              how-it-was-made.md, reverse-engineering.md, videos.md
scripts/           fetch-originals.(sh|ps1), package-windows.sh, render-videos.sh
```

303 also has `remake-v1/`, the first version rebuilt by eye from a video. It is kept as a record: do not develop it
further unless asked; fixes there should stay minimal.

## Hard rules

1. **Never commit the originals** or anything generated from them: executables, PAK/LZW files, unpacked images,
   memory dumps, emulator captures (`*.frames`, `*.ticks`, `*.gus`, `*.dram`), audio renders, videos, frame dumps.
   `.gitignore` covers the usual names; check `git status` before committing. The exceptions are a few small
   screenshots in `<demo>/docs/images/` for the documentation, and the redrawn pictures in `<demo>/remake/art/`
   (below).
2. **Never copy data from the originals into the source.** Tables, palettes, images, meshes, strings, lyrics,
   coordinates and music are read from the original files at run time, by address or by file name (see
   `paper/remake/src/exe.h`). Allowed in source: code constants (multipliers, loop counts, addresses and offsets,
   format constants), values measured from a capture, and our own parameters.
3. **Non-commercial.** The code is under the PolyForm Noncommercial license and the originals belong to their
   authors (NOTICE.md). Do not add anything that sells, advertises or monetises.
4. **No new dependencies** beyond SDL3 (and libopenmpt for 303) and the single-file stb headers in `third_party/`.
   Code must build with g++, MinGW and MSVC.
5. Never push, create releases or change GitHub settings unless the maintainer asks.

### Redrawn pictures

STARS, DOPE and Paper show high-resolution redraws of their hand-drawn pictures by default, and the original pixels
with `--original-art`. The redraws are AI redraws made from the original pictures, at the maintainer's request, and
stay faithful to them: the same composition, pose, proportions, framing, colours and text; only resolution and detail
change. They live in `<demo>/remake/art/` with a README that names the original artist, how each picture was made
and how it was aligned. Only pictures that are drawn as they are get a redraw; generated textures, maps and anything
an effect reads through palette indices stay computed from the original. When ChatGPT refuses a picture or does not
redraw it faithfully, try the Comfy Cloud route instead; a picture no tool redraws faithfully keeps the original
pixels. Never reword a prompt to get past a refusal. [docs/redrawing-pictures.md](docs/redrawing-pictures.md) is the
recipe.

## Fidelity rules

- Keep timing (to the tick), motion paths, camera, composition, colours, random sequences (replay the original's
  generator from its seed) and the visible quirks that are part of the original's look. Each notes file lists them.
- Evaluate exactly instead of copying low-resolution shortcuts: 8x8-block interpolation becomes per pixel; lookup
  tables (sine, reciprocal, 32-step shade ramps, integer fades) become continuous float maths; repeated box blurs
  become Gaussians of the same variance; 1-2 px lines become resolution-scaled strokes; nearest-neighbour reads
  become filtered reads; integer polygon vertices become floats with anti-aliased edges.
- Drop artefacts that only exist because of 320x200 / 8 bit (stale rows, truncation steps, aliasing), but never
  change what a viewer would describe as the content.
- A part's `render(ctx, time)` depends only on time: players seek and screenshots render in any order. Per-tick
  state is stepped exactly like the original (replayed from the part start after a backwards seek) and
  interpolated by the fraction of a tick.
- When something cannot be exact (frame-rate dependent effects, for example), say so in the remake's README under
  "Known differences".

## Building

Each remake is a standalone CMake project. If SDL3 / libopenmpt are not installed, CMake downloads them.

```
cmake -S paper/remake -B paper/remake/build -G Ninja && cmake --build paper/remake/build
./paper/remake/build/paper-remake --window   # finds paper/original/PAPER.EXE by itself
```

Windows from Linux: `mingw64-cmake -S <dir> -B <dir>/build-win -G Ninja && cmake --build <dir>/build-win`.
The maintainer's machine is Fedora Atomic: compilers, Python tools (numpy, PIL, capstone, unicorn) and dosbox live
in a toolbox container (`toolbox run -c acme303 <cmd>`). Install packages only inside the container, never on the
host. Use your own build directory (`build-<name>`) when other agents may build at the same time, and delete it
afterwards.

## Checking your work

- Every remake has offline modes: `--shots T1,T2 --shotdir DIR --shotsize WxH` writes frames to PNG, `--wav`
  renders the soundtrack, `--video` renders a range through ffmpeg, `--bench` reports GPU time per frame.
- Compare against the original, not against your expectations: `paper/remake/tools/compare.py` and
  `jizz/remake/tools/compare.py` put remake frames next to the emulator capture (regenerate it with
  `paper/re/run_paper.py` or `jizz/re/run_jizz.py`, see the `re/README.md`); `303/re/compare.sh` does the same
  against a video capture of 303. Look at the resulting image.
- JIZZ's music has a checker: `cmake --build jizz/remake/build --target test_music` compares the module, the GUS
  register stream, the GUS memory and the music positions with the capture.
- Seek determinism: rendering a time after seeking forwards and after seeking backwards must give identical frames.
- Performance target: 60+ fps at 2560x1920 on a mid-range GPU; all initialisation together under 2 s.

## Code conventions

- Portable C++20, small readable functions. Every source file starts with a comment saying what it draws or
  provides and which original routines (by address) it ports.
- Look in `src/demo.h` (framework, `Scene`, `REGISTER_SCENE`, the context) and `src/render.h` (GL helpers) before
  adding helpers; parts share code through the demo's core library (`paper/remake/src/paper/`, `jizz/remake/src/jizz/`,
  `303/remake/src/exefx.*`).
- Paper and 303 count time in their original's timer ticks; JIZZ counts seconds of music time and gets the
  original's timer from `ctx.timer(t)`, because its parts animate from MXMPlay's timer.
- Colours are display-space floats (a 6-bit VGA value v is v/63). The original 320x200 screen is shown at 4:3.

## Documentation

When behaviour changes, update the remake's README (fidelity, known differences) and, if the process changed, the
demo's `docs/making-of.md`. Reverse-engineering findings go into the demo's `re/notes-*.md` with addresses and
constants. Write plainly; describe what is there, not how good it is.

## Adding a new demo

Create `<demo>/` with the layout above, add the original's files and checksums to `scripts/fetch-originals.*`, a
row to the table in the root README, a build job to `.github/workflows/build.yml` and a packaging entry to
`scripts/package-windows.sh`. Start from the Paper remake's framework (`demo.*`, `render.*`, `gl.*`).

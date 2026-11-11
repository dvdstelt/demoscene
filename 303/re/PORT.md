# Porting decompiled effects into the 303 remake — shared brief

## Goal
The remake (C++20, SDL3, OpenGL 3.3) at 303/remake recreates Acme's 1997 DOS demo
"303". Several parts were rebuilt by eye from a video capture; the original executable has since been
decompiled into notes (pseudo-C with every constant). Your job: replace the scenes you own with faithful
ports of the original algorithms, at high resolution in true colour ("same as the original, but much
sharper"): evaluate per pixel what the original computed per 8x8 grid block, keep colours in floating point,
anti-alias lines and edges, but keep the original's look, motion, timing and composition. Where the original
is random, use a fixed-seed ExeRand and, where the capture shows a specific realisation that matters
(a mode switch, a word position), script that realisation.

## What you own
Only the scene file(s) named in your task, under remake/src/scenes/. Do not edit anything else
(exefx.*, fx.*, demo.*, render.*, timeline.cpp, other scenes, README). If you need a shared helper, copy it
into your own file (anonymous namespace). If your parts need different cue boundaries or names in
src/timeline.cpp, or a different split of scenes, put the exact change in your final report and the
coordinator applies it; for your own testing you may keep a local, uncommitted copy of timeline.cpp in your
build only if you restore it afterwards — simpler: make each of your scenes handle whatever time range the
current cue gives it, deciding internally by the exe's music position what to draw.
Current cues (video seconds → scene): intro 0–40.95, aap –54.7, blobby –65.3, alex –71.65, harl –81.95,
web –92.5, rings –103, face –120, facebox –124.9, typo –129.95, blobs –146.5, wireblobs –149.9,
spiral –156.6, vic –162.95, star –169.95, plasma –191.3, paranoid –197.4, facefloor –208.4,
dance –217.9, icons –230.6, outro –253.4. Scenes are registered by name with REGISTER_SCENE.

## Build and test (use your own build directory to avoid clashing with other agents)
From 303/remake (the compiler is in the toolbox container "acme303"):
    toolbox run -c acme303 sh -c 'cmake -S . -B build-<you> -G Ninja >/dev/null && cmake --build build-<you> 2>&1 | grep -E "error|warning"'
Compare against the capture (left original, right remake, at the given capture/video times):
    S=<scratch>
    cd $S && BIN=303/remake/build-<you>/acme303 ./compare.sh $S/<you>_cmp.jpg 120.5 121.3 ...
then Read the jpg. Each shot runs a 1.5 s warm-up at 60 fps first, so feedback effects are in steady state.
Shader compile errors are printed by the remake on stderr (compare.sh shows the first lines).
Frame grabs of the capture alone: $S/grab.sh out.jpg t1 t2 ... (4 per row). Video file:
a video capture of the original (not in the repository) (960x720, 30 fps).
Disassembly tools (if the notes leave something open): remake/tools/re303.py, see
$S/re/BRIEF.md. capstone only works inside the toolbox (toolbox run -c acme303 python3 ...).

## Engine essentials (read the headers for details)
- Scene interface (src/demo.h): `struct X : Scene { void init(Ctx&) override; void render(Ctx& c, float, float) override; }; REGISTER_SCENE(X, "name");`
  render() draws into `c.out` (bind it before the final pass). c.T = remake music time (seconds), c.dt = frame delta.
  c.w × c.h is the 4:3 render size. c.scratch[0..2] are full-size RGBA16F targets with depth; c.half[0..1] half size.
- Resources: `c.tex("name")` (PAK .raw → texture; "name#k" = k-th 256x256 or 320x200 tile of a stacked image,
  e.g. "tex3d#5", "blobby#1"), `c.video("ALEX"|"HARL"|"VS"|"PARANOID", frame)`, `c.meshData("spikey"|"icosa"|"faceo")`
  (original units), `c.pak.get("file")` raw bytes (palette = first 768 bytes, 6-bit), `c.font(...)`.
  Call c.tex(...) for every texture you need BEFORE binding anything in a frame (creating a texture binds it
  to the active unit).
- GL helpers (src/render.h, src/gl.h): Shader::build(vs, fs, name) prepends "#version 330 core" and kCommonGLSL
  (PI, hash12, rot2, texBicubic, luma); Shader::set overloads; Target::create(w,h,depth,fmt) / bind / bindTex;
  drawFullscreen() with kFullscreenVS (out vec2 uv, 0..1, y up); setBlend(Blend::None|Alpha|Add|Screen|Multiply|Premul).
  In original screen pixels: px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0). An original pixel is 1.2x taller than wide on the 4:3 screen.
- Timing (src/fx.h): musicPos(T) = the exe's position counter 2*(order*64+row)-2; musicTimeOfPos(pos);
  exeTime(c.T) maps remake time to the music time the original's visuals were at in the capture. It uses
  captureLead(m), a measured piecewise-linear table (visuals lead the sound by ~0.53 s at the start, shrinking to ~0 by
  150 s and staying ~0 after). If your section is still consistently offset, measure it and report it — the
  coordinator updates the table (do not add local offsets); kTickHz = 70.086 (vertical-retrace callbacks).
- Ported building blocks (src/exefx.h): ExeRand (Watcom rand), ExePalette (+ kExePaletteGLSL exePalette(), the
  palette level/flash/negative/multiplier system of 0x10124/0x1019c), kSphereRayGLSL + exeMatrix (0x1cd4b/0x135dc
  sphere-ray grid; note m[3] = -sin b·sin a), StepClock (fixed-rate steps for per-frame feedback), LineBatch (soft
  additive lines into an R16F target, original pixel coordinates), LyricLayer/LyricState/kLyricCycle (0x2a34c lyric
  renderer), exeComposite (0x29d1c inverted thumbnail inset + lyric lighten + palette), fadDarkTint.
- Reference ports to copy patterns from: src/scenes/intro.cpp, aap.cpp, blobby.cpp. They replay the 70 Hz
  callbacks from the part start every frame (cheap, deterministic, seek-safe), keep per-frame feedback at a fixed
  rate with StepClock, and finish with the palette pass.
- Fade tables (*.fad, 64x256): row 32 = identity, rows below scale towards black (r/32), rows above lerp towards
  white ((r-32)/31), snapped to the palette's nearest colour. Ghost tables (*.gst, 256x256) G[new][prev] are blends;
  fit them numerically against the palette if your part uses one (see $S/re/tables.py for an example fit).

## Report
Final message under 300 words: what you ported (and which notes sections), what is still approximate and why,
measured time offsets, and any timeline.cpp changes needed (exact cue lines).

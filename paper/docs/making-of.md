# Making of the Paper remake

Paper was the second production remade here, after [303](../../303/docs/making-of.md). With 303, rebuilding from a
video first and decompiling later meant doing much of the work twice. So Paper started with the executable, and the
brief was: reverse engineer everything, then port it. The work was done by Claude Code agents, directed by me,
Dennis van der Stelt; [../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the way of working.

## 1. Unpacking

`PAPER.EXE` is a 63,845-byte DOS executable compressed with PKLITE 1.15. Instead of a separate unpacking tool,
[`re/unpklite.py`](../re/unpklite.py) runs PKLITE's own decompression stub in the
[unicorn](https://www.unicorn-engine.org/) CPU emulator. It emulates real mode with a minimal DOS and stops when
the stub jumps to the original entry point. The result is a 0x269e0-byte program image.

Inside is Tran's PMODE 3.x DOS extender and a 32-bit segment of hand-written assembly, with no C runtime.
[`re/rep.py`](../re/rep.py) is a capstone helper on that segment that lists functions, call graphs and
cross-references. The remake does the same unpacking natively in C++ (`remake/src/pklite.cpp`), and its output is
byte-identical to the stub's.

## 2. Running it headless

Paper only plays music on a Gravis UltraSound, and the original's fast-PC behaviour skips a whole part. So instead
of DOSBox, [`re/run_paper.py`](../re/run_paper.py) runs the 32-bit code directly in unicorn:

- the segment is loaded at linear address 0 with flat selectors, and PMODE's variables are preset;
- interrupt-vector setup and real-mode interrupt calls are intercepted;
- VGA (palette DAC, retrace), the timer and interrupt controller, the keyboard and a GUS at port 0x240 are faked;
- the 70 Hz timer interrupt is delivered every N instructions of main-line code, so N sets how fast "the PC" is.

For every tick it records the 320×200 screen, the palette, every GUS register write and the intro's key variables.
That capture (`full.frames`, about 790 MB) is the ground truth for everything that follows. With snapshots
(`--snap-at`, `--load`), an agent can start the intro at any part, change a variable and watch the effect.

The capture also explained the missing part. The flock part waits for the palette fade to rise above zero before
its first loop test. The fade only rises in the timer interrupt, so if set-up and the first frame finish within
one tick, the part ends immediately. On a fast PC that is what happens, and that is why so many videos of Paper
lack the flock. The harness slows the PC down for that part (`--plane-budget 750000`, about 34 fps, the speed I
remembered it at).

## 3. Decompiling

The main program, the timeline and the shared code were mapped first ([`re/NOTES.md`](../re/NOTES.md),
[`re/PARTS.md`](../re/PARTS.md)). Then agents took one scope each ([`re/BRIEF.md`](../re/BRIEF.md)). Every agent
decompiled its routines into pseudo-C with exact constants, wrote a Python model, and checked the model against
the capture: pixel-exact where the original is deterministic, statistically where it is not.

| notes | scope |
|---|---|
| [notes-init.md](../re/notes-init.md) | start-up and shared code: tables, the random generator, 3D objects, the palette system, the 3D engine |
| [notes-music.md](../re/notes-music.md) | the tracker player, the GUS set-up, the sample upload |
| [notes-title.md](../re/notes-title.md) | the title, "the end" and the closing logo |
| [notes-sketch.md](../re/notes-sketch.md) | paper, red pencil and the pencil sketch |
| [notes-scene3d.md](../re/notes-scene3d.md) | the coloured 3D scene, the banner and the page peel |
| [notes-logo.md](../re/notes-logo.md) | the "present the Paper intro" page |
| [notes-planes.md](../re/notes-planes.md) | the paper plane over the teal canyon and the flock of 32 planes |
| [notes-water.md](../re/notes-water.md) | the greetings on the metallic surface |

A few findings:

- **Timing.** Almost everything advances in the 70.0018 Hz timer interrupt, not per drawn frame. The intro's own
  clock is therefore exact, and the remake can step the same state tick by tick.
- **Randomness.** One lagged-Fibonacci generator, seeded from the executable, drives the letter jitter, the warps,
  the canyon texture and the flock. Counting the calls made before each part lets the remake reproduce the exact
  sequence the original draws.
- **Music.** It is not synthesised: 8 sampled instruments are stored raw after a compact 10-channel pattern
  stream, and a small MOD-style player drives the GUS. [`re/player_ref.py`](../re/player_ref.py) mirrors the
  player routine by routine and regenerates every GUS register write of the capture byte for byte.
- **Quirks.** The flock camera subtracts all three camera coordinates from one axis, the teal plane's light
  vector overflows and flips its shading, and the closing "1996" glow decays in streaks made by rounding over 150
  blur passes. They are part of the look, so the remake keeps them.

The agents' models and check scripts are kept in [`re/models/`](../re/models/).

## 4. Porting

[`remake/docs/BRIEF.md`](../remake/docs/BRIEF.md) is the brief every porting agent worked from. It covers:

- the fidelity rules;
- the time and screen conventions;
- the rule that nothing from the executable may be copied into the source.

The work was split so that agents could build at the same time without touching each other's files:

- **The core 2D library:** palettes and the global fade, the font, the textures, the oscillators, the warp, the
  blur and the page peel.
- **The core 3D library:** exact 16.16 matrices, the object loaders, the renderer on the GPU and the pencil lines.
- **The music:** the player port and a model of the GUS GF1 chip.
- **One agent per part.**

Each part agent compared its frames with the capture using
[`remake/tools/compare.py`](../remake/tools/compare.py), and the check scripts they used are kept in
[`re/port-checks/`](../re/port-checks/). Then the parts were assembled and every part boundary was checked.
Seeking forwards and backwards was checked to give identical frames, and every part was timed on the GPU at
2560×1920.

The rules that make it sharp without changing it:

- the warps and the canyon are evaluated per pixel instead of on 8×8-pixel interpolation grids;
- colours are floats instead of 32-step palette ramps;
- Gaussians replace the repeated box blurs;
- text is drawn from distance fields;
- polygons are anti-aliased and pencil strokes scale with the resolution;
- the page peel is a continuous fold instead of 25 strips.

## 5. Checking it

- **Music.** The counter matches the reference on all 12,235 ticks. The rendered audio differs from the reference
  render by an RMS of 1.75·10⁻⁵, with a peak of one 16-bit step.
- **Picture.** Every part was compared with the capture, and part boundaries line up within one tick.
- **Seeking.** Frames rendered after seeking forwards and after seeking backwards are identical.
- **Speed.** About 1–13 ms per frame at 2560×1920, and about 150 ms to initialise everything.

What could not be checked: nobody has listened to the GF1 model next to a real GUS, and the Windows build has not
been run on Windows.

## Afterwards

The original's flock moves once per drawn frame, so its speed depended on the PC. The first remake stepped it too
fast. I remembered the planes turning less and flying in one direction for longer, and chose 34 fps from
three test renders.

## The redrawn sketch

After STARS and DOPE got redraws of their hand-drawn pictures, Paper got one too. Of the intro's static bitmaps
only one is a picture shown as it is: Statix's pencil sketch of the word "Paper" on the logo page (224x83, 16
darkness levels). The ink image the flood fill colours is a map of palette indices, so it keeps its computed
upscale; the font, the cloud texture and the strokes were already rebuilt or generated. The sketch was uploaded as
grey on a white 2:1 canvas and redrawn with ChatGPT's image generation; the second attempt was kept, because it
wrote the signature as a faint lowercase "stx" like the original's and kept the soft grey strokes on the "a", "e"
and "r". Chrome did not start downloads in that session, so the image was captured from the page at 1:1 in three
tiles. `remake/tools/align_art.py` fits it onto the original (offset under 0.6 original pixels, detail correlation
0.99) and maps its tones onto the original's; [`remake/art/README.md`](../remake/art/README.md) has the prompt and
the fit. The redraw's crisp strokes showed through the ink's light grey rims and highlights, which the remake draws
as partial coverage, so with the redraw the logo part hides the sketch under every ink pixel the fill has reached,
as the original does. `--original-art` shows the original sketch, drawn as before.

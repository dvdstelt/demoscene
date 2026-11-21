# How Paper was made: the original's techniques

*Paper* won the PC 64k intro competition at Wired '96 (Belgium, 1–3 November 1996) with 733 votes, more than
twice the 360 votes of the runner-up, Pulse's *Famous Cyber People*
([results](https://files.scene.org/view/parties/1996/wired96/results.txt)). It was made by:

- **Statix** (Alex Evans): code, graphics and design;
- **Vic** of Acme: music;
- **Tran**: PMODE, the DOS extender.

The intro was written for a 486 or Pentium with VGA mode 13h (320×200, 256 colours) and a Gravis UltraSound, in
the years before 3D cards. Every pixel is drawn by the CPU. The code is hand-written 32-bit assembly with no C runtime, and
together with its variables it takes under 29 KB. People who saw it at the party still remember the moment the pencil drawing turned out to be a 3D
scene and began to move ([pouët comments](https://www.pouet.net/prod.php?which=75)). Afterwards the hall spent
hours throwing paper planes
([Evans, 2014](https://www.mediamolecule.com/blog/article/paper_alexs_64k_demo_from_1996);
[interview, 1998](http://www.jerware.org/fanclub/spotlite.html)).

This page describes what the code does. It is based on our reverse engineering ([`../re/`](../re/README.md));
addresses are offsets in the intro's 32-bit segment.

## What fits in 64 KB

`PAPER.EXE` is 63,845 bytes, packed with PKLITE 1.15. Unpacked, the program image is 158,176 bytes, and its
largest part is music:

- **Samples:** eight 8-bit samples, 34,498 bytes, stored raw.
- **Patterns:** a 26,177-byte stream, stored channel by channel. That turns silent stretches into runs of identical
  bytes.

We used zlib as a stand-in for PKLITE to estimate the packed sizes. The samples come to about 25 KB of a 64 KB
result, and the pattern stream to about 1.3 KB.

Nearly everything else is generated at start-up by `0x5b7d`:

| what | generated from | routine |
|---|---|---|
| a 256×256 texture | a 64×64 4-bit picture, mirrored four ways and blurred | `0x10d3` |
| the paper grain | embossed random noise | `0x55e2` |
| a 512×256 light map | computed on the FPU | `0xc80` |
| 29 font glyphs | 1,980 bytes of run-length data | `0xe2e` |
| the banner's "Statix + Vic (acme)" drawing | 106 vector strokes (lines, circles and curves) in 1,192 bytes | `0x5979` |

The shade, multiply, sine and reciprocal tables are computed too. The largest stored pictures are the "Paper" logo
and its ink colours, two 4-bit bitmaps 224 pixels wide.

The buffers take more than 1.5 MB, which PMODE's flat 32-bit memory makes possible. The allocator (`0x240`) never
frees anything. While all this is built, a text-mode progress bar reads "Gosh <…>".

The NFO tells how the release nearly did not happen: packing the intro crashed the borrowed PC it was made on
([NFO](https://www.pouet.net/prod_nfo.php?which=75)). That PC was mine. Statix had asked in #coders on IRC whether
someone could lend him a machine at Wired to finish the intro. I was on #coders and #nlcoders a lot in those days,
and at parties I mostly chatted and socialised, so I said yes. I sat next to Statix for quite some time while he
coded. At the very end, when he packed the intro, my machine crashed and would not start again. Statix moved to the
machine of Retrox, the friend I had travelled to Wired with, and finished the intro there. Meanwhile I tried to
bring my machine back, because my BBS was on it and I could not afford to lose it. Nix of The Black Lotus, who wrote
JIZZ's vector engine, put the drive in his own machine and repaired it, and before the party was over my machine
booted again.

## The sketch is the 3D scene

The red pencil does not copy a picture. It traces 275 edges of the 3D scene that is later shown in colour, seen
from that scene's camera:

- **Which edges.** Each face links to its neighbours. Where the normals of two neighbouring faces differ by 81° or
  more, the shared edge is marked as a crease (`0x3eeb`). In each frame, an edge is drawn if it is a crease or lies
  on the silhouette.
- **Hidden lines.** The faces are filled with the paper colour, so they hide the lines behind them.
- **Stroke order.** Each stroke takes 5 bytes: the face's position in the painter's order of a reference render,
  and three edge bits. `0x5e7f` renders the scene once at start-up to turn those positions back into faces.
- **Pen speed.** The per-tick sequencer (`0x6267`) moves the pen at a speed that grows with the square root of
  the stroke length.

Each pencil line (`0x18b7`) is three overlapping 2-pixel lines with jittered endpoints. The jitter and the greys
are looked up by a hash of the endpoints, so a finished line is identical in every frame and does not flicker.
The paper grain is then applied over everything (`0x1943`). The pencil itself is a flat-shaded 3D object, and it
casts a shadow onto the paper from a point light (`0x42e4`).

The change to colour (`0x72c4`) renders the scene twice per frame, once as the sketch and once in colour. A
threshold over a sum of sine curves picks one of the two per pixel. The threshold falls from 255 to 0 over 3.6 s.

The 3D engine has no z-buffer. Faces are sorted into 1024 linked-list depth buckets (`0x3fb3`) and painted far to
near (`0x43df`), flat-shaded through 32-step palette ramps. A 2002 pouët comment by ryg calls Paper "the first demo
with cartoon rendering (I think)". We have not verified that.

## Interpolated grids

The title warp, the greetings surface and both canyons share one renderer, `0x3095`. The expensive maths runs only
at the 41×26 corners of 8×8-pixel blocks:

- **canyons:** a floating-point intersection of each corner's view ray with two planes (`0x2f24`);
- **warps:** a quadratic patch whose control points are sums of 72 sine oscillators (`0x2dc8`).

The unrolled inner loop then interpolates across each block:

- The texture is 64 KB-aligned, so `bh` and `bl` are its row and column, and the fractions are carried with `adc`.
- Each texel goes through a 64 KB-aligned shade table, which adds fog and brightness.
- Four pixels are written per 32-bit store.

The "water" behind the greetings is not a simulation. It is a static texture used as a height map, whose slopes
index the precomputed light map (`0xd28`). A 2005 pouët comment says Paper was one of the first productions to use
interpolated grids. That is not verified either.

## One 70 Hz clock

The timer runs at 70.0018 Hz (`0x1e32`), close to mode 13h's refresh rate. Its interrupt handler (`0x1e45`) does
three things:

- calls the music player at 50 Hz, through an accumulator;
- steps the palette fade, uploading the palette only when it changes;
- calls the current part's tick routine.

So the animation runs at the same speed on any PC, while the main loop draws as often as it can. Each frame is
copied to VGA (64,000 bytes) without waiting for the vertical retrace.

Even precalculation is spread out. The closing "1996" glow takes 150 blur calls, and 75 of them run at start-up.
The rest run in later parts' spare time (`0x13af`) under a fixed budget, so the result is the same on every machine.

The exception is the flock of 32 planes, which moves once per drawn frame. Each plane steers towards a randomly
wandering point, with some jitter (`0x342f`). The random generator always starts from the same seed, stored in the
exe. But the flock draws one set of random numbers per frame, so on a different PC "the end" picks up a different
stretch of the sequence. This is in line with Statix's own description, in 1998, of driving motion by randomness
rather than stored paths: "most of the way I drive the motion in my effects is algorithmic"
([interview](http://www.jerware.org/fanclub/spotlite.html)).

## Music on the Gravis UltraSound

The player (`0x1ed6`) is a small MOD-style player for 10 channels.

- **Finding the card.** It reads the `ULTRASND` environment variable.
- **Loading the samples.** It writes them into the card's memory one byte at a time through I/O ports, without DMA
  or card interrupts.
- **Playback.** 16 voices at 38,587.5 Hz. Every volume change is a hardware ramp that starts from the voice's
  current volume, which the player reads back from the card.
- **Without a GUS.** The intro asks for the port; space plays it silently. The player keeps counting rows in that
  case, so the parts stay in sync.

Statix first wrote the music himself, then asked Vic, who made the tune in two days
([interview](http://www.jerware.org/fanclub/spotlite.html)).

## Self-modifying code and other unusual code

We looked in two ways: we scanned every reachable instruction for stores into code, and every code address the
program loads into a register. There are six self-modifying places, listed below. All of them store a parameter in
an instruction's operand. None generates code.

| instruction | patched operand | written by | use |
|---|---|---|---|
| `0x14dc mov ebx, imm32` | hue angle (`0x14dd`) | +1 per tick in timer callbacks `0x27ae`, `0x26aa`; set at `0x25a2`, `0x13ea` | palette generator; this immediate is the variable's only copy, and the callbacks read it as data |
| `0x1504 mov cl, imm8` | colour amplitude shift (`0x1505`) | `0x2856`, `0x25ac`, `0x26a2`, `0x13f4` | palette generator |
| `0xeb6 add edi, imm32` | glyph row stride (`0xeb8`) | `0x2937` (80), `0x24f0` (52), `0x562d` (320); 640 in the exe | one glyph blitter for four buffer widths |
| `0x2efa shl eax, 5`, `0x2f00 shl eax, 7` | shift counts (`0x2efc`, `0x2f02`) | `0x256c`/`0x2573` (6/6), `0x79d7`/`0x79de` (5/5) | warp scale per part, with no need for `cl` |
| `0x42aa sar edx, 6` | depth-bucket shift (`0x42ac`) | `0x687a` (3), `0x699d` (6) | finer depth sorting for the lone teal plane |
| `0x7323 mov ch, 0xff` | colour threshold (`0x7324`) | decremented at `0x7266` each tick, inside the timer interrupt | the 2D-to-3D change: the interrupt rewrites an instruction of the main loop |

Three other unusual things:

- **Shared return.** 26 conditional jumps target one `ret` at `0x24e`, inside PMODE's allocator, as a
  conditional return.
- **Values left on the stack.** `0x3793` returns three random numbers, two of them left on the caller's stack by
  moving its own return address.
- **Skip key.** Pressing `9` during the pencil parts jumps to the colour scene (`0x78c8`).

The scene record says nothing about Statix's use of self-modifying code. In Paper it is limited to these six
places.

## Bugs that became part of the look

- **The flock is skipped on fast PCs.** The part waits for the palette fade, which only moves in the timer
  interrupt. If the set-up finishes within one tick, the part ends at once (`0x397e`).
- **The flock camera** subtracts all three camera coordinates from one axis.
- **The teal plane's lighting** overflows 32 bits, which flips the shading.
- **The "1996" glow** decays in streaks, from rounding in the integer blurs.

## Not verified

- The "first" claims (cartoon rendering, interpolated grids) come from single pouët comments.
- That the patterns were stored channel by channel to help the packer: the layout is fact, the reason a guess.
- What the NFO's crashed packer (Compack) did exactly, and how much had to be redone.
- demozoo could not be read. The credits and dates here come from the NFO, pouët and the party results.

## Sources

- Our notes: [NOTES.md](../re/NOTES.md), [PARTS.md](../re/PARTS.md), [notes-init.md](../re/notes-init.md),
  [notes-music.md](../re/notes-music.md), [notes-sketch.md](../re/notes-sketch.md),
  [notes-scene3d.md](../re/notes-scene3d.md), [notes-planes.md](../re/notes-planes.md),
  [notes-water.md](../re/notes-water.md), [notes-title.md](../re/notes-title.md),
  [notes-logo.md](../re/notes-logo.md), [making-of.md](making-of.md)
- pouët.net, Paper (prod page and comments): https://www.pouet.net/prod.php?which=75
- Paper NFO: https://www.pouet.net/prod_nfo.php?which=75
- Wired '96 official results: https://files.scene.org/view/parties/1996/wired96/results.txt
- Alex Evans, "Paper: Alex's 64k demo from 1996", Media Molecule blog, 2014:
  https://www.mediamolecule.com/blog/article/paper_alexs_64k_demo_from_1996
- Jeremy Williams, interview with Statix, PC Demo Fan Club, 1998: http://www.jerware.org/fanclub/spotlite.html

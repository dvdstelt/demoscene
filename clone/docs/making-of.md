# Making of the clone meets clone remake

clone meets clone was remade after STARS and DOPE, in one day, and like them it went straight to the executable.
Claude Code agents did the work, directed by Dennis van der Stelt, who supplied the original release.
[../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working. As in STARS
and DOPE, the remake shows high-resolution redraws of the hand-drawn pictures, and the original pixels on request.
It is the second intro in this repository with music played by Niklas Beisert's MXMPlay, after JIZZ, and Aap, who
drew its pictures, also drew 303's.

## Unpacking

`CLONE.EXE` (63,994 bytes) is three things in one file:

- **PMODE/W v1.23**, Daredevil and Tran's DOS extender, as the MZ stub;
- **a small LE program** that PMODE/W loads and runs, called "veloader" in its header: one page of code that opens
  its own file, reads a "VE" header behind itself, unpacks the program and its fixup list, adds the load address to
  2,570 dwords, zeroes 12 MB of BSS behind the image and calls the program;
- **the packed program**: LZ77 with a 4 KB window and canonical Huffman codes (Deflate's distance and length
  tables, cut short), 49,225 bytes that unpack to the 164,972-byte image of a Watcom C program.

`re/unpack.py` runs the veloader's own code in the unicorn emulator, at two load addresses to see every relocated
dword, and checks a Python reimplementation of its decompressor and fixups against it. The remake has the same in
C++ (`remake/src/unpack.cpp`, 1 ms) and checks the image by hash.

## Running it headless

`re/run_clone.py` runs the unpacked program in unicorn without PMODE/W or the veloader, with the registers the
veloader leaves. It answers the few DOS and DPMI calls the intro makes and fakes the hardware: a VESA 2.0 BIOS with
320x200 modes in 32 and 15 bits and a linear frame buffer, the timer chip, the interrupt controller and a Gravis
UltraSound (the JIZZ harness's). The intro runs at 150 million instructions per emulated second, a little faster
than the 133 MHz Pentium it asks for; the whole run takes about 17 minutes on one core and ends by itself.

The capture holds, for every timer tick of the music player (53.99 Hz), the picture on screen at the end of the
tick, the music position, the part and the frames shown so far; for every one of the 19,230 frames shown, the timer
value it was drawn with; every GUS register write and the GUS memory. Snapshots at the start of every part let a run
resume anywhere. Runs from the snapshots with the timer frozen and at 55.5 and 311 million instructions per second
(`re/fpscheck.py`) showed what depends on the frame rate: nothing moves without the timer except at sync events and
in part 1's trails; parts 7, 8 and 10 and the title mosaic give the same pictures at equal timer values at any
speed, parts 4, 6 and 9 nearly, while parts 1 and 3, and at low frame rates the tube and the zoom, do not.

## Decompiling

Seven agents decompiled the intro in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)): main, the
clock and the shared helpers (`core`), the textures and the pictures (`gfx`), the module builder and MXMPlay
(`music`), the 3D engine and Craft's texture mapper (`engine`), the four 3D parts (`parts3d`), Craft's grid filler
and the parts that use it (`grid`), and the three particle parts (`particles`). Each wrote a notes file with every
routine as pseudo-C with all its constants, and Python models in `re/models/` that reproduce what the original
computes, checked against the capture and the snapshots.

What came out:

- **One clock, integrated per frame.** The intro never waits for the vertical retrace and draws as fast as the CPU
  allows (26 to 725 frames per second in the capture). After every frame main reads the music player's timer and
  every part advances its variables by the time that passed, in a unit of 1/17043 of the timer chip's clock
  (1/70.0101 s: the authors took the chip for 1,193,010 Hz). Main reads the music position one player tick ahead of
  the sound and runs the events of a 98-entry sync table once per frame, before the part draws.
- **Per-frame state.** The particle parts change with the frame rate: how many dots are born per frame, a target
  count that truncates every frame, part 1's emitter that advances per birth, the trails that decay by 15/16 per
  frame, part 3's cloud that is magnified and darkened once per frame. One random sequence runs through parts 1, 6,
  3 and part 1's second run.
- **The music is built, not stored.** 0x414aee copies a 57,683-byte MXM module out of the image and appends 20
  synthesised 16-bit samples (12 drums, 8 tones) computed with the x87 set to truncate; with rounding to nearest
  211,193 of the 490,568 samples would come out different. The player is a build of MXMPlay whose timer, GUS update
  and player tick are JIZZ's instruction for instruction, with more effects and without others.
- **Brightness as logarithms.** The three texture maps and the two colour tables come from sines of the distance
  to the centres of repeating cells, stored as 8-bit levels 128 + K ln V. Craft's fillers add the light to the level
  and decode it with an exp table, so lighting a textured pixel is one add and one table read.
- **Craft's grid filler** evaluates a part's texture coordinates and light at 41 x 21 points and interpolates them
  over 8 x 8 blocks; the tube, the zoom and the tunnels are three functions fed to it. **His texture mapper** divides
  for perspective every 8 pixels and filters bilinearly with 4-bit weights.
- **Dune's radial blur** runs in place from the centre outwards, so every pixel adds a sample of pixels the pass has
  already brightened: one pass gives long streaks.
- **Quirks of the code**: part 10's voxel grid is laid out 6 x 6 x 6 while its loop runs to 10, so slots are
  shared and its neighbour test reads part 2's camera time past the end; part 9's neighbour test reads x + 2; event
  802 has a handler but no entry in the table; part 99 (a blue band) is never selected; a volume fade after the
  last row is dead code; the VESA library writes the request signature "VBE2" backwards, as "25BV".

## Porting

The framework, the unpacker, the running order and the music came first (`remake/src/`), with a second brief
([`re/PORT.md`](../re/PORT.md)) that fixed the conventions: music time as the clock, the original's t70 and every
part's clock derived from it, the part starts and clock origins taken from the capture, VGA coordinates on a 4:3
screen, colours as display-space floats with the log encoding decoded in float, how per-frame state is stepped at
the capture's frames and rebuilt after a seek. Placeholders showed every part with its captions at the right times
from the start. The music port, built on JIZZ's MXMPlay and the GF1 model of JIZZ and STARS with 16-bit voices,
matches the capture's GUS memory, register stream and music positions exactly (`remake/tools/test_music.cpp`).

Then three agents worked in parallel: the grid parts (Craft's filler as a per-pixel shader, the tube, the zoom, the
tunnels and the title mosaic), the 3D engine (the geosphere, matrices, culling, far cut and Craft's mapper as one
shader, checked against 28 draws dumped from the emulator with `remake/tools/enginetest`) and the particle parts;
the four 3D parts followed on the engine. The particle agent stepped the particles at the capture's own frame
times and checked them with `remake/tools/particlecheck.cpp`, which drives the original's 8-bit pipeline with the
remake's particle state and gets all 3,174 captured frames pixel for pixel. Each agent compared its parts with the
capture on side-by-side sheets (`remake/tools/compare.py`), checked that seeking gives the same frames and measured
the GPU time at 2560x1920.

A last pass checked the whole intro: contact sheets every 8 s in both art modes and at every part boundary, seeking
in both directions, consecutive frames at 60 fps in every part, the GPU time of every part on two GPUs, the start-up
time, the Linux and Windows builds, the music checker, the WAV and video output. It found nothing to change in the
pictures; `--bench` learned `--fps` (to measure at the steps of 60 fps playback, as DOPE's does). The numbers are in
[`remake/README.md`](../remake/README.md).

## The redraws

Aap's ten pictures (the four captions, the four brush-drawn glyphs, the title and the credits) were redrawn with
ChatGPT's image generation from the original pixels, scaled up at 4:3 so the generator sees the real pixels: the
texts grey on black, the glyphs on flat magenta that stands for transparency. The prompt asked for the same
composition, letter shapes, spacing and weight at a higher resolution, razor-sharp for the texts. ChatGPT refused
nothing; one caption needed a second attempt because the first was blurred. `remake/tools/align_art.py` fits each
redraw onto the original (scale and offset), then fits every word of a text and every line of the credits again on
its own, so each word sits on its pixels where the generator spaced the words a little differently, and turns the
glyphs' magenta into alpha. [`remake/art/README.md`](../remake/art/README.md) has the prompts, the attempts and the
residual of every picture.

## Decisions

The decisions taken while making the remake, so they can be revisited:

- **The 32-bit mode is the reference.** The original runs in a 32-bit VESA mode with four video pages, or in a
  15-bit mode that converts a 32-bit buffer in software; the capture and the remake follow the 32-bit mode. Rows a
  part does not write would show the page drawn four frames earlier; in the capture they are black at every part, and
  so they are in the remake.
- **Time base.** The clock is music time t = xmpGetTimer / 1,193,182 s, 0 at the player's second timer interrupt,
  when row 00:00 starts to sound (program time 0.72 s in the capture). The original's time t70 = timer / 17043 (the
  constant at 0x41a128, 1/70.0101 s, not 1/70) follows from it, and so does every part's clock.
- **Part starts and clocks.** A part starts at the music time of its event. Its clock, the sum of the dt the part
  has seen, starts where the capture's did: at the timer of the previous part's last frame, 0..17 ms before the
  event (`--cues` lists them). Part 1 starts with the image's dt of 6.0, and its second run continues the first
  run's clock (clock0 6.0 + 2114.1087), as the original's event 111 runs no set-up.
- **The capture's frame steps for the per-frame parts.** The particle parts (1, 6, 3) are stepped at the timer
  values the capture drew its frames with (`remake/src/scenes/particle_frames.h`, 3,430 values from `full.flips`),
  with main's dt and the x87's truncation, and their events act on the capture's frame. Their particles, counts and
  random numbers are then the capture's. Between two frames the dots move on with the part's clock and the trails
  and the cloud blend into the next frame; part 6's blinking and part 1's flash stay on their frames.
- **Capture-timed events and lags kept.** Where the capture's frame timing shapes the picture the remake uses the
  capture's values: part 2's camera position one frame (24.7 ms) behind its rotation and event 201's ramp from the
  frame before the event frame; part 5's direction flips (501) integrated from the capture's frame before each
  event, 10–15 ms before the row; part 4's pulse one frame behind and the switches of 401 and 403 from the capture's
  frames; part 9's axis switches (902) at the capture's timer values, which turn the block up to 5° differently
  from switches at the rows; part 7's motion frozen where the capture's freezes (t70 9956.198, the frame before
  28:00). Other events act at their music time (the original at up to one frame later).
- **The grid bugs kept.** Part 10's 6 x 6 x 6 field array written by a loop that runs to 10 (the bottom of the
  column repeats the top, 24 % of the cubes), its neighbour test reading part 10's time and part 2's camera time
  past the array, and part 9's x + 2 neighbour test shape what the original shows, so they stay.
- **Dune's radial blur in its own order on the GPU.** Order-free versions of the in-place blur match it only up to
  a level of about 0.8 and are off by up to 40 levels at the strongest pulses. The remake keeps the order: each
  pixel of the original's 320 x 200 grid reads the processed value of the footprint pixels the original's pass
  reaches before it and the input value of the others, and Jacobi iterations of that rule converge to the sequential
  pass (bit for bit in integer arithmetic, `tools/particlecheck.cpp`). In float, with continuous sample positions and
  a loss of half a level per step for the truncations, 100 iterations converge in every frame, 1.5 levels from the
  original on average. The blur stays on the original's grid (on a finer grid the recursion near the centre is not
  the original's) and its streaks are added to full-resolution stamps.
- **Texel and smooth texture styles.** The textures are evaluated from their formulas in two styles: the texel style
  is the original's 256 x 256 texels with the integer cell radius (square-ish cells with thin dark borders), the
  smooth style the continuous radius at four times the resolution (round spots). The grid parts use the texel
  style, which is what the original's filtered grid looks like; the 3D parts' maps use the smooth style (map C's
  stepped rings become round, about 5 % darker on average).
- **Log-domain filtering.** Textures are filtered in the log form, as the fillers filter their levels, and decoded
  after the light is added, with the clamp at white kept. Where a map is minified (part 8's fly-in, small cubes) the
  remake filters it to the mean of the decoded values, which the original's aliasing averages to.
- **3D: visibility.** A depth buffer orders the faces instead of the painter's sort into 4096 buckets (the same
  picture in every checked frame; the faces inside part 9's blocks, which only the sort shows, stay hidden). The far
  cut (faces with a mean depth beyond 6826.7 dropped) is kept: part 8's blob comes in face by face. The level-4
  geosphere with its integer vertices is kept, as a finer one changes the shading of part 7's bumps.
- **Continuous instead of truncated.** Texture levels, light, the bilinear sums, the decode, the tile colours, the
  feedback and part 3's darkening are continuous, with the truncations' mean loss subtracted where it shapes the
  picture (the trails end because each step loses 15/32 of a level).
- **Particles as sprites.** The 4 x 4 stamps are smooth sprites of the same footprint and integral at sub-pixel
  positions, clipped at the edges instead of dropped; the 8-bit buffers are float intensities coloured with the
  palettes' formula. Part 3's nearest-pixel magnification is filtered and its 4-tap blur a Gaussian of the same
  variance.
- **Feedback and seeking.** The trails (part 1) and the cloud (part 3) live on the GPU and are stepped while
  playing; after a jump they are rebuilt from the last 107 (64) frames, after which what came before weighs less
  than 1/1000.
- **Dropped**: the 8 x 8 blocks of the grid filler and their noise, perspective every 8 pixels, the one-pixel dot
  that the 0.001 guard in the tunnels' distance makes at a vanishing point, the clipper's re-interpolation of the
  shade.
- **The redraws** are shown by default, the original pixels with `--original-art`. The texts are white letters
  whose grey is their coverage, so over part 1's particles the title and the credits have no dark rim; the glyphs
  carry an alpha channel.
- **The start and the end.** The text-mode precalc (0.65 s in the emulator) is skipped: the remake starts with the
  music. It ends at 216.910 s, at the frame after interrupt 11713, where the capture's main stops the music.
- **Music: the card's reads.** The player's register writes are ported exactly; the volume it reads back before a
  ramp is answered by the card model (the voice's current volume), as on real hardware, where the capture's fake GUS
  answered with the last value written.
- **Music: 48 kHz voices.** The GF1 model evaluates the voices at the output rate (48 kHz) instead of the card's
  34,300 frames per second with 18 voices, keeping the card's pitch, ramps and timing.
- **Music: high-pass and level.** A 5 Hz one-pole high-pass stands for the card's AC-coupled line output, and the mix
  is normalised to a peak of −1 dBFS (the player's master volume 0x20 leaves each voice a quarter of full scale).

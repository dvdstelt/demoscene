# Making of the DOPE remake

DOPE was remade after 303, Paper and JIZZ, at the same time as STARS by NoooN, and like those two it went straight
to the executable. Claude Code agents did the work, directed by Dennis van der Stelt, who supplied the original
release. [../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working. As in
STARS, the remake shows high-resolution redraws of the hand-drawn pictures, and the original pixels on request.

## Unpacking

`DOPE.EXE` (584,133 bytes) is a chain of nine MZ executables in one file, each starting where the previous one's
size field ends:

- **The loader**: Tran's PMODE with a 32-bit program that loads `DOPE.MOD` (a 28-channel ProTracker module) into the
  Gravis UltraSound and keeps it playing from the GUS timer interrupt (the player's strings name Oskari Tammelin's
  ProTracker for GUS replay routine; DOPE.TXT credits Phantom of Sonic PC, "gus unit raped by jmagic"), and CDPI, a
  16-bit chain loader that runs the parts one after the other.
- **Seven parts**, plain real-mode programs, each packed with Rob Northen's ProPack (RNC method 1).
- **TERM**, which prints "■ TERM" in text mode.

`re/unrnc.py` splits the file and unpacks every part twice, by running the packer's own stub in the unicorn emulator
and with a Python RNC-1 decoder, and checks that both agree and both CRCs match. The remake does the same in C++
(`remake/src/rnc.cpp`, 18 ms) and checks every unpacked image by hash.

## Running it headless

`re/run_dope.py` runs the demo in unicorn without DOS: it answers the DOS, DPMI and mouse calls the loader and the
parts make and fakes the hardware the demo touches, the VGA (mode 13h and p1's tweaked unchained mode with its
split screen, the DAC, the retrace at 70.0863 Hz), the timer chip and a Gravis UltraSound with its timers and
memory. At 100 million instructions per emulated second it draws a frame at every retrace almost everywhere, as a
fast Pentium would. The capture holds, for every displayed frame, the visible screen and the palette, the music
position and the part, plus every GUS register write and the GUS memory; snapshots at the start of every part let a
run resume anywhere, and a run resumed from a snapshot reproduces the whole run bit for bit.

The capture has one property of the emulator that a real PC does not: three fades wait for the vertical-retrace bit
alone, and the emulator's bit changed about four times per frame, so those fades ran four times too fast there.

## Decompiling

Seven agents decompiled the demo in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)): the loader,
CDPI, the parts' framework and p1 (`core`), the music player (`music`), the 3D engine (`engine`), and the parts in
four groups (`p2`, `p3p5`, `p4`, `p6p7`). Each wrote a notes file with every routine as pseudo-C with all its
constants, and Python models in `re/models/` that reproduce what the original computes, checked against the capture
and the snapshots.

What came out:

- **No synchronisation.** The loader starts the music and CDPI runs the parts; each part installs a
  retrace-synchronised timer and counts retraces from its own start, and its script is a list of "run N retraces"
  calls. Nothing in the parts reads the music, so where a part is against the music depends on the machine (disk
  time, the parts' set-up).
- **One 3D engine in five copies.** p2, p3, p5, p6 and p7 carry the same engine (object list, matrix, vertex
  transform, back-face test, radix sort, two fillers) with small differences: the texel shift, where the texture
  coordinates come from, a negated env lookup, a pivot added twice. A bit-exact Python model of the whole frame
  build reproduces the engine's render target with 0 differing pixels in 19 frames.
- **p2's motion blur** is a 64 KB table of palette indices: every drawn frame each pixel becomes
  `T[old][new]`, an exponential moving average whose weight the script changes in steps of 4. The same table also
  fades the credits in and out and turns the label's old position black.
- **p4's fire** is a feedback through a polar swirl map with 15 levels and a 4x4 dither, mixed with the background
  by the palette; the landscape is a voxel ray-cast in 55 depth rows.
- **p6's shadows** are 32 pre-drawn 64x64 bit masks written into the cat's light map, one per 1/128 turn of the
  rings; **p7's light map** is a photograph with five light spots added per frame.
- **The music player** was ported to Python and checked against the capture's GUS register writes.

## Porting

The framework, the unpacker, the running order and the music came first (`remake/src/`), with a second brief
([`re/PORT.md`](../re/PORT.md)) that fixed the conventions: music time as the clock, every part counting retraces
from the start it has in the capture, VGA coordinates on a 4:3 screen, colours as display-space floats, fades as
colour multiplies, and how per-retrace state is evaluated in closed form and feedback buffers are rebuilt after a
seek. The music port matches the capture's GUS register stream, the GUS memory after the upload and the music
position after every timer interrupt exactly (`remake/tools/test_music.cpp`).

Then the 3D engine was ported to the GPU (`remake/src/dope/engine*`), with each copy's differences as options and a
checker against the bit-exact model (`remake/tools/enginetest`); p1 and p4 were ported at the same time. Three
agents ported the engine parts in parallel: p2; p3 and p5; p6 and p7. Each compared its parts with the capture on
side-by-side sheets (`remake/tools/compare.py`), checked that seeking gives the same frames and measured the GPU
time at 2560x1920.

A last pass changed four things and checked the whole demo:

- **p2's blur rate.** p2 blended four times per retrace, which rendered the 3D scene almost five times per displayed
  frame (15–22 ms at 2560x1920 on an integrated GPU). It now blends once per retrace with the original's weight, as
  the original does on a PC that draws every retrace, and the screen mixes the buffers after the two blends around
  the moment shown. The trails look the same next to the capture; p2 takes 4.4 ms per frame there (6.9 ms at the
  95th percentile).
- **The 16-bit back-face test.** The original's facing test multiplies screen coordinates in 16 bits, and on faces
  larger than about 181 pixels it overflows: from about 136 s to the end of p2 the near disc's big faces vanish and
  the "C" and its stand show through. The remake now does the same (below).
- **Frame timing.** p2's port had found that the capture shows its frame one retrace later than the brief said. A
  probe that renders the remake one or two retraces earlier and later than each capture frame and keeps the closest
  showed the labels of p3, p5, p6 and p7 exact to the retrace, p1 exact, and p4 one retrace ahead; p4 was moved back
  and the brief corrected.
- **The bump surfaces.** Reading the bump offsets filtered between texels turned the original's patchy relief into
  smooth veins. Side-by-side crops of the capture, filtered offsets and one offset per texel for every bump-mapped
  surface (p2's discs, p3's object, p5's glow, p6's fur, p7's tentacle) showed one offset per texel closer to the
  original everywhere it can be seen, so the engine now does that for all of them.

The whole-demo check: contact sheets every 8 s in both art modes, seeking in both directions, consecutive frames at
60 fps in every part, the GPU time of every part on two GPUs, the start-up time, the Linux and Windows builds, the
music checker, the WAV and video output. The numbers are in [`remake/README.md`](../remake/README.md).

## The redraws

The hand-drawn pictures (the eleven labels, the credits, "dope", the "Complex Media Labs" logo, the five
backgrounds and the three env maps) were redrawn with ChatGPT's image generation from the original pixels, scaled up
so the generator sees the real pixels, with a prompt that asks for the same composition, colours and text at a
higher resolution. `remake/tools/align_art.py` fits each redraw onto the original (scale and offset), gives the
colour pictures the original's colour distribution, and measures how well it matches;
[`remake/art/README.md`](../remake/art/README.md) has the prompts, the attempts and the residual of every picture.
The logo and the credits were redrawn in grey: the logo as a shade map that the remake colours through p1's two
palettes while they cross-fade, the credits as a coverage mask that the blur's text fade uses. Generated maps (p2's
env maps, the bump and fur maps, the Phong map, the photograph in p7's light map, p4's cloud and landscape maps)
keep the original's data.

## Decisions

The decisions taken while making the remake, so they can be revisited:

- **Time base.** The clock is music time: seconds since the GUS timer overflow after which the loader starts the
  player (program time 0.1104 s in the capture). The pictures never wait for the music, so each part starts where
  the capture starts it (no disk time, 100 million instructions per second) and counts retraces (70.0863 Hz) from
  there. On a real PC the pictures would drift later by the time it takes to read and unpack each part.
- **The three fades at VGA speed.** p5's hand-over to p6's picture and the exit fades of p6 and p7 wait for the
  retrace bit alone: one step per retrace on a VGA, about four in the emulator. The remake takes one retrace per
  step, so p6 starts 48 frames and p7 74 frames later against the music than in the capture, and the demo ends 266
  frames (3.8 s) later. Each part's timer calibration holds the music's interrupt and loses a tick there; those
  holds move with the later p6 and p7 starts, so the music loses its ticks where the remake's parts start.
- **What a frame shows.** Measured in the capture: p3, p5, p6 and p7 show the 3D state after r retraces and the
  label after r + 1; p2, whose frame copy blends 64,000 pixels first, shows the state after r − 1 and the label
  after r; p1 shows the scroll after r + 1 retraces (its handler changes the screen), p4 the state after r.
- **p2's blur rate.** One blend per retrace with the script's weight s / 256, the screen mixing the buffers after
  the blends before and after the moment shown. The original blends once per drawn frame: every retrace on a fast
  PC, 46–62 times per second in the capture's heaviest close-ups, where its trails fall apart into separate copies.
  The blend is a moving average of continuous ramp levels without the table's floor, so a still picture under heavy
  blur is not darkened. After a seek the blur is rebuilt from black far enough back that the history before it
  weighs less than 1/1000.
- **The 16-bit back-face test** is kept as a visible quirk of the original: the engine evaluates the original's
  integer test on the integer screen positions the original would compute and drops or draws the faces whose 16-bit
  products overflow as the original does; every other face is culled by its float winding. It is visible only in
  p2's close-ups; p5 and p6 have overflowing faces only far off screen.
- **One bump offset per texel**, as the fillers read the bump maps, with only the env read filtered and an edge
  about one pixel wide between texels: the patchy relief of p5's glow, p6's blocky fur and p3's rough surface.
  Where the texels are smaller than a pixel (the far torus, p7's tentacle) their offsets are averaged.
- **The shadow masks blended.** p6's 32 shadow masks are smoothed into distance fields and interpolated between
  neighbouring masks, so the ring shadows glide with soft edges instead of jumping every 4 retraces.
- **Continuous instead of stepped.** Motion, angles, labels, fades and palette mixes are evaluated between retraces;
  the 1/256 weights, the 64-level shade ramps and the 15 fire levels are continuous; p1's logo scrolls by half a
  pixel per retrace instead of a pixel every second retrace. The truncations are not imitated, so dark colours in
  fades and p2's blur are slightly brighter than in the capture, and p4 ends on the chequerboard at the brightness
  of its picture (the original leaves it one 6-bit step darker for p5).
- **Generated maps as formulas.** p3's Phong map is the radial gradient it was painted as, p4's swirl map its polar
  formula, p6's light map its radial formula, each fitted to or read from the stored map at start-up and evaluated
  continuously; p4's fire is a float recursion per output pixel, p4's landscape a continuous ray-cast, p7's light
  map is rendered at 4 x 4 samples per texel with the spots at continuous positions.
- **The redraws** are shown by default, the original pixels with `--original-art`; fades are colour multiplies, so
  they apply to both. The logo is a redrawn shade map coloured by p1's two palettes, so its colours cross-fade as in
  the original; the credits are a redrawn coverage mask.
- **Kept from the original**: the 0.75 vertical scale of the 3D engine and the per-object projection factor, p2's
  "C" sorted between the discs by the original's depth key, the black screen row 199 of p2, the label's black trail
  in the blur, the bump noise where p2's discs read the "C" mesh's bytes, p3's zig-zag bands where the normals wrap
  at 15 bits, the low-resolution rows of the bump maps.
- **Dropped as 8-bit artefacts**: the carries between packed 16-bit halves, the trivial reject of triangles with
  every corner left or right of the screen, the never-drawn column 319 of clipped spans, p7's bump-table carry.
- **The start and the end.** The loader's text-mode message while the module loads is skipped; the remake starts
  with the music. It ends where p7 sets text mode; the lines TERM and CDPI print afterwards are not shown.
- **The voices at the output rate.** The player's register writes are ported exactly. The GF1 voices are evaluated
  at the output rate (48 kHz for playback, `--wav` and `--video`; the music checker renders at 44.1 kHz) with the
  card's pitch, volume ramps and timing, instead of at the card's 22,050 frames per second with 28 voices, so the
  samples are interpolated more finely; the mix is normalised to 90 % of its peak.
- **The DC high-pass.** A 5 Hz high-pass stands for the card's AC-coupled line output: a voice parked on a non-zero
  sample byte would otherwise leave a constant offset in the mix.

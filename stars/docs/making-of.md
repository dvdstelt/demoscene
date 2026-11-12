# Making of the STARS remake

STARS was remade after 303, Paper and JIZZ, at the same time as DOPE by Complex, and like JIZZ it went straight to
the executable. Claude Code agents did the work, directed by Dennis van der Stelt, who supplied the original
release. [../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working. New
for STARS (and DOPE) are the redrawn pictures: the remake shows high-resolution redraws of RA's pixel art, and the
original pixels on request.

## Unpacking

`STARS.EXE` (1,224,140 bytes) is three things in one file:

- **Infiny Loader**, a small encrypting stub. Its first layers decrypt themselves, the next one runs through its own
  `int 1` / `int 3` handlers (a debugger that owns those vectors breaks it), and the last decrypts the program word
  by word and checks a checksum ("Infiny Loader -> file integrity error").
- **PKLITE 1.1x**, which decompresses the program: a 16-bit PMODE 3.07 kernel by Tran and the 32-bit demo, 0x7c0ab
  bytes in memory.
- **An overlay of 58 files** at the end of the file, 56 of them DIET-compressed: the music module, the pictures and
  palettes, the maps, the 3D objects and the tables. The program reads them by number through a directory at the
  very end of the file.

`re/unpack.py` runs the loader stub and PKLITE's decompressor in the unicorn emulator, twice at different load
segments, checks a Python reimplementation of both against them, and unpacks the overlay with a Python DIET
decompressor checked against the program's own routine. The remake has the same chain in C++
(`remake/src/unpack.cpp`, 15 ms) and checks the image and every file by hash.

## Running it headless

`re/run_stars.py` runs the unpacked program in unicorn without DOS or the PMODE kernel. It answers the DPMI and DOS
calls the demo makes (memory, descriptors, file reads on `STARS.EXE`, the clock) and fakes the hardware: the timer
chip, the VGA DAC and retrace, the DMA controllers and a Gravis UltraSound with its timers and DMA uploads (from the
JIZZ harness). The demo runs at 40 million instructions per emulated second, about a 486DX2/66, the machine the NFO
recommends. The whole demo takes about 12 minutes to emulate and ends by itself.

The capture holds, for every displayed frame (70.086 Hz), the 320x200 screen and the palette, the timer and music
ticks, the music position and the routine main is in, plus every GUS register write and the GUS memory. Snapshots at
the start of every part let a run resume anywhere. Runs at 20 and 80 million instructions per second, and runs with
the timer interrupt switched off, showed what depends on the frame rate: the 3D parts, the warp and the tunnel move
with the timer, while the particles, the dissolving woman and the face advance once per drawn frame.

The module's instrument names carry Groo's signature, "Groo/CNCD July 1995", and a message to an Olivier: this is
part 1, and the second part should follow without a break.

## Decompiling

Seven agents decompiled the demo in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)): the system and
main's script (`core`), LCA's MOD player (`music`), BARTi's 3D engine (`engine`) and four groups of parts. Each
wrote a notes file with every routine as pseudo-C with all its constants, and Python models that reproduce what the
original computes, checked against the capture and the snapshots.

What came out:

- **Three clocks.** The timer interrupt (70.06 Hz) drives the parts' animation, the vertical retrace (70.09 Hz)
  paces drawing and main's fades, and the music runs on the GUS's own timers: main's script waits for music
  positions, and the parts test them to switch phases. The player programs timer periods that make the song 1.2 %
  and 1.4 % faster than the module's nominal tempo.
- **One 3D engine for five parts.** BARTi's engine loads `B!` objects and the bee's `WLDD` world, morphs meshes,
  sorts faces by depth and fills them with affine scanline fillers that read 256x256 byte maps: environment maps
  indexed by the rotated vertex normal, light maps offset by a bump table and added to a texture, palette indices
  laid out as shade bands.
- **Per-frame effects.** The particles, the dissolve and the face are feedback effects on the screen buffer: zoom,
  blur and remap of the last frame. The dissolve's speed depends on the machine; the capture's 35 fps is the
  reference.
- **Quirks of the capture**: the bee's first frame after the cloud uses uninitialised memory as its palette, the
  star part draws with the camera distance the wall part left behind, the spike ball's path stalls on every 256th
  drawn frame (a byte counter), and the wall's fade-in stops one step short on a machine of that speed.

## Porting

The framework, the unpacker, the running order and the music came first (`remake/src/`), with a second brief
([`re/PORT.md`](../re/PORT.md)) that fixed the conventions: music time as the clock, VGA coordinates on a 4:3
screen, colours as display-space floats, fades as colour multiplies, and how per-frame state is stepped and
replayed after a seek. Then the 3D engine was ported to the GPU (`remake/src/stars/engine*`, checked against an
integer port of the original's transform with `remake/tools/enginetest`), and four agents ported the parts in
parallel: the intro, warp and tunnel; the particles, dissolve, face and picture sequences; the cloud, bee and spike
ball; the wall, star and torso. Each compared its parts with the capture on side-by-side sheets
(`remake/tools/compare.py`), checked that seeking gives the same frames, and measured the GPU time at 2560x1920.

The music port reproduces the capture's GUS register stream (144,716 writes), the GUS memory after the upload and
the music position after every one of the 16,871 player ticks exactly (`remake/tools/test_music.cpp`).

A last pass checked the whole demo: contact sheets every 8 s in both art modes and at every part boundary, seeking
in both directions, consecutive frames at 60 fps in every part, the GPU time of every part, the start-up time and
the Windows build. It changed two things: the 3D engine now interpolates the vertices' env coordinates as the
original does (the per-pixel version made the torso's ring and the wall's face dome darker than in the capture),
and the screen is black for the two frames between the spike ball and the warp, where the warp's set-up blacks the
palette. The numbers are in [`remake/README.md`](../remake/README.md).

## The redraws

RA's pictures (the title cards, the women, the poodle, the names and words, the flower texture, the 3D parts'
backgrounds, the ornament, the credits and the "23806 faces" sprite) were redrawn with ChatGPT's image generation
from the original pixels, scaled up so the generator sees the real pixels, with a prompt that asks for the same
composition, colours and text at a higher resolution. Some needed a second or third attempt (text that moved, a
missing signature, a wrong word). `remake/tools/align_art.py` fits each redraw onto the original (scale and offset)
and measures how well it matches; [`remake/art/README.md`](../remake/art/README.md) has the prompt, the attempts
and the residual of every picture. Picture 13, the third pose of the woman, was refused by the generator and keeps
its original pixels. The redraws are about 1450x1090 for a screen; `--original-art` shows the original pixels
instead.

## Decisions

The decisions taken while making the remake, so they can be revisited:

- **Time base.** The clock is music time: seconds since the player starts the GUS timers (program time 3.99 s in
  the capture). The music runs on the GUS crystal and main waits on music positions; the timer and the retrace are
  free-running and the parts only count them from their own start, so they are derived from music time: timer
  ticks at 70.0635 Hz, retraces at 70.0863 Hz, both from the part's start.
- **Part starts.** A part starts at the later of its music position and the moment main got there. Where the
  original's set-up routines take longer than the music (the bee, the wall and the two particle parts) the start is
  the frame where the capture's part begins, since set-up time depends on the CPU. Ends are the parts' own tests,
  on the tick.
- **Between parts.** The last frame stays where the original does not clear the screen (the cloud before the bee);
  elsewhere the screen is black, including the two frames after the spike ball, where the warp's set-up blacks the
  palette.
- **The start-up** (PMODE's messages, loading and the precalc, about 4 s) is skipped: the remake starts with the
  music.
- **Per-frame state rates.** What the original advances once per drawn frame runs at the capture's frame rate in
  that phase, blended between steps: the intro's band phases and card fades at 70.09 Hz (counted with the average
  number of drawn frames per card), the warp's oscillator and the tunnel's speed ramp at 35.04 Hz, the particles
  and the face at 70.09 Hz, the dissolve at 35.04 Hz.
- **Frame-rate dependent values from the capture.** Where the outcome depends on which drawn frame saw which tick,
  the remake takes the capture's: the cloud's and the wall's fades end at 63/64, the wall's crossfade starts at tick
  1080, the torso's rise stops at tick 158, the spike ball's path stalls for 2, 3 and 2 ticks, the bee's text blink
  follows the ticks the capture shows it in. The spike ball's DAC level (62 in the capture until tick 1604) is
  63/64 throughout.
- **The particles' words.** The random generator is replayed from its seed, and which drawn frame sees which timer
  tick follows the capture's frame timing (`CaptureClock`), so the words of file 58 appear on the same frames as in
  the emulator capture.
- **The dissolve's checkerboard.** The original shows its 18-bit RGB buffer as a checkerboard of (R, 0, B) and
  (0, G, 0) pixels in 256 colours; the remake shows the colour the eye mixes from it, half the light in each plane
  (0.73 of the display values), so it is about 1.46 times as bright as the capture averaged per pixel. Its 35 Hz
  steps are the capture's.
- **The face** runs its feedback in palette-index space at 4 times the resolution with nearest reads, because file
  35's remap tables decide how the trails fade; the colours are converted at the end and interpolated.
- **Feedback and seeking.** The feedback parts keep checkpoints every 48 or 64 steps and replay from the last one
  after a seek, so a frame is the same however it is reached.
- **The redraws and picture 13.** The redraws are shown by default, the original pixels with `--original-art`;
  fades are colour multiplies so they apply to both. Picture 13 keeps its original pixels in the intro, the warp
  and the dissolve. The dissolve stamps the redraws of 11 and 12, masked like the original's files 36 and 37.
- **The bee's background** (file 27) is a still of the cloud effect: with the redraws on, the cloud effect renders
  it at the offsets that reproduce it.
- **The bee's garbage frame.** The frame after the cloud shows uninitialised memory as a palette in the original;
  the remake shows the last cloud frame with its palette doubled, the evident intent.
- **Full frames for the cloud and the bee**: their undrawn rows 0, 198 and 199 (dark purple there) are drawn; the
  other 3D parts keep them black, as the original's frame copy does.
- **3D: env coordinates.** The vertices' env coordinates are interpolated across each face as the original's
  fillers do (perspective-correct, the short way round the map), not computed per pixel from the interpolated
  normal: on coarse meshes the per-vertex coordinates cut nearer the bright centre of the map, which is the look
  of the capture (the torso's ring, the wall's face dome), and the bee's zero normals stay at the map's centre.
- **3D: visibility.** A depth buffer orders the faces, except in the bee, whose objects overlap themselves and need
  the file order. Faces nearer than 2F to the eye are clipped at that plane instead of dropped whole; objects are
  still culled whole as the original does.
- **3D: the star's camera** uses F = 100, the value the wall part leaves behind (the star part never sets it).
- **The torso.** The reflection map is rendered at 1024x1024; the ring's env index is 0x60 + 31 v / 63 so that the
  filtered map stays on the red ramp.
- **Continuous instead of truncated.** Fades, light levels and palette products are continuous; the truncations
  are not imitated, so dark colours in fades and the 3D parts are slightly brighter than in the capture (up to one
  6-bit step).
- **Dropped races and tearing**: the warp's tearing and its race between the timer and the curve routine, the
  star's race that mixes two ticks' palettes.
- **The minute variant.** The original swaps two name cards and two credits on odd minutes; the remake shows the
  even-minute version, as in the capture, unless `--minute odd` or `--minute clock` is given.
- **Music: 48 kHz voices.** The GF1 model evaluates the voices at the output rate (48 kHz) instead of the card's
  19,293.75 Hz with 32 voices, keeping the card's pitch, ramps and timing.
- **Music: DC high-pass.** One-shot samples that end on a non-zero byte leave a voice at a constant level, which
  the card's AC-coupled output blocks; a 5 Hz one-pole high-pass does the same.
- **Music: level.** The player sets no master volume; the mix is normalised to 90 % of its peak. It stops at
  372.11 s, where main stops the music.

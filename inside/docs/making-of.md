# Making of the Inside remake

Inside was the seventh remake, and like the ones before it went straight to the executable. Claude Code agents did
the work, directed by me, Dennis van der Stelt; I supplied the original release and left the decisions to the
agents. [../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working.

## Unpacking

`INSIDE.EXE` is PMODE/W 1.22 with the demo's Watcom C program packed in PMODE/W's own "PMW1" format. `re/unpack.py`
runs the stub's self-decompression and then the extender's PMW1 decompressor on all six compressed streams in the
unicorn emulator, and checks an independent Python decompressor against it. The fixups (4,515 offsets and two
selectors) are applied as the extender does, at two different bases, to make sure every relocated dword is found.
The data is in `INSIDE.DAT`: one LZSS-compressed block that unpacks to 2.7 MB, with a directory of 93 files at its end (pictures,
palettes, shade and blend tables, 3D objects and one LightWave scene). The remake has both unpackers in C++
(`remake/src/unpack.cpp`) and checks the program image by hash.

## Running it headless

`re/run_inside.py` runs the unpacked program in unicorn without DOS or the extender. It answers the DOS, DPMI and
video calls the demo makes, and fakes the hardware it talks to: the VGA with its DAC and the retrace bit, the timer
chip in three modes, the interrupt controller, the DMA controllers and a Gravis UltraSound at port 0x220. The demo
loads DSMI's GUS driver from `SDIGUS.DSD` itself and calls into it, so the emulated card sees exactly the register
writes a real one would. The program runs at 150 million instructions per second, a fast Pentium as the README
asks for; the whole demo takes about 25 minutes. The capture holds one record per screen refresh, 13,081 of them,
with the screen, the palette, the music position and the timer counts, plus every GUS register write and
snapshots at every part to resume from.

The clock was the first thing to understand. Inside measures the screen's refresh at start-up and programs the
timer to interrupt four times per refresh, the first at the retrace. The music player runs in that interrupt, and
so do the palette steps, the screen copy and every part's tick routine: the whole demo runs on the refresh, 70.086
times per second. The main loop draws as fast as it can, so the frame rate is whatever the machine gives (48 frames
per second in the intro on the emulated Pentium, 62 under water), but the motion follows the refresh.

## Decompiling

Eleven agents decompiled the demo in parallel, one scope each, from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)):
the core (clock, palette, script), the music, the SurRender engine, and the eight parts. Each wrote a notes file with
every routine as pseudo-C with all its constants, and Python models that reproduce what the original computes,
checked against the capture and the snapshots: the flight paths, the camera matrices, the light fields, the ripple
of the water bit for bit, and the GUS register stream.

What came out:

- **A script table runs the demo.** Each 12-byte entry holds a music order, a row and a routine; after every frame
  the main loop runs the entries whose position the music has reached. The routines start and stop parts and fire
  the white flashes, the inverse flashes and the fades.
- **SurRender does all the 3D.** Hybrid's engine sorts the faces by depth (no z-buffer), lights the vertices with
  point lights and draws through six rasterizers. Translucency is table work: the pillars of the temple, the glass
  fish, the shadows and the flares look up a 256 × 256 table with the texel and the palette index already on screen.
  The billboards place sprites with the horizontal focal length in both directions, so they sit 1.2 times too far
  from the centre line vertically.
- **The flower part reads a LightWave scene** (`tunnel1.lws`) at start-up with its own parser and samples the
  motions with a cardinal spline.
- **Watcom's `rand()`** decides the credits' flight path (from the program's start), the flares of the temple and
  the light of the flower tube; the flight's init seeds it once.
- **Leftovers.** A sixth name picture, SANTTU, is loaded and never shown; a spiral unwrap of the twirl's picture is
  in the code and never called.

## Porting

The framework is the Paper remake's. The shared pieces came first:

- the unpackers and the clock, which counts in refreshes as the original does;
- the music: ports of DSMI's AMP 2.00 player and its GUS driver on a model of the GF1. Its 140,095 GUS register
  writes, the GUS memory and the music positions match the capture exactly (`remake/tools/test_music.cpp`);
- the palette fades and flashes, as continuous colour operations;
- SurRender, ported once: the transform, culling, lighting, sorting and clipping on the CPU, the rasterizers'
  table lookups in a shader. Drawn in the original's way (`INSIDE_ENGINE=original`: affine, nearest texels, no
  anti-aliasing) the logo part matched 99.97 % of the capture's pixels.

Then six agents ported the remaining parts at the same time. Each checked its work with side-by-side sheets against
the capture (`remake/tools/compare.py`), with pixel checks in the original's mode at 320 × 200, with checks that
seeking gives the same frames, and with timings at 2560 × 1920.

The tables that read the screen's index cannot be looked up on a true-colour screen. Each table row is fitted as a
blend, `out = s + b × background` per colour channel, and drawn with dual-source blending. The first version drew
into half-float targets, where those blends could go past white; the temple's overlapping flares then turned into
cyan and orange rings. The frame is now 16-bit normalised, which clamps every draw as the original's palette does,
and the fits are made within the range such a target can blend.

Two parts step a simulation once per drawn frame: the intro's drops of light and the water. Their motion depends on
the machine's frame rate, so the remake replays the frames of the capture there (the clock values each frame read,
measured from the capture) and blends between them.

## The redraws

Inside draws two kinds of hand-drawn picture as they are: the title "INSIDE" over the logo part and the five
names in the credits. Both were redrawn with ChatGPT's image generation from the original pixels.
`remake/tools/align_art.py` makes the upload and fits each redraw onto the original.

The title came out right at the first attempt. Each name picture is a level map that the credits add to the tunnel:
a flat grey star, the name, and a scanned photo of the group member's face. The faces are photos of real people and
are not redrawn: the uploads have them replaced by the star, and the remake draws the original's face pixels over
the redrawn star and letters. The fit maps each redraw exactly onto the original's three levels (black, star,
letters). The water part's background, a blurred photo of a room, was tried twice: one redraw was softer than the
original and the other moved its shapes, and both darkened one side, so it keeps its original pixels.
[`remake/art/README.md`](../remake/art/README.md) has the prompts, the attempts and the fits. ChatGPT refused
nothing. `--original-art` shows the original pixels.

## Decisions

For this remake I left the decisions to the agents. The main ones:

- **Clock.** The remake's time is the screen refresh, as a fraction, and every part steps its state exactly as the
  original does per refresh and blends within a refresh.
- **Part starts.** The original runs a script entry at the end of the first frame after the music reaches its row.
  The remake runs it on the row, as on an infinitely fast machine, so it can be up to a refresh ahead.
- **Frame-rate dependent state.** The intro's light and the water replay the capture's frames, blended at an even
  frame clock.
- **Blend tables** are fitted blends, clamped like the palette. The credits draw their names and water mask in the
  part itself, because those tables darken by subtraction.
- **Visibility.** The engine keeps the painter's order, which decides what the parts look like (the flower is drawn
  over the tube by its sort bias, the spiral in front of the fish). Only the flight's landscape gets a depth buffer,
  for its fog per pixel.
- **Stale pixels.** The original never clears the inside of the temple's pillars, which shows leftovers of older
  frames; the remake shows black there.
- **Sound.** The remake loses no timer interrupt (the emulated machine lost one in the credits), plays at 48 kHz and
  normalises the mix to −1 dBFS after the card's high-pass.

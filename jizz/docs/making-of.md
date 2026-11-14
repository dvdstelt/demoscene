# Making of the JIZZ remake

JIZZ was the third remake, after 303 and Paper, and it went straight to the executable: no version rebuilt from a
video. Claude Code agents did the work, directed by me, Dennis van der Stelt; I supplied the original release and
left the decisions to the agents for this one. [../../docs/how-it-was-made.md](../../docs/how-it-was-made.md)
describes the general way of working.

## Unpacking

`JIZZ.EXE` is a 32-bit program for Adam Seychell's DOS32 extender. The file is an MZ stub with the DOS32 kernel,
followed by an "Adam" executable whose program is compressed: 54,908 bytes in the file, 173,591 in memory.
`re/unadam.py` runs the kernel's own loader routine (decompress, relocate, clear the BSS) in the unicorn emulator,
twice at different load addresses to find every relocation, and checks a Python reimplementation against it. The
remake has the same unpacker in C++ (`remake/src/adam.cpp`) and checks the result by hash.

## Running it headless

`re/run_jizz.py` runs the unpacked program in unicorn without DOS or the extender: it answers the DOS32 and DOS
calls the intro makes and fakes the hardware it talks to:

- a VESA 2.0 BIOS with a 320x200 hicolor mode and a linear frame buffer (the intro picks 5:6:5 and flips between
  four pages);
- the timer chip, which MXMPlay reprograms on every interrupt;
- a Gravis UltraSound at port 0x240 (taken from the Paper harness).

The run records, for every timer tick, the visible page, the music position, the sync event, the frames drawn and
every GUS register write. The intro first spends 24 s in text mode generating its textures and samples, then plays
147.75 s of music and exits by itself. The emulator takes about ten minutes for the whole run. Runs at different
CPU speeds showed what depends on the frame rate.

The capture also found the "way to get the intro writing all the gfx and music to disk" that the DOC teases:
`JIZZ /pandora` writes the 16 textures as TGA files, `JIZZ /mp4` the module.

## Decompiling

Seven agents decompiled the intro in parallel, one scope each, from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)):
the shared code, the texture generator, the music, Nix's vector engine and three groups of parts. Each wrote a
notes file with every routine as pseudo-C with all its constants, and Python models that reproduce what the
original computes: tables in memory snapshots, textures, the music module, the GUS register stream, the camera
matrices and whole captured frames. Most models match the capture bit for bit.

What came out:

- **The textures are programs.** A byte-code interpreter with 39 ops (noise, clouds, sines, blobs, warps, blurs,
  emboss, colour ops) runs 16 scripts. The only bitmaps are two run-length coded greyscale pictures: the
  "~JIZZ~ TBL'97" lettering and Sjaak's logo. The "JacePeG cruncher" of the DOC is this generator.
- **Most of the music is synthesised.** Probe's "mp4 cruncher" stores the module's patterns and five samples; the
  other 32 instruments are rendered at start-up from 12 synthesiser records: saw, x^7 or noise oscillators through
  a swept resonant filter, with glides and echoes. The bytes depend on the x87 running at 24-bit precision.
- **MXMPlay is a cut-down build** that only handles the effects the song uses.
- **The 3D scenes are scripts too.** Nix's engine builds every object from a byte script (spheres, cones, tori,
  twists, copies), films it along a sine-based camera path and fills the faces with an environment map plus a
  highlight from the "spot" table, through colour tables that hold brightness, contrast and an inversion.
- **The "white flash" at 03:10 is an inversion** of the engine's colours, decaying over 30 frames, and the blob
  scene's "white" stretch is a negative. Several things run per drawn frame and not on the timer: the TBL part's
  fades and feedback, the inversion, the glow lines and rings of the ribbon and tunnel parts, and the start of the
  field lines.

## Porting

Agents ported the shared pieces first, each with its own files ([`re/PORT.md`](../re/PORT.md) is their brief):

- the core library (random generator, colour tables, fades, glow lines);
- the texture generator, exact to the byte at 256x256, and a float version that evaluates the scripts at
  1024x1024;
- the vector engine on the GPU;
- the music: the synthesiser, MXMPlay and a GF1 model.

Then three agents ported the parts on top of them. Each checked its work with side-by-side sheets against the
capture (`remake/tools/compare.py`), with checks that seeking gives the same frames, and with timings at
2560x1920.

The music port reproduces the module byte for byte and the capture's GUS register stream, GUS memory and music
positions exactly (`remake/tools/test_music.cpp`). The rounding of the synthesiser had to be rebuilt in portable
C++: every 24-bit result is computed as a double and rounded once, and products with double constants use an
exact product, so g++, MinGW and MSVC give the same bytes.

## Decisions

For this remake I left the decisions to the agents. The main ones:

- **Clock.** JIZZ animates from MXMPlay's timer, not from a fixed tick, so the remake's clock is the music time in
  seconds and every part evaluates the original's timer formulas continuously.
- **Part starts.** The original enters a part at the first frame after the music reaches its row. The remake
  starts on the row.
- **Frame-rate dependent state** runs at the frame rate the capture shows in that phase (70/3 fps for the TBL part,
  35 fps for most of the ribbon part), blended between steps.
- **The text-mode precalc screen** is skipped: the remake starts with the music.
- **Textures** are generated by the original's scripts at 1024x1024 where the float version matches the exact one
  (all but the unused map9), otherwise at 256x256 with filtering.
- **Visibility** uses a depth buffer instead of the painter's algorithm. It shows the true intersection where the
  tube's cones enter the ball.
- **The metaballs** are ray-marched as the exact implicit surface instead of the original's coarse polygonisation.
- **The TBL part** steps on the capture's own frame clock (three retraces per frame, four for 41 slow frames)
  instead of a flat 23.3 fps, so its colour pulse keeps the capture's phase.
- **Leftovers** from the TBL part's glow, which show for two or three frames at the start of the ribbon part in
  the original, are left out.
- **Volume.** How a real GUS clips 14 voices is unknown; the remake normalises the mix to 90% of its peak.

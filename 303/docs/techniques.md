# How 303 was made: the original's techniques

*303* by Acme won the PC demo competition at X'97 Takeover (Eindhoven, 4–6 April 1997), first of 13 entries with
209 points, ahead of Analogue's *Stasis* with 179
([results](https://files.scene.org/view/parties/1997/x-takeover97/results.txt)). It was made by:

- **Statix** (Alex Evans): code;
- **Vic**: music and vocals;
- **Aap**: graphics and design;
- **Harlequin**: the breakdancer in the video clips.

The NFO says it was finished ten minutes before the competition, four hours after the deadline, and that the last
four days cost two motherboards, a video capture card, an MPEG compressor and two hard disks. The invitation named
a Pentium 166 with 16 MB of RAM and a GUS PnP with 2 MB as the competition machine, allowed a demo at most 4 MB and
did not allow unpacking to the hard disk ([invitation](https://files.retropc.se/skene/PARTY/X97TAKE2.html)).

Statix's 64k intro *Paper* (1996) was hand-written assembly
([Evans, 2014](https://www.mediamolecule.com/blog/article/paper_alexs_64k_demo_from_1996)). 303 is mostly Watcom
C++ for the DOS/4GW extender, with assembly for the inner loops, and plays its music with the MIDAS Sound System
0.5.0 by Petteri Kangaslampi and Jarno Paananen. This page is based on our reverse engineering
([`../re/`](../re/README.md)). Addresses are linear addresses in the executable's image (code from 0x10000, data
from 0x50000).

## The executable still has its symbol table

`303.EXE` (414,153 bytes) ends with 45 KB of Watcom debug information: about 1,300 routine and variable names with
their addresses, and the source files: `E:\MUSTARD\main.cpp`, `demo3.cpp`, `demo4.cpp`, the assembler modules
`lzwasm.asm`, `asmutils.asm`, `demo2.asm`, `poly.asm`, `3dasm.asm` and `vocal.ASM`, and MIDAS under
`E:\midasnew\src\`. The project was called MUSTARD, as the start-up banner also says; the data files are MUSTARD,
GARLIC and KETCHUP, and the PAK header is `ALEX`. Our notes were written before we found these names; this page
uses them.

## Vocals in a tracker module

Vic's vocals and drum loops are 43 samples in slots 48–90 of a 30-channel PolyTracker module (`GARLIC.PAK`):
2,430,720 8-bit samples at 32 kHz, 76 seconds. Stored raw they would have made the release 5.4 MB instead of 3.6
MB, against the party's limit of 4 MB (4,194,304 bytes). `KETCHUP.PAK` holds them as one MPEG-1 Layer II stream (mono, 32 kHz, 64 kbit/s, 607,681 bytes); the module's
entries for them have no file name, and their offsets point into the decoded stream. In 1998 Statix said he wrote
the MPEG packers and the MIDAS extensions first and sent them to Vic to try out, and called 303 "a gigantic
experiment in vocals" ([interview](http://www.jerware.org/fanclub/spotlite.html)).

**The decoder.** Its routines (`0x116f0`–`0x12358`) have the names of the ISO MPEG audio reference decoder
(`decode_info`, `II_decode_bitalloc`, `SubBandSynthesis`, ...), and like that decoder it reads its bit-allocation
and window tables from files, here `bittab0`–`3.tab` and `window.tab` in `MUSTARD.PAK`. The synthesis filter, where
the time goes, is hand-written x87 assembly (`mpeg_asmproc_`, `0x1b2ec`/`0x1b4a7`), unrolled four multiply-adds at
a time with `fxch` between the loads and multiplies, the way the Pentium's pipelined FPU is kept busy. To output a
sample it adds 257.0 and reads byte 1 of the float's bits as the unsigned 8-bit sample, with byte 2 telling whether
to clip. The C code instead converts through Watcom's `__CHP` (`0x3235e`), which reloads the FPU control word
twice per call.

**Decoding in the background.** `runmpeg_` (`0x1259c`) decodes at most 768 samples per call. At start-up it runs
until a time budget ends: 15 s by default, measured on the BIOS tick counter, set with `-p`. The percentage shown
("Unpacking vocals") is of that time, not of the work. The rest is decoded during the demo, one call per drawn frame;
the help text warns that slow PCs need more time or the vocals crack.

**Through the GUS's memory.** On a Gravis UltraSound, MIDAS plays samples from the card's memory: at most 1 MB on a
classic GUS, 2 MB on the competition machine, against 0.7 MB of other instruments and 2.3 MB of vocals. Statix added
two entries to MIDAS's table of pattern commands (`0x5af38`, entries 34 and 35; `0x3f694`, `0x3f6c4`). They call
`togglevocalsample_` (`0x33394`), which puts a vocal sample on the card or removes it, so the song can load a sample
before it is needed and free it afterwards. MIDAS's GUS driver writes a sample byte by byte through the card's I/O
ports (`0x3c173`); patched (below), it queues the job instead (`scheduledownload_`, `0x33248`, eight jobs).
`doschedule_` (`0x332a4`, every frame) then uploads at most 368 bytes per elapsed retrace tick, about 25.8 KB/s,
with Statix's own port loop (`0x3ad22`). A DMA version (`0x3aca0`) is present but never switched on. With other
cards MIDAS mixes in software from main memory, and each vocal sample is handed over once decoded.

## Self-modifying code and other unusual code

We scanned every instruction for stores into the code, and every code address loaded into a register before a store
or string copy. Three places write to code. One is the Watcom runtime, which saves the data segment in a
code-segment variable at start-up (`0x34bba`). Two are Statix's:

| patched | written by | use |
|---|---|---|
| the destination row stride of the box blur `hblur_`, an `add edi, 0x140` immediate (`0x1c9c6`) | `setblurwid_` (`0x1c9d4`), called three times, always with 320 | the blur loop uses all seven registers, so the stride lives in the instruction; in 303 it never changes |
| the three upload loops (51, 51 and 50 bytes) in MIDAS's GUS `AddSample`, at `0x3c173` + 0x133, + 0x190, + 0x1d8 | `_hackmidas` (`0x3ad98`, `vocal.ASM`), at the end of the module loader | each loop is overwritten with a 51-byte stub (`0x3ae05`) that calls `scheduledownload_` and advances the registers as the loop would have, padded with `nop`s |

The stub is a template: its `mov esi, 0x12345678` placeholder is first patched with the hook's address, then copied
into the driver. `_hackmidas` also finds three of the driver's own variables (its sample counter, a busy flag and
the card's port) by reading the operands of its instructions at offsets 0x28, 0x12b and 0x136, so the patch only
fits this exact build of the driver. The stride patch is the idiom of Paper's glyph blitter, whose stride is also an
`add edi` immediate ([Paper's techniques](../../paper/docs/techniques.md)). Beyond these two places, 303's own code
does not modify itself.

Also unusual: effects that are linked in but never called, such as `mtvfx_` (which plays a `film.lzw` that is not
in the release), `preview_` and `test3d_`. Statix said in 1998 that the exe held code for six or seven effects that
did not make it.

## Video clips

Four clips: `HARL.LZW` (Harlequin, 200 frames), `VS.LZW` (147), `ALEX.LZW` (43) and `PARANOID.LZW` (45,
greyscale). A frame is 160×96 luma plus two 80×48 chroma planes (YIQ); the routines call the codec "jpeg".

- **Coding.** Every frame stands alone. Each 4×4 block is 16 bytes: a DC value stored as the difference from the
  previous block, and 15 AC coefficients in zigzag order, quantised more coarsely at high frequencies. A frame is
  always 23,040 bytes, and the clip is then compressed with LZW (Nelson's LZW15V, `lzwasm.asm`). In `HARL.LZW`, 93%
  of the AC bytes are zero, and LZW cuts the clips about 10:1: the four are 435 frames in 1,033,254 bytes.
- **Decoding.** The LZW stream is unpacked one frame at a time (`readflzw_`), never the whole clip. `unjpeg_`
  (`0x1b648`) dequantises and does the 4×4 inverse DCT with table lookups only (`_dctmultab`, first pass in 16 bits
  through `add`/`adc`); it has no multiply instruction.
- **Display.** `drawtruecol_` (`0x1b819`) shows the frame at twice the size with a 4×4 ordered dither into a fixed
  palette of 16 luma × 4 I × 4 Q levels (`0x13244`). The tick callback `jpegfp` (`0x30990`) decodes a new frame
  every 8 ticks (8.76 fps) in the dance and every 6 (11.68 fps) for Harlequin; in between the same frame is
  dithered again with the lyrics on top.

A 2005 pouët comment asks whether 303 was the first PC demo with video ([pouët](https://www.pouet.net/prod.php?which=44));
we found no answer.

## Effects on a 256-colour screen

Everything is drawn by the CPU into 320×200 buffers with 256 colours and copied to VGA without waiting for the
retrace.

- **Grids.** Many effects compute rays only at the 41×26 corners of 8×8 blocks (1,066 rays instead of 64,000) and
  interpolate texture position and light across each block. The code calls it a pin map (`drawpinmap_`, `0x1cddc`,
  with `ns`, `add` and `tr` variants).
- **Tables.** A fade table maps each colour to 64 levels from black through itself to white; a ghost table blends
  two colours. The 13 tables come precomputed in `MUSTARD.PAK`; the generators (`computefade_`, `computeghost_`)
  are in the exe but unused.
- **Palette and feedback.** `updatefade_` (`0x10124`) uploads the palette every tick, with fades, flashes, a
  brightness boost that decays 2% per tick, and a negative strobe. The intro mixes each frame 1/4 new with 3/4 of
  the last; the closing icons keep 0.65 of it. That runs per drawn frame, so the trails depend on the PC's speed.
- **Metaballs.** `march` (`0x25d00`) starts on each ball's surface and follows it from cell to cell, with a hash
  table of 8,192 visited cells, so only surface cells are evaluated; the triangles are environment-mapped and sorted
  into 100 depth buckets. `raysphere_` (`0x27958`) ray-traces spheres every 8th pixel and interpolates between.
- **Wireframes.** Triangles are filled black back to front (`blackpoly_`, `0x1f88b`), then their edges are drawn as
  glowing lines (`blurline_`, `0x2acf0`). The 1998 interviewer noted that *Square* carried this look over.
- **Polygon fillers.** `poly.asm` has eight fillers, each split into six routines. `polytex` builds, once per
  triangle, a table of texel offsets along a span, and the pixel loop just reads it.

## Motion from springs and a 70 Hz clock

MIDAS's timer is synchronised to the vertical retrace (`tmrSyncScr`, `0x374d0`) and calls `_midastimer` (`0x1bc01`)
about 70 times a second. That tick runs the palette, refreshes the music position (`_playpos`, `0x9b844` = 2 ×
(order × 64 + row) − 2) and calls the parts' callbacks; the script moves on at fixed music positions. `-v` turns the
retrace synchronisation off.

Statix said 303 has "no beziers or stored paths", only masses and springs. The code agrees: `elasticam_`
(`0x27444`) moves the camera once per tick with a damped spring (velocity × 0.79 + 0.002 × distance) towards goals
given by sine formulas. Many choices come from `rand()` (`0x34974`), called per drawn frame, so every run differs.

## Not verified

- **The first song demo.** The NFO asks whether it is one. The 1998 interviewer says 303 pioneered vocals in demos;
  Wikipedia
  ([2016 version](https://bafybeiemxf5abjwjbikoz4mc3a3dla6ual3jsgpdr4cjr3oz3evfyavhwq.ipfs.4everland.io/wiki/Alex_Evans_(video_game_developer).html))
  calls it one of the first, with the vocals "compressed into a standard module". pouët commenters name
  earlier, shorter singing: *Zero Gravity* (The Party 1996), Purple's *Skumle Planer* (1995) and a Hirmu demo. We
  can add that the vocals are a standard MPEG-1 Layer II file next to the module, not inside it.
- **Why the uploads are spread out.** The chunking is fact; that it avoids stalls on the GUS ports is our reading.
- **How the song uses the load and free commands.** We did not check the pattern data.
- **Which version this is.** In 1998 a "slightly enhanced" 303 was on Statix's home page. We assume scene.org holds
  the party version (its NFO is dated 6 April 1997).
- **demozoo** could not be read (bot check).

## Sources

- Our notes: [BRIEF.md](../re/BRIEF.md), [notes-aap.md](../re/notes-aap.md), [notes-mid1.md](../re/notes-mid1.md),
  [notes-mid2.md](../re/notes-mid2.md), [notes-mid3.md](../re/notes-mid3.md), [notes-late.md](../re/notes-late.md),
  [making-of.md](making-of.md); the executable, read with [`re303.py`](../remake/tools/re303.py) and its debug
  information
- The release and its NFO: https://files.scene.org/view/demos/groups/acme/demos/acme_303.zip
- pouët.net, 303 (prod page and comments): https://www.pouet.net/prod.php?which=44
- X'97 Takeover results: https://files.scene.org/view/parties/1997/x-takeover97/results.txt
- X'97 Takeover invitation, version 2.0: https://files.retropc.se/skene/PARTY/X97TAKE2.html
- Jeremy Williams, interview with Statix, PC Demo Fan Club, 1998: http://www.jerware.org/fanclub/spotlite.html
- Alex Evans, "Paper: Alex's 64k demo from 1996", Media Molecule blog, 2014:
  https://www.mediamolecule.com/blog/article/paper_alexs_64k_demo_from_1996
- Wikipedia, "Alex Evans (video game developer)", version of November 2016 (IPFS mirror):
  https://bafybeiemxf5abjwjbikoz4mc3a3dla6ual3jsgpdr4cjr3oz3evfyavhwq.ipfs.4everland.io/wiki/Alex_Evans_(video_game_developer).html

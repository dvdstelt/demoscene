# How clone meets clone was done: the techniques of 1998

*clone meets clone* won the PC 64k intro competition at Takeover '98 in Eindhoven (15–17 May 1998) with 395 points,
ahead of Analogue's *Mindflow* with 257 ([results][dzparty]). It was made by Echo of the Danish group Fudge and Aap
of the Dutch group Acme, with music by Simon; demozoo lists it as the second intro of 1998 in the Fleur 3 charts of
May 1999 ([demozoo][dz]), and pouët records it as a scene.org viewing tip of 1998 ([pouët][pouet]). Its INFO file
asks for a 133 MHz Pentium or an AMD K6 and a Gravis UltraSound with 1 MB of memory.

How it works comes from the reverse engineering in this repository ([`../re/`](../re/); addresses are linear
addresses of the program loaded at 0x400000). What we could not check is listed at the end.

## 64 kilobytes, unpacked by its own loader

`CLONE.EXE` is 63,994 bytes. It starts with Daredevil and Tran's PMODE/W v1.23, which loads and runs an LE program
of a single page, named "veloader" in its header. The veloader opens its own file, finds a "VE" header behind
itself and unpacks the real program: LZ77 with a 4 KB window and canonical Huffman codes, its distance and length
tables Deflate's cut short, 49,225 bytes of stream for a 164,972-byte image, then 2,570 relocations from a packed
list of deltas and 12 MB of zeroed memory behind the image ([`../re/NOTES.md`](../re/NOTES.md)). This is not PMODE/W's
own compressor: PMODE/W just runs the LE, and the LE is the unpacker. The program inside is Watcom C with
hand-written assembler for the player, the fillers and the blits, and no C library; everything it allocates comes
from one 10 MB heap in that block, in a fixed order.

## A soundtrack computed at start-up

While the title is on the text screen, `samples [....]` fills with dots: 0x414aee copies a 57,683-byte MXM module
(Simon's 18-channel song with its 63 patterns and one 4,282-byte 8-bit sample) out of the program and appends 20
16-bit samples it computes, 981 KB in all ([`../re/notes-music.md`](../re/notes-music.md)): 12 drums from a sine
whose frequency falls by a step that itself changes by a constant (kicks, toms, clicks), and 8 tones from a sine
whose period is modulated by another sine, times a slow envelope (bass, pads, leads, a sweep). The x87 runs with
truncation as its rounding mode, and the samples depend on it: with rounding to nearest 211,193 of the 490,568
samples come out different. The stored module's last byte is the first instruction byte of the drum synthesiser
that follows it in the program: the copy runs one byte into the code, and the instrument never plays that byte.

The song is played from the middle of these samples: the tonal phrases start their 65,536-sample tones with 90
different sample offsets. Uploaded to the GUS the samples fill 985,424 bytes of its memory, which is why the INFO
asks for a card with 1 MB. The player is Niklas Beisert's MXMPlay ("cubic tiny gus xm player v1.4"), the same
family as the one in JIZZ (Wired '97): its timer, card update and tick are JIZZ's instruction for instruction, with
a different set of effects (this one has arpeggio, portamento, vibrato and retrigger, but no pattern break). With 18
voices the card mixes at 34,300 Hz.

## True colour through VESA, without a retrace

Submissive's VESA library looks for a 320x200 mode with 32 bits per pixel and a linear frame buffer, and falls back
to 15 bits ([`../re/notes-core.md`](../re/notes-core.md)). In 32 bits the intro draws into one of four pages of video
memory and shows it at once through the VBE display start; in 15 bits it draws into a 32-bit buffer and converts it
to the screen in software, clearing the buffer as it reads. It never waits for the vertical retrace (and its VBE
request writes the signature "VBE2" backwards, as "25BV"). The screen has no palette: every pixel is written as 8
bits per channel, and the particle parts colour their 8-bit buffers through 256-entry tables before they write
them.

## Time without a frame rate

The intro draws as fast as the CPU allows: in the emulator at 150 million instructions per second, from 26 frames
a second in the last particle storm to 725 in the title mosaic ([`../re/PARTS.md`](../re/PARTS.md)). After every
frame main reads the music player's timer and every part advances its variables by the time that passed, so motion
keeps its speed on any machine. Its time unit is 1/17043 of the timer chip's clock: 1,193,010 / 70, a slightly
wrong clock rate that makes the "1/70 s" 0.014 % short. A table of 98 music positions drives the parts and their
events (the camera's turns, the direction changes, the light pulses), checked once per frame before the part draws.

Some of it still depends on the speed: the particle parts give birth to dots per frame and decay their trails by
15/16 per frame, so on a slower machine part 1's trails last longer (0.31 s at 52 frames a second, 0.62 s at 26)
and its emitter runs faster along its curve.

## Textures stored as logarithms

The "textures [....]" bar computes three 256x256 maps for the 3D objects and two colour tables for the grid parts
([`../re/notes-gfx.md`](../re/notes-gfx.md)): the brightness of each texel is a cosine of its whole-pixel distance to
the centre of a repeating 8, 16 or 32-texel cell, with a colour drift from two sines over the texture. Each channel
is stored as a logarithm, 128 + K ln(value). The fillers add the light to that level and read the colour from an
exponential table, so lighting a textured pixel costs one add and one table read, and the table's clamp at 255 is
what turns lit spots white.

## Craft's grid filler

The tube, the rotating zoom and the tunnels are one routine, Craft's grid filler (0x402970,
[`../re/notes-grid.md`](../re/notes-grid.md)). A part computes three numbers, the texture coordinates and the light,
at 41 x 21 points every 8 pixels; the filler interpolates them over each 8 x 8 block, reads the texture with a
bilinear filter and decodes the colour. The tube casts a ray per point into a flat elliptic cylinder, with each
row of points turned a little further; the zoom varies its scale over the screen as a product of sines; the
tunnels intersect three cylinders. Only the per-point function changes.

The bilinear filter does three channels in one go on the FPU: the filler spreads a texel's red, green and blue into
the mantissa of a double (red at bit 16, green at bit 32, blue at bit 0, under the exponent of 1.0), multiplies four
such doubles by their weights, adds them and stores the sum, whose bytes are then the three filtered channels.
Where two neighbouring points lie more than half the texture apart, the block interpolates the short way round,
which shows as block noise at the far ends of the tunnels and in the zoom's strongest magnification.

A pouët commenter on the Win32 port thought the textures looked bilinearly filtered (xernobyl, 2007,
[pouët][pouetwin]): they are, in both of Craft's fillers, with 16 steps between texels.

## Craft's texture mapper and the 3D parts

The four 3D parts share an engine with a perspective-correct texture mapper (0x415127,
[`../re/notes-engine.md`](../re/notes-engine.md)). It divides for perspective every 8 pixels, and while the FPU works
on the next block's division the integer unit draws the current one; the spans enter an 8-times unrolled loop in
the middle (a jump table, Duff's device). The texel is filtered with the same mantissa trick, and the shade is added
to the log levels before the exponential table. Faces are sorted into 4,096 depth buckets and drawn back to front;
faces beyond a depth of 6,826 fall off the end of the table, so part 8's blob flies in face by face.

The blobs of parts 7 and 8 are a geosphere: an octahedron subdivided four times into 2,048 faces, deformed every
frame and lit per vertex from normals that the engine recomputes from the faces. Parts 9 and 10 are metaballs: three
blobs on Lissajous paths sampled on a small voxel grid (6 x 6 x 6 and 4 x 10 x 4), each full cell drawn as a cube
whose faces are only drawn where the neighbour is empty ([`../re/notes-parts3d.md`](../re/notes-parts3d.md)). Part
10's field array is declared 6 x 6 x 6 while its loop runs to 10 in one axis, so the bottom of the column shares
slots with the top and the neighbour test reads past the array into other parts' variables; part 9's neighbour test
looks two cells over instead of one. Both bugs shape the columns and blocks on screen.

## Particles and Dune's radial blur

Parts 1, 6 and 3 draw into 8-bit intensity buffers: thousands of particles stamped as 4 x 4 blobs, the buffer then
coloured through 256-entry palettes (part 1's darken towards the top and bottom of the screen)
([`../re/notes-particles.md`](../re/notes-particles.md)). Part 1 keeps a second buffer that each frame takes 15/16 of
itself and 1/16 of the new frame, which gives the trails. Part 3 blurs its buffer, magnifies it 2.048 times about the
centre and darkens it to 3/4 every frame, so each dot blows up into a soft cloud.

The radial blur, credited to Dune, adds to each pixel a part (the level) of a sample taken 2.5 % closer to the
centre. It runs in place
from the centre outwards, so the sample has already been brightened by the pass: one pass gives a streak that runs
outwards from every bright point, as long as the level allows (short at 0.6, to the screen's edge near 1). Part 6
swings the vertical part of the step with a sine, and when it passes zero the streaks become horizontal lines across
the screen.

## Pictures

The only stored pictures are ten small 8-bit images with 16-level grey palettes: four captions and the title in a
Helvetica-like typeface with anti-aliased edges, the credits, and four brush-drawn glyphs that look like Chinese
characters, each shown next to its caption. Everything else, the tile mosaics included, is generated.

## Left-overs

- Main has a volume fade after order 64, row 32, past the end of the song's 61 orders: dead code.
- A part 99, black with a blue band, has a case in main's part switch but no event selects it.
- Event 802 has a handler (part 8's deformation would change) but no entry in the sync table, and event 402 sets a
  variable that nothing reads.
- Two of the 63 patterns (23 and 61) are never played; the other 61 are each played once.

## Not verified

- **"segment bss"**: the additional credit for Unreal. The loader zeroes 12 MB of BSS behind the program, which may
  be what it refers to; nothing in the code names it. What Assa's "network solutions" were is not known either.
- **Who wrote what**: the texture mapper and grid filler are Craft's and the blur Dune's by the INFO, not by
  anything in the code.
- **Speed on a real 133 MHz Pentium.** The frame rates above come from an emulator that does not charge for video
  memory writes or port I/O; a real machine would be somewhat slower, and parts 1 and 3 would look different.
- **Pouët claims:** that some glyphs are Chinese characters upside down (Frequent, 2011); that the music sounds
  different on a later GUS with more memory (asasel, 2002); that the soundtrack repeats (krabob 2006, ileaardbei
  2024: the song plays 61 different patterns, so the repetition is in the phrases). The soundtrack is also listed as
  "Clown Meets Clown" ([demozoo][dz]).
- **The Win32 port** (Echo, 2001, with *Kill the Clone* and *Alien Sex Clone*, [Hugi #23][hugi23]) was not examined;
  pouët commenters give its size as 700–800 KB.

## Sources

Reverse engineering in this repository: [`../re/NOTES.md`](../re/NOTES.md), [`../re/PARTS.md`](../re/PARTS.md),
[`../re/notes-core.md`](../re/notes-core.md), [`../re/notes-music.md`](../re/notes-music.md),
[`../re/notes-gfx.md`](../re/notes-gfx.md), [`../re/notes-grid.md`](../re/notes-grid.md),
[`../re/notes-engine.md`](../re/notes-engine.md), [`../re/notes-parts3d.md`](../re/notes-parts3d.md),
[`../re/notes-particles.md`](../re/notes-particles.md), [making-of.md](making-of.md),
[`../remake/README.md`](../remake/README.md), and `INFO` from the release.

The scene record:

- [pouët: clone meets clone][pouet] (comments 2000–2026) and [Clone meets Clone win32][pouetwin] (2001–2025)
- [demozoo: Clone Meets Clone][dz], [Takeover 1998 results][dzparty]
- [Hugi #23: scene news][hugi23]

[pouet]: https://www.pouet.net/prod.php?which=915
[pouetwin]: https://www.pouet.net/prod.php?which=2018
[dz]: https://demozoo.org/productions/41197/
[dzparty]: https://demozoo.org/parties/995/#competition_3923
[hugi23]: https://hugi.scene.org/online/hugi23/scnews23.htm

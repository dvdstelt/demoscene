# How STARS was done: the techniques of 1995

*STARS: Wonders of the World* won the PC demo competition at Assembly '95 with 3,646 points, more than twice the
1,482 of the runner-up, Psychic Link's *Juice* ([DemoNews #99][dn99], [results][pouetasm]); a party report says a
crowd gathered whenever NoooN showed it in the hall ([DemoNews #100][dn100]). It was the French group's second demo,
and the NFO calls it a "5 days production at KARL's place". It needs a 386 and recommends a 486DX2/66 with a VLB or
PCI graphics card and a Gravis UltraSound.

How it works comes from the reverse engineering in this repository ([`../re/`](../re/); addresses are offsets in
the unpacked program image). What we could not check is listed at the end.

## One file, three layers

`STARS.EXE` starts with a stub that calls itself "Infiny Loader". It decrypts itself in two layers; the second points
the single-step and breakpoint vectors (`int 1`, `int 3`) into the stub, masks the timer and keyboard interrupts and
decrypts the program one word per `int 3` call, so a debugger that owns those vectors breaks it. A checksum stops a
modified copy with "file integrity error". Inside is a PKLITE executable holding Tran's PMODE 3.07 and the 32-bit demo
([`../re/NOTES.md`](../re/NOTES.md)). The data is an overlay appended to the file: 58 files, 56 DIET-compressed,
which the demo reads by number from its own EXE (`0x4a9f3`), refusing to run from a floppy.

## Protected mode and three clocks

PMODE gives the demo flat 32-bit memory under raw, XMS, VCPI or DPMI; the demo takes one 2.9 MB block and runs KARL's
own allocator in it ([`../re/notes-core.md`](../re/notes-core.md)). Every interrupt handler also gets a real-mode
callback (`0x4a620`), so animation and music keep running while DOS reads the file.

The timer chip is set to 70.06 Hz (`0x4abd4`) and steps each part's animation, the retrace paces drawing and fades,
and the music runs on the sound card's own timer; the main script (`0x4d`) starts each part at a music position.
Because motion follows the timer, a slower PC shows fewer frames, not slower motion: in our emulator, at about the
speed of a 486DX2/66, the 3D parts draw 22–43 frames a second, the cloud, particles and face 70. The screen is plain
mode 13h without Mode X tricks: parts draw into buffers and copy them to video memory, the cheaper way on VLB and PCI
cards ([PC Demos FAQ][faq], 5.2).

## LCA's player on the Gravis UltraSound

LCA's MOD-Player uploads the 16-channel module's 480 KB of samples into the card's memory by DMA (`0x1a1b0`), then
frees the module ([`../re/notes-music.md`](../re/notes-music.md)). Each channel has two GF1 voices: a note starts on
one while the other fades out over 2.7 ms, so there are no clicks. The player's tick is the card's timer interrupt
(`0x1abb0`), so the music and the whole running order keep time on the card's crystal. The final release also plays
on Sound Blaster cards with a software mixer, but the NFO calls them "unsuitable for demos" and warns the demo slows
down: the mixing a GUS does in hardware then costs CPU time.

## BARTi's 3D engine

The engine (`0x7871c..0x7a20c`) is integer-only: fixed-point matrices, 12-bit angles, one divide per vertex, and the
matrix patched into the transform loop's instructions (`0x79b16`) ([`../re/notes-engine.md`](../re/notes-engine.md)).
It culls back faces, sorts faces by depth with a quicksort and draws back to front. Every 3D object in STARS is
environment-mapped: the rotated vertex normal indexes a 256x256 map with a highlight. Mode 15 (`0x51593`) also adds a
texture and shifts the lookup by a bump map, which embosses the spike ball and the wall. The palettes are laid out in
bands, one entry per light level and texture value, so a single byte add gives a lit, textured colour in 256 colours.

DemoNews's "What's Hot" list that August had phong shading and environment mapping in, Gouraud shading and texture
mapping out ([#100][dn100]). On [pouët][pouet], Scali (2002) remembered the engine as fast and accurate on a 486.

## The bee and its "23806 faces"

During the bee a caption, "23806 faces", blinks in the corner. File 28 holds the bee as 68 objects with 5,595 faces
([`../re/notes-parts3d-a.md`](../re/notes-parts3d-a.md)). The engine sorts the 68 parts by their centres but draws
each part's faces unsorted, in file order (`[0x582ac] = 1`). The wings, antennae and mouth move through four poses,
the camera through nine keys with straight-line interpolation (`0x121b4`), and the highlights move because the
environment angles are offset, not because the bee turns.

The [PC Demos FAQ][faq] (11.6) asked whether there really were 23,800 faces; a reader's answer called the bee
"optimised to hell" and guessed: no hidden-face test, positions computed every eighth frame, no divides, wings as
sprites. The code culls back faces, divides per vertex, animates the wings in 3D and interpolates the camera from the
timer ticks; the file holds less than a quarter of the claimed faces. On pouët, winden (2005) praised "great fakes on
the code (esp. the bee :))", and roudoudou (2023) said the polygon counts betray the text. Optimus (2001) remembered the
bee as the first 3D object this complex he had seen.

## A face in the wall, a chrome star

The wall is a 23x23 grid whose vertices and normals morph between a flat surface and eight face reliefs, stepped by
the timer (`0x78fda`) and blended once per frame (`0x78ffe`). Later its texture swings between a stone frieze and
Celtic knots in eight interpolated steps (`0xa5c5`) ([`../re/notes-parts3d-b.md`](../re/notes-parts3d-b.md)).
The face pushing out of the plane is what pouët commenters mention again and again (phred and Preacher 2004, Alain
2007, venex 2020). The chrome star morphs between three spike shapes; the row of small shapes behind it is 64 frames
rendered at start-up (`0x16945`) and kept as 1.2 MB of run-length sprites.

## A mirror that reflects live

Once the mirror has flown into the torso part, every frame copies a painted pink sphere (file 44) into a 256x256
buffer and renders the torso and ring into it, mirrored (`0x173a1`). The mirror object is then environment-mapped
with that buffer, so it reflects the moving torso: rendering to a texture, in today's terms. The ring also lights the
torso: before it is drawn, 16 rows of its light map, placed by the ring's height, are shifted to a blue ramp, and
shifted back afterwards (`0x17459`).

## Two tunnels

The cloud tunnel (`0x126ae`) reads each pixel's angle and depth from a table built by plotting points around 199
circles, so no square roots or arctangents are computed. Each pixel reads one 4-bit cloud texture twice at two moving
offsets, one copy stored shifted by 4 bits; the sum is `layer1 + 16 × layer2`, and a 16x16 palette colours every
combination: two blended layers for one add, at 70 frames a second.

The voxel tunnel (`0x11e8`) wraps a height map and colour map around the inside of a cylinder. For each of 256
angles it steps through 105 depths, looks the projected height up in a table and fills a column buffer front to back;
a polar map copies the columns to the screen ([`../re/notes-parts2d-a.md`](../re/notes-parts2d-a.md)), and offsets
that vary with depth twist it. Years later pouët commenters still asked how it was done (friol 2008, flure 2009).

## Code that writes code

For the particles and the dissolve, the set-up writes the zoom-blur's inner loop (`0x13922`, `0x1554d`;
[`../re/notes-parts2d-b.md`](../re/notes-parts2d-b.md)): per row, 80 groups of instructions that load the four
neighbours of four pixels as dwords, add them, shift right by 2 and mask with `0x1f1f1f1f`, averaging four pixels at
once in one register. The zoom's columns are built in as constant offsets. The face's warp is unrolled code whose 320
placeholder offsets the set-up finds by searching for the value 0x1234 (`0x10b26`) and rewrites every frame
(`0x10bf3`); the picture warp patches a shift count into a `shl` (`0x9a0f`). Constants in the code keep the
per-pixel loops free of table reads and spare registers, of which the 486 had few.

## 18-bit colour in 256, and a head without geometry

The dissolving woman lives in an 18-bit RGB buffer. Each pixel shows either its red and blue or its green, in a
checkerboard whose phase flips every frame (`0x7a061`): dithering in space and time that the eye mixes. The blue head
at the end has no geometry: file 34 stores the environment-map coordinates of a pre-rendered head per pixel, so
moving the light means shifting the lookup. Its trails are feedback through a remap table that settles on grey-blue.

## Hidden things and left-overs

- **A snake game.** Behind "dual monitor support": with a monochrome adapter as second display (`0x939`), the timer
  interrupt runs a snake game on its text screen, reading the keyboard port directly. Its random numbers move the
  particles on such a machine.
- **The minute decides the billing.** On an odd minute BARTi's and KARL's name cards and credits swap places.
- **A tuning tool's keys.** Beside the picture warp's parameters sits a key table nothing reads (`0x9630`): the
  bindings of the tool the warp was tuned with.
- **Messages.** The fault handler prints "General protecion fault ;) ;)". Groo's instrument names tell an Olivier
  that part 2 should follow part 1 without a break; Olivier is Ra's first name ([Hugi #35][hugi35]), and Ra wrote
  the script.
- **Unused code.** An engine routine (`0x79da0`) builds the cosine table by recurrence, the method of KARL's 40-byte
  sine generator ([PC Demos FAQ][faq], 10.8); the demo uses a stored table.

Not everyone was won over: DemoNews columnists that autumn counted NoooN among groups that keep an env-mapped torus
on screen too long ([#104][dn104]) and ranked STARS below demos with strong code and strong packaging ([#105][dn105]).

## Not verified

- **The 23,806 faces.** The release has 5,595; the FAQ answer guessed the 3D Studio model had more. In 2014 Barti
  posted on [pouët][pouet] a version that, he wrote, has the full 3D objects, repacked to put the right object into
  the effect after the logo, which would be the bee. We have not seen it.
- **Who wrote what.** The engine is BARTi's and the 2D effects KARL's by the credits, not by anything in the code.
- **Pouët claims:** Groo reused the samples in CNCD's *Closer* (maytz, 2011); the voxel tunnel runs at 30 frames a
  second on a 486SX with a Trident card (moredhel, 2013).
- **Infiny** was also a PC group (*Cardiac*, [DemoNews #106][dn106]); nothing found ties it to the loader.
- **The party version** ([DemoNews #99][dn99]) was not examined; this page describes the final release, listed as
  "bugfixed" in [DemoNews #103][dn103]. Frame rates come from an emulator, not a real 486.

## Sources

Reverse engineering in this repository: [`../re/NOTES.md`](../re/NOTES.md), [`../re/PARTS.md`](../re/PARTS.md),
[`../re/notes-core.md`](../re/notes-core.md), [`../re/notes-music.md`](../re/notes-music.md),
[`../re/notes-engine.md`](../re/notes-engine.md), [`../re/notes-parts2d-a.md`](../re/notes-parts2d-a.md),
[`../re/notes-parts2d-b.md`](../re/notes-parts2d-b.md), [`../re/notes-parts3d-a.md`](../re/notes-parts3d-a.md),
[`../re/notes-parts3d-b.md`](../re/notes-parts3d-b.md), [making-of.md](making-of.md),
[`../remake/README.md`](../remake/README.md), and `STARS.NFO` from the release.

The scene record:

- [pouët: Stars: Wonders of the World][pouet] (comments 2000–2025)
- [pouët: Assembly 1995 results][pouetasm]
- [DemoNews #99][dn99] (Assembly '95 results, August 1995), [#100][dn100] (party report, "What's Hot, What's Not"),
  [#103][dn103], [#104][dn104], [#105][dn105], [#106][dn106] (autumn 1995)
- [PC Demos FAQ, version 2.02][faq] (comp.sys.ibm.pc.demos, 1998): questions 5.2, 10.8, 11.6
- [Hugi #35: In Focus: Ra of Nooon & Sanity][hugi35]

[pouet]: https://www.pouet.net/prod.php?which=301
[pouetasm]: https://www.pouet.net/party.php?which=7&when=1995
[dn99]: https://resources.openmpt.org/resources/demonews099.txt
[dn100]: https://resources.openmpt.org/resources/demonews100.txt
[dn103]: https://resources.openmpt.org/resources/demonews103.txt
[dn104]: https://resources.openmpt.org/resources/demonews104.txt
[dn105]: https://resources.openmpt.org/resources/demonews105.txt
[dn106]: https://resources.openmpt.org/resources/demonews106.txt
[faq]: https://sunsite2.icm.edu.pl/pub/demos/resources/in4k/pcdemos_faq.pdf
[hugi35]: https://www.hugi.scene.org/online/hugise03/hugi%2035%20-%20demoscene%20interviews%20magic%20ra.htm

# JIZZ: what its makers achieved

*JIZZ* by The Black Lotus won the PC 64k intro competition at Wired '97 in Wasmes, Belgium (18–20 July 1997)
with 826 points; the runner-up, *Thorn* by Fuse, had 302 ([results][wired]). Jace did the code, design and
graphics, Probe the music, Nix the vector engine and the metaballs, Ballein the VESA code, and Sjaak of Trepaan
the logo. The program runs under Adam Seychell's DOS32 extender and plays its song with Niklas Beisert's MXMPlay.
The release's JIZZ.DOC says it took three weeks of "freaking like hell". It needs a 486DX (the x87 FPU is used
throughout), recommends a P166, and plays music only on a Gravis UltraSound with 1 MB.

This page draws on our reverse engineering of the executable ([`../re/`](../re/)) and on what the scene wrote
about the intro.

## 63,960 bytes that grow to more than 4 MB

The file is a DOS32 stub followed by a compressed "Adam" executable: 54,908 bytes that the extender's loader
unpacks into a 173,591-byte program ([NOTES.md](../re/NOTES.md)). At start-up the program spends about 24 s in
text mode (at 100 million instructions per second in our emulator) building a 928,668-byte music module and 17
textures of 196,608 bytes each. Hornet's DemoNews summed it up in September 1997 as 950k of music and 3 megs of
textures in 64k, made by "some kind of witches" ([DemoNews 148][dn148]). kb later remembered joking about the
long precalc, until the intro started ([pouët][pouet]).

## The music: a synthesiser called a cruncher

JIZZ.DOC credits Probe with the music and an ".mp4 cruncher", and Jace thanks him because he "managed to put
950Kb of music into 20K". The routine behind "extracting music" (0x1632b) decompresses nothing. It copies 32,625
stored bytes (module header, 37 instruments, 17 patterns and 14,037 bytes of real samples for the drums and two
short tones) and renders the other 32 instruments, 896,032 bytes, from 12 synthesiser records of 123 bytes at
0x16423 ([notes-music.md](../re/notes-music.md) §2).

Each record describes a bar of 16 notes: an oscillator (sawtooth, sawtooth plus a copy detuned by 2 %, a wave
shaped by a seventh power, or noise), a pitch per note, glides between tied notes, and a two-pole resonant filter
whose cutoff moves sample by sample from a start value that is scaled at every note-on, so each note gets its own
sweep. An echo with feedback, three notes long, is added, and the bar is stored as 8-bit samples at half the rate,
without an anti-alias filter. A record rendered several times gives variations of its bar. The song starts one
bar-sample every 16 rows, so the filter sweeps and glides live inside the samples and the tracker only places
bars. All of it is x87 floating point with the precision set to 24 bits (0x18712); the module only comes out
byte-identical with that setting, so our remake had to reproduce the rounding.

The limit this works around is the 64 KB itself: instruments that sound like filtered analogue synthesizers cost
hundreds of kilobytes as samples, and 64k soundtracks were usually made of tiny, chip-like samples. In 2001 kb
called this method, a tracked module with samples precalculated by a softsynth, "the Stash way" (after TBL's next
intro), said he respected Probe's results, and explained why he chose real-time synthesis for Farbrausch instead
([conspiracy.hu][kb2001]).

### Who wrote the synthesiser

The DOC credits the cruncher to Probe, and the code shows that the cruncher is the synthesiser. In a 2005
interview for Amiga Music Preservation, Probe says he became a coder because of his music: having no synthesizer,
he turned his Amiga into one, and that work later became the softsynth soundtracks of *JIZZ* and *Stash*
([AMP][amp]); a 2026 pouët thread cites the same interview ([pouët BBS][bbs2026]). No source we found says whether
Probe also wrote the x86 code in JIZZ.EXE or someone else in TBL ported it. A 2014 pouët post recalls, with an
"iirc", that the generator was first written in Pascal and converted to assembler by hand ([pouët BBS][bbs2014]).

## Was it the first 64k with synthesised samples?

The sources do not support "first". At Easter 1997, less than four months before Wired, *Totraum 209* by Purge
won the PC 64k competition at Mekka & Symposium ([results][ms97]) with music the intro synthesised itself. A
pouët comment calls it the first intro with real-time music synthesis, and kb says most of its sound code was
Räd-Äd's ([pouët][totraum]). When a 2004 comment called JIZZ the first intro with generated samples, another user
answered that Totraum had done it earlier ([pouët][pouet]).

One difference remains: Totraum computed its sound while playing (DemoNews found it very slow on a Cyrix,
[DemoNews 145][dn145]), while JIZZ renders its samples once and leaves playback to the sound card. That JIZZ was
the first 64k to pre-render its samples this way is not confirmed by any source we found; we did not survey other
platforms or the 4k class.

The music's reception is better documented. Pouët comments single out Probe's music; one viewer
played the tune to a friend over the phone; iq wrote in 2007 "I love that sample generator"; a 2002 comment on
*Stash* says others had used softsynths before, but TBL's actually sounded good ([pouët][pouet], [Stash][stash]).
A 2014 post counts JIZZ and *Stash* among the first intros with both generated textures and generated samples
([pouët BBS][bbs2014]). No source says the music decided the vote: the 826 to 302 margin shows how clearly the
hall preferred JIZZ, not why.

## MXMPlay on a Gravis UltraSound

MXMPlay, Niklas Beisert's XM player for the GUS, is built here with only the effects this song uses; every other
effect points to a `ret` (0x296dc..0x2a316). The GUS mixes 14 voices at 44.1 kHz in hardware from its own memory,
so the CPU only updates the card's registers 60 times a second and stays free for the graphics. The price is the
card's memory: the 910,000 bytes of samples are sized for a GUS with 1 MB. A 2014 pouët post tells of a viewer who
bought a memory upgrade for his GUS MAX to hear JIZZ with all its samples ([pouët BBS][bbs2014]).

## Textures as programs ("JacePeG")

"decrunching gfx" (0x12c50) runs 16 scripts, 2,041 bytes in total, through a byte-code interpreter (0x12e72)
with 39 ops on 256×256 RGB buffers: noise, midpoint-displacement clouds, sine plasmas, blobs, cones, Hermite
curves, warps, blurs, emboss, HSV conversion and more ([notes-textures.md](../re/notes-textures.md)). Each script
reseeds the random generator, so every texture can be rebuilt on its own. The only bitmaps are two 4-bit
run-length pictures (1,966 and 4,463 bytes), the end lettering and Sjaak's logo, which the scripts multiply with
clouds. The DOC credits Jace with a ".jpg (JacePeG) cruncher", but the program has no JPEG-like decoder; we read
the name as a joke like ".mp4" (our interpretation).

## Nix's vector engine

The 3D scenes are scripts too: five byte scripts of 44 to 331 bytes build every object from spheres, cones, tori,
twists, rotations and copies (0x14394, [notes-engine.md](../re/notes-engine.md)). A negative radius turns a sphere
into a dome seen from inside and a torus into the tunnel; dropping every other quad of a twisted torus gives the
ribbons. The renderer (0x1f5d1) radix-sorts the faces and fills them with an affine-mapped texture plus an
environment highlight from a precalculated spot (0x6592); three lookup tables turn the result into a 16-bit pixel
and also carry brightness, contrast and the colour inversion of the 03:10 flash.

## The metaballs

Nix's blobs are six balls whose field is the sum of 1/distance, read from a reciprocal square-root table
([notes-parts3d-b.md](../re/notes-parts3d-b.md) §5). Every frame, marching cubes polygonise the surface on a
32×32×32 grid, but only along the surface: from each ball the code walks outwards until it leaves the field, then
follows the surface from cell to cell (0x769f), so empty cells are never visited. Normals come from the exact
gradient of the field. A frame has 750 to 1,240 vertices and 1,500 to 2,490 faces.

## Hicolor through VESA 2.0

Ballein's VESA code (0x56c3, 0x5a50) asks the video BIOS for 320×200 with 15 or 16 bits per pixel and a linear
frame buffer, or else 640×400 or 640×480 hicolor with the picture scaled up. It reads the colour masks from the BIOS instead of
assuming 5:6:5 and flips between four pages during the vertical retrace. With 65,536 colours and no palette, the
generated textures can use any colour; a 2002 comment holds JIZZ partly responsible for the nickname "dutch color
scheme" ([pouët][pouet]).

## The music as the clock, and two hidden switches

Almost everything moves with MXMPlay's timer, and the parts change when the song reaches positions in a sync
table (0x6417) checked at every frame start (0x638b), so the intro stays in sync on slow and fast machines alike.
The DOC teases a way to make the intro write its graphics and music to disk: `JIZZ /pandora` writes the textures
as TGA files, `JIZZ /mp4` the module as `smokeout.mxm`.

## Sources

- Our notes: [NOTES.md](../re/NOTES.md), [notes-music.md](../re/notes-music.md),
  [notes-textures.md](../re/notes-textures.md), [notes-engine.md](../re/notes-engine.md),
  [notes-parts3d-b.md](../re/notes-parts3d-b.md), [notes-core.md](../re/notes-core.md),
  [making-of.md](making-of.md), [remake README](../remake/README.md)
- JIZZ.DOC, in the release archive: <https://files.scene.org/view/demos/groups/tbl/pc/jizz.zip>
- pouët, *JIZZ* and its comments: <https://www.pouet.net/prod.php?which=2>
- Wired '97 official results: <https://www.pouet.net/party_results.php?which=49&when=1997>
- pouët, *Totraum 209* by Purge: <https://www.pouet.net/prod.php?which=1501>
- Mekka & Symposium '97 results: <https://www.pouet.net/party_results.php?which=26&when=1997>
- pouët, *Stash* and its comments: <https://www.pouet.net/prod.php?which=3>
- pouët BBS, "TBL-Jizz Module?" (2014): <https://www.pouet.net/topic.php?which=10116>
- pouët BBS, "aardbei, threestate and tbl in your browser" (2026): <https://www.pouet.net/topic.php?which=13065>
- Amiga Music Preservation, Probe and his 2005 interview: <https://amp.dascene.net/composer/5914>
- kb, "The Workings of FR-08's Sound System, Part 1" (2001): <https://conspiracy.hu/articles/5>
- DemoNews 145 (April 1997): <https://resources.openmpt.org/resources/demonews145.txt>
- DemoNews 148 (September 1997): <https://resources.openmpt.org/resources/demonews148.txt>
- Demozoo (not read): <https://demozoo.org/productions/12/>

[pouet]: https://www.pouet.net/prod.php?which=2
[wired]: https://www.pouet.net/party_results.php?which=49&when=1997
[totraum]: https://www.pouet.net/prod.php?which=1501
[ms97]: https://www.pouet.net/party_results.php?which=26&when=1997
[stash]: https://www.pouet.net/prod.php?which=3
[bbs2014]: https://www.pouet.net/topic.php?which=10116
[bbs2026]: https://www.pouet.net/topic.php?which=13065
[amp]: https://amp.dascene.net/composer/5914
[kb2001]: https://conspiracy.hu/articles/5
[dn145]: https://resources.openmpt.org/resources/demonews145.txt
[dn148]: https://resources.openmpt.org/resources/demonews148.txt
[demozoo]: https://demozoo.org/productions/12/

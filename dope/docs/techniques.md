# How DOPE was made: the original's techniques

*DOPE* by Complex won the PC demo competition at The Gathering '95 in Stavanger with 299 points; the runner-up,
Scoop's *Luminous*, had 169 ([results](https://www.exotica.org.uk/wiki/The_Gathering_%2795)). It was made for the
PCs of spring 1995: a 486 or an early Pentium, a VGA card in 320 x 200 with 256 colours, no 3D hardware, and a
Gravis UltraSound. jmagic's own PC was a 486SX-33, a chip without a floating-point unit
([DemoNews 91](https://resources.openmpt.org/resources/demonews091.txt)), and the parts contain no floating-point
instructions. Jugi told DemoNews that jmagic had built the routines over about two months, that the group put the
demo together in little over a week, and that the "routine boxes" were 3D Studio renders he sent to Reward to be
dithered. He called the boxes an old Amiga trick that they thought no PC demo had used yet
([DemoNews 93](https://resources.openmpt.org/resources/demonews093.txt)). Each box names its effect in grand words.
People argued about the names from the first showings, and on pouët they became a running joke. This page sets
each label against what our reverse engineering ([`../re/`](../re/)) found the code doing.

## The frame around the effects

`DOPE.EXE` is nine programs in one file ([NOTES.md](../re/NOTES.md)): Tran's PMODE 3.x with a 32-bit GUS music
player, CDPI v0.2 (a chain loader that runs each appended program as a child), seven ProPack-packed parts, and TERM.
The parts are real-mode DOS programs using the 386's 32-bit registers. They share only the music, playing in
protected mode underneath, and what the previous part left in the VGA: p4 starts by reading p3's last screen back.

Each part turns the timer chip into a retrace interrupt: it measures the frame period, then re-arms the PIT as a
one-shot that fires just before each vertical retrace (p2 0x1a5f, 0x1a21). Motion is stepped in that interrupt,
70 times a second, so a slower PC shows the same motion with fewer frames ([notes-core.md](../re/notes-core.md)).
The 28-channel module plays from the GUS's own memory and timer, and nothing in the parts reads its position. Jugi
in 1995: "the music syncing is kinda of joke in dope", since the gaps between parts depend on the disk
([DemoNews 93](https://resources.openmpt.org/resources/demonews093.txt)). With 28 voices the GUS mixes at 22.05 kHz
instead of 44.1 kHz, which jmagic regretted before the release
([DemoNews 91](https://resources.openmpt.org/resources/demonews091.txt); [notes-music.md](../re/notes-music.md)).

## One engine behind most labels

p2, p3, p5, p6 and p7 carry copies of one 3D engine ([notes-engine.md](../re/notes-engine.md)): rigid meshes, a
rotation matrix from a 2048-step sine table, a back-face test with 16-bit products, a radix sort of depth sums
(painter's order, no depth buffer), and affine span fillers. No light is computed. Each vertex normal is rotated
into view space, and its x and y become a coordinate in a 128 x 128 picture of colour indices; the span loop (p2
0x2660) interpolates it and reads one texel per pixel. Whatever is painted in that picture, a highlight, a
reflection, a lit area with shadows, is what the surface shows. A bump map can shift the read by up to about 8
texels through a 256-word offset table. Both maps are 256 bytes per row, so a texel's address is one 16-bit
register, row in the high byte. "Phong" costs about as much as texture mapping.

## Label by label

| label on screen | what the code does |
|---|---|
| phong BLUR motion (p2) | Env-mapped discs and the Complex "C". The blur is a 64 KB table built at 0x1967: every drawn frame each pixel becomes `T[old][new] = (s*new + (256-s)*old) >> 8`; the palette is a ramp, so index arithmetic is colour arithmetic. The script moves `s` between 11 and 256, and the same table fades the credits ([notes-p2.md](../re/notes-p2.md)). |
| 7800 PHONG polys (p3) | 514 vertices and **1024 triangles**: a tetrahedron split 16 times per edge and deformed. "Phong" is the map read above, a radial gradient with a white disc, plus a plasma bump map addressed by the object-space normal (span loop 0x1ff0). The largest mesh in the demo is p7's, with 2888 triangles ([notes-p3p5.md](../re/notes-p3p5.md)). |
| larusse FIRE clouds (p4) | A feedback buffer read through a polar map: a tunnel effect with a trail (below). |
| larusse FRACTAL scape (p4) | A voxel landscape from a stored 256 x 256 height map and a pre-shaded colour map (0x1efc). |
| phong ENV mapping (p5) | What it says: a painted gold reflection read by the normal (0x2370). |
| BUMP glow (p5) | The bump offset table is rebuilt every retrace (0x3099) from a depth that pulses with \|sin\| about every 1.2 s, over a red map with a bright highlight. The "glow" is that pulse. |
| SHADOW volume (p6) | 32 pre-made 64 x 64 bit masks, one per 1/128 turn of the rings, written into the cat's light map every frame (0x2e05). There is no shadow geometry ([notes-p6p7.md](../re/notes-p6p7.md)). |
| material MORPH (p6) | A palette morph: colours 16..207 cross-fade from fur ramps to blue-grey ramps over 256 retraces (0x3895) while the bump depth falls to zero. The geometry does not change. |
| METAL shading (p6) | The same light map and masks with no bump, through the blue-grey ramps. |
| multiple LIGHT sources (p7) | The light map is rebuilt every frame: a 256 x 256 base (a faint digitised photograph) plus five copies of a 128 x 128 spot, added with 32-bit adds at positions that follow sines (0x3046). |
| 1995 WORLD domination (p7) | A slogan. The object is M. Hakulinen's eight-armed shape, 1446 vertices and 2888 triangles, bump-mapped in that light map. |

## The fire and the landscape

p4 does not use the engine ([notes-p4.md](../re/notes-p4.md)). Its fire buffer holds one value per two pixels. On
every other retrace each value becomes `(232*F + 24*C + 63) >> 8`, capped at 112 (0x1aab), where `C` is a texel of
a 256 x 256 cloud texture. The texel's address comes from a stored map of radius and angle around the screen
centre, plus an offset that moves the texture outward one row per step and swings it with a sine: the classic
tunnel lookup, with a feedback trail of about a third of a second. A 4 x 4 ordered dither turns the value into
one of 15 levels, and the screen byte is `16*level + background` (p3's 16-colour nebula). The palette composites:
its 240 entries hold every pair of fire level and background colour, recomputed 35 times a second (0x1999), so
fading the fire costs 720 DAC writes, not 64,000 blends. In April 1995 jmagic joked about a "dithered tunnel in
dope" ([DemoNews 91](https://resources.openmpt.org/resources/demonews091.txt)).

The fire step modifies its own code: it writes the texture offset into two `add bx, imm16` instructions (0x1b09,
0x1b1e) and reads the old immediate back to see whether the offset changed. The engine's span loops get their
slopes and map segments patched in the same way; an immediate costs no register and no memory read.

The "fractal scape" is a height field sampled along 55 depth rows from far to near, each screen column filling the
strip between consecutive rows, with colours from a map that already holds the shading. The camera flies straight
ahead at a fixed height. It is the kind of landscape NovaLogic's *Comanche* (1992) had made familiar.

## What the scene said about the labels

On pouët ([comments](https://www.pouet.net/prod.php?which=37)), kb remembers crowds at X'95 asking whether there
were really 7800 polygons and saying the shadows were only clever environment mapping; the code agrees. Later
comments speak of "fakepolycounting" (PigPen) and an "amusing abuse of envmaps" (titanstar), and Photon asked since
when several lights were harder than one; in p7 they are five additive copies of one picture. Others saw a new
level: Scali listed bump mapping, shadows, material morph and the polygon count, and saffron credited DOPE with
bringing effect boxes to the PC. At NAID '95 a visitor took the bump-mapped torus for a pre-rendered animation
([report](https://sunsite2.icm.edu.pl/pub/music/demos/parties/1995/naid95/misc/report.txt)). Within months
*[Nope](https://www.pouet.net/prod.php?which=13390)* and *[Rope](https://www.pouet.net/prod.php?which=18535)*
parodied it. "Larusse fire clouds" is still a forum joke about effect names
([2003](https://www.pouet.net/topic.php?which=778), [2008](https://www.pouet.net/topic.php?which=4930)), and the
fire was shown at Master Boot Record's concert at Revision 2025 ([pouët](https://www.pouet.net/topic.php?which=12830)).

The coder knew. Every engine part carries a message for whoever disassembles it: "DON'T TELL ME IT'S NOT OPTIMIZED
OR 'REAL' BECAUSE I KNOW THAT ALREADY!", then "WHY OPTIMIZE ANYTHING FOR PENTIUM 100 MHZ?". Reward said the same
in the April interview: nobody optimises when the compo machine is a 100 MHz Pentium.

## Who is Larusse?

The earliest explanation we found is the interview of 12 April 1995, three days before the release: jmagic asks
Reward about "that french guy whose fractals we used", and Reward answers that Larusse is his friend
([DemoNews 91](https://resources.openmpt.org/resources/demonews091.txt)). The whole interview is in that joking tone
(a whale hidden inside the asteroid, "say no to disassemblers"). In 2003 Optimus wrote that the MindCandy DVD
commentary suggests Mr. Larusso was a coder's maths professor ([pouët](https://www.pouet.net/prod.php?which=37)),
and Scali remembered a maths teacher ([pouët](https://www.pouet.net/topic.php?which=544&page=3)). tomcat called the
name nonsense because the effect was not new, and pointed to an interview with jmagic that would appear in *Freax*
"someday" ([pouët](https://www.pouet.net/topic.php?which=544&page=4)). In 2004 jmagic called the MindCandy
commentary "total bollocks" ([pouët](https://www.pouet.net/prod.php?which=37)).

## Not verified

- Who Larusse is. No source is both first-hand and serious, and the *Freax* interview was not found. The name is
  close to that of the French encyclopedia Larousse; whether that is the joke is a guess.
- What "7800" counts: no mesh, sum of meshes or vertex count in the demo gives it.
- The MindCandy commentary and the YLE *Demoscene* documentary (2010, episode 4, on Complex and DOPE) were not seen.
  A 2025 forum post says Sandman of Valhalla nearly fell over laughing at the 7800 box before the showing
  ([pouët](https://www.pouet.net/topic.php?which=12807)); second-hand.

## Sources

- Our reverse engineering: [NOTES.md](../re/NOTES.md), [PARTS.md](../re/PARTS.md),
  [notes-core.md](../re/notes-core.md), [notes-engine.md](../re/notes-engine.md),
  [notes-music.md](../re/notes-music.md), [notes-p2.md](../re/notes-p2.md), [notes-p3p5.md](../re/notes-p3p5.md),
  [notes-p4.md](../re/notes-p4.md), [notes-p6p7.md](../re/notes-p6p7.md), [making-of.md](making-of.md),
  [README.md](../README.md), [remake/README.md](../remake/README.md), and `DOPE.TXT` in the release
- pouët: [DOPE](https://www.pouet.net/prod.php?which=37) and its comments,
  [Nope](https://www.pouet.net/prod.php?which=13390), [Rope](https://www.pouet.net/prod.php?which=18535)
- pouët forum: "Demo quotes game" ([page 3](https://www.pouet.net/topic.php?which=544&page=3),
  [page 4](https://www.pouet.net/topic.php?which=544&page=4), 2003);
  ["New demoeffects?"](https://www.pouet.net/topic.php?which=778) (2003);
  ["Real definitions of oldschool or newschool software effects"](https://www.pouet.net/topic.php?which=4930) (2008);
  ["Learning ab demoscene?"](https://www.pouet.net/topic.php?which=12807) (2025);
  [Larusse fire clouds at Revision 2025](https://www.pouet.net/topic.php?which=12830)
- [demozoo](https://demozoo.org/productions/57/) (behind a bot check when this was written; not read)
- The Gathering '95 results: [ExoticA](https://www.exotica.org.uk/wiki/The_Gathering_%2795),
  [DemoNews 89](https://resources.openmpt.org/resources/demonews089.txt) (also a review of `dope.mod` and `onward.xm`)
- Popcorn / Hornet, interview with Jmagic and Reward, 12 April 1995,
  [DemoNews 91](https://resources.openmpt.org/resources/demonews091.txt)
- GraveDigger / Hornet, interview with Jugi, 8 May 1995,
  [DemoNews 93](https://resources.openmpt.org/resources/demonews093.txt)
- Phoenix, [NAID '95 party report](https://sunsite2.icm.edu.pl/pub/music/demos/parties/1995/naid95/misc/report.txt)
  (written 1998)

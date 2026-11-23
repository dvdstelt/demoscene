# Reve: what its makers achieved

*Reve* by Pulse came 5th in the PC demo competition at The Party 1995 in Fredericia, Denmark (27–29 December
1995), with 339 points; *Caero* by EMF and Plant won with 1,546 ([results][res], [demozoo][dzparty]). Thirty demos
were entered. Pulse had formed on 5 November 1995 from the Polish members of Surprise! Productions and the French
members of Distorsion and Infiny ([demozoo][dzgroup]), and Reve was its first production. Its credits: code LCA,
graphics Antony and AJT, music Shad ([demozoo][dzprod]). Half a year later Pulse released a final version, which says
it has "many bugs fixed and many new 3D scenes"; that version is the one remade here.

This page draws on our reverse engineering of the executable ([`../re/`](../re/)) and on what the scene wrote about
the demo.

## Infiny, Pulse and LCA

Most of Reve's makers came from Infiny, whose *Epsilon* had won at Volcanic in 1995 with code by LCA, graphics by
Antony, 3D objects by AJT and music by Shad ([demozoo][dzeps]). Hornet's DemoNews introduced Pulse at The Party as a
new group made of what used to be Infiny, to be joined by Distorsion and the Polish section of Surprise! Productions
([DemoNews 112][dn112]). Pulse went on to win at Delirium, Intel Outside and Gravity in 1996, and at The Party 1997
with Melon Dezign ([pouët][pouetgroup]).

LCA wrote nearly everything the demo stands on. The executable carries his own DOS extender, "FLAT OS V1.0", his own
archive format, "LCA's Packer", and his own module player, "LCA's MOD-Player V3.03" (the strings name him and date
the player 1993–95 and the extender 1995–96). Version 3.02 of the same player plays the music of NoooN's *STARS*,
the Assembly 1995 winner, which credits LCA with its sound code. No web page we found mentions the extender or the
player by name.

## Everything runs on the screen refresh

The demo's timer interrupt locks itself to the monitor ([NOTES.md](../re/NOTES.md)). Each time it fires it checks
whether the vertical retrace has already begun: if it has, the timer count was too long and the handler shortens it
by one; if not, it lengthens it and waits for the retrace. The count settles where the interrupt fires just before
every retrace (0x427f, 70.09 Hz), and the handler returns right on it. There it compares the music position with the
current part's end position and calls the part's retrace routine, which moves everything by one step. The main loop
only draws, at most one frame per refresh, so the motion keeps to the refresh while the frame rate follows the
machine (45 to 70 frames per second on the capture's emulated Pentium).

The running order is a table of thirty entries: an init that loads the part's files, a routine for every refresh, a
routine for every drawn frame, a routine for the end, and the music position at which the part ends. The music
plays on the Gravis UltraSound's own timers, so it keeps going while an init unpacks its files.

## 3D through tables

The 3D engine ([notes-engine.md](../re/notes-engine.md)) loads its worlds, `3D\*.WLD`, with objects, faces, normals
and bounding boxes, and plays their animations, `3D\*.ANI`, as a starting key plus a 16.16 delta per refresh for the
camera and every object. It culls objects by distance and by their boxes, sorts the objects and each object's faces
with a recursive quicksort on the depth of a face's first vertex, and draws them back to front. Faces are flat,
textured or textured with Gouraud shading: the light level, from the angle to a light point and the distance, picks
one of `GOURAUD.PAL`'s 16 rows of 256 colours. All textures are 64 × 64 tiles of one 256 × 256 sheet, `TEXTURE.RAW`.
PART4 draws the bounding boxes too, as white wireframes around the objects.

The look that people still ask about is a blur. pouët's commenters noticed that the scenes are "blurred, not
aliassed", and wondered how a 1995 demo could anti-alias the whole screen; LCA later explained that it was done in one
pass ([pouët][pouet]). The pass ([notes-engine.md](../re/notes-engine.md), 0x2045fa) reads `ANTIALIA.PAL`, a 256 × 256
table that gives for two palette indices an index of roughly their average colour, three times per pixel: the pixel
above with the pixel below, the pixel itself with the one to its left, and the two results. Every pixel becomes about
the average of four, at the cost of three table reads. The table is rough: for about half of the pairs it gives the
nearest colour, and it does not map every colour to itself, so the pass also shifts some colours, which turns PART4's
beige letters white.

## 2D effects

The 2D parts are table work as well ([notes-parts2d-a.md](../re/notes-parts2d-a.md),
[notes-parts2d-b.md](../re/notes-parts2d-b.md)):

- **Strobes.** A counter drops by a growing step every frame; each time it runs out, the picture flashes for one
  frame, alternately as it is and as its negative. The negative goes through a table of the palette entry nearest to
  each inverse colour: `NEGATIF.PAL` for the first picture, computed at start-up for the others.
- **Twirls.** At start-up the demo sorts every pixel of the picture into one of 120 rings about the centre. Each
  frame it rotates the colours of each ring by its own amount, more towards the rim, so the picture winds into a
  spiral and unwinds.
- **Rotozoom.** The layers under WIZ write only a quarter of the pixels per frame, cycling through four classes, and
  the rest of the screen stands; two layers turn against each other in it.
- **Feedback.** The dot spirals blend the screen with a zoomed copy of itself through `SPIRBLUR.PAL` (three quarters
  of the pixel, a quarter of the zoomed one), so the dots leave echoes that drift outwards and fade.
- **Planes.** DADA and TOM / BOZ are two textured planes seen by two cameras, one on the even lines of the screen and
  one on the odd lines.

## Packing, data and sound

`REVE.EXE` is exactly 999,999 bytes. Its real-mode start is the Infiny Loader, which decrypts itself in 64 layers,
each a copy loop over the next. Then a packed loader unpacks FLAT OS. The extender needs XMS and runs three ways:
in plain real mode it switches the processor to protected mode by itself, under EMM386 it takes over EMM's page
tables, and with `DPMI` on the command line it runs as a DPMI client. It serves the demo through its own interrupt,
0FFh: memory, files and IRQ handlers.

The rest of the file is LCA's archive: 36 files compressed with LZW, among them the demo itself (`REVE.LFL`, a flat
32-bit program with 2,937 relocations), the module, 18 pictures, the palettes and tables, and six 3D worlds with
their animations. The music is "PULSE", an 8-channel MOD by Shad, dated 18 December 1995 on Demozoo ([demozoo][dzmod]).
The player drives a Gravis UltraSound, with two GUS voices per channel, or a Sound Blaster; `NOSOUND`, `MONO`, `8BIT`
and `MIX:nn` on the command line change its output.

## Hidden and left over

- The extender keeps four 512-byte interrupt stacks right below the demo's 2 KB stack. The 3D engine's recursive
  sort can reach into them, and when an interrupt arrives at that moment it overwrites the sort's return addresses.
  On the capture's emulated machine that happened once, 108 seconds in; on a real PC it depends on when the
  interrupt comes. The emulator moves the stacks apart.
- The party version is a different program: a 16 KB `REVE.EXE` with the data in `REVE.LFL` and `REVE.PAK`, and fewer
  3D scenes. Those who saw both on pouët remember other camera angles and graphics in it ([pouët][pouet]).

## How it was received

DemoNews 112 (6 January 1996) rated the party version ***+ in its upload list. Its review of The Party's demos gave it
four stars for effects and three for design and music: complex vector scenes with a figure surfing through the sky
around them, still pictures by Antony, and rock music by Shad that the reviewer found odd. It said fans of the
Amiga's Polka Brothers would like it and hardcore fans of *Second Reality* would not ([DemoNews 112][dn112]). The
final version arrived at Hornet in July 1996 with a **** rating ([DemoNews 125][dn125]). On pouët it has 64
comments: many call it a favourite, surreal and dream-like and well synchronised with the music, which itself
divides them ([pouët][pouet]). It is on the *MindCandy Volume 1* DVD of PC demos (2002).

## Sources

- Our notes: [NOTES.md](../re/NOTES.md), [PARTS.md](../re/PARTS.md), [notes-engine.md](../re/notes-engine.md),
  [notes-parts3d.md](../re/notes-parts3d.md), [notes-parts2d-a.md](../re/notes-parts2d-a.md),
  [notes-parts2d-b.md](../re/notes-parts2d-b.md), [notes-music.md](../re/notes-music.md),
  [making-of.md](making-of.md), [remake README](../remake/README.md)
- FILE_ID.DIZ and PULSE!.NFO, in the release archive: <https://files.scene.org/view/demos/groups/pulse/pls_revf.zip>
- The Party 1995 results: <https://files.scene.org/view/parties/1995/theparty95/results.txt>
- Demozoo, The Party 1995: <https://demozoo.org/parties/94/>; Reve: <https://demozoo.org/productions/7641/>; Pulse:
  <https://demozoo.org/groups/575/>; Epsilon: <https://demozoo.org/productions/8894/>; the module:
  <https://demozoo.org/music/17782/>
- pouët, *Reve* and its comments: <https://www.pouet.net/prod.php?which=70>; Pulse:
  <https://www.pouet.net/groups.php?which=56>
- DemoNews 112 (January 1996): <https://resources.openmpt.org/resources/demonews112.txt>
- DemoNews 125 (July 1996): <https://resources.openmpt.org/resources/demonews125.txt>

[res]: https://files.scene.org/view/parties/1995/theparty95/results.txt
[dzparty]: https://demozoo.org/parties/94/
[dzprod]: https://demozoo.org/productions/7641/
[dzgroup]: https://demozoo.org/groups/575/
[dzeps]: https://demozoo.org/productions/8894/
[dzmod]: https://demozoo.org/music/17782/
[pouet]: https://www.pouet.net/prod.php?which=70
[pouetgroup]: https://www.pouet.net/groups.php?which=56
[dn112]: https://resources.openmpt.org/resources/demonews112.txt
[dn125]: https://resources.openmpt.org/resources/demonews125.txt

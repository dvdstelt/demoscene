# Vivid Experiment: what its makers achieved

*Vivid Experiment* by Doomsday came 3rd in the PC demo competition at Assembly '96 in Helsinki (16–18 August 1996),
with 764 points, behind *Machines of Madness* by Dubius (1,452) and *Toasted* by Cubic Team (1,418) and just ahead of
Orange's *Super Television* (736) ([official results][res]). It was Doomsday's first demo after two 64k intros, and
one of its coders wrote that it barely made it to the big screen ([MRI's page][mri]). The group's next big demo,
*Boost* (Assembly '97, 1st), called itself its sequel ([Boost's info file][boost]).

The demo's credits screen names Doom and MRI for the code, Dice and Damac for the graphics and Swallow and Wode for
the music. The two halves have their own modules: part I plays Wode's "Vivid" (EXPRMNT1.XM, 16 channels), part II
Swallow's "Experiment II" (EXPRMNT2.XM, 20 channels) ([Modland][modland], [Demozoo][dz]); the file names put the
title together.

This page draws on our reverse engineering of the executable ([`../re/`](../re/)) and on what the scene wrote about
the demo.

## A demo system with a script

Most demos of 1996 hard-coded their running order. Vivid Experiment has a small demo system: a script interpreter
and a set of effects that read their parameters from files. Each half is a text script of commands with a music
position:

```
        024 000 RunBITMAPEFEK           efek/1efek1.prm
        xxx xxx InitFADE                pal/plo2.pal   pal/white.pal  80 0 255
```

A command with a position runs until the music reaches it; "xxx" runs at once. The effects take their settings from
small text files: the "bitmap effects" from `.ini` files that name pictures and tables and `.prm` files with their
speeds, the bump mapping from one line of eleven values, the 3D scenes from key = value files with comments (the
landscape's lists the twelve shading types the engine offers). The script file itself is headed "EXAMPLE DEMO SCRIPT
FILE". The program still carries effects the scripts never call (a tunnel, a globe, one called "Dog"), and part II's
script ends with the musician's notes on the song, in Finnish, as comments ([NOTES.md](../re/NOTES.md),
[notes-core.md](../re/notes-core.md)).

All the data, 159 files in three archives (pictures, palettes, tables, objects, scripts), is linked into the program
itself: the unpacked program is 4.2 MB, of which the code is 177 KB. PMODE/W, the DOS extender by Daredevil and Tran,
compresses the whole of it into the 1.8 MB `VIVID.EXE`; only the two modules sit next to it.

## On the music and the screen at once

The music is played by the MIDAS Sound System 0.5a, Sahara Surfers' library, with its XM player and Gravis
UltraSound driver ([notes-music.md](../re/notes-music.md)). MIDAS's timer serves two masters with one timer chip: the
player's ticks at the module's tempo (2,982,950 / BPM timer clocks) and an interrupt just before every screen
retrace, in which it counts the demo's frames. Every effect animates from that frame counter, 70 times a second,
however many frames the PC manages to draw; the fades step with it as well. The demo also forces mono output, so the
modules' panning is computed and never heard.

## Bitmap effects

The first half's main effect is what the scripts call "bitmap effects" ([notes-bitmap.md](../re/notes-bitmap.md)): a
256 × 256 texture (a hand-made marbled picture) read through two per-pixel tables, one for the texture's column and
one for its row, plus an offset that moves every frame. The tables are polar maps around a point (distance, angle,
distances and angles with sine wobbles, mirrored and tiled copies), stored as 64,000-byte files, one byte per
screen pixel; drawing a frame is two table reads, an add and a texel read per pixel. Changing the tables and the
speeds gives tunnels, whirlpools and kaleidoscopes from the same few textures. Over them the first half lays a photo
of a woman, drawn in the palette's top 64 colours with one colour left transparent. The second half combines two
textures through a 32 KB colour table that dithers their 50 % mix, and a 2 × 2 version adds three textures' indices
into a fiery ramp. The inner loops patch their own addresses and constants before they run (self-modifying code).

## Bump mapping in water

Three effects of the second half light a height picture with a moving light: the map of Finland ("SUOMI"), a lumpy
ice surface and the word "doomsday" ([notes-bump.md](../re/notes-bump.md)). The slope at each pixel (the difference
of its neighbours' heights) plus the light's position picks a pixel from a 256 × 256 picture of a light spot. In the
Finland and doomsday versions, the picture stands in water: two sets of circular waves, rolling out from two corners,
make a water level, and wherever it is higher than the land the pixel is water (lit from the waves' slopes, in the
palette's blue half) and elsewhere land (the green half), so the coastline moves with the waves. The ice version
slides a coloured rock texture across the bumps instead of a light spot.

## Mirrors without reflections

The second half opens with four hearts over a photo of a star cluster ([notes-water.md](../re/notes-water.md)): three
orange ones on one spline path, and a larger blue one that looks like glass. Each is environment mapped: the
normal at each vertex gives the position in a 256 × 256 picture of a lit sphere (through an arcsine, so the map's
edge is the silhouette), and a bump picture shifts those positions per pixel. The blue heart is not transparent. Its
environment map is a picture of a glassy sphere, and while an orange heart flies more than 500 units behind it, the
demo draws that heart, at a sixteenth of its size, into a copy of the map, so the blue heart seems to show what is
behind it.

## Twelve ways to shade a triangle

The 3D scenes ("LANDSCAPE", and ten tumbling blobs over a sunset photo) come from an engine that MRI documented at
the time: his tutorial on texture mapping, dated June and July 1996, describes the same rasterizers, extended to
Phong shading, environment bump mapping and Phong shaded texture bump mapping ([fatmap.txt][fatmap]). The engine
offers twelve shading types, from flat to "floating environment bump mapping"; the two scenes use three
([notes-generic.md](../re/notes-generic.md)):

- environment bump: the environment map read at the interpolated normal, offset by the slopes of a bump picture;
- floating environment bump: the same with the bump picture read at the environment coordinates, so the bumps
  float over the object as it turns;
- Phong shaded texture bump: a texel lit through a 128-shade light table, by a Phong highlight computed into a
  256 × 256 map (ambient, diffuse, specular and an exponent from the ini file), offset by the bumps.

The inner loops step four coordinates in 8.8 fixed point and patch their steps into their own code. Faces are sorted
by depth (an insertion sort for small objects, a heapsort for the landscape's 2,048 triangles), objects follow
natural cubic splines through key positions and rotations, and a face with a vertex closer than the near limit is
dropped whole: in the landscape's low pass, whole triangles of the hills blink away as the camera skims them.

## Pictures

The pictures mix drawn art and borrowed images. The lettering of the opening and the credits' names are Doomsday's
own, and so is the word "doomsday" of the last bump mapping. The winged man at the end of part I is a fantasy
painting in Boris Vallejo's style (Demozoo tags the demo with his name). The title picture, the greetings and the
photos in the bitmap effects are photos of women, two with Doomsday's "DSD" logo printed on their shirts; the slides
between the effects are airbrushed pin-ups with captions in a script typeface that quote a 1995 pop song. The NFO
jokes about it, in the style of a software cracker's release notes, that the group ripped 160 MB of JPEGs and clip
art.

## What the scene said

Viewers on pouët remember the first half's dance music with vocals and the second half's 2D and 3D effects, the
"Finland bumpmapper" and the pictures of women, which some call ripped ([pouët][pouet]). Demozoo lists it with tags
for nudity and for Boris Vallejo ([Demozoo][dz]).

[res]: https://ftp.scene.org/pub/parties/1996/assembly96/info/a96resul.txt
[mri]: https://www.multi.fi/~mbc/personal_dsd.html
[boost]: https://ftp.scene.org/pub/demos/groups/doomsday/v2_boost.diz
[modland]: https://ftp.modland.com/pub/modules/Fasttracker%202/
[dz]: https://demozoo.org/productions/10152/
[pouet]: https://www.pouet.net/prod.php?which=2167
[fatmap]: https://ftp.scene.org/pub/mirrors/hornet/code/3d/trifill/texmap/fatmap.txt

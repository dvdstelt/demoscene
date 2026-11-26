# Vivid Experiment: the timeline

From the capture `full.*` (the whole demo at 100 M instructions per second): refresh k = record k of
`full.frames`, program time (k + 1) / 70.086 s. "Lines" are the script lines (`full.script`: the frame at which the
interpreter reached them; NOTES.md "The script"). Music positions are order:row of the module playing. The demo is
395.1 s long: part I 206 s, part II 189 s.

`part2.*` is part II run on its own (`run_vivid.py --args 2`): its refresh k is refresh k + 14444 of `full.*`.

## Part I: `script/demo1.scr`, EXPRMNT1.XM ("Vivid" by Wode, 16 channels)

The module starts at refresh 23 (the first script line runs then).

| refreshes | music | lines | what is on screen |
|---|---|---|---|
| 23 – 919 | 0:00 – 2:00 | 8–17 | the sky (`taivas.pcx`) fades in from black over 400 steps; a white flash at 1:00 (`taivas4.pal`, 2 steps, then back over 30) |
| 920 – 1402 | 2:00 – 4:00 | 19–28 | "in the year 1996" (`alkutext.pcx` rectangle over the sky) appears and goes again through the palette (`taivas.pal` ↔ `taivas2.pal`, 200 steps each way) |
| 1403 – 1886 | 4:00 – 6:00 | 30–39 | "a doomsday production", the same way |
| 1887 – 2603 | 6:00 – 8:62 | 41–50 | "at assembly", the same way |
| 2604 – 3571 | 9:00 – 12:62 | 52–63 | the code credits: the label "coding" in the corner, a flash, then DOOM (`doom.pcx`) zooms over the sky (RunZOOMSYNC to 10:62), a flash, MRI (to 12:62) |
| 3572 – 4538 | 13:00 – 16:62 | 65–76 | the graphics credits: "graphics", DICE, DAMAC |
| 4539 – 5505 | 17:00 – 20:62 | 78–90 | the music credits: "music", SWALLOW, WODE |
| 5506 – 5512 | 21:00 | 92–96 | the "music" label erased (an empty rectangle of `alkutext.pcx`; the word "called" in that picture is never shown) and a fade toward white cut short after 6 steps |
| 5513 – 5990 | 21:00 – 23:00 | 99–102 | the title picture `vivid.pcx` fades in from white over 150 steps |
| 5991 – 7757 | 24:00 – 30:60 | 107–123 | bitmap effects #1 (`efek1.ini`: the photo `nainen1.raw` over effects of `plo2.raw`; then `4efek1.ini`, `16efek1.ini`; to white from 30:40) |
| 7758 – 8658 | 31:15 – 34:63 | 125–145 | slides #1: `synknai1.pcx` with four caption lines of `synktxt1.pcx` appearing one by one, then `synknai2.pcx` with `synktxt2.pcx`; fade to black |
| 8659 – 10442 | 36:00 – 43:00 | 147–163 | bitmap effects #2 (`efek2.ini`: `nainen2.raw` and `plo6.raw`; `4efek2.ini`, `16efek2.ini`; to white) |
| 10443 – 11329 | 43:15 – 46:63 | 165–185 | slides #2: `synknai3.pcx` / `synktxt3.pcx`, `synknai4.pcx` / `synktxt4.pcx`; fade to black |
| 11330 – 13114 | 48:00 – 55:00 | 187–205 | bitmap effects #3 (`efek3.ini`: `nainen3.raw` and `plo5.raw`; `2efek3.ini`, `4efek3.ini`; to black) |
| 13115 – 14004 | 55:00 – 59:00 | 207–212 | the 2 × 2 effect (`efek2x2/efek2.ini`: three planes of `2x2_3.raw` / `2x2_4.raw`) |
| 14005 – 14446 | 59:00 – 60:63 | 213–217 | the winged man `siipiman.pcx` fades in from white over 250 steps |
| 14447 | | 218 | the screen is cleared; the module stops |

## Part II: `script/demo2.scr`, EXPRMNT2.XM ("Experiment II" by Swallow, 20 channels)

The module starts at refresh 14459, 12 refreshes after part I ends (the emulated disk is instant).

| refreshes | music | lines | what is on screen |
|---|---|---|---|
| 14459 – 17150 | 0:00 – 4:63 | 8–9 | the water scene (`water/water.ini`): the transparent object and the letter objects along spline paths over the star picture `watback.pcx`; to white at the end |
| 17151 – 18235 | 5:00 – 7:00 | 11–16 | "SUOMI BUMP": the map of Finland bump-mapped (`bump1.ini`), from white |
| 18236 – 20389 | 7:00 – 11:00 | 17–35 | bitmap effects with two planes (`efek4.ini`: `plo3.raw`, `plo6.raw`, `dtable.raw`; `efek5.ini`: `plo2.raw`, `plo5.raw`, `dtable2.raw`), four sections |
| 20390 – 22544 | 11:00 – 15:00 | 36–39 | "LANDSCAPE": the generic 3D scene `gen/gen.ini` (the landscape `maa.m3d` along `land.spl`) |
| 22545 – 23620 | 15:00 – 17:00 | 41–43 | "ICE BUMP" (`bump3.ini`: `bumppic3.pcx`) |
| 23621 – 25774 | 17:00 – 21:00 | 44–47 | the second generic scene `gen2/gen.ini`: ten objects along `path.spl` over the sunset `back.pcx` |
| 25775 – 26887 | 21:00 – 23:00 | 49–53 | "DOOMSDAY BUMP" (`bump2.ini`: the word "doomsday"); to white from 22:55 |
| 26888 – 27689 | 23:00 – 24:63 | 55–58 | the greetings picture `greets.pcx` fades in from white over 130 steps |
| 27690 | | | text mode; the program ends |

Each section's notes file gives the exact frames and the per-frame behaviour: notes-core.md (pictures, fades, the
zoom credits, the clock), notes-bitmap.md, notes-water.md, notes-bump.md, notes-generic.md, notes-music.md.

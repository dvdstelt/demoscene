# Reve — the timeline

From the capture (`full.log`, `full.ticks`; `python3 parts.py full` makes the table, `--sheet` a contact sheet with
the first, middle and last record of every entry). Refreshes are records of `full.frames` (70.086 per second);
music time 0 is the refresh at which the player starts (about record 31, notes-music.md). Each script entry
(0x205ed0, NOTES.md "The script table") has an init, a done routine, a retrace routine (called once per refresh by the
timer interrupt), a frame routine (called once per drawn frame) and the music position at which it ends.

"init at" is the record in which main called the entry's init, "starts at" the record in which its retrace routine
was set (after the init, which loads files: up to 11 refreshes); "fps" is frames drawn per second of the entry. The
3D entries draw 44 to 60 frames per second in the capture (100 million instructions per second), the 2D entries
every refresh. 0x20483f is an empty routine; 0x25f3ae is the 3D animation step, 0x20497a frees a 3D world, 0x204ee9
ends a swirl.

| entry | init | done | retrace | frame | ends at | init at | starts at | refreshes | frames drawn | fps | scope | on screen |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 204840 | 20483f | 20483f | 20489c | 1:00 | 30 | 32 | 620 | 617 | 69.7 | 2d-a | the title `GFX\REVE.RAW` ("RĒVE") on black, through TEXTURE.RAW's palette |
| 1 | 2048e2 | 20483f | 25f3ae | 2049c4 | 2:00 | 648 | 652 | 675 | 494 | 51.3 | engine | the credits: a textured 3D knot (`3D\INTRO.WLD`) with a surfer flying around it; the name LCA (`GFX\CREDITS.RAW`) over it; the whole frame blurred |
| 2 | 20483f | 20483f | 25f3ae | 204a10 | 3:00 | 1327 | 1327 | 626 | 459 | 51.4 | engine | the same, the name AJT |
| 3 | 20483f | 20483f | 25f3ae | 204a5c | 3:32 | 1953 | 1953 | 418 | 300 | 50.3 | engine | the same, the name ANTONY |
| 4 | 20483f | 20497a | 25f3ae | 204aa4 | 4:00 | 2371 | 2371 | 366 | 267 | 51.1 | engine | the same, the name SHAD |
| 5 | 204aef | 20483f | 20483f | 204b2c | 5:48 | 2736 | 2737 | 400 | 400 | 70.1 | 2d-a | `GFX\PULSE.RAW` ("Pulse" and a face) flashing between black, the picture and its negative (`GFX\NEGATIF.PAL`) |
| 6 | 20483f | 20483f | 20483f | 204bd8 | 6:00 | 3137 | 3137 | 56 | 56 | 70.1 | 2d-a | `GFX\PULSE.RAW`, steady |
| 7 | 204bed | 20483f | 204c5d | 204cb4 | 10:00 | 3193 | 3193 | 904 | 897 | 69.5 | 2d-a | the sprite `GFX\WIZ.RAW` zooming in and out over two rotozoomed layers of TEXTURE.RAW's textures |
| 8 | 204d3d | 20497a | 25f3ae | 204dc8 | 20:00 | 4089 | 4097 | 2234 | 1452 | 45.6 | engine | 3D (`3D\P3D1.WLD`): textured objects and blocks, the surfer, blurred |
| 9 | 204de0 | 20483f | 204ef8 | 204fe8 | 22:00 | 6328 | 6331 | 445 | 445 | 70.1 | 2d-a | `GFX\PLAYMO.RAW` (a toy figure) twisted into a spiral |
| 10 | 20483f | 204ee9 | 204f6f | 204fe8 | 24:00 | 6776 | 6776 | 454 | 449 | 69.3 | 2d-a | the spiral keeps turning |
| 11 | 2057b0 | 20497a | 25f3ae | 205850 | 28:00 | 7224 | 7230 | 892 | 682 | 53.6 | engine | 3D (`3D\PART2.WLD`): a red landscape, the surfer |
| 12 | 20562c | 20483f | 2056f3 | 20578c | 32:00 | 8119 | 8122 | 893 | 893 | 70.1 | 2d-a | glowing dots on a spiral over a dark rotozoomed background (`GFX\SPIRALE.RAW`, `GFX\SPIRBLUR.PAL`) |
| 13 | 20569c | 20483f | 2056f3 | 20578c | 36:00 | 9015 | 9015 | 900 | 897 | 69.9 | 2d-a | the same |
| 14 | 205881 | 20497a | 25f3ae | 20590c | 40:00 | 9911 | 9915 | 894 | 762 | 59.7 | engine | 3D (`3D\P3D3.WLD`): a landscape, the surfer |
| 15 | 2053cc | 204ee9 | 2054c4 | 205578 | 44:00 | 10806 | 10809 | 894 | 894 | 70.1 | 2d-a | `GFX\BOUCHE.RAW` (a mouth) twisted into a spiral |
| 16 | 205261 | 20483f | 20483f | 20483f | 45:00 | 11702 | 11703 | 224 | 224 | 70.1 | 2d-b | `GFX\JUNIOR.RAW` ("Junior .born to code") |
| 17 | 2052a0 | 20483f | 20483f | 20483f | 48:00 | 11926 | 11927 | 696 | 696 | 70.1 | 2d-b | `GFX\MRTOMMY.RAW` (a face with glasses) |
| 18 | 20508d | 20483f | 2050ce | 20510c | 49:00 | 12622 | 12623 | 618 | 618 | 70.1 | 2d-b | `GFX\DADA.RAW` (a winged pony) smeared along horizontal lines |
| 19 | 20483f | 20483f | 2050ed | 20510c | 50:00 | 13241 | 13241 | 786 | 785 | 70.0 | 2d-b | the same |
| 20 | 205924 | 20483f | 20483f | 205950 | 51:48 | 14025 | 14027 | 398 | 398 | 70.1 | 2d-b | black (`GFX\GENE.RAW` loaded) |
| 21 | 20483f | 20483f | 20483f | 2059fc | 52:00 | 14425 | 14425 | 67 | 57 | 59.6 | 2d-b | `GFX\GENE.RAW` ("Pulse Generation") |
| 22 | 205a11 | 20497a | 25f3ae | 205ac0 | 56:00 | 14481 | 14492 | 887 | 571 | 45.1 | engine | 3D (`3D\PART4.WLD`): objects in wireframe boxes, letters |
| 23 | 20516b | 20483f | 2051dd | 20520c | 58:00 | 15377 | 15379 | 446 | 446 | 70.1 | 2d-b | `GFX\TOM.RAW` and `GFX\BOZ.RAW` warped and smeared |
| 24 | 2052df | 20483f | 20483f | 20483f | 59:00 | 15825 | 15825 | 224 | 224 | 70.1 | 2d-b | `GFX\DOG.RAW` ("Biscuit Addicted") |
| 25 | 20531e | 20483f | 20483f | 20483f | 60:00 | 16048 | 16049 | 227 | 224 | 69.2 | 2d-b | `GFX\MRWAAF.RAW` ("Waaf .the dog") |
| 26 | 205bb0 | 204ee9 | 205c87 | 205c90 | 62:00 | 16272 | 16276 | 446 | 445 | 69.9 | engine | 3D (`3D\FIN.WLD`): a spiked ring growing out of the dark |
| 27 | 205ad8 | 20483f | 20483f | 205b04 | 63:48 | 16720 | 16722 | 390 | 390 | 70.1 | 2d-b | black (`GFX\BOUGE.RAW` loaded) |
| 28 | 20483f | 20483f | 20483f | 2059fc | 64:00 | 17112 | 17112 | 57 | 57 | 70.1 | 2d-b | `GFX\BOUGE.RAW` (a puppet) |
| 29 | 205368 | 20483f | 20483f | 20483f | 64:63 | 17168 | 17169 | 559 | 350 | 43.9 | 2d-b | `GFX\YOCOL.RAW` (a boy with pink hair); after the last entry 0x20538d shows the title again until the end |

After entry 29 (music 64:63), main runs 0x20538d (the title again), waits 210 refreshes and stops the music; the
program exits at record 17,729 (252.96 s of program time).

## Scopes

| scope | entries | notes |
|---|---|---|
| core | the loaders, the extender, main 0x204770, the script, the timer 0x2044cc, the frame wait 0x2045d0 | NOTES.md |
| music | LCA's MOD-Player V3.03 (0x25f698.., 0x2601dc.., 0x26e000..) | notes-music.md |
| engine | the 3D engine (0x21d440 world loader, 0x25f3ae animation step, 0x25f5b5 renderer, the sort 0x21e6ec, the blur 0x2045fa) and the 3D entries 1-4, 8, 11, 14, 22, 26 | notes-engine.md, notes-parts3d.md |
| 2d-a | entries 0, 5-7, 9, 10, 12, 13, 15 | notes-parts2d-a.md |
| 2d-b | entries 16-21, 23-25, 27-29 and the end 0x20538d | notes-parts2d-b.md |

## Files loaded (record: file)

2 REVE.EXE (the whole archive, by the extender), 11 REVE.LFL, 26 ZIC\PULSE.MOD; 31 GFX\TEXTURE.RAW, 32 GFX\REVE.RAW
(entry 0); 649 GFX\CREDITS.RAW, 650 3D\INTRO.WLD and .ANI, 652 GFX\ANTIALIA.PAL, GFX\GOURAUD.PAL (entry 1); 2737
GFX\PULSE.RAW, GFX\NEGATIF.PAL (5); 3193 GFX\WIZ.RAW (7); 4092 3D\P3D1.WLD and .ANI, 4097 ANTIALIA.PAL (8); 6329
GFX\PLAYMO.RAW (9); 7226 TEXTURE.RAW, 7228 3D\PART2.WLD and .ANI, 7230 ANTIALIA.PAL (11); 8121 TEXTURE.RAW,
GFX\SPIRALE.RAW, 8122 GFX\SPIRBLUR.PAL (12); 9912 3D\P3D3.WLD and .ANI, 9915 ANTIALIA.PAL (14); 10807 GFX\BOUCHE.RAW
(15); 11703 GFX\JUNIOR.RAW (16); 11926 GFX\MRTOMMY.RAW (17); 12623 GFX\DADA.RAW (18); 14026 GFX\GENE.RAW (20); 14482
TEXTURE.RAW, 14487 3D\PART4.WLD and .ANI, 14492 ANTIALIA.PAL (22); 15378 GFX\TOM.RAW, 15379 GFX\BOZ.RAW (23); 15825
GFX\DOG.RAW (24); 16049 GFX\MRWAAF.RAW (25); 16273 TEXTURE.RAW, 16274 3D\FIN.WLD and .ANI, 16276 ANTIALIA.PAL (26);
16721 GFX\BOUGE.RAW (27); 17169 GFX\YOCOL.RAW (29); 17519 GFX\REVE.RAW (the end).

## The capture's stack

The face sort's deepest recursion reached ESP 0x13ae4 at record 7570 (entry 11), 0x3e4 bytes below the lowest of the
extender's interrupt stacks (0x13ec8..0x140c8) and 0xde4 below the demo's stack top: it overwrote kernel bytes at
offsets 0x2e84..0x3268 (run_reve.py moved the interrupt stacks away, see NOTES.md). The run went on normally.

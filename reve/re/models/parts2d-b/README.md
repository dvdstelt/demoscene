# Models of scope "2D parts B" (script entries 16-21, 23-25, 27-29 and the end)

Python models of the routines in `../../notes-parts2d-b.md`, checked against the reference capture
(`reve/re/full.frames`, `full.log`, `full.ticks`). Run from `reve/re` inside the toolbox:

```
toolbox run -c acme303 python3 models/parts2d-b/<model>.py ...
```

| file | what | result |
|---|---|---|
| `common.py` | shared: the RAW reader, the 6-bit palette (`v >> 2`), the sine table 0x2176c8, the capture, the entry starts of `full.log` | |
| `pictures.py dump` | writes `reve/re/dump/pictures/<NAME>.png` (RGBA: the picture through the palette it is shown with; alpha 0 at the transparent index) for every picture of the scope, and `TEXTURE-tile0.png` | 11 pictures + the tile |
| `pictures.py check` | the still screens of entries 16, 17, 24, 25, 29 and the end (clear to 0, then the picture centred by 0x25f0be with index 0 transparent, or loaded to 0xa0000) | every record of each still equals the model in all 64,000 indices and the 768 DAC values: 223 + 696 + 223 + 223 + 350 records, and the end screen 210 records |
| `plane.py check` | integer-exact model of 0x259de4 / 0x259ed0 / 0x259f70 / 0x25a048 and the row drawers 0x259a08 / 0x259b50 / 0x259c98, with the angles of the retrace routines 0x2050ce / 0x2050ed / 0x2051dd | entry 18: 618 of 618 records exact, entry 19: 784 of 784, entry 23: 446 of 446 (all 64,000 indices), with record = start + n (n = retrace calls) |
| `plane.py shot A1 B1 A2 B2 TEX out.png` | one frame of the model at given angle words (TEX 18: tile + DADA, 23: TOM + BOZ) | |
| `plane.py` `draw_float`, `compare_float` | the floating-point form of the same plane (notes section 4.4) and how many indices it shares with the integer model | 56-62 % of the indices for entries 18/19 (64 x 64 noise texture), 81-87 % for entry 23; the texel coordinates differ by about 1-4 texels, from the integer eye and corner points of the original |
| `strobe.py table` | the negative table 0x25a5ff for GENE and BOUGE and how far it is from the true negative | mean 2.2 (GENE) / 1.0 (BOUGE) six-bit units per channel, pixel-weighted; max 18 / 15 |
| `strobe.py check` | the strobe counters of 0x205950 / 0x205b04 per drawn frame, the negative frames through the model table, then 0x2059fc | entry 20: 398 of 398 records, entry 27: 390 of 390 (black, picture or negative in all 64,000 indices, the frame counter from `full.ticks`); entries 21 / 28: the picture in every record from start + 1 |

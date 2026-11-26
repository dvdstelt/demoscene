# models/landscapes — scripts of the "landscapes" scope

Python models and checks for the third part 0x3554b and the fifth part 0x38520 with its set-up 0x38469
(notes: [../../notes-landscapes.md](../../notes-landscapes.md)). Run them from this folder inside the toolbox
(`toolbox run -c acme303 python3 <script>`); they read `../../unpacked/korso2.bin`, `../../snaps/*.snap` and
`../../full.*`.

| script | what it does |
|---|---|
| `common.py` | helpers: the program image, snapshots (`Mem`), the capture (`Capture`), `full.ticks`, 32-bit arithmetic (`idiv_shl8` = `cdq; shl eax, 8; idiv`) |
| `framemap.py` | which capture record first shows each drawn frame (found by the noise, which advances 16,000 generator steps per drawn frame) and the callback count the frame was drawn with; cached in `framemap_<part>.npy` |
| `part3.py` | model of part 0x3554b: callback 0x357b3 (`State`), main loop + 0x353be + line drawer 0x350c8 (`Renderer.draw`) |
| `test3.py` | renders every drawn frame of part 3 and compares indices and palette with the capture: `python3 test3.py 0 1112` |
| `probe3.py` | the one mismatching record (3269): shows that the capture tore there (copy to video memory interrupted by the GUS interrupt) |
| `terrain.py` | model of the set-up 0x38469: 0x374da (midpoint displacement) and 0x377b2 (smoothing); compares with the map in `part_2c26f.snap`; writes `map_raw.npy`, `map.npy` |
| `part5.py` | model of part 0x38520: callback 0x38985 (`State`), 0x38331 projection, edge sort, line drawer 0x3801c with the floating horizon (`Renderer`) |
| `textprint.py` | the shared text printer 0x4cacc as these parts use it (pages, centring, opaque glyphs) |
| `test5.py` | renders every drawn frame of part 5 (with the captions) and compares with the capture: `python3 test5.py 0 1100` (about 15 min) |
| `viz5.py` | model, capture and difference of one drawn frame of part 5 side by side (`viz5.png`): `python3 viz5.py 30` |
| `pal5.py` | checks the palette (fade) of every record of part 5 |
| `captions5.py` | which caption page each drawn frame of part 5 shows |
| `zrange5.py` | depth range of part 5's vertices; frames with a vertex behind the eye |
| `stats.py` | glow overflow statistics (`python3 stats.py 3 7`, `python3 stats.py 5 5`) |
| `dump3.py`, `sine.py` | dumps of part 3's static data and of the sine table in the snapshots |

Results: part 3, 1112 of 1113 drawn frames equal to the capture in all 64,000 indices and the palette (the 1113th
differs only because the capture tore); part 5, all 1047 drawn frames equal, captions included; the height map
equal to the snapshot in all 65,536 bytes.

Generated files (`*.npy`, `*.png`, `*.jpg`, `*.log`) are made from the original and are not committed.

# models/gfx

Models of the gfx scope (`../../notes-gfx.md`). Run inside the toolbox from `clone/re`:

| file | what it does |
|---|---|
| `texgen.py [--snap snaps/t11.snap] [--png dump/textures] [--fyl2x old\|new]` | exact model of the texture precalc (main 0x400240..0x4008b4) and the grid texture 0x4033a7, with a truncating soft float; compares maps A/B/C, colour tables 1/2 and the grid texture with a snapshot byte for byte (identical at t11, t2316, t6156, t9324, t11148) |
| `texplain.py [--png N]` | the same formulas in numpy doubles (as a port would write them), the continuous variants and their distance from the original bytes; `--png N` writes N× continuous textures to `dump/textures/hires_*.png` |
| `pictures.py [--check snaps/t11.snap] [--out dir]` | decodes the ten stored pictures like 0x403f53 (palettes A, B, C) and writes `dump/pictures/<name>.png` (RGBA) plus `extra/` (indices, 8× views, part 11's mosaic table); compares with the snapshot copies |
| `overlay_check.py [ticks]` | boxes, captions and glyphs of parts 1, 2, 3, 4, 5, 11 against `full.frames` |
| `fade_levels.py` | replays the overlay fade levels (part 1 title and credits, part 11 title, part 3) from `full.flips` and compares them with the capture |

All outputs go to `clone/re/dump/` (ignored by Git: made from the original).

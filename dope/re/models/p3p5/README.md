# models/p3p5: p3 ("7800 PHONG polys") and p5 ("ENV mapping", "BUMP glow")

Scripts behind `../../notes-p3p5.md`. Run them inside the toolbox from `dope/re`
(`toolbox run -c acme303 python3 models/p3p5/<script> ...`). They read `unpacked/p3.bin`, `unpacked/p5.bin`,
`snaps/part_p3.snap`, `snaps/part_p5.snap` and `full.frames`. Everything they write (`out/`, `*.npz`) is derived
from the original and ignored by Git (`.gitignore` here).

| script | what it does |
|---|---|
| `peek.py p3 SEG:OFF LEN [b\|w\|d]` | hex dump of an unpacked image |
| `p3_data.py`, `p5_data.py` | palettes as the parts load them (`out/p3_pal.npy`, `out/p5_pal.npy`, needed by the verifiers), previews of the maps, picture and labels |
| `pictures.py` | writes the hand-drawn pictures as palette PNGs to `dope/re/dump/pictures/` (p3-nebula, p3-label-phong, p5-envmaps, p5-label-env, p5-label-bump, p5-swirl, p5-chequerboard) |
| `bumpmaps.py` | nibble statistics of the two bump textures, curl test, least-squares height-field reconstruction (`out/*bump*`) |
| `phongfit.py` | fits index = f(distance) to p3's Phong map |
| `run_dope.py` | copy of `../../run_dope.py` (unchanged), imported by `hookrun.py` |
| `hookrun.py p3\|p5 SNAP OUT.npz --until F [--dump F1,F2]` | runs a part from its snapshot with hooks: per drawn frame the retrace count, the camera object (object 0) and object 1, the label position, p5's bump table; the script-step calls; the engine frame buffer just before it goes to VGA for the chosen frames. `hookrun.py p3 snaps/part_p3.snap X/p3hook.npz --until 13640 --dump 10300,11000,11800,12500,13000,13300` and `hookrun.py p5 snaps/part_p5.snap X/p5hook.npz --until 25590 --dump 21300,21600,22000,22400,23300,23600,24200,24800,25200` (about 10 minutes each) |
| `engine_model.py` | integer model of the engine path these parts use: sine table, matrix, vertex transform and projection, the two per-vertex shading attributes, 16-bit back-face test, depth key, radix sort, triangle set-up and the span loops with their packed 32-bit arithmetic |
| `p3model.py`, `p5model.py` | whole frames: background, object (p3 Phong+bump filler; p5 env-only filler and bump filler), label |
| `verify_p3.py NPZ [DIR]`, `verify_p5.py NPZ [DIR]` | compare the models with the dumped engine frame buffers (and, for p5, the bump-table formula with all 4480 tables); DIR gets side-by-side PNGs |
| `capmatch.py NPZ full.frames` | which capture frame shows a dumped frame buffer (the copy frame) |
| `steps.py NPZ` | step calls vs the summed N (overshoot), and the per-retrace integration of the camera state |
| `handover_p5.py full.frames` | p5's cross-fade to p6's picture: captured palettes vs the mixing formula |
| `quirk_p3.py NPZ` | p3 frames with and without the 15-bit wrap of the bump coordinates (`out/p3_quirks_*.png`) |
| `spans_p5.py`, `spans_p5b.py` | p5 ENV: object extent and span lengths (8-bit span counter, right-edge clip) |
| `cull16.py`, `depth_range.py` | 16-bit back-face test errors; nearest vertex distance |
| `cmpfill.py` | instruction diff of p3's filler and p5's bump filler |

Results (section 8 of the notes): p3 6/6 frame buffers identical; p5 9 frame buffers identical except one pixel in
one ENV frame; all 4480 p5 bump tables reproduced; camera state matches the per-retrace integration at every drawn
frame of both parts; captured palettes and the hand-over cross-fade palettes match.

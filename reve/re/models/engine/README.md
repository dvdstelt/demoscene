# Reve: models of the 3D engine (engine scope)

Python models that check `reve/re/notes-engine.md` and `reve/re/notes-parts3d.md` against the original. Run them
inside the toolbox from this folder: `toolbox run -c acme303 python3 <script> ...`. They read `../../unpacked/`
(the image and the archive's files), `../../full.frames` / `full.ticks` and the snapshots in `../../snaps/`.

| file | what it is |
|---|---|
| `engine.py` | the model: `World` (WLD loader 0x21d440 with normals and boxes), `Anim` (ANI and the step 0x25f3ae), `Renderer` (render 0x25f5b5: camera, cull, sort, transform, near clip, lighting, the rasterizers and screen clippers, PART4's boxes and lines), `aa_pass` (0x2045fa), `fin_pass` (0x2046c7). Integer-exact |
| `run_reve.py` | a copy of `../../run_reve.py` with `--dumps`: whole-memory dumps at chosen addresses |
| `dumps.py` | reads those dumps (and snapshots) and turns them into model state |
| `test_render.py DIR NAME` | per dump pair (render start 0x25f5b5 / end 0x25f65b): model render vs the back buffer [0x205e8c], and every transformed vertex of the drawn objects |
| `test_anim.py DIR NAME [--snaps ...]` | the ANI model vs the camera and object state of dumps and snapshots: prints the number of steps that reproduces each |
| `test_frame.py DIR NAME ENTRY` | dump state → model render → the entry's 2D passes, vs the capture records |
| `model_part.py ENTRY [--every N]` | end to end, from the archive's files and the refresh number only: sampled capture records of a 3D entry, classified as exact (with the state's age), torn, or split; counts culls decided by stale outcode bytes and whether they matter |
| `painter.py ENTRY` | the same frames with the original's painter's order and with a per-pixel depth test: how much of the picture the order decides |
| `check_isqrt.py` | runs the square root 0x2175dc in unicorn against floor(sqrt) |
| `tables.py` | GOURAUD.PAL and ANTIALIA.PAL against TEXTURE.RAW's palette; the palette of each 3D entry in the capture |
| `fit_tables.py` | fits the formulas the two tables quantise (brightness factor per level; nearest-average) |
| `rates.py` | frame rates of the 3D entries in the capture, and the AA pass's unwritten border |
| `wld.py` | prints a survey of every WLD / ANI file (objects, face types, colours, textures, keys) |

## Regenerating the dumps

The dumps are memory images of the original, so they stay out of Git (`dumps_*/` is ignored). From `reve/re`:

```
toolbox run -c acme303 python3 models/engine/run_reve.py --load --snap snaps/f700.snap --frames 712 \
    --out models/engine/dumps_intro/x --dumps 25f5b5,25f65b --dump-max 6 --dumpdir models/engine/dumps_intro
```

(`mkdir` the folder first.) The set used for the notes: `dumps_intro` (f700 → 712), `dumps_p3d1a` (f4200 → 4214),
`dumps_p3d1b` (f5600 → 5614), `dumps_part2` (f7700 → 7714), `dumps_p3d3` (f10500 → 10514), `dumps_part4`
(f14700 → 14714), `dumps_fin` (f16100 → 16310 with `--dump-from 16290`). Then for example
`toolbox run -c acme303 python3 test_render.py dumps_part4 PART4`.

## Results

- `test_render.py`: 37 renders (INTRO 5, P3D1 10, PART2 5, P3D3 6, PART4 5, FIN 6): **0 pixels different**, all
  transformed and projected vertices of the drawn objects equal. Exercised: texture and texture + Gouraud faces,
  flat faces (PART4), near-plane clipping (up to 89 faces a frame in PART2), screen clipping and fans, the boxes.
- `test_anim.py`: the camera (integer and fraction) and all object positions and angles equal the original's in 36
  dumps and 10 snapshots at n = refresh − r0 (INTRO modulo its 626-refresh loop; in the capture P3D1 is one step
  behind after its lost timer interrupt at refresh ~5140).
- `test_frame.py`: 36 captured screens reproduced exactly, one record torn.
- `model_part.py` (`--every 37`, FIN `--every 20`): 213 records; 191 exact, 17 torn (exact above and below the
  tear), 2 split states, 3 PART4 records that differ only in box lines to corners behind the camera (the original
  draws them to screen positions from earlier frames).
- `check_isqrt.py`: 0x2175dc = floor(sqrt) on 100,023 values.
- `painter.py`: a depth test instead of the painter's order changes 0.36 % (P3D3) to 4.7 % (PART4) of the drawn
  pixels by more than 12/63: whole surfaces swap.

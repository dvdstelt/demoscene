# models/sprites — scripts behind notes-sprites.md

Run everything from `inside/re` inside the toolbox (`toolbox run -c acme303 python3 ...`). Nothing here contains
data from the original; the run outputs (`runs/`) and pictures the scripts write are generated from it and stay out
of Git (regenerate them with the commands below).

| file | what it does |
|---|---|
| `pics.py OUTDIR` | writes the part's pictures (backgrounds, sprites, shadows) and tables (.shd, .qnt, .sdw) from INSIDE.DAT as PNG |
| `blend.py` | shows that nigga1.shd / nigga2.shd are 50 % blend tables (symmetric, identity diagonal, ≈ nearest palette colour to the average) |
| `shades.py` | fits nigga9/4.qnt (64 light levels: colour × L / 32, clamped per channel above 32) and shadow9/4.sdw (16 levels: colour × (1 − s / 24)) |
| `snap.py SNAP` | prints the part's pointers, objects, materials, the light and the counters from a snapshot |
| `run_inside.py` | copy of `../../run_inside.py` with `--hook FILE` and a fix for resuming from a snapshot (the shared copy restarts its refresh timer at 0 on `--load`, so it records refreshes without emulating them) |
| `hook_dump.py` | hook for `run_inside.py --hook`: per drawn frame the background before the sprite, the finished frame, the warp parameters, camera / light / scene / objects (matrices, vertex and face arrays), materials, and every rasterizer call with copies of its face, vertices and material; `DUMP_FROM=<refresh>` skips earlier frames. Writes `<out>.dump.pkl` |
| `warp_model.py DUMP` | the warp background of pictures 1 / 2: texture coordinates (exact), projection (to 1/16 px), picture (affine, 2 texture reads, .shd blend) against the dumped background |
| `box_model.py DUMP [i] [png]` | the shadow scenes: per-vertex light levels (exact), camera transform and projection, picture (textured Gouraud through .qnt, shadow quad through .sdw) against the dumped background |
| `angles.py DUMP...` | accumulated rotation of the warp grid and the box (multiples of 0.01 rad) per frame |
| `tfit.py DUMP` | which value of the music clock M = [0x4248c4] the light / shadow animation used: T = 4 N − 155 for the frame shown at record N |
| `uncovered.py` | does the warp grid cover the screen (corner margins over the visible rotations; check against full.frames) |

Runs used for the notes (each a minute or less; resumed from the part snapshots):

```
cd inside/re/models/sprites
toolbox run -c acme303 python3 run_inside.py --load --snap ../../snaps/f4255.snap --out runs/a --frames 4275 --hook hook_dump.py
toolbox run -c acme303 env DUMP_FROM=4605 python3 run_inside.py --load --snap ../../snaps/f4255.snap --out runs/b --frames 4640 --hook hook_dump.py
toolbox run -c acme303 python3 run_inside.py --load --snap ../../snaps/f9790.snap --out runs/c --frames 9815 --hook hook_dump.py
toolbox run -c acme303 env DUMP_FROM=4780 python3 run_inside.py --load --snap ../../snaps/f4255.snap --out runs/d --frames 4964 --hook hook_dump.py
toolbox run -c acme303 env DUMP_FROM=10220 python3 run_inside.py --load --snap ../../snaps/f9790.snap --out runs/e --frames 10576 --hook hook_dump.py
cd ../.. && toolbox run -c acme303 python3 models/sprites/warp_model.py models/sprites/runs/e.dump.pkl
```

The resumed runs reproduce `full.frames` exactly (every pixel and DAC entry of records 4255..4274 compared).

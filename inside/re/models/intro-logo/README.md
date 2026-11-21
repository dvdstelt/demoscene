# models/intro-logo

Models and checks behind `../../notes-intro-logo.md` (the intro's light drops and the logo part). Run them inside
the toolbox from this folder (`toolbox run -c acme303 python3 <script> ...`). Runs, dumps and PNGs go to a scratch
folder, not here.

| file | what it does |
|---|---|
| `run_inside.py` | copy of `../../run_inside.py` with `--hooks FILE` (a Python file with `install(mu, st, args)`) and `--hook-arg`; a run resumed with `--load` starts its refresh clock at the snapshot's refresh (the shared copy starts at 0) |
| `hooks_intro.py` | records every intro tick (X, Y, seed), every frame's drops and seed, and for chosen frames the light buffer and draw target: `run_inside.py --load --snap ../../snaps/f38.snap --out DIR/intro --frames 1450 --no-frames --hooks hooks_intro.py --hook-arg 0-60,700-800,960-975` |
| `hooks_logo.py` | records the logo's matrix per tick and per frame the engine's object (vertices, faces), camera, light, sorted face list and the draw target after the 3D scene and after the overlays: `--snap ../../snaps/f1446.snap --frames 2150 --hooks hooks_logo.py` |
| `intro_model.py` | tick routine, drops, blur, shade; checks every tick and frame of a dump; `--chain` replays the whole intro in the captured event order |
| `intro_start.py` | the intro's start state: drops from seed 1, the generator's state at the first frame (0xf83d63af = 63 calls after srand(0x45eafc)), checked against `snaps/f38.snap` |
| `intro_schedule.py` | the frame schedule (ticks before each frame's drop loop), mid-loop ticks, the 3:00 event; writes the table |
| `logo_model.py` | step matrix, normals, vertex light, transform, back faces, outcodes, sort, near clipping, affine rasteriser; compares with the dump (`--geom`: geometry of every frame; `--png DIR`: capture / model / difference) |
| `logo_overlay.py` | the mask and text overlays, and the finished frames against `full.frames` |
| `logo_light.py`, `logo_normals.py` | the point light per vertex; the engine's face and vertex normals and R = S^k |
| `logo_compare.py` | affine (original) vs perspective-correct vs depth buffer |
| `sro.py` | reads a SurRender `.sro` as the loader 0x410bb0 does |
| `pics.py` | writes the intro's and logo's pictures, palettes and shade tables as PNGs |
| `shades.py`, `shades2.py` | intro.shd, logo.shd and inmask.shd against their palettes |
| `palette_check.py` | the DAC in `full.frames` during the intro's fade-in and the logo's flashes |

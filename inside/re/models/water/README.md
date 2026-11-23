# models/water — checks for notes-water.md

Run from `inside/re/models/water` inside the toolbox (`toolbox run -c acme303 python3 <script>`). Generated files
(`*.pkl`, `*.png`, `*.log`, `*.frames`, ...) are git-ignored.

| file | what it is / what it checks |
|---|---|
| `run_inside.py` | copy of `inside/re/run_inside.py` with two changes: a resumed run starts its refresh clock at the snapshot's own refresh (as in `models/core/`), and `--water-dump FILE` installs `water_hooks.py` |
| `water_hooks.py` | per call of 0x4078d4 (one drawn water frame): refresh, interrupt counter c = [0x4248c4], rand seed, the 8 fish (position, matrix, vertices), the obuke matrix, the projected fish positions, a / s / r of 0x407400; for the frames in `--water-full` also both height fields before and after the ripple, the back buffer after the ripple and after the 3D render, and every face the two rasterizers get (face and its 3 vertices); per refresh the obuke matrix in the tick routine |
| `snap.py` | snapshot reader |
| `fish_seed.py` | finds the water init's 24 rand() calls in the sequence after the flight init's srand(0x45eafc): calls 39..62, seed 0xef012bc7 before them (the core scope's `start.events` agrees) |
| `fish_init.py` | replays the init's fish placement and the first frame's fish update from that seed: the 8 positions and matrices in `snaps/f8468` exactly (c = 33719 for the first frame) |
| `seed_start.py` | the rand() seed at the start of the part's first frame in the capture: 0xc249a56a (11 calls in the first frame) |
| `water_model.py` | the model of 0x4078d4 (0x407400 per fish, the drops, the ripple 0x401234) and of the obuke's rotation 0x407c78, checked against a dump: `python3 water_model.py run2` |
| `envfit.py` | the per-vertex texture coordinates the rasterizers get against the engine's environment mapping 0x41acc4: 0 of ~770 vertices differ per frame (run1) |
| `geometry.py` | object → world → screen for the obuke and the fish against the vertices the engine projected (max 1/16 pixel, its truncation); object sizes |
| `tables.py` | what water.int (a 50 % blend) and water.shd (64 light levels, unused here) hold |
| `frame_times.py` | drawn frames of the part in a run: c − 4 × refresh, refreshes between shown frames, mean rate (`--list` prints all) |
| `watch_uv.py` | finds the code that writes the obuke's vertex records (it found 0x41acc4's u,v writes at 0x41ada2 / 0x41adb2) |
| `check_run.py` | compares a resumed run's `.frames` with `full.frames` (run1 without `--no-frames`: 92 of 92 records identical) |

The runs (from `inside/re`):
```
toolbox run -c acme303 python3 models/water/run_inside.py --load --snap snaps/f8468.snap --out models/water/run1 \
    --frames 8560 --water-dump models/water/run1.pkl                       # 8 s, frames 0..5, 40 full
toolbox run -c acme303 python3 models/water/run_inside.py --load --snap snaps/f8468.snap --out models/water/run2 \
    --frames 9791 --water-dump models/water/run2.pkl --no-frames \
    --water-full 0,1,300,301,600,601,900,901,1100,1101,1160,1161           # the whole part, 2 minutes
```
Results (run2, 1170 frame-to-frame steps): the ripple (height field and background rows 2..197) is exact in every
full frame; fish positions, matrices, tails, projections, the seed and the height-field writes are exact in every
frame, given the c each fish read (76 fish updates read c + 1 or c + 2 because a timer interrupt came during the
frame's 8 updates; in 4 a tick came inside one fish's update, between its reads of c); the obuke turns by exactly
0.0185 rad in each of the 1321 tick routine calls.

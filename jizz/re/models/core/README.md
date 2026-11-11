# models/core: checks for jizz/re/notes-core.md

Python models of the core routines of JIZZ.EXE and the scripts that compare them with the snapshots and the capture.
Run from `jizz/re` inside the toolbox (`toolbox run -c acme303 python3 models/core/<script>`). They need
`jizz_img.bin`, `snaps/` and `full.frames` / `full.flips` (all generated, not in Git).

| file | what it does |
|---|---|
| `snap.py` | loads a snapshot (`snaps/t<tick>.snap`) and reads memory |
| `frames.py` | reads `full.frames` records (320x200 5:6:5), `full.ticks`, `full.flips` |
| `rng.py` | the random generator 0x141f6 |
| `layout.py` | main's 36 buffer pointers vs the t430 snapshot |
| `precalc.py` | 0x6592 spot, 0x6b16 1/sqrt, 0x6cc0 squares, 0x10fa2 sine, 0x16f4f polar, 0x11dfe random, 0x6cdd marching-cubes cases vs t430 (all exact) |
| `colour.py` | 0x1a336 / 0x1a236 colour tables, 0x11f73 TBL LUT, 0x13d73 / 0x13eec converters vs snapshots (exact); `--flash runs/f1365.flash.pickle`: the tables of every frame through the 03:10 flash |
| `fade.py` | 0x1131b fade tables vs snapshots; 0x17979 / 0x17999 / 0x179f6 vs the captured end-picture fade (130 of 130 frames exact) |
| `glow.py RUN.dump.pickle` | 0x183a9 (+ 0x1122f / 0x1145c lines and the RNG), >> 3, 0x11eb2, 0x12111, 0x11396 vs buffer dumps (exact) |
| `run_jizz.py` | copy of `jizz/re/run_jizz.py` with `--plugin FILE` (exec'd after loading a snapshot; may define `on_tick(t)` and `atexit_plugin()`) |
| `plug_calls.py` | plugin: logs calls of the core helpers with their arguments, RNG calls by site, colour-table reads by EIP (`<out>.calls.txt`) |
| `plug_dump.py` | plugin: dumps the glow buffers / page around 0x183a9 .. 0x11396 in 0x17003 and 0x169e7 (`<out>.dump.pickle`) |
| `plug_flash.py` | plugin: the colour tables after each frame start / flash (`<out>.flash.pickle`) |
| `bufuse.py` | the instructions that use each of main's buffer pointers |
| `xr.py`, `xrange.py`, `ctx.py` | static helpers: references to an address, references into a range, call sites with the instructions before them (`ctx.py` caches its decode in `insns.pickle`) |

Runs used for the notes (outputs go to `runs/`, generated, delete after use):

```
python3 models/core/run_jizz.py --load --snap snaps/t<T>.snap --out models/core/runs/c<T> --ticks <T+120> --plugin models/core/plug_calls.py
python3 models/core/run_jizz.py --load --snap snaps/t1365.snap --out models/core/runs/d1365 --ticks 2160 --plugin models/core/plug_dump.py
python3 models/core/run_jizz.py --load --snap snaps/t2515.snap --out models/core/runs/d2515 --ticks 3050 --plugin models/core/plug_dump.py
python3 models/core/run_jizz.py --load --snap snaps/t1365.snap --out models/core/runs/f1365 --ticks 2275 --plugin models/core/plug_flash.py
```

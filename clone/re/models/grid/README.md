# grid scope models

Python models for `clone/re/notes-grid.md` (Craft's grid filler 0x402970 and parts 11, 2, 5, 4). Run them from
`clone/re` inside the toolbox (`toolbox run -c acme303 python3 models/grid/<file>.py`). Everything they write goes
to `clone/re/dump/grid/` (git-ignored).

| file | what it does / proves |
|---|---|
| `run_clone.py` | copy of `clone/re/run_clone.py` with `--dump FILE [--dump-every N] [--dump-max M]`: per frame of parts 2, 4, 5, 11 a record with the watched variables at the part entry, at the filler call and at the part return, the 41 x 21 grid (0xea57b4), the timer, and the draw page before the part, after the filler and at the return |
| `fpusym.py` | symbolic x87 tracer: `fpusym.py START:END[,..] name=addr ..` prints every store / fistp / compare of a straight instruction path as an expression (used to read the long FPU sequences of parts 2, 4, 5) |
| `filler.py` | the filler 0x402970 as an integer model, the tables T (0x40347a) and the bilinear weights (0x4033ee) from their formulas; checks the tables against the snapshots and the filler against every dumped frame: **bit-exact** (parts 2, 5, 4: 445 frames) |
| `gridparts.py` | the grids of parts 5, 2, 4 from their state, and the state recurrences; part 5 **bit-exact** (275 frames at two CPU speeds), part 2 98.3-99.0 % of the entries exact, the rest +-1..2 (1/256 texel), part 4 99.7 % exact, the rest +-4 (one step of its x4 coordinates); all state updates exact |
| `part11.py` | part 11's tiles and state: **bit-exact** on 150 frames (the title blend is the core scope's 0x40442c) |
| `clocks.py` | closed forms of the dt-integrated state as functions of the timer (start and event switches at the timer of the frame before the event frame); checked on the capture-speed dumps and on 55.5 M instructions-per-second dumps (`p5slow`, `p2slow`) |
| `event_lag.py` | for each event of these parts: the row start, the timer from which the change is integrated in the capture, and the lag |
| `perpixel.py` | the per-pixel functions the remake evaluates (u, v, light at any (x, y)) and the colour decode; renders them at 320 x 200 next to the original frames (mean difference 3.4..4.9 of 255: block interpolation and 8-bit steps) and counts grid neighbours more than 128 texels apart |
| `lag_effect.py` | part 2: how much the one-frame position lag changes the picture at 15, 41 (capture) and 84 fps |

Typical sequence:

```
toolbox run -c acme303 python3 models/grid/run_clone.py --load --snap snaps/t2316.snap --out dump/grid/p2 \
    --ticks 3090 --no-frames --dump dump/grid/p2.pkl --dump-every 4         # likewise t6156 (5), t9324 (4), t1645 (11)
toolbox run -c acme303 python3 models/grid/filler.py
toolbox run -c acme303 python3 models/grid/gridparts.py 2 4 5
toolbox run -c acme303 python3 models/grid/clocks.py 5 dump/grid/p5.pkl full.flips
```

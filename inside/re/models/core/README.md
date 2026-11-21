# models/core: checks behind notes-core.md

Run from `inside/re` inside the toolbox: `toolbox run -c acme303 python3 models/core/<script>.py`. They read
`full.ticks`, `full.frames`, `full.gus`, the snapshots and `../original/INSIDE.DAT`; nothing here is generated
from the originals except `palette_steps.json` (record numbers measured by `palette_model.py`).

| script | what it checks or prints |
|---|---|
| `music_clock.py` | the music clock: song row R starts in callback c_R = floor((1200 + 768 R) / 35) + 1 (rows of order 36: 2,304 units), against every row change in `full.ticks`: 2,327 of 2,327 (`--no36` without the order 36 rule: 23 misses) |
| `player_ticks.py` | every timer interrupt with GUS writes after the music start is a player tick of the model (period 200 for ticks 1..6, then 192, 4 ticks per row, 12 in order 36): 3,035 of 3,035 |
| `lost.py` | records where the part tick counter or the frame clock advanced differently from the interrupts (the lost callback at record 3045, the skipped retrace work at record 11967) |
| `rows.py` | row changes with record and interrupt count |
| `palette_model.py` | the palette system (0x400364..0x400612) with x87 rounding, started from snapshot f38 and driven by the script's palette routines: equals the DAC of every record 38..13080. `--pc64` uses 64-bit precision instead of 53 (same result); `--passes` lists the event groups. Writes `palette_steps.json` (entry → record of its first fade step) |
| `script_final.py` | the timing table of notes-core.md 1.4 (needs `palette_steps.json`) |
| `rates.py` | frames shown per refresh per frame routine |
| `palptrs.py` | which INSIDE.DAT palette each script routine passes to 0x400364 (from snapshot f38) |
| `rand_steps.py` | rand() steps between the seeds found in the snapshots |
| `run_inside.py` | copy of `../../run_inside.py` with `--events FILE` (logs script entries, frame routine starts with the FPU control word, flips, every timer callback with its guards and the FPU control word, rand / srand calls, AMP player ticks) and a fix for `--load` (`next_frame` restored, otherwise a resumed run does not emulate) |
| `latency.py EVENTS` | from an events file: frames drawn / shown / replaced, and M at the retrace callback that showed a frame minus M at its frame routine's start |
| `rand_calls.py EVENTS` | from an events file: rand / srand calls grouped by calling routine, with the seeds; checks the LCG |
| `dump.py`, `calls_in.py`, `owner.py` | reading aids: listings, calls and global stores of a routine, which routines reference a global |

The events files used in the notes (regenerate them into a scratch folder, they are emulator output):

```
toolbox run -c acme303 python3 models/core/run_inside.py --out /tmp/x/start --frames 160 --no-frames --events /tmp/x/start.events
toolbox run -c acme303 python3 models/core/run_inside.py --load --snap snaps/f1446.snap --out /tmp/x/s1446 --frames 1600 --no-frames --events /tmp/x/s1446.events
toolbox run -c acme303 python3 models/core/run_inside.py --load --snap snaps/f4255.snap --out /tmp/x/s4255 --frames 4330 --no-frames --events /tmp/x/s4255.events
```

Their `.ticks` equal the matching lines of `full.ticks`.

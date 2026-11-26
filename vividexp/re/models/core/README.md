# models/core: checks behind notes-core.md

Run from `vividexp/re` inside the toolbox: `toolbox run -c acme303 python3 models/core/<script>.py ...`. They read
`full.frames`, `full.script`, `full.ticks` and the archives in `unpacked/vivid_img.bin`. `runs/` holds emulator
output, listings and pictures made from the original: scratch, never commit it.

| script | what it does |
|---|---|
| `core_model.py` | Python ports of InitPCX 0x401884, PutPCXSYNC 0x401c2c, the palette 0x401ed0 / 0x4020b8 / 0x402054 / 0x401fbc, ClearSCR and the zoom 0x402494 / 0x40ccb8, plus `replay()`, which runs a part's script lines at the times `full.script` logged them (before or after the retrace of their refresh period) and steps fades / zoom frames at each retrace during a wait. `core_model.py full.frames full.script FIRST LAST` compares DAC and VGA with part I's records (effect sections skipped) |
| `zoomcheck.py full.frames full.script` | every record of the six zooms equals the model frame of that refresh or of the previous one (2,003 / 852; record 4659 is a copy under way) |
| `zoomdiff.py full.frames full.script K...` | where model and capture differ for some records, with the zoom parameters |
| `part2check.py full.frames full.script` | part II: the flash before the first bump (DAC) and the greetings picture (VGA and DAC) |
| `fadefit.py full.frames P1 P2 STEPS FIRST LAST` | which fade step(s) reproduce each record's DAC exactly |
| `clock.py full.ticks` | frame counter vs record, refreshes per row, the record at which each order's row 0 is first polled |
| `run_vivid_pos.py` | copy of `../../run_vivid.py` that also writes `<out>.pos`: after every timer interrupt the time, refresh period, line, frame counter, MIDAS's live order / row and the demo's polled copy |
| `rowdelay.py RUN.pos` | delay from the player's row change to the retrace at which the demo sees it |

Runs used in the notes (scratch output in `runs/`):

```
toolbox run -c acme303 python3 models/core/run_vivid_pos.py --load --snap snaps/f2611.snap --out models/core/runs/z2611 --frames 2900 --no-frames
toolbox run -c acme303 python3 models/core/run_vivid_pos.py --out models/core/runs/start --frames 60 --no-frames
toolbox run -c acme303 python3 models/core/rowdelay.py models/core/runs/z2611.pos
```

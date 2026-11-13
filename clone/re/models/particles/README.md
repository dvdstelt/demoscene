# Models of the particle parts (1, 3, 6)

Notes: [`../../notes-particles.md`](../../notes-particles.md). Run from this folder inside the toolbox
(`toolbox run -c acme303 python3 <script>`); they read `../../unpacked/`, `../../snaps/` and `../../full.*` and
write nothing.

| script | what it checks or shows |
|---|---|
| `emu.py` | unicorn harness: loads a snapshot and calls one routine of the original (fninit + control word 0x1f7f first) |
| `fpu.py` | float32 stores with truncation, x87 64-bit truncation (Fractions), main's t70 / dt |
| `blur_model.py` | the radial blur 0x403df7 in Python; `python3 blur_model.py` compares it byte for byte with the routine |
| `parts_model.py` | parts 1, 3, 6 frame by frame; `python3 parts_model.py PART FRAMES [SNAP] [frame:addr=value ...]` compares the model with the part routine in unicorn on the same state, frame after frame (dt from `full.flips`, events poked) |
| `capture_check.py` | `python3 capture_check.py 1a|1|3|6 2000`: steps the original and the model with the capture's dt and events from a snapshot and compares both with `full.frames` |
| `fps_table.py` | the capture's frame step per 16 rows in parts 1, 3, 6 |
| `blur_study.py` | the in-place blur against order-free formulations (fixed point, log-doubling, single echo) |
| `rate_study.py` | part 1 at fixed frame steps: how the emitter and the count advance (`python3 rate_study.py 0.75 1.33 2.7`) |

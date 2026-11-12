# models/p4 — scripts behind notes-p4.md

Run from `dope/re` inside the toolbox (`toolbox run -c acme303 python3 models/p4/<script>`). `out/` holds what the
hooked run records (made from the original, ignored by git); run `hook_p4.py` first (about 6 minutes).

| script | what it does |
|---|---|
| `run_dope.py` | unchanged copy of `dope/re/run_dope.py` (imported by `hook_p4.py`) |
| `hook_p4.py` | runs p4 from `snaps/part_p4.snap` to p5's start with hooks: `out/frames.log` (state at every frame-loop call: IRQ count, counters, fade, label, fire phase / position / offset, landscape position, CRC32 of the fire buffer), `out/retrace.log` (every IRQ), `out/init.npz` (tables and buffers after set-up), `out/fire_<n>.npy`, `out/land_<n>.npz` (landscape buffers), `out/lbm.npz` (decoded chequerboard) |
| `fire_model.py` | the fire tables, feedback step, composite and palette from the image only; checks the tables against `init.npz` and the fire buffer after all 3715 steps against the CRCs and dumps |
| `land_model.py` | the voxel landscape 1efc from the image only; checks the dumped buffers (with a split for an IRQ inside the frame) |
| `timeline.py` | phase, position, fade, label and landscape position as functions of the IRQ count; checks every logged call |
| `check_capture.py` | whole frames (indices and DAC) against `full.frames` |
| `check_torn.py` | explains the remaining frames row by row as torn composites of the last loop iterations (slow: pure Python landscape) |
| `fire_float.py` | compares the integer feedback with float forms over the part |
| `fit_tmap.py` | fits the polar formula of the swirl map 07d9 |
| `dump_pictures.py` | writes the pictures and maps to `dump/pictures/p4-*.png` |
| `dumpvars.py` | prints p4's static variables and small tables |

# models/p2 — scripts of the p2 scope (notes-p2.md)

Run from `dope/re` inside the toolbox (`toolbox run -c acme303 python3 models/p2/<script>`).

| file | what it does |
|---|---|
| `script.py` | parses p2's main script 0010:377F..43ED into its 153 steps (N retraces + what each sets) and prints the nominal timeline |
| `camera.py` | steps the per-retrace routine 2580 (camera record, discs' pivot, label slide, white fade) from the script; `--trace out/runs/p2.p2log` compares every retrace with the emulator (using the measured step starts) and lists the steps that start late |
| `p2trace.py` | runs p2 from `snaps/part_p2.snap` with the copy `run_dope.py` and hooks at 2580, 3732, 354A, 3668, 36E3: writes `out/runs/p2.p2log` (per retrace / step / drawn frame) and `out/runs/p2_dump_<n>.npz` (render buffer, output buffer before and after 360C, blend table) at 10 moments. Full run: `cd models/p2 && python3 p2trace.py --load ../../snaps/part_p2.snap --out out/runs/p2 --stop-part p3 --start-frame 99999999` (about 25 minutes) |
| `run_dope.py` | unmodified copy of `../../run_dope.py` (p2trace.py and p2bump.py subclass its Machine) |
| `blend_check.py` | model of 360C (credit band, blend through the table, label) and of the table builders 1998 / 1967 / 3A5D, compared with the dumps and with full.frames |
| `project.py` | model of the object placement and projection (camera record, pivot, f, matrix, sine table), rasterised and compared with the engine's render buffer in the dumps |
| `texsel_check.py` | which env-map half each object samples (vertex samples of the render buffer) |
| `cull_check.py` | the engine's 16-bit back-face cross product in p2's dumps: faces whose sign is wrong, and the pixels they cover |
| `cull_frames.py` | the same for every drawn frame (camera from the F lines): which frames lose front faces / show back faces |
| `reject_check.py` | projected coverage with the engine's back-face test and the filler's trivial reject, against the render buffer in the dumps |
| `p2bump.py` | short hooked run (frames 2705-2740): every bump-map read of the discs' filler (26D2 / 27F2) against the address formula of notes 7.3 (86,608 reads, all equal, only even rows) |
| `stats.py` | per-phase drawn frames per second and step overshoot from `out/runs/p2.p2log` |
| `mesh.py`, `mesh_view.py` | parse the two meshes (1E71 discs, 2121 "C"); orthographic wire views in `out/` |
| `p2data.py`, `dump_pictures.py` | RIX3 pictures, palette, bump map; writes the hand-made pictures to `../../dump/pictures/p2-*.png` and viewing copies / generated maps to `out/` |
| `out/sheet_*.jpg`, `out/f*.jpg` | contact sheets of the capture used while reading |

Everything generated from the original (trace logs, memory dumps, contact sheets, viewing PNGs) is written to
`out/`, which the repository's `.gitignore` ignores (`out/`); the hand-made pictures go to `dump/pictures/`
(ignored too). Commit only the scripts and this README.

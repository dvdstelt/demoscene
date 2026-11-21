# models/flower — checks for notes-flower.md

Run inside the toolbox from `inside/re` (`toolbox run -c acme303 python3 models/flower/<script> ...`).

| file | what it does |
|---|---|
| `run_flower.py` | copy of `run_inside.py` with dump hooks for this part (camera after 0x4080d8, flower after the morph, every 6th drawn frame the tube / flower objects after the render, every tick, the events) and a fix for resuming from a snapshot (the refresh clock restarts at the snapshot's frame). `python3 models/flower/run_flower.py --load --snap snaps/f4963.snap --out models/flower/run/part --frames 7064 --no-frames` (a few minutes; writes `run/part.dump`, generated, not for Git) |
| `dump.py` | reader for the `.dump` records |
| `lws.py` | model of the LightWave reader 0x408030..0x408588, the spline 0x4090cc and the camera evaluation 0x4080d8; compares the key table with `snaps/f4963.snap` (0x426618) |
| `camera_check.py` | camera position / matrix of every drawn frame and the tick recurrence (f, face window, texture scroll) against `run/part.dump` |
| `sro.py` | `.sro` reader and engine-object reader for snapshots |
| `tunnel.py` | tube survey; per-vertex light levels with rand() replayed, against the snapshot (finds the seed) |
| `flowers.py` | survey of kukka*.sro (counts, extents, shared faces) |
| `flower_check.py` | morph (positions, normals) and rotation of the flower in every drawn frame; lists the events |
| `flower_cam_check.py` | flower in camera space and its environment-mapping u, v |
| `proj_check.py` | projection constants from the engine's per-vertex results |
| `order_check.py` | painter's keys of the flower against the tube |
| `path_vs_tunnel.py` | camera against the tube's rings, face window per tick |
| `render.py` | simple rasterizer of one drawn frame from the dumped per-vertex results, compared with `full.frames` (`--png` writes comparison images, generated) |
| `render_variants.py` | texture orientation / affine vs perspective variants of `render.py` |
| `textures.py` | writes `tex.png` / `pal.png` (the textures and the shade table through the palette; generated) |

Disassembly used: `python3 rep.py func 0x409730` etc. and `python3 models/engine/dump.py 0x408030 0x409ea0`.

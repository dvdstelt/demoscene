# Engine scope models (SurRender P5 1.02 as INSIDE uses it)

Python models and tools behind `inside/re/notes-engine.md`. Run everything inside the toolbox from this folder:
`toolbox run -c acme303 python3 <script> ...`. Outputs go to `out/` (generated from the original: not in Git).

| file | what |
|---|---|
| `engine.py` | the render path: matrices (0x414014, 0x4144ec, Euler 0x4145f4), the view (0x40c4e4), object → camera (0x40c158), back faces (0x41a458), projection (0x41afa0), face list and keys (0x41a9d4), lighting (0x41b508), the radix sort (0x40c384), the near clipper (0x41c0e0), the triangle setup (0x41be91), the attribute gradients and the rasterizer 0x414e5c, 2D bitmaps (0x415df0) |
| `sro.py` | the .sro loader (0x410bb0) and the face / vertex normals (0x410918, 0x410a70) |
| `lws.py` | the LightWave reader of the flower part (0x408030..0x409358): parser, per-frame sampling, cardinal spline, evaluation. `python3 lws.py ../../snaps/f38.snap` compares the sampled table with 0x426618 |
| `lws_check.py` | compares `lws.evaluate` (positions, rotations Ry(H)·Rx(−P)·Rz(−B)) with the camera and tunnel objects in a flower dump |
| `logo_model.py` | the logo part's render from the engine state only (object rotation, camera, light, scene): `--final` also draws the mask and text bitmaps |
| `scene_model.py` | any part's render from a `hooks_scene.py` dump: hierarchy, culling, the 0x1000 stage, lighting, all six rasterizers, billboards (0x415878) |
| `run_engine.py` | a copy of `../../run_inside.py` with `--hooks FILE` (code hooks that dump engine state) and the fix for resuming from a snapshot (the refresh clock restarts at the snapshot's frame) |
| `hooks_logo.py` | hooks for the logo part: state before / after each render and the finished frame (`LOGO_DUMP`, `LOGO_FROM`) |
| `hooks_scene.py` | generic hooks: the scene's objects, vertices, faces, materials, textures, the buffer before and after a render call (`SCENE_PTR`, `CALL_AT`, `AFTER_AT`, `FROM`, `TO`, `EVERY`, `EXTRA`, `DUMP`) |
| `dump_summary.py` | lists what a scene dump contains (objects, flags, parents, rasterizers) |
| `call_engine.py` | calls one routine of the original in unicorn from a snapshot's memory (FPU control word 0x127f); checks the look-at matrix 0x414cec against the model for 200 random directions |
| `cmp_raster.py` | compares the six rasterizers instruction by instruction with 0x414e5c |
| `calls.py`, `sites.py`, `dump.py` | call graph of the engine range, the instructions before each call site, linear disassembly of a range |

Reproducing the checks (each run takes 10 s to 4 min):

```
python3 run_engine.py --load --snap ../../snaps/f1446.snap --out out/logo --frames 1730 --hooks hooks_logo.py
python3 logo_model.py --final                                   # 327 frames, 0 pixels differ
env DUMP=out/flower_dump.pkl FROM=4963 TO=5200 EVERY=8 EXTRA=4b685c python3 run_engine.py --load \
    --snap ../../snaps/f4963.snap --out out/flower --frames 5200 --hooks hooks_scene.py --no-frames
python3 scene_model.py out/flower_dump.pkl ../../snaps/f4963.snap
python3 lws_check.py out/flower_dump.pkl ../../snaps/f4963.snap
env DUMP=out/flight_dump.pkl SCENE_PTR=425008 CALL_AT=402936 AFTER_AT=40293b FROM=7063 TO=7400 EVERY=10 \
    python3 run_engine.py --load --snap ../../snaps/f7063.snap --out out/flight --frames 7400 --hooks hooks_scene.py --no-frames
env DUMP=out/credits_dump.pkl SCENE_PTR=426460 CALL_AT=4067f7 AFTER_AT=4067fc FROM=2150 TO=2700 EVERY=25 ...  (f2148)
env DUMP=out/sprites_dump.pkl SCENE_PTR=425fc4 CALL_AT=404bc0 AFTER_AT=404bc5 FROM=4256 TO=4960 EVERY=30 ... (f4255)
env DUMP=out/temple_dump.pkl SCENE_PTR=425eb4 CALL_AT=404453 AFTER_AT=404458 FROM=11290 TO=11700 EVERY=12 ... (f7063)
env DUMP=out/water_dump.pkl SCENE_PTR=4265ec CALL_AT=407dd9 AFTER_AT=407dde FROM=8470 TO=8800 EVERY=20 ...  (f7063)
```
(`env` variables must be passed inside the toolbox: `toolbox run -c acme303 env NAME=value python3 ...`.)

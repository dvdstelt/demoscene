# models/temple: how the temple notes were checked

Everything runs in the toolbox from this folder: `toolbox run -c acme303 python3 <script> ...`. Generated files go
to `out/` (git-ignored; they are made from the original and are not committed).

## Runs (resumed from `snaps/f11276.snap`, about 3 minutes for the whole part)

`run_temple.py` is a copy of `../../run_inside.py` with:
- a fix for resumed runs (the next refresh is `(frame + 1) * FRAME`, not `FRAME`; without it a resumed run records
  every refresh at once and ends at once);
- `--dump FILE` (pickle per drawn temple frame: the part's arrays, the objects, flares, camera, holder, light,
  scene, materials, the engine's globals 0x4b8f00..0x4b903f and the sorted face list at the start of the draw stage
  0x40c13c, the back buffer before and after), `--lite` (state only), `--dumpfrom R`;
- `--pixframes R1,R2`: every write to the back buffer during the draw stage with the face being drawn, and the
  level / texel byte of the pipe and hall rasterizers (code hooks 0x417110, 0x4154cb);
- `--sentinel I`: fill the back buffer with index I before each frame's faces are drawn.

```
cd inside/re
toolbox run -c acme303 python3 models/temple/run_temple.py --load --snap snaps/f11276.snap --out models/temple/out/t2 --frames 11300 --dump models/temple/out/t2.pkl --pixframes 11290,11298
toolbox run -c acme303 python3 models/temple/run_temple.py --load --snap snaps/f11276.snap --out models/temple/out/t3 --frames 11524 --dump models/temple/out/t3.pkl --dumpfrom 11518 --pixframes 11520
toolbox run -c acme303 python3 models/temple/run_temple.py --load --snap snaps/f11276.snap --out models/temple/out/full_lite --frames 12960 --dump models/temple/out/full_lite.pkl --lite
toolbox run -c acme303 python3 models/temple/run_temple.py --load --snap snaps/f11276.snap --out models/temple/out/sent --frames 12960 --sentinel 255
```

## Scripts

| script | what it checks / shows |
|---|---|
| `snap.py`, `dumpread.py` | read a snapshot / a dump (memory blocks, ints, doubles, matrices) |
| `emu.py`, `rot.py` | call single routines in unicorn; 0x4145f4 builds Rx(a) Ry(b) Rz(c) |
| `randseq.py` | the flare positions are rand() calls 10..39 after srand(0x45eafc) |
| `cmpcap.py out/x.frames 11276` | a resumed run against `full.frames` (0 differences) |
| `state.py out/full_lite.pkl` (via `check`) | the model of the tick and frame routines against every dumped frame |
| `latency.py out/full_lite.pkl` | frame time, display latency, tick count against M, the lost tick at 11967 |
| `proj.py out/t2.pkl` | world → camera → screen for every visible vertex |
| `faces.py out/t2.pkl` | back-face culling and the sort keys |
| `envlight.py out/t2.pkl` | environment-map u, v (0x41acc4) and the point light (0x41b508) per vertex |
| `lookups.py out/t2.pkl out/t3.pkl` | pipe `flare.shd[32 + level][old]` and hall `temple.int[level][texel]` on every pixel |
| `levels.py out/t2.pkl` | the interpolated levels against screen-linear / perspective interpolation |
| `flare.py out/t2.pkl` | the flare sprite model, pixel for pixel |
| `sentcmp.py out/sent.frames 11276` | pixels that depend on the old buffer content, per refresh |
| `nearclip.py` | (frame 11519 of t3) those pixels are not near-clipping losses: their rays hit no opaque surface |
| `bbox.py out/full_lite.pkl` | the pipes' stale bounding boxes never cull a visible pipe |
| `tables.py`, `shdfit.py` | temple.pal, temple.int, flare.shd: what the tables encode, additive colour per flare.shd row |
| `meshes.py` | the meshes as the engine holds them, `out/meshes.png` (top and side view) |
| `dis_415878.txt`, `dis_41b508.txt` | listings of the billboard draw and the point light |

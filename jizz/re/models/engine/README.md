# Engine scope: models and helpers

Models and scripts for `jizz/re/notes-engine.md` (Nix's vector engine). Run inside the toolbox from `jizz/re` or
from this directory: `toolbox run -c acme303 python3 models/engine/<script> ...`. They need `jizz_img.bin`,
`rep.py` and, for the snapshot-based checks, `snaps/`. Render dumps are generated from the original: write them
outside the repository (a scratch directory).

| file | what it does |
|---|---|
| `snap.py` | loads a run_jizz.py snapshot (`Snap('1365')`), reads / disassembles its memory |
| `scripts.py` | decodes the five object scripts at 0x157a4 / 0x157fa / 0x15945 / 0x15973 / 0x159cb |
| `model_objects.py` | the script interpreter 0x14394 with all generators, the seam fix and the normals 0x1a089; compares every object with the snapshot of its part (all 0 ulps) |
| `run_engine.py` | copy of `run_jizz.py` with `--dump-render DIR` (pickles the state at the call of the renderer 0x1f5d1 as `rNNNN.pre` and the page after it as `rNNNN.post`), `--dump-max`, `--dump-tick0`, `--list-renders` |
| `model_render.py DIR N [--png out.png]` | the renderer 0x1f5d1 (transform, face list, radix sort, triangle filler with both span loops) on a dump; prints the number of pixels that differ from the emulator (0 in all 23 dumps) |
| `match_capture.py DIR N..` | finds the `full.frames` records identical to a dumped page |
| `model_camera.py DIR N..` | camera chain 0x159fb / 0x15c40 / 0x154bc from t; ulps against the matrices in a dump |
| `check_tables.py DIR N` | formula check of the shading tables 0x1a479.. (core 0x1a336) and of the highlight map 0x220000 (core 0x6592) |
| `stats.py DIR N` | per-frame statistics: faces dropped by the near limit, clipped faces, depth key range, the area near-dropped faces would cover if clipped |
| `dump_listing.py` | prints the disassembly of every engine routine (keep the output out of the repository) |

Example (the dumps used for the notes):
```
D=<scratch>/engine/tube2
toolbox run -c acme303 python3 models/engine/run_engine.py --load --snap snaps/t3285.snap --out $D/x \
    --ticks 3720 --dump-render $D --dump-max 3 --dump-tick0 3700
toolbox run -c acme303 python3 models/engine/model_render.py $D 0 --png $D/cmp0.png
toolbox run -c acme303 python3 models/engine/match_capture.py $D 0 1 2
toolbox run -c acme303 python3 models/engine/model_camera.py $D 0 1 2
```
Dumps used: t3285 -> ticks 3290..3301 and 3700.. (tube), t2515 -> 2600.. (tunnel), t4435 -> 4700.. (spiky),
t7506 -> 7800.. (metaballs), t1365 -> 1500.. (ribbons), 2228.. (inset 175x110), 2425.. (inset 160x100).

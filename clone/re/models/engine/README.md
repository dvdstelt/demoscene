# Engine scope models

Python models that check `../../notes-engine.md` against the original. Run them inside the toolbox from
`clone/re` (`toolbox run -c acme303 python3 models/engine/<script>`). Generated files go to `clone/re/dump/engine/`
(git-ignored).

| file | what it checks |
|---|---|
| `fpu.py` | helper: x87 arithmetic (numpy longdouble with the host FPU set to truncate), float stores, magic-number roundings, fsin / fcos as the emulator computes them |
| `emu.py` | helper: call one routine of `clone_img.bin` in unicorn with chosen registers / stack arguments |
| `run_engine.py` | copy of `run_clone.py` with `--dump-draws N,..` (state at the entry of the draw 0x4169f8 and the target page after it) |
| `engine.py` | the engine model (normals, back-face test, buckets, clipper, Craft's mapper): byte-exact against 28 dumped draws of parts 7..10 (20 non-empty) |
| `capture_check.py` | the dumped / modelled pages against the `full.frames` record that shows them: identical (17 frames) |
| `matrix.py` | the seven matrix / vector routines against the model on random inputs: identical |
| `geosphere.py` | the geosphere generator against `snaps/t11.snap`: identical |
| `tables.py` | the shade table 0x426b8c and the weight table 0x426d8c against their formulas; the maps' extra row |

Example:

```
toolbox run -c acme303 python3 models/engine/run_engine.py --load --snap snaps/t6924.snap \
    --out dump/engine/q7 --ticks 7880 --no-frames --dump-draws 0,300,600
toolbox run -c acme303 python3 models/engine/engine.py --smooth dump/engine/draw_t7304_c300.npz
toolbox run -c acme303 python3 models/engine/capture_check.py dump/engine/draw_t7304_c300.npz
```

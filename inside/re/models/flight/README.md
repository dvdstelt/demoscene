# models/flight

Python models of the flight part (`inside/re/notes-flight.md`), each checked against the emulator. Run inside the
toolbox from this folder (`toolbox run -c acme303 python3 <script>`).

First make the dump (about 6 minutes; files are generated from the original and stay out of Git):

```
cd inside/re
toolbox run -c acme303 python3 models/flight/run_inside.py --load --snap snaps/f7063.snap \
    --out models/flight/run1 --frames 8468 --sprites-until 7300
```

| file | what |
|---|---|
| `run_inside.py` | copy of `inside/re/run_inside.py` with hooks: `run1.dump` (TICK: state at the tick routine 0x4021f8; REND: state, landscape object, scene, camera, ships, sun and flares before the scene is drawn; AFTR: the drawn buffer, sun, flares and ship 0 after it; RAND: every rand() call) and `run1.sprites` (the draw buffer before / after each sprite draw until `--sprites-until`) |
| `fdump.py` | reader for `run1.dump` |
| `ucall.py` | calls a routine of the program in unicorn on a snapshot's memory (used to check sin / cos helpers, the x87 precision and the rotation conventions) |
| `heightmap.py` | the height field (0x401450) and its light (0x40136c); compares with snapshot f7063: identical |
| `flightmodel.py` | init, tick routine (ships, control point, camera) in x87 extended precision and the camera jump; compares with every TICK record: bit-exact |
| `remake_check.py` | the integer rule for the yaw arguments n1..n4 (T = 0..19,999) and a plain-double model of the motion (error ≤ 1e-11) |
| `landmodel.py` | the landscape grid (0x401ab8) and fog (0x401c64) per vertex, and the faces' rasterizers; compares with every REND record: identical |
| `sunmodel.py` | sun position, camera-space coordinates (0x40c158) and flare visibility (0x4024d0); identical in every frame |
| `spritemodel.py` | the sprite drawer 0x415878 (sun opaque, flares through flare.shd); compares with `run1.sprites`: identical |
| `landrender.py` | per-pixel model of the landscape (affine texture, Gouraud light, fog) vs. the drawn frames; writes `cmp_<frame>.png` (model, original, difference) |

`python3 landrender.py 100 600 1200` takes record numbers of the REND list (default those three).

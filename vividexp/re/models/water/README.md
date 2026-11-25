# Water scope: models and checks

Python models of WaterInit 0x405bd0 / WaterEffect 0x4064b0 (notes in `../../notes-water.md`). Run them inside the
toolbox from this folder: `toolbox run -c acme303 python3 <script>`. Generated files go to `runs/` (git-ignored).

| file | what |
|---|---|
| `wdata.py` | the scene's data built as WaterInit builds it: ini, the two env-map halves of watenv.pcx, palette, background, the bump table (0x405fcf), the .m3d (0x412bf0 with flags 3, uv scaled by 0x413330), the .spl columns, the 16.16 natural spline (0x413870) and its evaluation (0x413b90), the sine table (0x412560 with the Taylor cos 0x422120), sin/cos (0x413ce4), the asin table at 0x825428 |
| `wmodel.py` | WaterEffect per frame: placement, the three rotations, projection 0x414000, back faces 0x4140e0, face keys 0x411980 and both sorts (insertion 0x4119f0, heap 0x411a30), the bump-environment rasterizer 0x414f13 and the affine rasterizer 0x41448a, the letters drawn into the transparent heart's env map, the object order 0x415480, a whole frame |
| `wsnap.py` | snapshot memory access (caches `runs/<snap>.mem`) |
| `run_vivid.py` | copy of `../../run_vivid.py` with water hooks: logs every clock read of the object loop (0x406565), object draws with their vertex / normal / face-flag arrays (0x40580c), flips (0x406a85) and the retrace routine's VGA copies (0x405a30) to `runs/w.wlog`, `runs/w.wdump`, and the transparent env map after each frame with letters drawn in (`runs/w.wenv`, first 400) |
| `wlog.py` | reads those logs: drawn frames with their per-object clock values and the record that shows each |
| `check_tables.py` | tables against the image and `snaps2/f21.snap`: sine table, asin table, bump table, env halves, palette, background, all 12 spline channels |
| `check_vertices.py` | placement, rotation, projection, env coordinates, bump coordinates and back-face flags against every object draw of the run |
| `check_frames.py` | `env N`: the transparent env map with the letters against the run's dumps; `frames ...`: rendered frames against the `part2.frames` records that show them (writes `runs/frame_<i>.png` for a mismatch) |
| `stale_env.py` | given frames without a heart behind the transparent heart, rendered with the stale env map (as the original) and with a clean one (`runs/stale_<i>.png`) |
| `stale_scan.py` | the same for every such frame: the record ranges where the stale map is visible |

Reproduce the run (about a minute):

```
cd vividexp/re
toolbox run -c acme303 python3 models/water/run_vivid.py --load --snap snaps2/f21.snap --out models/water/runs/w --frames 2712 --no-frames > models/water/runs/w.log
cd models/water
toolbox run -c acme303 python3 check_tables.py
toolbox run -c acme303 python3 check_vertices.py
toolbox run -c acme303 python3 check_frames.py env 400
toolbox run -c acme303 python3 check_frames.py frames every:1
```

Results (all exact): tables 9 of 9; 6,968 object draws (all 1,742 drawn frames of the run); 400 env maps; 1,741 of 1,741
frames (records 23..2706 of `part2.frames`) pixel for pixel.

# models/generic: GenericInit / GenericEffect (LANDSCAPE and the second generic scene)

The record of how `../../notes-generic.md` was checked. Run everything from this directory inside the toolbox
(`toolbox run -c acme303 python3 <script> ...`); the files come from `../../unpacked/files/`, snapshots from
`../../snaps2/`, the capture from `../../part2.frames`. Generated files go to `runs/` (git-ignored).

| file | what |
|---|---|
| `gen.py` | the model's library: ini and .spl readers, the fixed-point spline (0x413870 / 0x413b90), the sine table (0x412560 with the Taylor cosine 0x422120), sin / cos, the .m3d reader, the PCX decoder, the bump gradient table (0x4074e4), isqrt (0x4221e0), the Phong map (0x413a60), the environment coordinate table at 0x825428 and its formula |
| `render.py` | one GenericEffect frame, integer-exact: object state from t, rotations, projection, back-face and near tests, object / face sorts (insertion sort, the reversed NR heapsort), the rasterizers 0x414f13 / 0x418313 and the inner loops of types 9, 10, 11. `python3 render.py gen/gen.ini runs/land 0 5` compares draws with a `--gen-dump` run |
| `snap.py` | reads a snapshot (pickle) as flat memory |
| `check_init.py` | `python3 check_init.py 5951 gen/gen.ini`: object records, every spline record, the sine table, the environment table against the snapshot |
| `check_maps.py` | `python3 check_maps.py 9191 gen2`: environment blocks, bump tables, the type 11 table, the Phong map, the light table against the snapshot |
| `run_vivid.py` | copy of `../../run_vivid.py` with `--gen-dump PREFIX`: writes `PREFIX.pages` (every page GenericEffect finishes, at its call of the flip, 0x40a16b) and `PREFIX.gentxt` (per draw: refresh, effect counter, page, each object's pos and X Y Z A B C; per display by 0x4073fc: refresh, page, minimum interval). Used: `--load --snap ../../snaps2/f5951.snap --out runs/landall --frames 8102 --no-frames --gen-dump runs/landall` and the same from `f9191` to 11332 (`runs/gen2all`) |
| `check_dump.py` | `python3 check_dump.py runs/landall 5951 gen/gen.ini 40`: frame intervals, display lags, capture records against the shown pages, and the frame model against every 40th page |
| `check_states.py` | `python3 check_states.py runs/gen2all gen2/gen.ini`: every draw's object states against the model's spline |
| `compare_capture.py` | model frame next to the capture record that shows it, as PNG (`runs/cmp_*.png`) |
| `spline_float.py` | the fixed-point spline against a double-precision natural spline (max difference per column) |
| `stats.py` | mesh statistics: faces whose mapping coordinates span more than half the texture |
| `near_check2.py`, `near_check4.py`, `near_mask.py` | the near test (depth ≤ 280): objects passing the eye; the screen area the dropped faces would cover if clipped instead; a picture of it (`runs/near_674.png`) |
| `painter_check.py` | painter's order against a depth buffer with the original's coverage |
| `wrap_mask.py` | the terrain faces whose mapping coordinates span more than half the texture, tinted on a model frame (`runs/wrap_1443.png`) |
| `fit_lit.py` | the light table against the palette |
| `show_pics.py` | the pictures, palettes and the Phong map as PNG |

`runs/*.lst` are disassembly listings made with `../../rep.py` while reading the code.

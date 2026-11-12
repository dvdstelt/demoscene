# models/engine — checks for notes-engine.md (BARTi's 3D engine)

Run inside the toolbox from this folder (`toolbox run -c acme303 python3 <script>`). Everything is read from
`../../unpacked/`, `../../snaps/` and `../../full.frames`; nothing from the original is stored here.

| file | what |
|---|---|
| `engine.py` | integer model of the engine: matrices (0x7984e, 0x79d25), transform / projection (0x79b16), normals (0x79a17), scene list (0x79483), face list and quicksort (0x78b08, 0x7898d), per-vertex parameters (0x4d4b4 / 0x4d4e7 / 0x4da28 ...), the fillers 0x4e710, 0x51593, 0x4f47c, 0x52e96, 0x54548 with their exact rounding |
| `run_engine.py` | copy of `../../run_stars.py` with `--engine-dump DIR --engine-every N --engine-max K`: saves memory (0..4 MB) before and after a part's call of 0x79483 / 0x789c4 as `DIR/d<frame>_<entry>_<n>.npz` |
| `test_dump.py` | runs `engine.py` on a dump and compares the target buffer with what the original drew (0 differing pixels in all 27 dumps: spike ball, textured surfaces, bee, chrome star, torso; modes 2, 3, 15, 20, 21) |
| `match_capture.py` | finds the `full.frames` record that shows a dumped draw (rows 1..197): identical for the spike ball, textured surfaces and bee dumps |
| `envmap.py` | the environment map generator 0x7873a / square-root table 0x7871c against three snapshots (identical), and the cosine table formula |
| `objects.py` | summary of the `B!` files (18, 24, 45, 46, 50, 51) and the `WLDD` file 28 |
| `xins.py` | prints the instruction behind each `rep.py xref` hit |

Example (spike ball):
```
python3 run_engine.py --load --snap ../../snaps/f7170_6534.snap --out /tmp/x --frames 7260 \
        --engine-dump /tmp/dumps --engine-every 10 --engine-max 4
python3 test_dump.py /tmp/dumps/*.npz
python3 match_capture.py /tmp/dumps/*.npz
```
Other parts: snapshots f5947_11e7f (bee), f13810_a0ca (textured surfaces), f16643_164dc (chrome star),
f19504_16f7c (torso); the dumps used for the notes were taken with `--engine-every` 50 / 20 / 397 / 211.

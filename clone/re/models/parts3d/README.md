# models/parts3d

Scripts behind `../../notes-parts3d.md` (parts 7, 8, 9, 10). Run them inside the toolbox from `clone/re`; they
read `unpacked/`, `snaps/` and `full.*`. Outputs (dumps, frames) go to a scratch directory, not here.

| script | what it does |
|---|---|
| `run_clone.py` | copy of the shared runner with `--p3d FILE`: per frame of parts 7..10 the dt, the state before / after, the matrix, the vertices handed to the engine, and for parts 9 / 10 the grid, source vertices and faces (pickle) |
| `model.py` | the model: state steps, matrices, deformations (7, 8), metaball fields, voxel cubes, shades, uv (9, 10), the tile band |
| `check_matrix.py DUMP.pkl` | rotation axis / sign conventions against a dumped matrix |
| `check78.py DUMP.pkl RUN.frames RUN.flips` | parts 7, 8: state, vertices and tile band per frame |
| `check910.py DUMP.pkl RUN.frames RUN.flips` | parts 9, 10: state, grid, cubes, faces, shades, uv and tile band per frame |
| `timebase.py DUMP.pkl RUN.ticks` | closed forms against the timer, part origins, part 9's angle switches, 803 / 901 starts |
| `quirks.py DUMP.pkl` | how often the x + 2 neighbour test, part 10's aliasing and its reads past the array matter |
| `borders.py`, `extent.py` | rows the parts leave unwritten; rows / columns the objects reach (full.frames) |
| `dump_data.py`, `snapinfo.py`, `events.py` | static tables and initial values; geosphere and state in the snapshots; events and part starts |

Example (part 9, about 3 minutes):

```
toolbox run -c acme303 python3 models/parts3d/run_clone.py --load --snap snaps/t4621.snap --out $TMP/p9 \
    --ticks 6157 --flip-frames --p3d $TMP/p9.pkl
toolbox run -c acme303 python3 models/parts3d/check910.py $TMP/p9.pkl $TMP/p9.frames $TMP/p9.flips
toolbox run -c acme303 python3 models/parts3d/timebase.py $TMP/p9.pkl $TMP/p9.ticks
```

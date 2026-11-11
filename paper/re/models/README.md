# Decompiling agents' models and checks

Each folder holds the scripts one decompiling agent wrote while working on one scope of the intro. The notes
file named below explains what they model and what they showed. Typical contents:

- a Python model of the routines (`model.py`, `sim.py`, `gen*.py`);
- scripts that run the original in the emulator with extra hooks or from a snapshot (`run_*.py`);
- comparisons of the model with the capture (`cmp*.py`, `*chk.py`).

| folder | notes |
|---|---|
| `init/` | [notes-init.md](../notes-init.md): tables, the random generator, objects, palettes, the canyon texture |
| `music/` | [notes-music.md](../notes-music.md) (the finished player is [`../player_ref.py`](../player_ref.py)) |
| `pklite/` | the C++ PKLITE unpacker's tests and benchmarks |
| `title/` | [notes-title.md](../notes-title.md) |
| `sketch/` | [notes-sketch.md](../notes-sketch.md) |
| `scene3d/` | [notes-scene3d.md](../notes-scene3d.md) |
| `logo/` | [notes-logo.md](../notes-logo.md) |
| `planes/` | [notes-planes.md](../notes-planes.md) |
| `water/` | [notes-water.md](../notes-water.md) |

These scripts are kept as they were left, not as maintained tools. Many contain absolute paths from the machine
they ran on: `/home/dennis/Downloads/acme_303/paper/re` was this `paper/re` folder, and `/tmp/claude-1000/.../scratchpad/re-<scope>`
was the agent's scratch folder, which is now `models/<scope>`. Their data files (dumps, snapshots, captures) are
not kept, because they were generated from the original; regenerate them with the tools in [`..`](../).

# models/twirl — checks for notes-twirl.md

Run from `inside/re` inside the toolbox (`toolbox run -c acme303 python3 models/twirl/<script>`). Generated files
(`pre.npz`, `*.png`, `*.log`) are git-ignored.

| script | what it checks |
|---|---|
| `snapinfo.py [snap]` | prints the twirl's pointers, parameter sets, phases and flags from a snapshot (default f10574) |
| `tables.py` | rebuilds the pre-table (0x40a29c) and its conversion (0x40a408) and compares them with the twirl table in f10574: 0 of 64,000 entries differ, every pixel is written. Writes `pre.npz` (needed by the others) |
| `fit.py` | how far the table's (row, column) are from the continuous polar coordinates of each pixel centre (row +0.315, column +0.19..0.23 texel on average) |
| `frame.py [first last]` | the frame (0x40a5a4 + 0x40a54c) and tick (0x40a20c) model with the event schedule: renders every record 10575..11275 of `full.frames` exactly (701 of 701, all 64,000 indices); prints the state each record shows |
| `probe.py records` | for set 2 (one ring-table value for all rings): finds the picture and the value that render a record exactly (used to find the event timing) |
| `events2.py` | finds the tick step at which the 30:48 and 30:12 events took effect (both in the same refresh) |
| `endstate.py` | the per-refresh model's phases equal the snapshot f11276 bit for bit after 701 steps; the closed form agrees to 1e-5 |
| `continuous.py [records]` | the per-pixel continuous formula of the notes against the capture (`cont_<k>.png`: capture next to a 1280x800 render) |
| `palette.py` | the DAC through the flashes and fades of the part |
| `pics.py` | the five pictures and palettes: INSIDE.DAT against the loaded block in f10574, and PNGs of them |

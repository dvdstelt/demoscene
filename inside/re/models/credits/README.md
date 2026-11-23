# models/credits

Models and checks behind `../../notes-credits.md` (the credits part: tunnel, smoke, spiked ball, name pictures).
Run them inside the toolbox from `inside/re` (`toolbox run -c acme303 python3 models/credits/<script> ...`). Runs,
dumps and images go to a scratch folder, not here.

| file | what it does |
|---|---|
| `run_inside.py` | copy of `../../run_inside.py` with dump hooks: `--dump PREFIX` writes PREFIX.pkl (a pickle stream: per tick the state at 0x405f30, per drawn frame the state before 0x4061fc and before the render: camera, tunnel object and vertices, ball, smoke objects, scene); `--dump-buf all / every:N / n1,n2` adds the draw target after the 3D render and after the overlays (with the state after the render, incl. the ball's vertices); `--mark K` fills the draw target with index K before the clear (0x41127c); `--sprites` records every sprite draw (0x415878's record and the projection globals). Example: `run_inside.py --load --snap snaps/f2148.snap --out DIR/full --frames 4262 --dump DIR/full --dump-buf every:40` (about 10 minutes) |
| `cmpcap.py` | compares a run's `.frames` with `full.frames` record by record (`cmpcap.py DIR/full.frames 2148`) |
| `snap.py`, `dumpread.py` | read a snapshot's memory; read the dumps |
| `path.py` | 0x40684c, the flight path (53-bit x87 arithmetic, the image's constants); run alone it compares all 1,024 entries with `snaps/f2148.snap` |
| `seed.py` | finds the number of rand() calls before the path (260, from seed 1) from the radius floats |
| `model.py` | the tick routine 0x405f30 and the frame's camera, tunnel vertices, seam, smoke and ball (0x4061fc, 0x406470, 0x4063fc); `model.py DIR/full.pkl` checks every tick and frame of a dump |
| `blit.py` | the water mask and name overlays (0x416174, 0x416324); `blit.py DIR/full.pkl` checks the dumped buffers |
| `coverage.py` | pixels the clear and the render leave untouched, from two `--mark` runs (`coverage.py DIR/mk7.pkl DIR/mk200.pkl`) |
| `latency.py` | at which record of `full.frames` a dumped frame is shown, against its tick count |
| `tables.py` | fits of tunnel.shd, smoke.shd, tunnel.int, wtrmask.int against tunnel.pal (section 5 of the notes) |
| `pics.py`, `pics_grey.py` | write the part's pictures as PNG (palette colours; levels as grey) to a folder given as argument |

Results (whole part, run from `snaps/f2148.snap` to record 4262): the run's 2,114 records equal `full.frames`; path
table exact; 2,106 ticks and 1,970 frames exact (camera, matrix to 3e-16, all tunnel vertices, smoke, ball); 50
dumped frames' overlays exact (plus 6 in a short run); no untouched pixel in 24 marker-run frames.

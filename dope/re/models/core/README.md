# models/core — scripts of the "core" scope (notes-core.md)

Run from `dope/re` inside the toolbox (`toolbox run -c acme303 python3 models/core/<script>`). They read
`unpacked/`, `full.frames` / `full.ticks` and `../original/DOPE.EXE`; none of them writes into the repository
except `pictures.py` (into the git-ignored `dump/pictures/`).

| script | what it checks or makes | result |
|---|---|---|
| `p1_model.py` | p1 from the decompiled code: VGA planes after the logo blit, start address / panning / split, the A/B palette cross-fade; compared with every p1 frame of the capture | 2690 frames: pixels and DAC 0..63 identical |
| `rnc_from_notes.py` | an RNC-1 unpacker and MZ chain walker written only from NOTES.md | all nine `unpacked/*.bin` identical, CRCs check |
| `exit_fades.py` | p6's (3d22) and p7's (33be) final fades: DAC = (v * L) >> 8, L = 256 - 7 n / 256 - n | exact; about 4 steps per captured frame |
| `handover.py` | statistics of the frames around every part boundary (mode, DAC, colours used) | section 5 of the notes |
| `dacsum.py F1 F2` | DAC sums and colours on screen for a frame range | used for the p5 -> p6 boundary |
| `framework_scan.py` | the shared timer / Cirrus / exit code of every part, found by byte pattern | section 3.1 |
| `retrace_waits.py` | every busy wait on 3DAh bit 3, and which ones do not wait for the end of the retrace first | section 5.2 |
| `pictures.py` | writes `dump/pictures/p1-logo*.png` (indexed, with palette B / A) and prints the two ramps | section 8 |
| `runs/first_irq.sh` | runs each part snapshot for 40 frames with `run_dope.py --trace` on its int 8 handler | first handler runs: p1 frame 15, p2 2713, p3 10032, p4 13651, p5 21091, p6 25597, p7 29605 (outputs deleted) |

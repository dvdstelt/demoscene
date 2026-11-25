# Bitmap effects: models and checks

Python models and checks behind `vividexp/re/notes-bitmap.md`. Run from `vividexp/re` inside the toolbox
(`toolbox run -c acme303 python3 models/bitmap/<script>`). Images and dumps go to `runs/` (not in Git).

| file | what |
|---|---|
| `bitmap.py` | the model: `Effect(ini, part)` builds what InitBITMAPEFEK builds (PREC words, PICRGB planes, TAUSTA, DTABLE, palette), `Effect.draw(offsets)` is the drawers 0x40c3ad..0x40cbe7, `step()` is 0x40c22c with RunBITMAPEFEK's argument packing, `read_prm()` is 0x401024 |
| `verify.py` | runs the model over every frame of every RunBITMAPEFEK line of `full` or `part2` and checks each record pixel for pixel (new frame over the previous one, split in drawing order); prints the run's start / end frame counter. Result: 8362 / 8362 records of `full`, 2146 / 2146 of `part2` |
| `palcheck.py` | the DAC during the runs (the effect's palette) and the InitFADE fades inside the sections |
| `fittabs.py` | regenerates every `.1x1` table from its formula and counts the matching bytes |
| `fit2x2.py` | fits of the three `.2x2` tables (projective maps, angle + cosine) and how many bytes they match |
| `dtable.py` | DTABLE against a 50 % blend of the two textures' colours (rms, nearest colour, dither test) |
| `tabfit.py` | helpers for the table fits (load a table, polar grid, truncation, byte comparison) |
| `gn.py` | a small Levenberg-Marquardt fitter (no scipy in the toolbox) |
| `snapoffs.py` | prints the offsets, speeds, ini numbers and fade state in every snapshot (`snaps`, `snaps2`) |
| `survey.py` | headers, sizes and index ranges of the pictures and tables |
| `showtabs.py`, `viewtab.py`, `showpics.py` | contact sheets of the tables (with contour lines) and the textures with their palettes |

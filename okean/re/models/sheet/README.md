# models/sheet: the seventh part 0x5334f (the curling sheet)

Scripts of the "sheet" scope ([../../notes-sheet.md](../../notes-sheet.md)). Run them from `okean/re` in the toolbox
(`toolbox run -c acme303 python3 models/sheet/<script>`). They read `snaps/part_5334f.snap`, `unpacked/korso2.bin`
and the capture `full.*`; nothing is written outside this folder.

| script | what it does / checks |
|---|---|
| `sheet_model.py` | the model: callback state as a closed form of the callback count, grid heights, noise, rotation and projection (0x531fe), the stamp line drawer (0x52fed, byte-exact dword adds), the text printer as this part uses it (0x4cacc), the palette fade and cross-fade |
| `compare_all.py` | runs the model over the whole part on the capture's schedule and compares every record 6896..8096 with `full.frames`: 1173 of 1173 pictures equal (all 64,000 indices), every palette explained. `--show 7180,8060` writes model / capture / difference images `show_<rec>.png` |
| `snapdump.py` | the part's variables and tables in the snapshot and in the file (all static, nothing carried over from earlier parts except the sine table's generated part) |
| `check_static.py` | which inputs differ between file and snapshot (only the sine table beyond entry 1024, built by the title part's set-up 0x2c26f) |
| `script.py` | the text printer's state at the part start and the caption script |
| `captions.py` | which caption page each record shows, with the music positions |
| `paltest.py`, `paltest2.py` | the fade level of the captured palettes at the start and in the ending |
| `probe.py` | which noise frame a record shows (found the draw that is never recorded, before record 7517) |
| `stats.py` | the stamp kernel, noise histogram, stamps per frame, highest intensity (217: the byte adds never wrap) |
| `ranges.py`, `near.py` | depth range, vertices behind the camera (never visible), lines cut by the 8-bit stamp count (never on screen) |
| `ending.py` | the fall and the cross-fade in order 27, per callback |
| `kernel_fit.py` | smooth fits of the 8x8 stamp and the cross-section of an axis-aligned and a diagonal line |
| `rng.py` | the noise generator over the part: no repeated state in 18.8 M steps, neighbour correlation |

Images (`cap_*.png`, `show_*.png`) are made from the original and are not committed.

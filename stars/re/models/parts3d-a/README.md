# parts3d-a models (STARS: the precalc 0x16945, the cloud, the bee, the spike ball)

Python models and checks for `../../notes-parts3d-a.md`. Run inside the toolbox from this folder
(`toolbox run -c acme303 python3 <script>`). They read `../../unpacked/`, `../../full.frames`, `../../full.ticks`
and `../../snaps/`. Images and arrays they make go to `$P3A_OUT` (default: `<tmp>/stars-parts3d-a`), never here:
they are made from the original.

| script | what it does | result |
|---|---|---|
| `common.py` | readers for capture frames, ticks, snapshots, overlay files; the engine's COS table; `out()` | |
| `hooklib.py` | parser for `run_hook.py` dumps | |
| `run_hook.py` | copy of `../../run_stars.py` plus `--hook ADDR --hook-dump A:N,... --hook-out FILE`: one line per execution of ADDR with the frame, PIT ticks, the memory ranges and the DAC palette | |
| `cloud_model.py` | the tunnel table 0x1259c and the draw 0x125f4; writes `cloud_table.npy` (run it first) | table equal to snapshot f5421 (64,000 words; 4 centre pixels never written) |
| `cloud_check.py` | draw with the callback's speed sum S(n), n = record PIT - 5386, against full.frames | 16 frames 5425..5941 equal (rows 1..197) |
| `cloud_fit.py` | continuous radius / angle of each table pixel | R - r = -0.04 +- 0.31, angle error -0.005 +- 0.006 rad |
| `bee_bg_check.py` | file 27 against cloud frames of the part's own offset family | no match |
| `bee_bg_fft.py`, `bee_bg_refine.py`, `bee_bg_where.py` | file 27 against the cloud effect with free offsets (FFT correlation in angle x depth space), refinement, map of the mismatches | layer 1 (86, 108), layer 2 (218, 228): 87 % one-to-one index mapping |
| `bee_check.py DUMP` | camera 0x121b4, env angles, wing poses 0x122d7, palette 0x11fa1 | 430 drawn frames, 0 mismatches (palette within one level) |
| `bee_pal_start.py` | the fade from white (0x120df) in full.frames | levels 0, 8, 12, ..., 60, 63 = 4 n |
| `spike_check.py DUMP` | path 0x6888, angles and env angle 0x6905, palette 0x6861 | 1,083 drawn frames, 0 mismatches; path stall at the counter wraps |
| `precalc_check.py DUMP` | placement 0x16ba1 | 64 frames, 0 mismatches |
| `precalc_rle.py` | decodes the 64 RLE frames from snapshot f235 | contact sheet of frames 0..31 |

The dumps (a few MB each, 1-3 minutes of emulation):

```
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/f5947_11e7f.snap --out <tmp>/bee --frames 7020 \
    --hook 0x11f23 --hook-dump 55836:64,11b7a:4,121ac:8,11b86:8,11f9a:8,2b6d88:2176,127e8:4 --hook-out <tmp>/bee_hook.txt
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/f7170_6534.snap --out <tmp>/spike --frames 9480 \
    --hook 0x6584 --hook-dump 55836:64,6498:32,6518:4,6789:8 --hook-out <tmp>/spike_hook.txt
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/f63_16945.snap --out <tmp>/pre --frames 236 \
    --hook 0x16a5b --hook-dump 16160:512,163ae:4,55836:64,57ab6:4,5594c:1,55948:4 --hook-out <tmp>/pre_hook.txt
```

0x2b6d88 is where file 28's descriptors are in this run (file 28 loaded at 0x2b6d80, see `full.log`).

# parts2d models

Python models for `../../notes-parts2d.md` (TBL 0x12013, logo 0x1212a, radial zoom 0x177bf, field lines 0x17c8a,
end loop 0x18c25). Run inside the toolbox from this folder: `toolbox run -c acme303 python3 <script>`.

| file | what it does |
|---|---|
| `common.py` | capture frames (`full.frames`, `full.flips`, `full.ticks`), snapshots, texture dumps, PNG output |
| `rng.py` | the random generator 0x141f6 (zero seed); its first 80 values are the TBL spark tables (checked against `snaps/t430.snap`) |
| `run_hook.py` | copy of `../../run_jizz.py` with `--hooks FILE` (a module with `install(mu, st, args)` / `finish()`) |
| `hook_tbl.py` | hooks for the TBL part: per frame the timer reads, the bump image, the work buffer before `>> 3`, the work + feedback buffers, LUT, light, fade, pulse |
| `hook_generic.py` | per frame timer reads plus memory grabs at code addresses, configured by `<out>.cfg` (a Python literal) |
| `tbl_model.py` | the TBL frame (3.1–3.5 of the notes); compares every stage and the captured page |
| `ema_bias.py` | integer feedback vs a float EMA on the recorded TBL inputs (the -1/3 bias in 3.6) |
| `polar.py` | the polar table 0x16f4f and the warp 0x16dc8 |
| `jizz_model.py` | the logo part (star table, warp, brightness, 0x179f6 darkening, picture) |
| `zoom_model.py` | the radial zoom part (radius table, offsets, 0x16d43, timer fades) |
| `field_model.py` | the field table 0x17b2f and the field lines part (S ramp, poles, per-pixel sum) |
| `end_model.py` | the end loop (picture + fade) against `full.frames` |

Recordings (in `out/`, git-ignored, generated from the original; delete when done):
```
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/t440.snap --out out/tblall --ticks 1366 --hooks hook_tbl.py
toolbox run -c acme303 python3 tbl_model.py out/tblall.tbl.pkl            # 341 frames, all bit-exact
toolbox run -c acme303 python3 ema_bias.py out/tblall.tbl.pkl

echo "{'frame_end': 0x12378, 'grabs': {0x1236e: [('tab', 0x179f2, 257, True), ('warp', 'PAGE', 128000, False)], 0x12378: [('page', 'PAGE', 128000, False), ('ev', 0x6413, 4, False), ('par', 0x1238e, 13, False)]}, 'regs': {0x1236e: ['eax']}}" > out/jizz.cfg
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/t4050.snap --out out/jizz --ticks 4435 --hooks hook_generic.py
toolbox run -c acme303 python3 jizz_model.py out/jizz.hook.pkl            # 220 frames, all bit-exact

echo "{'frame_end': 0x17960, 'grabs': {0x17956: [('tab', 0x179f2, 257, True), ('add', 0x16cd1, 4, False), ('tex', 0x17971, 8, False), ('fade', 0x179d6, 12, False)], 0x1795b: [('warp', 'PAGE', 128000, False)], 0x17960: [('page', 'PAGE', 128000, False), ('fade2', 0x179d6, 12, False), ('ev', 0x6413, 4, False)]}, 'regs': {}}" > out/zoom.cfg
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/t5205.snap --out out/zoom --ticks 5970 --hooks hook_generic.py
toolbox run -c acme303 python3 zoom_model.py out/zoom.hook.pkl            # 695 frames, all bit-exact

echo "{'frame_end': 0x17fe4, 'grabs': {0x17cc2: [('tab', 0x16f4b, 816000, True)], 0x17f65: [('code', 0x17f85, 0x30, False), ('S', 0x17ff5, 4, False), ('tau', 0x2000, 4, False), ('st', 0x6542, 4, False)], 0x17fdf: [('pre', 'PAGE', 128000, False), ('fade', 0x179d6, 12, False)], 0x17fe4: [('page', 'PAGE', 128000, False), ('ev', 0x6413, 4, False)]}, 'regs': {}}" > out/field.cfg
toolbox run -c acme303 python3 run_hook.py --load --snap ../../snaps/t5970.snap --out out/field --ticks 6300 --hooks hook_generic.py
toolbox run -c acme303 python3 field_model.py out/field.hook.pkl          # 335 frames, all bit-exact

toolbox run -c acme303 python3 end_model.py                               # 318 captured frames, all bit-exact
```
The `*_check.txt` files in `out/` are the per-frame results of the last runs. A hooked run of a few hundred ticks
takes well under a minute; the pickles are large (up to 180 MB).

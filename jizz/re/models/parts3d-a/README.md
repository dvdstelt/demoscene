# parts3d-a models

Python models and checks for `jizz/re/notes-parts3d-a.md` (parts 0x17003 and 0x169e7). Run inside the toolbox from
this directory: `toolbox run -c acme303 python3 <script>`. They read `jizz/re/jizz_img.bin`, `snaps/`, `dump/` and
`full.frames` / `full.flips`.

| script | what it checks |
|---|---|
| `snap.py` | helpers: snapshot memory, capture frames, flips / ticks |
| `texid.py`, `texid16.py` | which `dump/*.tga` a buffer pointer holds (planar RGB / 16-bit 5:6:5) |
| `clouds.py TICK EVENT` | floor / ceiling planes 0x17553 + band 0x1770b against full.frames (e.g. `1701 130`, `2121 205`) |
| `tunnel2d.py TICK [WINDOW]` | swirl table 0x16f4f against the snapshot, mapper 0x16cd3 against full.frames (`2950`, `2620 0x29220`) |
| `objects.py` | object generators (sphere op 0, torus op 14, op 15) against the snapshot vertex / face arrays |
| `insets.py` | white inset frames (0x1151c) and sprite blits (0x174e5) in full.frames |
| `camera.py` | camera path 0x159fb / 0x15c40 against the hooked values (needs the hook dumps) |
| `rings.py` | ring table init 0x180e5 and per-frame step / projection 0x18009 (needs `p1_hooks.pkl`) |
| `lines.py` | thick line drawer 0x1145c / 0x1122f (needs `p1_hooks.pkl`) |
| `overlay.py [PKL]` | overlay chain: decay, blur 0x11eb2, trail 0x12111, blend 0x11396 |
| `rects.py` | rectangle corner tables 0x18228, RNG 0x141f6 jitter and drawing 0x183a9 (needs both dumps) |
| `run_hook.py` | copy of `../../run_jizz.py` with `--hookmod FILE` (a module with `HOOKS = {addr: fn(mu, st)}`, `finish()`) |
| `hooks_p1.py`, `hooks_p2.py` | hooks that write `p1_hooks.pkl` / `p2_hooks.pkl` (per-frame time, rings, camera, rectangles, overlay buffers) |

Making the hook dumps (about a minute each), from `jizz/re`:

```
toolbox run -c acme303 python3 models/parts3d-a/run_hook.py --load --snap snaps/t1365.snap \
    --out models/parts3d-a/p1run --ticks 2300 --hookmod models/parts3d-a/hooks_p1.py
toolbox run -c acme303 python3 models/parts3d-a/run_hook.py --load --snap snaps/t2515.snap \
    --out models/parts3d-a/p2run --ticks 3285 --hookmod models/parts3d-a/hooks_p2.py
```

The runs reproduce the capture (same flips as full.ticks). The dumps, the `p?run.*` outputs and any PNG the
scripts write are generated from the original: delete them after use, they must not be committed.

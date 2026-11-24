# Models of the scope "morph + star" (parts 0x367e5 and 0x3c8da)

Findings: [../../notes-morph-star.md](../../notes-morph-star.md). Run everything inside the toolbox from this folder
(`toolbox run -c acme303 python3 ...`).

| file | what |
|---|---|
| `common.py` | program image, the sine table rebuilt from the file, the blob line drawer (positions and the exact dword adds), the caption the shared printer draws, capture access |
| `morph.py` | the fourth part: `frame(c, seed)` from the callback count and the noise seed; `python3 morph.py c ...` compares with `full.frames` assuming no dropped refreshes |
| `star.py` | the sixth part: `frame(c, seed)`; `python3 star.py c ...` likewise |
| `run_okean.py` | unchanged copy of `../../run_okean.py`, imported by `hookrun.py` |
| `hookrun.py` | runs from a part's snapshot and logs every callback, main-loop start and copy to video memory with the part's variables (`<out>_hooks.log`); `--dump F,..` saves the draw buffer at those copies (`<out>_buf<F>.npz`) |
| `hooks.py` | reads the log: per record, the callback count and the drawn-frame number it was drawn with |
| `verify_morph.py`, `verify_star.py` | compare the models with every record of the parts (pixels and DAC) |
| `stats.py` | per frame: largest summed index, pixels at white, pixels above 255 |
| `trunc.py` | lines cut by the line drawer's 8-bit step counter |

```
toolbox run -c acme303 python3 hookrun.py --load ../../snaps/part_367e5.snap --out p4 --frames 4700 --part 4
toolbox run -c acme303 python3 hookrun.py --load ../../snaps/part_3c8da.snap --out p6 --frames 6900 --part 6
toolbox run -c acme303 python3 verify_morph.py > verify_morph.log
toolbox run -c acme303 python3 verify_star.py > verify_star.log
toolbox run -c acme303 python3 stats.py 4 --every 25 > stats4.log
toolbox run -c acme303 python3 trunc.py 6 --every 40
```

Generated (from the original, not to be committed): `p4.*`, `p6.*` (`.frames`, `.ticks`, `.gus`, `_hooks.log`,
`.log`), `*.npz`, `*.png`, the `*.log` outputs.

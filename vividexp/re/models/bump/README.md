# models/bump: the bump-mapping effect (notes-bump.md)

Run everything from `vividexp/re` inside the toolbox: `toolbox run -c acme303 python3 models/bump/<script> ...`.

| file | what |
|---|---|
| `bump.py` | the model: ini / PCX / .val readers, InitBUMP's buffers, the stepping loop (`State.step`), the drawers (`Bump.frame`, bit-exact), and `frame_hd`, a continuous sketch of the remake suggestions |
| `check_init.py SNAP INI FLAG` | init buffers of the model against a snapshot taken at RunBUMPEFEK's start (`snaps2/f2724`, `f8106`, `f11334`) |
| `run_bump.py` | copy of `run_vivid.py` with hooks: logs the state at every draw (`<out>.draws.txt`) and dumps the screen after every draw (`<out>.draws`) |
| `check_draws.py runs/NAME INI` | every dumped screen against `Bump.frame`, and the logged state against `State.step` |
| `check_capture.py FRAMES runs/NAME FIRST LAST` | capture records against the complete draws (complete / being drawn) |
| `check_pal.py SNAP` | the DAC of a snapshot against the candidate palettes |
| `show_pics.py OUT` | contact sheet of the height and light pictures |
| `analyse_tables.py`, `fit_gen.py`, `fit_gen2.py`, `fit_gen3.py`, `fit_tab.py` | fits of bumpwave.val, the ring table (exact: the generator 0x402820), the light spot and bumptab.val |
| `show_hd.py INI N B OUT` | the original frame (4× nearest) next to `frame_hd` |

`runs/` (git-ignored): `dis.txt`, `dis_asm.txt` (listings), `suomi.*`, `ice.*`, `doom.*` (run_bump.py runs from
`snaps2/f2724`, `f8106`, `f11334`), contact sheets and `hd_*.png`.

Reproduce the main check:
```
toolbox run -c acme303 python3 models/bump/run_bump.py --load --snap snaps2/f2724.snap --out models/bump/runs/suomi --frames 3800 --no-frames > models/bump/runs/suomi.log
toolbox run -c acme303 python3 models/bump/check_draws.py models/bump/runs/suomi bump/bump1.ini
```

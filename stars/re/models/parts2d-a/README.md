# parts2d-a models

Python models and tools behind `stars/re/notes-parts2d-a.md` (intro name cards, picture warp, voxel tunnel). Run
them inside the toolbox from this folder (`toolbox run -c acme303 python3 ...`). They read `../../unpacked/` and the
capture; the hooked runs write to `runs/` and the images to `out/` (both generated from the original: do not commit).

| file | what it does |
|---|---|
| `common.py` | image / overlay file / capture readers, the sine table 0xa740, PNG output |
| `run_hook.py` | copy of `run_stars.py` with `--hook ADDR:MEM/LEN,...` (logs registers and memory to `<out>.hook` every time ADDR runs) |
| `view_files.py` | files 5, 6, 7 as PNG (`out/`) |
| `flower_model.py` | name cards: polar map 0x5c97, draw 0x5ece, palette 0x61b1 / 0x615b; compares screen and DAC of every card frame |
| `warp_model.py` | picture warp: textures 0x9d95, curves 0x9989, patch 0x9a62; compares every completed frame |
| `warp_check.py` | warp curves against the arrays dumped at 0x9a62, and the patch fill from the dumped curves |
| `warp_float.py` | how far the packed 8.8 patch is from the same formula in floats |
| `tunnel_tables.py` | writes the tunnel's polar map and projection table from snapshot f11498 to `runs/` |
| `tunnel_model.py` | tunnel: polar map 0x136a and projection table (against the snapshot), wiggle 0x1158 / 0x11ce, voxel columns 0x1261, blit; compares every frame |

Runs used (each 20-40 s):

```
mkdir -p runs out
python3 run_hook.py --load --snap ../../snaps/f279_5a92.snap --out runs/intro --frames 5215 \
    --hook 0x5ed8:5410/2,5412/2,5a77/24,5465/4,541e/4 --hook 0x4a7f8:4a7a0/4,4a7a8/4
python3 flower_model.py runs/intro                 # 4 cards, 1736 frames: 0 pixel / 0 palette differences

python3 run_hook.py --load --snap ../../snaps/f9478_975b.snap --out runs/warp --frames 11320 \
    --hook 0x9a62:9458/48,9734/8,9618/24,4a7a0/16 --hook 0x9d14: --hook 0x4a7f8:4a7a0/4,4a7a8/4
python3 warp_model.py runs/warp                    # 852 of 915 exact (the rest: PIT tick inside 0x9989, and the clear)
python3 run_hook.py --load --snap ../../snaps/f9478_975b.snap --out runs/warp2 --frames 11320 \
    --hook 0x9989:9458/48 --hook 0x9a62:9458/48,9734/8,9618/24,6990/8320 --hook 0x9d14:
python3 warp_check.py runs/warp2                   # curves 853 / 915 equal; patch from dumped curves 914 / 915
python3 warp_float.py

python3 tunnel_tables.py                           # runs/tunnel_map.npy, runs/tunnel_depth.npy from snapshot f11498
python3 run_hook.py --load --snap ../../snaps/f11498_11e8.snap --out runs/tunnel --frames 13806 \
    --hook 0x1261:cb8/8,ec6/28,f6d/4,4a7a0/16 --hook 0x1367:
python3 tunnel_model.py runs/tunnel                # 1152 of 1153 exact (the last record is the cleared screen)
```

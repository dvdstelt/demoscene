# parts3d-b models

Checks for `../../notes-parts3d-b.md` (the textured surface 0xa0ca, the chrome star 0x164dc, the torso 0x16f7c).
Run everything inside the toolbox from this folder (`toolbox run -c acme303 python3 ...`). Generated files (run
logs, memory dumps, PNGs) go to `out/`, which Git ignores: they are made from the original.

## Runs (regenerate first)

`run_stars_b.py` is a copy of `../../run_stars.py` with two options: `--log-at ADDR=a:f,...` writes the frame, the
PIT tick count and the listed values every time ADDR runs (`<out>.log_at`), `--dump-at ADDR:N1,N2` pickles the 8 MB
memory at the N-th execution of ADDR (`<out>_dump<N>.pkl`). About a minute per part.

```
mkdir -p out/runs
python3 run_stars_b.py --load --snap ../../snaps/f13810_a0ca.snap --out out/runs/tex --frames 15975 \
 --log-at '0xa128=a046:i,a52c:i,a528:i,9f80:i,9f84:i,9f88:i,9f8c:h,9f8e:h,9f90:h,9fa0:i,9fa4:i,a076:i,a07a:i,a725:i,a3c6:h,a3c4:B' \
 --dump-at '0xa132:3,60,200,400,700'
python3 run_stars_b.py --load --snap ../../snaps/f16643_164dc.snap --out out/runs/star --frames 18455 \
 --log-at '0x165e0=163ae:i,16360:i,16364:i,16368:i,1636c:h,1636e:h,16370:h,16380:i,16384:i,164bb:B,164bc:B,164bd:B,164be:B,164c1:h,164c3:i,164c7:i,164cb:i,164cf:i,164d3:B,164d4:i,164d8:i,164ba:B' \
 --dump-at '0x16607:3,100,400,900'
python3 run_stars_b.py --load --snap ../../snaps/f19504_16f7c.snap --out out/runs/torso2 --frames 21960 \
 --log-at '0x16fbd=16d56:i,16cc8:i,16ccc:i,16cd0:i,16cd4:h,16cd6:h,16cd8:h,16ca8:i,16cac:i,16cb0:i,16cb4:h,16cb6:h,16cb8:h,16d7e:i,1733e:h,17340:h,1707f:B,17080:B,1707e:B,1707b:h'
python3 run_stars_b.py --load --snap ../../snaps/f19504_16f7c.snap --out out/runs/torso --frames 21960 \
 --dump-at '0x17059:3,100,250,400,600,800'
```
(The `.frames` / `.gus` files these runs also write are not needed.)

## Scripts

| script | what | result |
|---|---|---|
| `snap.py` | snapshot / capture access (`load`, `Mem`, `frames`, `ticks`) | |
| `model_tex.py out/runs/tex.log_at` | surface: motion keys (0xa656), morph key/tick (0x78fda), crossfade triangle [0xa076] | 700 / 700 frames exact |
| `model_star.py out/runs/star.log_at` | star: callback 0x16646 (angles, y, z, morph, beats, final phase) | 1103 / 1103 frames exact |
| `model_torso.py out/runs/torso2.log_at` | torso: 0x17084 phases + callback 0x17513 | 1055 / 1055 frames exact |
| `palette_check.py` | palette formulas of the three parts vs `full.frames` | every distinct palette produced; 6 star frames are a mid-computation mix of two (race) |
| `morph_check.py DUMP OBJ FILE` | 0x78ffe vertex morph (e.g. `out/runs/star_dump400.pkl 0x16360 51`) | bit-exact |
| `transform_check.py DUMP OBJ scene\|direct [da,db,dc [dx,dy,dz]]` | engine matrix, projection, env coordinates (steps undo PIT ticks between draw and dump: star `10,9,6`, torso `0,-12,0 775,0,0` / `-1473,0,0`) | exact |
| `lightmap_check.py` | 0x7873a light maps of both parts (needs tex_dump200, torso_dump400) | byte-exact |
| `texture_check.py DUMP...` | 0xa5c5 crossfade frames, 0xa57d tiling | exact |
| `render_check.py DUMP MODE [OUT.png [nobump]]` | per-pixel formulas (modes 15, 3, 20) with an approximate rasteriser on the engine's vertex data; env `OX=0.5` shifts the sample point | 73–84 % (mode 15), 61 % (3), 54–59 % (20) exact pixels; visually identical; mode 15 without the bump term: 0.6–20 % |
| `obj24.py` | `B!` reader (`load_b(n)`), file 24 facts, key-set height maps | |
| `textures_a0ca.py` | maps 21 / 23 with palettes, crossfade frames, palettes 20 / 22, background 25 (PNGs) | |
| `pictures.py` | env maps 49 / 29 / 44, background 47, palettes 42 and 0x15e60 (PNGs) | |

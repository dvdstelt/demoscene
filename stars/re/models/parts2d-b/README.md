# parts2d-b models (particles, dissolving picture, face)

Python models and checks behind `../../notes-parts2d-b.md`. Run inside the toolbox from this folder. The hooked runs
write `runs/*` (frames and logs made from the original: do not commit them, nor images made from the files).

| file | what |
|---|---|
| `snap.py` | load a snapshot, read memory, read an overlay file |
| `run_hook.py` | copy of `../../run_stars.py` with `--hookmod FILE` (`install(mu, st, args)`, `finish()`) |
| `hook_particles.py`, `hook_dissolve.py`, `hook_face.py` | the hooks: event logs (PIT callbacks with the music position, frame starts, values at the instructions that read them) and every drawn frame → `runs/<name>.pkl` |
| `particles_model.py` | particles 0x133ad / 0x133f1: zoom-blur, blobs, words, orbits, random replay; `--ticks` recomputes the tick-driven inputs |
| `particles_check_capture.py` | the model against `full.frames` (VGA and palette) |
| `particles_text_timeline.py` | which word is drawn in which frame |
| `particles_float_test.py` | integer zoom-blur vs float without / with a -3/8 bias |
| `pics.py` | decodes files 36..41 (span lists + 6:6:6 planes) |
| `dissolve_model.py`, `dissolve_check_capture.py` | dissolving picture 0x1535c: stamps, 3-plane zoom-blur with per-row offsets, G / R-B dither conversion; `--ticks` recomputes offsets and stamps |
| `face_data.py` | decodes file 34 (face spans, texture coordinates as 0x11a0a converts them) and file 35 (palette, env map, remap tables) |
| `face_model.py`, `face_check_capture.py` | face 0x1089c: remap + env-mapped face, warp tables, the script; modes `recorded`, `formula`, `script` |
| `remap_fit.py`, `remap_fit2.py` | file 35's remap tables vs nearest-colour rules |
| `zoom_fit.py` | continuous equivalents (scale, expansion centre) of the integer zoom maps |
| `insp1.py` .. `insp4.py` | first looks at the tables and parameter blocks in the snapshots |

```
mkdir -p runs
python3 run_hook.py --load --snap ../../snaps/f18451_133ad.snap --out runs/p1 --frames 18998 --hookmod hook_particles.py
python3 run_hook.py --load --snap ../../snaps/f21956_133f1.snap --out runs/p2 --frames 23434 --hookmod hook_particles.py
python3 run_hook.py --load --snap ../../snaps/f18999_1535c.snap --out runs/d --frames 19470 --hookmod hook_dissolve.py
python3 run_hook.py --load --snap ../../snaps/f23436_1089c.snap --out runs/f --frames 25164 --hookmod hook_face.py
python3 run_hook.py --load --snap ../../snaps/f23436_1089c.snap --out runs/ffrozen --frames 23836 --freeze-pit --hookmod hook_face.py
python3 particles_model.py runs/p1.pkl 1 f18451_133ad.snap --ticks     # 546 frames, 0 differ
python3 particles_model.py runs/p2.pkl 2 f21956_133f1.snap --ticks     # 1477 frames, 0 differ
python3 dissolve_model.py runs/d.pkl --ticks                            # 234 screens, 0 differ
python3 face_model.py runs/f.pkl script                                 # 1727 frames, 0 differ
```

# parts3d-b models

Python models for `../../notes-parts3d-b.md` (the tube 0x15c8b, the spiky object 0x15d20, the blobs 0x10fda). Run
inside the toolbox from this directory: `toolbox run -c acme303 python3 <script> ...`. They read `jizz_img.bin`,
`snaps/` and `full.frames` from `jizz/re`; nothing from the originals is stored here.

| file | what |
|---|---|
| `snap.py` | helpers: load a snapshot, read memory, read a captured frame |
| `script.py ADDR..` | disassembles Nix's object scripts (0x157fa tube, 0x15973 spiky, 0x159cb blobs) |
| `objects.py SCRIPT TICK` | the script interpreter and generators (sphere, cone, torus, twist, rotations, copies, normals); compares every object with the engine buffers of a snapshot (3285 / 4435 / 7506) |
| `camera.py TICK` | camera path 0x159fb + look-at 0x154bc; compares with a snapshot |
| `inspect_objs.py TICK` | prints the engine's object table, counters and texture slots |
| `texmap.py TICK..` | which dump/*.tga is in which texture slot (planar or interleaved) |
| `blobs.py DUMP` | ball paths 0x7aae, field 0x7a4e, marching cubes 0x77ad/0x6e74, normals 0x79a8; compares with the blob mesh in a dump |
| `project.py DUMP` | projection, depth and env-map coordinates against the engine's per-vertex results |
| `tables.py DUMP E S F` | colour tables 0x1a336 against memory |
| `render.py DUMP OUT.png [--model]` | approximate rasteriser (z-buffer, affine UV, nearest texel) with the engine's shading; writes page / render / difference side by side |
| `blob_extent.py` | blob mesh size and extent over the whole part |
| `coverage.py` | checks that the background sphere covers the viewport over each part |
| `run_jizz_b.py` | copy of `../../run_jizz.py` with `--dump-at ADDR:T1,T2,.. --dump-out FILE{tick}` (16 MB of memory + the page [0x2060] when EIP reaches ADDR at or after each tick) |

Dumps used (written to a scratch directory, not kept here):
```
python3 run_jizz_b.py --load --snap ../../snaps/t3285.snap --out X/tube --ticks 3720 --dump-at 0x15c3a:3300,3600,3700 --dump-out "X/tube{tick}.pkl"
python3 run_jizz_b.py --load --snap ../../snaps/t4435.snap --out X/spiky --ticks 4720 --dump-at 0x15c3a:4700 --dump-out "X/spiky{tick}.pkl"
python3 run_jizz_b.py --load --snap ../../snaps/t7506.snap --out X/blob --ticks 8710 --dump-at 0x11130:7520,7700,7900,8100,8300,8500,8700 --dump-out "X/blob{tick}.pkl"
```
(0x15c3a = return of 0x15bef after the render, 0x11130 = after the render in the blob loop; both before the fade.)

Results: see section 9 of the notes.

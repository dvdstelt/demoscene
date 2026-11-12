# models/p6p7: checks for notes-p6p7.md

Run everything from this folder inside the toolbox (`toolbox run -c acme303 python3 <script> ...`). The scripts read
`../../unpacked/p6.bin` / `p7.bin`, snapshots in `snaps/` and memory dumps in `dumps/` (both generated, see below;
neither may be committed: snapshots are `*.snap` (ignored), delete `dumps/` after use).

| script | what it does |
|---|---|
| `snap.py`, `mesh.py`, `sine.py` | helpers: snapshot loading and image addresses; the engine mesh format; the sine table as 179b / 19f4 build it (`python3 sine.py` prints its error against `round(32767 sin)`) |
| `timeline.py` | p6 and p7 script step tables and the per-retrace integration (motion, label, bump strength, morph) against 11 snapshots |
| `check_p6_tables.py snaps/f*.snap` | p6 palette morph (ramps, DAC 16..207), bump offset table cs:0010, shadow mask in the env map |
| `check_p6_transform.py snaps/f26300.snap ...` | p6 vertex transformation: screen, env coordinates, depth for the cat and the rings |
| `check_p7_transform.py dumps/0F74_2C90_*.bin` | p7 transformation incl. the normal-based bump coordinates |
| `check_p6_probe.py probe6.log`, `check_p7_probe.py probe7.log` | the fillers' texel addressing against register probes |
| `check_p7_lightmap.py snaps/f*.snap` | the p7 light map (base + 5 spots) from the retrace counter |
| `p7_overflow.py` | the light map's maximum over the whole part (no byte overflow) |
| `raster6.py A B [code\|plain [out.png]]`, `raster7.py A B [out.png]` | approximate rasterisers from the engine buffers (dump at the draw loop start A) compared with the frame buffer (dump at the copy B) |
| `maps.py` | bump maps as gradient fields, the light spot's profile |
| `shadow_fit.py`, `shadow_fit2.py`, `shadow_fit3.py` | attempts to explain the shadow masks geometrically (none fits) |
| `spanwidth.py dumps/*` | widest triangles (the plain filler's 8-bit span counter) |
| `look6.py`, `look7.py`, `dump_objs.py snaps/...` | exploration printouts (data contents, palettes, object records) |
| `export_pictures.py` | writes the hand-drawn pictures and the photo as indexed PNGs into `../../dump/pictures/` |
| `run_dope.py` | copy of `../../run_dope.py` with two additions: `--dump SEG:OFF@F1/F2,...` (write the first 1.1 MB of memory + the DAC to `dumps/` when SEG:OFF runs at or after frame Fi) and `--probe SEG:OFF@F:N,...` (print the registers the first N times SEG:OFF runs from frame F on) |

Regenerating the inputs (each run takes a few minutes):

```
python3 run_dope.py --load ../../snaps/part_p6.snap --out run6 --stop-part p7 \
    --snap-at 25700,26300,27000,27900,28300,28700,29100 --snap 'snaps/{tag}.snap'
python3 run_dope.py --load ../../snaps/part_p7.snap --out run7 --stop-part term \
    --snap-at 29900,30300,30700,31100 --snap 'snaps/{tag}.snap'
python3 run_dope.py --load ../../snaps/part_p6.snap --out trace6 --stop-part p7 --trace 0F74:2DE6,0F74:3D22  # step starts
python3 run_dope.py --load ../../snaps/part_p7.snap --out trace7 --stop-part term --trace 0F74:3070,0F74:33BE,0F74:3410
python3 run_dope.py --load ../../snaps/part_p6.snap --out dump6 --frames 29150 \
    --dump '0F74:2A60@26300/27000/27310/28300/29100,0F74:2D5C@26300/27000/27310/28300/29100'
python3 run_dope.py --load ../../snaps/part_p7.snap --out dump7 --frames 31120 \
    --dump '0F74:2C90@30300/30700/31100,0F74:2F8C@30300/30700/31100'
python3 run_dope.py --load ../../snaps/part_p6.snap --out probe6 --frames 26303 \
    --probe '0F74:1E22@26300:3000,0F74:1E3C@26300:3000' > probe6.log
python3 run_dope.py --load ../../snaps/part_p7.snap --out probe7 --frames 30303 \
    --probe '0F74:2052@30300:3000,0F74:206C@30300:3000' > probe7.log
```
(delete the `.frames` / `.gus` / `.dram` outputs afterwards; only the snapshots, dumps and logs are used.)

Results at the time of writing: `timeline.py` all OK; `check_p6_tables.py` exact in 5 snapshots;
`check_p6_transform.py` exact (the snapshot may fall inside a transform: the first vertices then come from the newer
one); `check_p7_transform.py` 1446/1446 in 3 dumps; probes 3000/3000 each; light map exact in 4 snapshots;
`raster6.py` 31-51 % identical 3D pixels, mean colour difference 2.4-5.4 / 63; `raster7.py` 11-23 %, 3.7-4.1 / 63.

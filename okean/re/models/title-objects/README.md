# Models of the scope "title + objects" (notes: ../../notes-title-objects.md)

Run from this folder inside the toolbox: `toolbox run -c acme303 python3 <script>`. They read
`../../unpacked/korso2.bin`, `../../full.frames` / `full.ticks` and `../../snaps/part_2c26f.snap`,
`part_2e779.snap`.

## Models

| file | what |
|---|---|
| `common.py` | helpers: file and snapshot reads at code32 addresses, capture records, `.ticks` rows |
| `title.py` | the title part 0x2c26f: the generated sine table, the noise, the 256 points, the two stamp-line drawers 0x2bc7c / 0x2bf4f, `frame(c, seed, mask_reader)`; `snap_mask_reader` reads the bytes around the mask from the snapshot |
| `objects.py` | the objects part 0x2e779: the cog mesh, 0x2e5f5 (transform, projection), 0x2e304 (stamp line, slow path with the byte counter), the background, the callback 0x2eb81, the palette blend; `draw_exact` adds the stamps as dwords with the byte carries |
| `vlog.py` | parser of the emulator variable log |
| `run_okean.py` | copy of `../../run_okean.py` with `--vlog 'addr:var,...;...' --vlog-out FILE`: logs dwords (`bXXX` byte, `wXXX` word) whenever an address runs |

## Checks

| file | what it shows |
|---|---|
| `title_check.py` | the sine table built by `title.sine_table()` equals the one in `part_2e779.snap`; first records |
| `title_cmp.py`, `title_cmp2.py` | record k shows draw k - 5 (noise) with k - 5 callbacks |
| `title_all2.py` | **every title record 5..620 bit for bit** |
| `title_offscreen.py` | the start points above the screen whose outer beam would draw (the y-clip quirk) and the byte read there |
| `title_pal.py` | the palette of records 3..624: 0x2b65c, then the fade f = 248 .. 0 in records 573..604 |
| `title_stats.py` | index ranges (maximum stamp count 73, no wrap) |
| `title_view.py` | capture / outer beams / inner beams side by side (`title_view.png`) |
| `dump_mask.py` | the title mask: file = snapshot, 19,235 set pixels, `title_mask_1502c.png` |
| `obj_vars.py` | the objects part's variables, edges and both palettes |
| `obj_sched.py` | from `vlog2.txt`: the 29 intervals without a copy, no draw split by a callback |
| `obj_verify.py` | **every copy of the objects part against the capture** (1678 of 1679 bit for bit; record 1191 is torn) and every object's variables against the log; palettes |
| `obj_quirks.py` | per draw: vertices at z <= 0, stamps of lines to them, stamps of lines cut by the byte counter (`quirks2.txt`) |
| `obj_layers.py` | capture beside the model's normal / behind-camera / cut lines (`obj_layers.png`) |
| `obj_dbg.py`, `obj_dbg4.py` | single-record comparisons used while debugging |

The emulator log `vlog2.txt` (needed by `obj_*.py`) was made with

```
toolbox run -c acme303 python3 run_okean.py --load ../../snaps/part_2e779.snap --out run2 --frames 2340 \
  --start-frame 1000000 --vlog '2e9ca:b12b75,w12b73,2c9b4,2cac4,2cac0;2e5f5:2cabc,2c9bc,2cab4,2ca98,2ca94,2c9c0,2ca90,b2e376;2eb46:;2eb81:b12b75,w12b73;2eb31:2c9ac,2c9b0' \
  --vlog-out vlog2.txt
```

(about one minute). Generated files, not to be committed: `vlog2.txt`, `run2.*`, `*.pkl`, `quirks2.txt`,
`verify_all.txt`, the PNG and JPG pictures (contact sheets `sheet_*.jpg`, `font_strip.png`, and
`title_mask_1502c.png`, which is the original's bitmap).

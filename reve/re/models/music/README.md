# models/music: checks for reve/re/notes-music.md

Python models of Reve's music: LCA's MOD-Player V3.03 on the GUS, and the tools that compared it with V3.02 (STARS).
Run inside the toolbox from this folder (`toolbox run -c acme303 python3 ...`). They read `../../unpacked/reve_img.bin`,
`../../unpacked/files/ZIC/PULSE.MOD` and the capture `../../full.gus` / `full.dram` / `full.ticks` (all generated from
the original, not in Git). Nothing they write belongs in Git (logs, DRAM images, WAVs: write them to a scratch
directory).

| file | what it does |
|---|---|
| `lcaplay.py` | port of the player routine by routine (started from `stars/re/models/music/lcaplay.py`, the V3.03 differences marked): card detection and GF1 init (0x25f698), module conversion and sample upload with the 64 KB page splits (0x26e16c, 0x26e9bb), period → FC table (0x26e8ba), start (0x26ec3e), the timer IRQ tick with all effects (0x26eecc), stop (0x26ecc4); both GUS timers free-running (the song switches between them); main's stop after the last script entry; a GF1 model (16 voices, 38,587.5 Hz) for the render |
| `vs302.py` | aligns V3.03's code (REVE.LFL) with V3.02's (STARS.EXE, `stars/re/unpacked/stars_img.bin`) instruction by instruction with addresses masked, and prints every block that differs |
| `addrmap.py` | the data addresses the aligned code uses in both versions and their offsets (all consistent: globals +0x243c04, GUS data and tables +0x244484) |
| `tables.py` | the four jump tables of both versions through the alignment: only E-tick entry F differs |
| `run_reve.py` | copy of `../../run_reve.py` with three additions for this scope: `--music-log N` prints the program time at which the GUS timers start and the first N timer overflows, `--music-pos FILE` logs AX after the timer interrupt's call of 0x2602eb (frame, time, GUS interrupts, AX), `--trace` prints the exact time (PIT ticks and refresh) |

```
python3 lcaplay.py                       # -> DRAM identical (1,048,576 bytes), GUS log 111,241 lines identical,
                                         #    timer interrupts before the retrace identical in all 17,729 records,
                                         #    position after them: 17,728 identical, 1 differs (record 137, see the
                                         #    notes section 5.3); about 0.5 s
python3 lcaplay.py --check "" --out X    # writes X.gus (byte-identical to full.gus) and X.dram
python3 lcaplay.py --times X.txt         # per tick: time in s after the timer start, order and row after it
python3 lcaplay.py --info                # module, sample records / DRAM layout, upload blocks, effects used
python3 lcaplay.py --check "" --wav X.wav  # GF1 render from the timer start to main's stop (252.53 s, ~5 s; numpy)
python3 vs302.py | less                  # the V3.02 -> V3.03 differences (needs STARS unpacked)
python3 run_reve.py --out /tmp/x --frames 800 --no-frames --music-log 100 --music-pos /tmp/x.pos
python3 run_reve.py --load --snap ../../snaps/f17500.snap --out /tmp/e --no-frames --frames 17740 \
        --trace 0x204814,0x2053ca,0x204826     # when the last entry ends, the title is drawn, the music stops
```

`--ticks N` stops before tick N instead of at main's stop (for a partial capture: `--check DIR --prefix NAME`).
The C++ port (`reve/remake/src/lcaplay.cpp`, `gf1.cpp`, `music.cpp`) is checked against the capture and against
`lcaplay.py --wav` by `reve/remake/tools/test_music.cpp` (`--pywav X.wav`: every sample within 1 LSB).

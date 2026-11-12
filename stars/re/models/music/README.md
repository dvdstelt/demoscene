# models/music: checks for stars/re/notes-music.md

Python model of STARS.EXE's music: LCA's MOD-Player V3.02 on the GUS. Run inside the toolbox from this folder
(`toolbox run -c acme303 python3 lcaplay.py ...`). It reads `../../unpacked/stars_img.bin`,
`../../unpacked/files/01.bin` (the module) and the capture `../../full.gus` / `full.dram` / `full.ticks` (all
generated from the original, not in Git). Nothing it writes belongs in Git (logs, DRAM images, WAVs: `*.wav` is
ignored; write the rest to a scratch directory).

| file | what it does |
|---|---|
| `lcaplay.py` | port of the player routine by routine: card detection and GF1 init (0x1ba6c), module conversion and sample upload (0x1a1b0), period → FC table (0x1a594), start (0x1a91c), the timer IRQ tick with all effects (0x1abb0), stop (0x1a9a2); the GUS timer model (tick times); a GF1 model (32 voices, 19,293.75 Hz) for the render |

```
python3 lcaplay.py                       # -> DRAM identical, GUS log 144,716 lines identical, music position of
                                         #    26,387 frames identical: "ALL MATCH" (about 1 s)
python3 lcaplay.py --check "" --out X    # writes X.gus (byte-identical to full.gus) and X.dram
python3 lcaplay.py --times X.txt         # per tick: time in s after the timer start, order and row after it
python3 lcaplay.py --info                # module, sample records / DRAM layout, effects used by the song
python3 lcaplay.py --wav stars_music.wav # GF1 render from tick 0 (372.09 s, about 15 s to run; needs numpy)
```

`--ticks N` (default 16871, the capture's tick count when main stops the music) sets where the stop is logged.

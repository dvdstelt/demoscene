# models/music: checks for inside/re/notes-music.md

Python models of INSIDE.EXE's music: DSMI's AMP 2.00 player and the GUS driver SDIGUS.DSD, ported routine by routine,
playing EYE.AMF. Run inside the toolbox from this folder (`toolbox run -c acme303 python3 <script>`). They read
`../../unpacked/inside_img.bin`, `../../../original/EYE.AMF` and `SDIGUS.DSD`, the capture `../../full.gus` /
`full.ticks` / `full.dram` and the snapshots `../../snaps/f*.snap` (all generated from or part of the original, not
in Git). Nothing they write belongs in Git either (logs, WAVs: write them to a scratch directory).

| file | what it does |
|---|---|
| `amf.py` | EYE.AMF reader (format in its docstring); run alone it dumps the instruments, orders and effect counts |
| `dsmi.py` | the port: `Card` (the capture's fake GUS, logs in `full.gus` format), `Driver` (SDIGUS.DSD: reset, setupChannels, volume ramps, the CDI functions, DRAM allocator, DMA upload), `Amp` (AMP 2.00: init, play, poll, tick, row reader, all effects), `Dsmi` (start-up sequence of 0x4010b4, the timer callback 0x400fcc, the exit) |
| `player.py` | plays the song against the capture: start-up writes (same order and values), music and exit writes (same values and interrupt stamps), the order / row of every `full.ticks` record. `--out` writes the model's log, `--ticktimes` the interrupt of every player tick, `--skip` the interrupts whose callback was lost (default 12117) |
| `statecheck.py` | runs the port to each snapshot's interrupt count and compares 214 fields (AMP globals, channel and sample records, DSMI time, the driver's 24 voice records) |
| `timeline.py` | the closed form tick → DSMI time → interrupt → refresh (`--check` against the port, `--rows` every row) |
| `gf1.py` | GF1 model (clone's `mxmplay.py` class GF1 behind `dsmi.Card`'s interface; reads answered as hardware does) |
| `render.py` | renders the song to a WAV (25,725 Hz, from record 0 of the capture; `--rate 44100` resamples) |
| `snapfind.py`, `snapinfo.py`, `snapdis.py` | find SDIGUS.DSD's code in a snapshot, print the DSMI state (driver entry points, CDI table, module header, DSMI time), disassemble snapshot memory (`dis` / `func` / `hex`) |

```
python3 player.py                       # -> "ALL MATCH" (140,095 GUS writes, 13,044 positions)
python3 statecheck.py                   # -> "ALL MATCH" (10 snapshots)
python3 timeline.py --check             # -> "9510 ticks, 0 differ from the closed form" + order start table
python3 render.py <scratch>/inside.wav  # about 20 s
```

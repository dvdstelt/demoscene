# models/music: MIDAS 0.5a as VIVID.EXE plays EXPRMNT1.XM / EXPRMNT2.XM

Run everything inside the toolbox from this folder (`toolbox run -c acme303 python3 ...`). Tables are read from
`../../unpacked/vivid_img.bin` (through `../../rep.py`), the modules from `../../../original/`. Generated files go in
`runs/` (git-ignored). See `../../notes-music.md`.

| file | what |
|---|---|
| `xm.py` | plain XM reader (FT2 format) and a summary of a song: `python3 xm.py ../../../original/EXPRMNT1.XM` |
| `midas.py` | the port: XM loader and pattern conversion (0x418d44, 0x418918), the Generic Module Player for XM (0x40d820.., commands 0x41e930..), the GUS sound device (0x41b1a0..0x41cd10) with the GF1 port access `Gf1Port` (writes logged as `run_vivid.py` logs them; reads answered as its fake GUS does, or by a GF1 model) |
| `player.py` | MIDAS's screen-synchronised timer (0x410740..) in PIT clocks and the run of both parts; compares with the captures: `python3 player.py full ../../full.gus --dram ../../full.dram --positions ../../full.ticks` (and `part2 ../../part2.gus ...`); `--out` writes the stream, `--ticks` every player tick |
| `statecheck.py` | the GMP channels, play position and GUS voice records against every snapshot: `python3 statecheck.py full ../../snaps`, `part2 ../../snaps2` |
| `timeline.py` | the time of every player tick (refresh, PIT clocks after its retrace, late starts): `python3 timeline.py full` |
| `songinfo.py` | what a song uses after MIDAS's conversion (commands, parameters, speed / tempo changes, quirk cases) |
| `render.py` | WAV render: the port drives a GF1 model that answers reads as a card does: `python3 render.py full runs/full.wav` |
| `snapinfo.py` | prints the timer, GMP and GUS-device globals of a snapshot |

Results (November 2026): `full.gus` 372,039 of 372,039 writes equal (values, order, interrupt stamps), `part2.gus`
238,948 of 238,948; `full.dram` and `part2.dram` equal byte for byte; 66 of 68 (`snaps`) and 19 of 21 (`snaps2`)
snapshots equal in every compared field, the other two of each differ only in fields MIDAS never initialised before
part II's first row (heap garbage).

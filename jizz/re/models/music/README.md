# models/music: checks for jizz/re/notes-music.md

Python models of JIZZ.EXE's music code. Run inside the toolbox from this folder
(`toolbox run -c acme303 python3 <script>`). They read `../../jizz_img.bin`, `../../dump/smokeout.mxm` and the
capture `../../full.gus` / `full.dram` / `full.ticks` (all generated from the original, not in Git). Nothing they
write belongs in Git either (modules, logs, WAVs: write them to a scratch directory).

| file | what it does |
|---|---|
| `mp4synth.py` | the "mp4 cruncher" 0x1632b: copies the stored module part and synthesises the 32 sample passes with x87 24-bit rounding; `--out x.mxm`; compares with `dump/smokeout.mxm` (identical, 928,657 bytes + the 11 zero bytes of the buffer) in about 10 s |
| `emu_synth.py` | runs the original 0x1632b alone in unicorn (`--pc 0/2/3`: FPU precision 24/53/64 bit, `--trace N`: filter inputs of the second record); 24 bit reproduces the dump, 53 bit differs in 3,716 bytes, 64 bit in 85,395 |
| `mxm.py` | MXM reader (format in its docstring) and a dump of the module: header, instruments, samples, patterns, effects used |
| `mxmplay.py` | MXMPlay port (xmpInit / upload, xmpPlay, IRQ0 + timer, GUS update, player tick, new note, effects, envelope, pitch and volume tables) + GF1 model. Default: fake-GUS volume reads like the capture: DRAM, register log (685,612 lines) and music position (8,923 ticks) all identical to `full.dram` / `full.gus` / `full.ticks`. `--synth` builds the module with `mp4synth.py` first (same result). `--real-volume-read --out x` writes the register stream real hardware would get (433,593 lines). `--wav x.wav` renders through the GF1 model at 44,100 Hz (14 voices), starting at the end of tick 434 = `jizz_reference.mp4` frame 0 |

```
python3 mp4synth.py                     # -> "0 bytes differ"
python3 mxmplay.py                      # -> "ALL MATCH"
python3 mxmplay.py --synth              # from jizz_img.bin alone -> "ALL MATCH"
python3 mxmplay.py --wav <scratch>/jizz.wav
```

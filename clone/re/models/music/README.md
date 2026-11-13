# models/music: checks for clone/re/notes-music.md

Python models of CLONE.EXE's music code. Run inside the toolbox from this folder
(`toolbox run -c acme303 python3 <script>`). They read `../../unpacked/clone_img.bin`, the capture
`../../full.gus` / `full.dram` / `full.ticks` and the snapshots `../../snaps/t*.snap` (all generated from the
original, not in Git). Nothing they write belongs in Git either (modules, logs, WAVs: write them to a scratch
directory).

| file | what it does |
|---|---|
| `modsynth.py` | the module builder 0x414aee with the synthesisers 0x4149b4 / 0x414a38, in the capture's x87 arithmetic (64-bit mantissas, truncation, fsin = host double sin); parameters decoded from the code. `--out x.bin` writes the 1,200,000-byte buffer, `--check module.bin` compares it with a dump from `snaps/t11.snap` (identical), and it always compares the uploaded part with `full.dram` (identical). `--variant nearest / double` shows what the rounding does (211,193 / 0 samples differ). About 3 s |
| `dumpmod.py` | writes the module buffer (0x428470) and MXMPlay's data block (0xe30aec) from a snapshot, for `modsynth.py --check` |
| `mxm.py` | MXM reader and dump (JIZZ's `jizz/re/models/music/mxm.py`; the format is the same, plus 16-bit samples) |
| `mxmplay.py` | port of this MXMPlay build (xmpInit / upload, xmpPlay, IRQ0 + timer, GUS update, player tick, new note, the effects of the jump table 0x40274d, envelope, pitch / volume / vibrato tables) + GF1 model with 16-bit voices. Default: builds the module with `modsynth.py` and uses the capture's fake-GUS volume reads: DRAM, register log (1,024,247 lines) and music position + timer (11,713 ticks) all identical to `full.dram` / `full.gus` / `full.ticks`. `--stats` counts the effect handlers, `--positions f` writes order / row / timer per IRQ, `--real-volume-read --out x` the register stream real hardware would get (862,725 lines), `--wav x.wav` renders through the GF1 model at 34,300 Hz (18 voices), from IRQ 1 |
| `statecheck.py` | runs `mxmplay.py` to each snapshot and compares the whole data block (header copy and pointers, volume and vibrato tables, timer, 32 channel records: 5,613 bytes) with the snapshot's: identical in all 12 |
| `playerdiff.py` | disassembles this build and JIZZ's (`jizz/re/jizz_img.bin`), maps JIZZ's data-block offsets onto this build's and diffs them routine by routine (notes section 4) |
| `song.py` | the instruments with the synth call that made each sample, the order list with start IRQs and times, the notes / channels / effects per instrument |

```
python3 modsynth.py                     # -> "GUS DRAM ... identical"
python3 mxmplay.py                      # -> "ALL MATCH"
python3 statecheck.py                   # -> "ALL MATCH"
python3 mxmplay.py --wav <scratch>/clone.wav
```

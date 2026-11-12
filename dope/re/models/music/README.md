# models/music: checks for dope/re/notes-music.md

Python model of DOPE's music: Oskari Tammelin's "ProTracker for GUS replay routine" (1994) in the loader's 32-bit
code, GUS path. Run inside the toolbox from this folder (`toolbox run -c acme303 python3 dopeplay.py ...`). It reads
`../../unpacked/loader.bin`, `../../../original/DOPE.MOD` and the capture `../../full.gus` / `full.dram` /
`full.ticks` (all generated from the original, not in Git). Nothing it writes belongs in Git (logs, DRAM images,
WAVs: `*.wav` is ignored; write the rest to a scratch directory).

| file | what it does |
|---|---|
| `dopeplay.py` | port of the player routine by routine: GUS init 0x45b4 (DRAM probe, timer 2), MOD load and PIO upload 0x2d15 / 0x4450, pan table 0x4587, start 0x2c17 / 0x4369, the GUS IRQ 0x4040 (timer path 0x41e9: tick 0x22c4 with every effect, tempo → count, note-off ramps 0x421a, voice update 0x4299; ramp path 0x40d8), stop 0x2cff; a model of run_dope.py's fake GUS (registers, log format, continuous-time volume ramps, IRQ source queue, timer 2); a GF1 model (28 voices, 22,050 Hz, bidirectional loops) for the render |
| `analysis.py` | the measurements quoted in the notes: quirks the song triggers, the tick schedule against the closed form and the capture's lost timer overflows, the ramp-path delays, the capture's stream against the hardware-time stream |

```
python3 dopeplay.py                       # DRAM identical, GUS log 281,388 lines identical, music position of
                                          #   32,113 frames identical: "ALL MATCH" (about 4 s)
python3 dopeplay.py --out X               # also writes X.gus (byte-identical to full.gus) and X.dram
python3 dopeplay.py --time-order          # ramp ends queued in time order instead of the capture's order (7 IRQs differ)
python3 dopeplay.py --ideal --out X       # interrupts at their hardware times (no CPU latency)
python3 dopeplay.py --times X.txt         # per timer tick: hardware time after the first row tick, order, row
python3 dopeplay.py --info                # module, instruments, DRAM layout, cells used by the song
python3 dopeplay.py --wav dope_music.wav  # GF1 render from the first row tick (458 s, about 15 s; needs numpy)
python3 analysis.py                       # the numbers behind the notes (about 5 s)
```

The check replays the capture's interrupt service times (the `irq` lines of full.gus) and, for the 7 ramp
interrupts where the emulator found several ramp ends in one step, the order it queued them; everything written
is computed. `--eax` / `--ebx` set the interrupted code's registers that leak into the stream (0x808 / 0 in the
capture, see the notes section 9).

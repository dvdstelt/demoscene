# models/music: checks for okean/re/notes-music.md

Python model of the music of The Sea Robot of Love: the ProTracker player with its GUS driver in KORSO2.EXE's
code32 (0x10414..0x14bd3), a version of DOPE's player (`dope/re/models/music/`). Run inside the toolbox from this
folder (`toolbox run -c acme303 python3 okeanplay.py ...`). It reads `../../unpacked/korso2.bin` (the module is part
of the program image, at code32 0xae0) and the capture `../../full.gus` / `full.dram` / `full.ticks` (all generated
from the original, not in Git). Nothing it writes belongs in Git (logs, DRAM images, WAVs: `*.wav` is ignored;
write the rest to a scratch directory).

| file | what it does |
|---|---|
| `okeanplay.py` | port of the player routine by routine: GUS init 0x14935 (DRAM probe, timer 2), MOD load 0x13f28 with the pan table 0x1490c and the PIO upload 0x14788, start 0x13eb1 / 0x14714, the GUS IRQ 0x1443c (timer path 0x14591: tick 0x1343c with every effect, tempo → count, the voice loop that ramps each new note down and waits for the nested ramp IRQ, voice update 0x14663; ramp path 0x144b2), stop 0x13f18 / 0x1475b and shutdown 0x148ca; the last part's master-volume fade at the refreshes of the 50 Hz mode; a model of run_okean.py's fake GUS (registers, log format, continuous-time volume ramps, IRQ queue, timer 2 with lost overflows); a GF1 model (16 voices, 38,587.5 Hz) for the render |
| `analysis.py` | the measurements quoted in the notes: volume table in dB, pitch error, one-shot end bytes, ticks per order, the capture's held and lost timer interrupts at the part starts, the ramp-path delays with hardware times |

```
python3 okeanplay.py                          # DRAM identical, GUS log 70,027 lines identical, music position of
                                              #   9,107 frames identical: "ALL MATCH" (about 1 s)
python3 okeanplay.py --out X                  # also writes X.gus (byte-identical to full.gus) and X.dram
python3 okeanplay.py --ideal                  # interrupts at their hardware times (the capture's 8 held timer
                                              #   interrupts kept): the same writes, only the irq times differ
python3 okeanplay.py --times X.txt            # per timer interrupt: overflow time, service time, order, row, countdown
python3 okeanplay.py --info                   # module, instruments, DRAM layout, cells used by the song
python3 okeanplay.py --wav okean_music.wav    # GF1 render from the first player tick (182.5 s, about 5 s; numpy)
python3 analysis.py                           # the numbers behind the notes (about 3 s)
```

The check replays the capture's interrupt service times (the `irq` lines of full.gus); everything written is
computed. `--other-read` sets what a read of register select 0x09 returns (the emulator: 0; see the notes section
11), `--no-fade` renders without the last part's master volume fade.

# The Sea Robot of Love — reverse-engineering notes and tools

Everything learned from `KORSO2.EXE`, and the tools that learned it. Addresses are code32 offsets: offsets in
PMODE's 32-bit segment, the program's own near pointers (file offset in `unpacked/korso2.bin` = 0xec0 + offset;
linear address in the headless run = 0x8fc0 + offset).

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: unpacking (RNC ProPack), the image layout, PMODE, the runtime model, main's flow, the 50 Hz mode, the parts' common structure (the retrace timer), the music, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every part with its frames, music positions and what is on screen |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes table |
| `notes-*.md` | one per scope: core, music, title-objects, landscapes, morph-star, sheet |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (in the toolbox: `toolbox run -c acme303 ...`),
ffmpeg for the video (the host's, reached through `flatpak-spawn --host` from the toolbox), and `KORSO2.EXE` in
[`../original/`](../original/). Run them from this folder:

```
python3 unpack.py ../original/KORSO2.EXE               # unpacked/korso2.bin, .exe, .txt (stub in unicorn + Python decoder)
python3 rep.py code32 func 100c4                       # capstone helper: func / dis / calls / xref / funcs / str / ints / ports
python3 run_okean.py --out full --snap-parts > full.log    # the whole intro headless: full.frames, .ticks, .gus, .dram
                                                       # and snaps/part_<entry>.snap where each part starts (~5 min)
python3 run_okean.py --out m70 --args=-M --frames 900  # the same with -M (70 Hz mode 13h instead of the 50 Hz mode)
python3 frames2mp4.py full.frames okean_reference.mp4  # watch the capture (60 fps video of the 49.85 Hz frames)
python3 sheet.py full.frames overview.jpg --every 3 --cols 10 --scale 0.5   # one frame every 3 s
python3 sheet.py full.frames sheet.jpg 621 2331 3471   # chosen frames
python3 run_okean.py --load snaps/part_3554b.snap --out x --frames 2500     # resume at the third part
```

`run_okean.py --help` lists the options (`--mips` speed, `--frames`, `--snap-at`, `--trace`). The emulator runs
the program instruction by instruction at 100 M instructions per second of emulated time, about 0.8x real time.

The generated files (`unpacked/`, `full.*`, `snaps/`, `*.mp4`, `*.jpg`) are ignored by Git. They are made from the
original and stay on your machine.

## Agent scripts

`models/<scope>/` holds the Python models and verification scripts of the decompiling agents, one folder per scope,
each with a README (see BRIEF.md).

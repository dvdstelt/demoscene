# STARS — reverse-engineering notes and tools

Everything learned from `STARS.EXE` (NoooN, 1995), and the tools that learned it. Addresses are offsets in the
unpacked program image (`unpacked/stars_img.bin`), which are also the 32-bit program's near pointers.

## Notes

| file | contents |
|---|---|
| [NOTES.md](NOTES.md) | overview: file layout, the loader chain (Infiny Loader, PKLITE), the overlay and DIET, PMODE, services and ports, timing, LCA's player, how the headless run works |
| [PARTS.md](PARTS.md) | the timeline: every routine main calls, with frames, time, music position, PIT callback, frame rate and what is on screen |
| [BRIEF.md](BRIEF.md) | the brief for the decompiling agents, with the scopes |
| `notes-<scope>.md` | written by the decompiling agents (see BRIEF.md) |

## Tools

You need Python 3 with `unicorn`, `capstone`, `numpy` and `pillow` (on the maintainer's machine: inside the toolbox,
`toolbox run -c acme303 python3 ...`) and the original `STARS.EXE` in [`../original/`](../original/). Run them from
this folder, in this order:

```
python3 unpack.py ../original/STARS.EXE      # unpacked/stars_img.bin, stars_unp.txt, stars_pklite.exe,
                                             # files/01.bin .. 58.bin, files.txt (about 20 s)
python3 rep.py funcs                         # capstone helper: func / dis / dis16 / calls / xref / funcs / str / ints
python3 run_stars.py --out full --snap-at parts --snap 'snaps/f{frame}_{part}.snap'
                                             # the whole demo headless (mkdir snaps first): full.frames, .ticks,
                                             # .gus, .dram; stdout is the log (redirect to full.log)
python3 sheet.py full.frames sheet.jpg --every 2 --cols 6 --scale 0.5    # contact sheet, one frame every 2 s
python3 frames2mp4.py full.frames stars_reference.mp4                   # 60 fps video from the music start (ffmpeg)
python3 fpscheck.py snaps/f18999_1535c.snap   # what moves without the PIT, and the fps at 20 / 40 / 80 M instr./s
```

The full run executes the whole demo (6 min 20 s of it) instruction by instruction at 40 M instructions per
emulated second and takes about 12 minutes on one core; `--frames N` stops early, `--load --snap file` resumes from
a snapshot, `--freeze-pit` and `--part-ips ADDR=IPS` are for checking what depends on the frame rate.

The generated files (`unpacked/`, `full.*`, `snaps/`, `*.mp4`) are ignored by Git. They are made from the original
and stay on your machine.

# Reverse-engineering tools

The tools used to take the originals apart, and how to run them. They are written in Python 3, and some need
`capstone` (disassembler), `unicorn` (CPU emulator), `numpy` and `pillow`:

```
pip install capstone unicorn numpy pillow
```

All of them read the original files from `<demo>/original/` (see `scripts/fetch-originals.sh`).

## 303 (Watcom C/C++, DOS/4GW, LE executable)

| tool | what it does |
|---|---|
| [`303/remake/tools/le_load.py`](../303/remake/tools/le_load.py) | loads the LE executable like DOS/4GW: objects at their linear addresses (code at 0x10000, data at 0x50000), fixups applied |
| [`303/remake/tools/re303.py`](../303/remake/tools/re303.py) | capstone helper: `func ADDR` (recursive-descent listing, x87 constants annotated with their values), `dis ADDR N`, `calls ADDR`, `str NAME` (string address and the code that uses it) |
| [`303/remake/tools/lzw.py`](../303/remake/tools/lzw.py) | reads the PAK archives (LZW15V variant) |
| [`303/remake/tools/dumpvideo.cpp`](../303/remake/tools/dumpvideo.cpp) | decodes the `.LZW` video clips to images |
| [`303/remake/tools/rowtime.cpp`](../303/remake/tools/rowtime.cpp) | times the module's rows, to convert the music position counter to seconds |
| [`303/re/pos.py`](../303/re/pos.py) | music position counter → music time and capture time |
| [`303/re/compare.sh`](../303/re/compare.sh) | contact sheet: frames of a video capture (`CAPTURE=...`) next to the remake's frames at the same moments |

```
cd 303/remake/tools
python3 re303.py str tunnel.raw       # where the code loads a texture
python3 re303.py func 0x2d81c         # list a routine
```

## Paper (PKLITE, PMODE, hand-written 32-bit assembly)

| tool | what it does |
|---|---|
| [`paper/re/unpklite.py`](../paper/re/unpklite.py) | runs PKLITE's own decompression stub in unicorn (real mode, minimal DOS) and dumps memory at the original entry point |
| [`paper/re/rep.py`](../paper/re/rep.py) | capstone helper on the 32-bit segment: `func`, `dis`, `calls`, `xref`, `funcs` |
| [`paper/re/run_paper.py`](../paper/re/run_paper.py) | runs the whole intro headless in unicorn with fake VGA, timer, keyboard and Gravis UltraSound. Records per 70 Hz tick: screen, palette, GUS writes, key variables. Snapshots let you start at any part |
| [`paper/re/player_ref.py`](../paper/re/player_ref.py) | the intro's music player in Python, routine by routine, plus a GF1 model to render audio. Regenerates the captured GUS writes byte for byte |
| [`paper/re/gf1synth.py`](../paper/re/gf1synth.py) | an earlier, simpler GF1 renderer (has known bugs, see notes-music.md) |
| [`paper/re/frames2mp4.py`](../paper/re/frames2mp4.py), [`sheet.py`](../paper/re/sheet.py), [`compare.py`](../paper/re/compare.py) | turn a capture into a video, a contact sheet, or a side-by-side video |
| [`paper/remake/tools/compare.py`](../paper/remake/tools/compare.py) | the remake's frames next to the capture's, with an optional difference score |

[`paper/re/README.md`](../paper/re/README.md) lists the commands in order.

## JIZZ (DOS32, VESA 2.0 hicolor, hand-written 32-bit assembly)

| tool | what it does |
|---|---|
| [`jizz/re/unadam.py`](../jizz/re/unadam.py) | runs the DOS32 kernel's own loader routine (decompress, relocate, clear the BSS) in unicorn, twice at different bases, and checks a Python reimplementation against it |
| [`jizz/re/rep.py`](../jizz/re/rep.py) | capstone helper on the program image: `func`, `dis`, `calls`, `xref`, `funcs`, `str`, `ints` |
| [`jizz/re/run_jizz.py`](../jizz/re/run_jizz.py) | runs the whole intro headless in unicorn with fake DOS32 services, a VESA 2.0 BIOS with a linear frame buffer, the timer chip and a Gravis UltraSound. Records per timer tick: the visible page, the music position and sync event, the page flips, GUS writes. Snapshots, a frozen timer and per-part CPU speeds help find frame-rate dependent state |
| [`jizz/re/fpscheck.py`](../jizz/re/fpscheck.py) | compares two runs made at different CPU speeds at equal timer values |
| [`jizz/re/frames2mp4.py`](../jizz/re/frames2mp4.py), [`sheet.py`](../jizz/re/sheet.py) | turn a capture into a video or a contact sheet |
| [`jizz/re/models/`](../jizz/re/models/) | the agents' Python models: textures, the music synthesiser and MXMPlay, the engine, every part |
| [`jizz/remake/tools/compare.py`](../jizz/remake/tools/compare.py) | the remake's frames next to the capture's, with an optional difference score |

[`jizz/re/README.md`](../jizz/re/README.md) lists the commands in order.

## STARS (Infiny Loader, PKLITE, PMODE, hand-written 32-bit assembly)

| tool | what it does |
|---|---|
| [`stars/re/unpack.py`](../stars/re/unpack.py) | runs the Infiny Loader stub and PKLITE's decompressor in unicorn, twice at different load segments, and checks a Python reimplementation of both against them; unpacks the 58 files of the overlay with a Python DIET decompressor checked against the program's own routine |
| [`stars/re/rep.py`](../stars/re/rep.py) | capstone helper on the program image: `func`, `dis`, `dis16`, `calls`, `xref`, `funcs`, `str`, `ints` |
| [`stars/re/run_stars.py`](../stars/re/run_stars.py) | runs the whole demo headless in unicorn without DOS or PMODE: answers the DPMI and DOS calls, fakes the timer chip, the VGA DAC and retrace, the DMA controllers and a Gravis UltraSound with its timers and DMA uploads. Records per displayed frame: the screen, the palette, the timer and music ticks, the music position and the routine main is in; every GUS write and the GUS memory. Snapshots at every part; `--freeze-pit` and `--part-ips` help find frame-rate dependent state |
| [`stars/re/fpscheck.py`](../stars/re/fpscheck.py) | from a snapshot: what moves without the timer interrupt, and the frame rate at 20, 40 and 80 M instructions per second |
| [`stars/re/frames2mp4.py`](../stars/re/frames2mp4.py), [`sheet.py`](../stars/re/sheet.py) | turn a capture into a video or a contact sheet |
| [`stars/re/models/`](../stars/re/models/) | the agents' Python models: the core tables and pictures, the music player, the 3D engine, every part |
| [`stars/remake/tools/compare.py`](../stars/remake/tools/compare.py) | the remake's frames next to the capture's |
| [`stars/remake/tools/align_art.py`](../stars/remake/tools/align_art.py) | the redrawn pictures: makes the nearest-neighbour upload for the image generator, fits a redraw onto the original (scale and offset) and measures the fit, and makes the comparison sheet |

[`stars/re/README.md`](../stars/re/README.md) lists the commands in order.

## DOPE (a chain of RNC-packed executables: PMODE loader, 16-bit real-mode parts)

| tool | what it does |
|---|---|
| [`dope/re/unrnc.py`](../dope/re/unrnc.py) | splits DOPE.EXE into its executables (the loader, seven parts, TERM) and unpacks the eight packed ones: runs every ProPack stub in unicorn and a Python RNC-1 decoder, and checks that both give the same image and both CRCs |
| [`dope/re/rep.py`](../dope/re/rep.py) | capstone helper per image (`loader`, `cdpi`, `loader32`, `p1` .. `p7`): `func`, `dis`, `calls`, `xref`, `funcs`, `far`, `str`, `ints`, `ports` |
| [`dope/re/run_dope.py`](../dope/re/run_dope.py) | runs the whole demo headless in unicorn without PMODE or DOS: two CPUs share one memory, one in real mode (the chain loader and the parts), one in 32-bit mode (the music player). Fakes the BIOS, DOS, the VGA with its planes and CRTC timing, the timer chip and a Gravis UltraSound with its timers and volume ramps. Records per displayed frame: the screen, the palette, the music position, the part; every GUS write and the GUS memory. Snapshots where each part starts; a resumed run reproduces the uninterrupted one exactly |
| [`dope/re/capture.py`](../dope/re/capture.py) | re-captures every part from its snapshot in parallel and stitches the result (about 10 minutes instead of 40) |
| [`dope/re/timeline.py`](../dope/re/timeline.py) | per-part table of a capture: frames, times, music position, drawn frame rate |
| [`dope/re/frames2mp4.py`](../dope/re/frames2mp4.py), [`sheet.py`](../dope/re/sheet.py) | turn a capture into a video or a contact sheet |
| [`dope/re/models/`](../dope/re/models/) | the agents' Python models: the core, the music player, the 3D engine, the parts |
| [`dope/remake/tools/compare.py`](../dope/remake/tools/compare.py) | the remake's frames next to the capture's |
| [`dope/remake/tools/align_art.py`](../dope/remake/tools/align_art.py) | the redrawn pictures: makes the uploads, fits a redraw onto the original, optionally matches its colours to the original's, and makes the comparison sheet |

[`dope/re/README.md`](../dope/re/README.md) lists the commands in order.

## The emulator harness in short

`run_paper.py` doesn't emulate a PC. It emulates just enough for this one program:

- **Memory.** The 32-bit segment is loaded at linear address 0 with flat segment selectors, so the intro's own
  "linear base" variable is 0 and VGA memory is at 0xa0000.
- **The DOS extender.** PMODE's variables are preset. Its interrupt-vector service is intercepted, so the intro's
  timer and keyboard handlers are known. Its real-mode interrupt call (`int 0x33`) is emulated for the few
  services the intro uses (set the video mode, DOS print, exit).
- **Ports.** Reads and writes are hooked: VGA palette and retrace, the timer chip, the interrupt controller, the
  keyboard and the GUS (registers, DRAM and voices).
- **Time.** The timer interrupt is delivered every N instructions of main-line code. N sets the speed of the
  emulated PC, which matters for anything the intro does per drawn frame.

That is a few hundred lines of Python. It gives an exact, repeatable run of the original that can be stopped,
inspected and resumed at any tick. `run_jizz.py` does the same for JIZZ, with DOS32's services and a VESA BIOS
instead of PMODE and VGA, and `run_stars.py` for STARS. `run_dope.py` adds a second CPU, because DOPE's parts run in
real mode while its music player runs in 32-bit protected mode, and it skips the time a retrace wait would spin.

## In the remakes

The remakes do their own loading at run time, in C++:

- `paper/remake/src/pklite.cpp` unpacks PAPER.EXE. Its output is byte-identical to the stub's.
- `paper/remake/src/exe.*` reads by address from the unpacked image.
- `303/remake/src/assets.*` reads the PAK archives, the LZW codec, the video codec and the meshes.
- `jizz/remake/src/adam.cpp` unpacks JIZZ.EXE like the DOS32 loader; `jizz/remake/src/exe.*` reads by address.
- `stars/remake/src/unpack.cpp` decrypts and decompresses STARS.EXE like Infiny Loader and PKLITE, and unpacks the
  DIET-compressed files of its overlay; `stars/remake/src/exe.*` reads by address and by file number.
- `dope/remake/src/rnc.cpp` unpacks the RNC-packed executables in DOPE.EXE; `dope/remake/src/exe.*` reads by
  `SEG:OFF` address in each.

The redrawn pictures of STARS and DOPE are made outside the remakes (ChatGPT's image generation, then
`tools/align_art.py`) and read from `<demo>/remake/art/` as PNG files; see [how-it-was-made.md](how-it-was-made.md).

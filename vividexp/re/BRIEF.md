# Reverse-engineering VIVID.EXE: shared brief for decompiling one scope

## Goal

*Vivid Experiment* (Doomsday, Assembly '96 PC demo; code Doom and Mri, graphics Dice and Damac, music Swallow and
Wode; MIDAS Sound System 0.5a by Sahara Surfers, PMODE/W by Daredevil and Tran) will be remade in C++20 / SDL3 /
OpenGL 3.3, like the other remakes in this repository. The remake renders at any resolution in true colour: "the
same as the original, but much sharper". For that it needs the exact algorithms. Your job: decompile the routines
in your scope into readable pseudo-C with every constant, so that someone who has not seen the assembly can port
them. You are read-only on everything except your own notes file and your own models directory.

## Read first

- `vividexp/re/NOTES.md`: unpacking, the file archives, main's flow, **the clock** (MIDAS's screen-synchronised
  timer, the frame counter [0x825c5c] at 70.086 Hz), the script and its commands, the code map.
- `vividexp/re/PARTS.md` (when it exists): the timeline (frames, music positions, script lines, what is on screen).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so
  for every static table, picture, palette, mesh, string or parameter say where it is (address in the image, or
  archive file name, size, format) so the remake can read it at run time.
- For the style of the result, look at `inside/re/notes-water.md`, `inside/re/notes-twirl.md` or
  `inside/re/notes-engine.md` (Inside is also a Watcom C program under PMODE/W).

## Files and tools (all in `vividexp/re`, run inside the toolbox container)

- `unpacked/vivid_img.bin`: the program from 0x400000 (object 1: code to 0x42b2a8, then the archives; object 2 at
  0x810000; object 3 at 0x820000: data to 0x825c3f, then the BSS to the stack top 0x847fc0). **Addresses are linear
  addresses** = the program's near pointers = addresses in the emulator and in snapshots.
- `rep.py` (capstone):
  ```
  cd vividexp/re
  toolbox run -c acme303 python3 rep.py func 0x401270     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x401270 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x40241c    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x825c5c     # code/data containing this 32-bit value
  toolbox run -c acme303 python3 rep.py funcs | str | ints | ports
  ```
  In Python (`import rep`): `rep.rd(a, n)`, `rep.rd16/rd32/rdf/rdd(a)`, `rep.cstr(a)`, `rep.dis`, `rep.func`.
  The listing annotates float / double constants (`; =0.005`) and BSS addresses (`; bss`).
- `files.py`: the archives. `python3 files.py` lists every file with its address and size;
  `unpacked/files/1/`, `unpacked/files/2/`, `unpacked/files/3/` hold them extracted (archive 1 = part I, 2 and 3 =
  part II). In Python: `import files; files.entries(img, files.TABLES[k])` → [(name, address, size)].
- **The code is Watcom C** (register calling convention: arguments in EAX, EDX, EBX, ECX, then on the stack; some
  routines are `__cdecl` with every argument on the stack; floats and doubles go on the stack) plus hand-written
  assembler (inner loops, MIDAS). Watch for the FPU's rounding mode and for Watcom's float → int conversions (find
  the helper the code calls before `fistp` and say whether it truncates or rounds).
- Shared helpers (the core scope describes them; use them by what goes in and out): 0x40241c file by name →
  pointer to its data; 0x400104 reads the music position into [0x825c48] order, [0x825c50] row; the frame counter
  [0x825c5c] (+1 per screen refresh, 70.086 Hz); the palette in 16.16 fixed point at 0x826abc (256 × 12 bytes) with
  the fade step 0x401ed0 and the fade set-up 0x4020b8; 0x403738 sets the routine [0x832504] that the retrace
  interrupt calls; Watcom's malloc / free / memcpy / sscanf / atoi / atof (name them when you meet them).
- **Say where your part draws**: straight into VGA memory at 0xa0000, or into a buffer that is copied (when, and
  waiting for the retrace or not). **Say whether it overwrites every pixel of the frame**, and what it would show
  otherwise (stale pixels, the previous part).
- Dynamic analysis: `run_vivid.py` runs the demo headless in unicorn (see its docstring). Snapshots (pickle: `'mem'`
  = 64 MB from linear 0, `'ctx'`, `'st'`, `'gus'`, `'dma'`, `'eip'`) are in `vividexp/re/snaps/f<frame>.snap`
  (the whole demo) and `vividexp/re/snaps2/f<frame>.snap` (part II run on its own with `--args 2`), taken at the first
  screen refresh after the script ran an effect's Init or Run command (`full.script` / `part2.script` list the script
  lines with their frames). Unpickling needs unicorn imported, so run inside the toolbox. Read generated tables,
  textures, meshes and the loaded module straight from `snap['mem']`. To run on from a snapshot with your own hooks,
  COPY `run_vivid.py` into your models directory and edit the copy (never the shared one; a copy anywhere below
  `vividexp/re` finds `unpacked/` by itself):
  ```
  toolbox run -c acme303 python3 <models>/run_vivid.py --load --snap snaps/f5000.snap --out <models>/x --frames 5600
  ```
  (`--frames` is the absolute frame to stop at.) The emulator runs about 5 to 25 screen refreshes per second of
  wall time, depending on the part. Do not start runs of the whole demo unless you really need one.
- Reference captures: `full.*` (the whole demo) and `part2.*` (part II alone; its frame numbers start again at 0).
  **They are being written while you start**: a capture is complete when its `.time` file is not empty
  (`full.time`, `part2.time`). `.frames` (24-byte header `VIVDFRM1`, then per screen refresh (70.086 Hz) the 64,000
  bytes of VGA memory and the 768-byte DAC palette; record k = refresh k = program time (k + 1) / 70.086 s),
  `.ticks` (per refresh: frame, program time, music order, row, the frame counter [0x825c5c], script part and line,
  timer interrupts, main EIP), `.script` (every script line with the frame it ran at), `.gus` (GUS writes, stamped
  with the number of timer interrupts so far), `.dram` (GUS memory at the end). In Python: `from sheet import
  open_frames, rgb, indexed`. `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> f1 f2 ...` makes a
  contact sheet (then Read it).
- Do NOT modify anything in `vividexp/re` except your own notes file and your models directory. Scratch files and
  your Python models go in `vividexp/re/models/<scope>/` (keep the models: they are the record of how a finding was
  checked; add a short README.md there listing them). Large generated files (dumps, `.npy`, images, runs) stay out of
  Git: put them in `vividexp/re/models/<scope>/runs/` or `dump/`.

## Time bases (state one for every animated variable: this decides how it is ported)

- **Frame counter** [0x825c5c] (70.086 Hz, +1 per screen refresh in the timer interrupt): give the formula of every
  variable computed from it, and its origin (the value it is measured from, e.g. when the Run command started).
- **Per drawn frame**: anything stepped once per frame the main line draws, without reading the frame counter, is
  frame-rate dependent: name it, say how it is stepped, and say what it looks like. The capture runs at 100 M
  instructions per second; give the frame rate your part reaches there (frames drawn per refresh).
- **Music position**: order / row from 0x400104; the script waits for positions; some effects may also read the
  position or MIDAS's sync value [0x825c54] themselves. Give every such use.
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Colours: 8 bits through a palette

Everything is drawn in 256 colours. The remake draws true colour: a pixel's colour is the palette entry of its
index, as the DAC shows it (6-bit value v → v / 63). So for every drawn pixel say how its index is made: straight
from a picture, through a table (what the table's two inputs are), additive or saturating index arithmetic, palette
ranges. The remake will evaluate the same mapping with continuous inputs (filtered textures, continuous shades
interpolated between the table's levels). Say which palette is active and which palette effects the part runs
itself (fades, flashes, writes to the DAC).

## Pictures

The remake shows faithful high-resolution redraws of hand-drawn pictures by default and the original pixels on
request (`AGENTS.md`, "Redrawn pictures"; photos of real people are not redrawn); generated textures, maps and
anything an effect reads through palette indices stay computed from the original. So for every picture or texture
your scope uses, say:
- **drawn, a photo, a rendered image, or generated**;
- its file, size, format, palette, and how it is used: where on screen, at what size, blend or transparency (which
  index is transparent), whether an effect reads it through palette indices or tables (then a redraw would have to
  keep its indices: say so), whether it is mapped on 3D geometry;
- for text pictures: the look of the letters and where they are placed (do not transcribe text that quotes song
  lyrics; say what it is instead).

## Output: `vividexp/re/notes-<scope>.md`

Per routine or effect:
1. what it draws, in one or two sentences, with its frames / music positions (PARTS.md or the `.script` files);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed and how the pixel's index is made (above), and the formulas for every generated table,
   texture or mesh. Procedural generators must be described precisely enough to regenerate them at a higher
   resolution. Say which data is static (address, or archive file, size, format);
3. the time base of every animated variable (above);
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (low-resolution grids and
   interpolation, lookup tables, integer steps, nearest-neighbour texture reads, palette quantisation, 8-bit
   blends), with what they approximate; and the grain that must stay (noise, dither and texture patterns are drawn
   one cell per original pixel in the remake: say which patterns those are);
5. whether the part overwrites the whole frame;
6. what you could not determine (mark guesses as guesses).

Check your reading against the capture: a Python model of the routine that reproduces a captured frame (or a table
in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines | notes file |
|---|---|---|
| core | main 0x4001a0 (every step); the archives 0x402200 / 0x402398 / 0x40241c / 0x402354; the script: the reader 0x40b6e4, 0x40b5a0, the interpreter 0x40b760 with **every command's exact argument meaning** (InitPCX, DeinitPCX, PutPCXSYNC: all eight numbers, the slot, the "1/0" flag; LoadPAL, InitFADE: steps, first, last; NegPAL; ClearSCR; WaitSYNC 0x40279c; ScriptJMP; SwapMOD_*); the PCX decoder; the palette 0x401ed0 / 0x401fbc / 0x402000 / 0x402054 / 0x4020b8 (16.16 arithmetic, when the DAC is written relative to the retrace, rounding); the zoom credits InitZOOM 0x402460, RunZOOMSYNC 0x402644, DeinitZOOM 0x402630 (the zoom's maths per frame: the 900 / 200 / -3.5 / 28 arguments, which picture is zoomed, how the name picture is laid over it); the clock: MIDAS's timer 0x410740..0x410f00 as the demo uses it (screen sync, the routines 0x40007c / 0x403728, when the frame counter and the music position change relative to the retrace); and from `full.ticks` / `full.script`: **the exact relation between program time, refreshes, the frame counter and the music position**, how late after the music position each script line runs, and what the remake should take as time. Also whether part II's "not found" files matter (pal/greets.pal, pal/phong2.pal, gen2/gen.ini: archive 3 holds them; check the lookup walks into it) | notes-core.md |
| music | MIDAS Sound System 0.5a as the demo uses it: the XM loader (EXPRMNT1.XM, EXPRMNT2.XM: header, orders, patterns, instruments, samples, envelopes; the songs: orders, speed, tempo, effects used), the XM player (every effect and the envelopes, auto-vibrato, the volume / panning / frequency it gives the sound device, the position / sync values it reports), the GUS sound device (0x41b1a0..: sample upload into GUS memory through port 0x327, voice allocation, how it turns the player's calls into GF1 register writes: frequency, volume and ramps, panning, sample start / loop / end, voice stops), the timer's music tempo (how the player's tick period is set from the BPM and how it shares the interrupt with the screen sync). Deliver a Python player that reproduces `full.gus` (order of writes and values, stamped by timer interrupt) and the order / row columns of `full.ticks` from the XM files, a WAV render through a GF1 model, and the exact time of every player tick in timer interrupts and refreshes. Compare the GUS handling with `inside/remake/src/gf1.h` / `gf1.cpp` (and `gusdrv.*`, `amp.*` there) so the remake can reuse its GF1 synthesiser; say what the remake's player must do differently | notes-music.md |
| bitmap | the "bitmap effects": InitBITMAPEFEK 0x400ae4 (the `.ini` format: type, planes, background picture, the picture and table names; what 0x4004e0..0x400ae4 build from them: palettes, shifted copies, the "PICRGB", "TAUSTA", "DTABLE", "BUFF" buffers), RunBITMAPEFEK 0x401270 / 0x401024 (the `.prm` format: the eight speeds), DeinitBITMAPEFEK 0x400ff0, ClearEFEKPARAMS 0x401588, and the drawers 0x40c3ad, 0x40c455, 0x40c543, 0x40c626, 0x40c74f, 0x40c86d, 0x40cab5, 0x40cb33, 0x40cbe7 (1 × 1 with 1..4 planes, 2 × 2 with 3 planes): how a table (`tabs/*.1x1`, `*.2x2`) and the offsets combine into texture coordinates, how the planes' indices are combined (sum, table), the palettes (`plo*.pal` etc.) and the fades. Part I's three "1x1 Efekti" sections and the 2x2 section; part II's efek4 / efek5 sections | notes-bitmap.md |
| bump | InitBUMP 0x402ec4, RunBUMPEFEK 0x403434, DeinitBUMP 0x40315c and 0x402948..0x403434: `bump/bump1.ini` .. `bump3.ini`, `bumpprm1.val`, `bumpwave.val`, `bumptab.val`, the height pictures `bumppic1..3.pcx`, the light pictures `phong*.pcx`, the palettes; the light's path, the wave, how a pixel's index is made. Part II's "SUOMI BUMP" (Finland), "ICE BUMP", "DOOMSDAY BUMP" | notes-bump.md |
| water | WaterInit 0x405bd0 and WaterEffect 0x4064b0 with everything they call that is not the shared ini / spline readers: `water/water.ini`, the spline paths `wat_s.spl`, `wat_sphe.spl` (format, how a position on the path is evaluated with SplineSpeed and ObjectSpacing), the objects `wat_s.m3d` (the DSD letters) and `wat_sphe.m3d` (the transparent sphere) (format), the environment map `watenv.pcx`, the bump map `watbump.pcx`, the background `watback.pcx`: transforms, projection, sorting, the rasterizers and how they make each pixel's index (environment bump mapping, the transparent object over the background), the palette. Part II's opening | notes-water.md |
| generic | GenericInit 0x408cec, GenericEffect 0x409d74 and the 3D engine they use (0x4074e4..0x40b5a0, and in 0x411000..0x42b000 the ini reader, the spline reader 0x413390.., the `.m3d` loader, the memory pool, transforms, projection, sorting, the rasterizers): the shading types that `gen/gen.ini` and `gen2/gen.ini` use (9 environment bump, 10 floating environment bump, 11 Phong texture bump; describe the others only as far as the code shares them), `texture.lit` (the light table), the maps, the background picture, the spline paths `land.spl`, `path.spl` and how objects move along them (SplineSpeed, SplineStartPos, looping), the camera. Part II's "LANDSCAPE" and the second generic scene | notes-generic.md |

The part scopes call into the core helpers: describe those calls by what goes in and what comes out. Where the
routine lists above are incomplete, follow the calls: everything a part's routines call that is not shared belongs
to the part. Water and generic may share engine routines: each describes the ones it uses.

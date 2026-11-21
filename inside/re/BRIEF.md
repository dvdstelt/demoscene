# Reverse-engineering INSIDE.EXE — shared brief for decompiling one scope

## Goal

*Inside* (CNCD, The Gathering 1996 PC demo, 1st place; code Wili, Jouni and Juliet, graphics Destop, additional
graphics Santtu, music Yolk and Legend; DSMI by Otto Chrons, PMODE/W by Thomas Pytel, the SurRender 3D engine by
Hybrid) will be remade in C++20 / SDL3 / OpenGL 3.3, like the Paper, JIZZ, STARS, DOPE and clone meets clone
remakes in this repository. The remake renders at any resolution in true colour: "the same as the original, but
much sharper". For that it needs the exact algorithms. Your job: decompile the routines in your scope into readable
pseudo-C with every constant, so that someone who has not seen the assembly can port them. You are read-only on
everything except your own notes file and your own models directory.

## Read first

- `inside/re/NOTES.md`: unpacking, INSIDE.DAT, the runtime model, main's flow, **the clock** (the retrace timer:
  4 interrupts per screen refresh, the frame clock, the part tick counter), the script table, the palette system,
  the screen buffers, the headless run, the code map by part.
- `inside/re/PARTS.md` (when it exists): the timeline (frames, music positions, script entries, what is on screen).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so
  for every static table, picture, palette, mesh, string or parameter say where it is (address in INSIDE.EXE, or
  file name and offset in INSIDE.DAT, size, format) so the remake can read it at run time.
- For the style of the result, look at `clone/re/notes-parts3d.md`, `jizz/re/notes-parts3d-a.md` or
  `paper/re/notes-planes.md`.

## Files and tools (all in `inside/re`, run inside the toolbox container)

- `unpacked/inside_img.bin`: the program, object 1 (code) at 0x400000, object 2 (data, BSS, stack) at 0x420000.
  **Addresses are linear addresses** = the program's near pointers = addresses in the emulator and in snapshots.
  Initialised data ends at 0x423c45; the BSS (zero in the file) runs to 0x4ba6a0.
- `rep.py` (capstone):
  ```
  cd inside/re
  toolbox run -c acme303 python3 rep.py func 0x403748     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x403748 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x400614    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x4248c8     # code/data containing this 32-bit value
  toolbox run -c acme303 python3 rep.py funcs | str | ints | ports
  ```
  In Python (`import rep`): `rep.rd(a, n)`, `rep.rd16/rd32/rdf/rdd(a)`, `rep.cstr(a)`, `rep.dis`, `rep.func`.
  The listing annotates float / double constants (`; =0.005`) and BSS addresses (`; bss`).
- `datfile.py`: `import datfile; d = datfile.Dat('../original/INSIDE.DAT'); d.get('logo/logo.pal')`; d.files =
  {name: (offset in the unpacked block, size)}. A part gets a file's address as [0x424894] + offset (0x400614).
- **The code is Watcom C** (register calling convention: arguments in EAX, EDX, EBX, ECX, then on the stack, the
  callee pops them with `ret n`; floats and doubles are passed on the stack) plus hand-written assembler (the
  rasterizers, DSMI). Float → int conversions call 0x40ba78 (truncate) before `fistp`, so they **truncate toward
  zero**; the FPU otherwise rounds to nearest, at 53-bit precision in main-line code and 64-bit inside the timer
  interrupt (notes-core.md section 10). `fsin` / `fcos` in the emulator are computed in
  double precision on the host, so captured tables can differ from a real Pentium in the last bits.
- Shared helpers (the core scope describes them; use them by what goes in and out): 0x400614 data file by name
  → pointer; 0x40b4d8 allocate (EAX size, EDX name) → EAX; 0x40ad40 memset; 0x4134cd rand() (Watcom LCG),
  0x4134f1 srand; 0x400364 / 0x4004ac / 0x400498 / 0x400454 / 0x400438 the palette (NOTES.md "Palette");
  0x400af4 / 0x400b04 set the tick / frame routine; 0x400b14 flip; 0x40a83c screen geometry and clip window;
  0x415e50 makes a SurRender bitmap (0x28-byte struct) from pixels; the SurRender API (0x410520.., the engine scope
  describes it).
- The draw target of a frame is [0x4b6ec0] (8-bit, 320 × 200, pitch [0x4b6ec4] = 320). It is the buffer shown two
  frames earlier (two buffers, flipped by 0x400b14). **Say whether your part overwrites every pixel of the
  frame**, and what it would show otherwise.
- Dynamic analysis: `run_inside.py` runs the demo headless in unicorn (see its docstring and NOTES.md). Snapshots
  (pickle: `'mem'` = 64 MB from linear 0, `'ctx'`, `'st'`, `'gus'`, `'dma'`, `'eip'`) are in
  `inside/re/snaps/f<frame>.snap`, taken at the first screen refresh after a part's frame routine was set
  (PARTS.md lists them; f38 = the intro's first frame, all precalc done). Unpickling needs unicorn imported, so run
  inside the toolbox. Read generated tables, textures, meshes, the loaded module and driver straight from
  `snap['mem']`. To run on from a snapshot with your own hooks, COPY `run_inside.py` into your models directory and
  edit the copy (never the shared one; a copy anywhere below `inside/re` finds `unpacked/` by itself):
  ```
  toolbox run -c acme303 python3 <models>/run_inside.py --load --snap snaps/f1446.snap --out <models>/x --frames 1600
  ```
  (`--frames` is the absolute frame to stop at.) A run of a few hundred frames takes a minute or two. Do not start
  runs of the whole demo (about an hour) unless you really need one.
- Reference capture: `full.frames` (24-byte header `INSDFRM1`, then per screen refresh (70.086 Hz) the 64,000 bytes
  of VGA memory and the 768-byte DAC palette; record k = refresh k = program time k / 70.086 s), `full.ticks` (per
  refresh: frame, program time, music order, row, the frame clock [0x424880], the part tick counter [0x4248c8],
  the script index [0x4248bc], frame routine, tick routine, frames shown [0x42488c], timer interrupts, main EIP),
  `full.flips` (every copy of a finished frame to the screen), `full.gus` (GUS writes, stamped with the number of
  timer interrupts so far), `full.dram` (GUS memory). In Python: `from sheet import open_frames, rgb, indexed,
  read_ticks`. `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> f1 f2 ...` (or `--every 2 --from-time
  40 --to-time 60`, or `--pos 3:0 5:32`) makes a contact sheet (then Read it).
- Do NOT modify anything in `inside/re` except your own notes file and your models directory. Scratch files and
  your Python models go in `inside/re/models/<scope>/` (keep the models: they are the record of how a finding was
  checked; add a short README.md there listing them).

## Time bases (state one for every animated variable: this decides how it is ported)

The demo runs on the screen refresh (NOTES.md "Clock"):
- **Refresh tick** (70.086 Hz): the parts' tick routines ([0x4248b4], called from the timer callback after the
  frame clock [0x424880] += 1) and the palette fade steps run once per refresh. State they advance is stepped per
  refresh: give the recurrence and, where it is linear, the closed form in refreshes since the part began.
- **Part ticks** [0x4248c8] (280.34 Hz, 4 per refresh, reset to 0 when the script sets the part's routines): give
  the formula of every variable computed from it (or from the frame clock [0x424880]) directly.
- **Per drawn frame**: the frame routine [0x424888] runs once per frame the main loop draws, as fast as the CPU
  allows (the capture is at 150 M instructions per second; PARTS.md has the frame rate per part). Anything that
  changes once per drawn frame without reading a clock is frame-rate dependent: name it and say what it looks like.
- **Script events**: routines of the script table (NOTES.md, PARTS.md) that run at music positions: give every
  variable your part's event routines set and what the part does with it.
- **Random numbers**: name the generator (0x4134cd), its seed and when it is called, so the remake can replay the
  sequence.

## Colours: 8 bits through a palette

Everything is drawn in 256 colours. The remake draws true colour: a pixel's colour is the palette entry of its
index, as the DAC shows it (6-bit value v → v / 63). So for every drawn pixel say how its index is made: straight
from a texture, through a shade table (`.shd`: which axis is the colour and which the light level, how the level
is computed: Gouraud, distance, lens flare), a blend table (`.int`, `.qnt`, `.sdw`, `.fog`: what the two inputs
are), additive or saturating index arithmetic, palette ranges. The remake will evaluate the same mapping with
continuous inputs (filtered textures, continuous light levels interpolated between the table's levels). Say which
palette is active (0x400364 with which file) and which palette effects the part runs itself (writes to the base
palette or the DAC, cycling).

## Pictures

The remake shows faithful high-resolution redraws of hand-drawn pictures by default and the original pixels on
request (`AGENTS.md`, "Redrawn pictures"; photos of real people are not redrawn); generated textures, maps and
anything an effect reads through palette indices stay computed from the original. So for every picture or texture
your scope uses, say:
- **hand-drawn, a digitised photo, or generated** (a `.raw` file in INSIDE.DAT is drawn or scanned by Destop or
  Santtu unless the code makes it; computed = generated);
- its file, size, format (header of sprites), palette, and how it is used: where on screen, at what size, blend or
  transparency (which index is transparent), whether an effect reads it through palette indices or shade tables
  (then a redraw would have to keep its indices: say so), whether it is mapped on 3D geometry;
- for text pictures (the credits): the exact text and the look of the letters.

## Output: `inside/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its frames / music positions (PARTS.md);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed and how the pixel's index is made (above), and the formulas for every generated table,
   texture or mesh. Procedural generators must be described precisely enough to regenerate them at a higher
   resolution. Say which data is static (address, or INSIDE.DAT file, size, format);
3. the time base of every animated variable (above);
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (low-resolution grids and
   interpolation, lookup tables, integer steps, nearest-neighbour texture reads, palette quantisation, 8-bit
   blends), with what they approximate;
5. whether the part overwrites the whole frame;
6. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured
frame (or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines | notes file |
|---|---|---|
| core | main 0x400010, init 0x4007c0 (every step), the main loop 0x400cb4, the script 0x400b3c and its table 0x4222e4 (list every entry with what its routine does), the retrace timer 0x40bd48..0x40bfdf and the callback 0x40070c exactly (incl. the first refreshes after the music starts, the reentrancy guards [0x42278c] / [0x422798] / [0x4248c0], what [0x4248c4] is for), the palette 0x400364..0x400612 (exact float arithmetic, clamping, rounding), the screen 0x40a83c / 0x400694 / 0x400b14, the exit 0x400a10.., the data file 0x4000e8 / 0x4001c4 / 0x40022c / 0x400614, memory 0x40b10c / 0x40b4d8 / 0x40b5c4 / 0x40b6fc / 0x40b784 / 0x40b7a4, rand, the music start 0x4010b4 / 0x400efc / 0x401010 (the calls into DSMI by what they do; the music scope decompiles DSMI itself). Then **the clocks the remake needs**: from `full.ticks`, the exact relation between program time, refreshes, the frame clock, part ticks, the timer interrupts and the music position (when each script entry ran, in frames; how late after the music position was reached), and how the remake should time a part (origin of its tick counter) | notes-core.md |
| music | DSMI as the demo uses it: the AMF loader 0x4103d0 (EYE.AMF's format: header, orders, tracks, instruments, samples; the song: orders, speed, tempo, effects used), AMP 2.00 (0x40c788..: ampPlayModule 0x40dbb9, the poll 0x40d878, the player tick 0x40d7c0, every effect, the position getters 0x40de09.., the tick period 0x40dfab), the MCP / CDI layer it calls, the timer service 0x40f5a1..0x40f6cc (DSMI time), and the GUS driver SDIGUS.DSD as loaded (find it in a snapshot; its sample upload (DMA, `full.dram`), voice allocation, how it turns MCP calls into GF1 register writes: frequency, volume (ramps), panning, sample start / loop / end). Deliver a Python player that reproduces `full.gus` (order of writes and values, stamped by timer interrupt) and the order / row columns of `full.ticks` from EYE.AMF, a WAV render through a GF1 model (`clone/re/models/music/`, `jizz/re/models/music/`), and the exact time of every player tick in refreshes / timer interrupts. Compare the GUS handling with the clone and JIZZ remakes' `gf1.*` (`clone/remake/src/gf1.h`) so the remake can reuse their GF1 synthesiser | notes-music.md |
| engine | SurRender P5 1.02 (0x410520..~0x417f00) as the parts use it: the scene / object / camera / light / material / bitmap structures and every API routine the parts call (0x410520, 0x4105e4, 0x410618, 0x410850, 0x410868, 0x410878, 0x4108a8, 0x4108dc, 0x410bb0, 0x410f34, 0x411154, 0x41172c, 0x411c80, 0x413510, 0x4145f4, 0x415e50, ..), the `.sro` object format (vertices, faces, normals, mapping coordinates, materials), the `.lws` LightWave scene reader (flower/tunnel1.lws: keyframes, splines, how a frame number is evaluated), transforms (matrix layout, Euler order), projection (field of view, centre, near plane, aspect), clipping, sorting / z-buffer, back-face culling, lighting (flat, Gouraud, environment mapping, how the light level indexes a `.shd` table), every rasterizer the parts use (flat, Gouraud, textured, perspective or affine, env-mapped, transparent / `.int` blends) with its sub-pixel conventions. A Python model that reproduces a 3D frame of the capture (e.g. the logo's object at f1446..f2147) | notes-engine.md |
| intro + logo | intro: 0x403770, 0x4038c8, 0x403214, 0x403748, 0x4031a0, 0x403164 (light drops over intro/intro.raw); logo: 0x402f9c, 0x402ca0, 0x402d0c, 0x402e30, 0x402f1c, 0x402f10 (twlogo, the INSIDE text, obuke.sro, the mask) | notes-intro-logo.md |
| credits | 0x406d10, 0x40684c, 0x406c70, 0x4073c8, 0x405f30, 0x406794, 0x405f1c, 0x405f10, 0x4073e8 (tunnel, spike ball, smoke, the name pictures, the water mask) | notes-credits.md |
| sprites + shadow scene | 0x404fe8, 0x40506c, 0x405110, 0x4051d4, 0x405298, 0x4057bc, 0x404738, 0x4047a0, 0x404680, 0x404728, 0x405ba0, 0x405f00, 0x40585c, 0x40571c and the routines they call (the four pictures with their sprites and shadows, the shadow map, warp grid, cube) | notes-sprites.md |
| flower | 0x409730, 0x408030.., 0x4098c4, 0x40940c, 0x4095f8, 0x409358, 0x4093f0, 0x409e90 (the tunnel with its LightWave camera, the flower morph, the pictures) | notes-flower.md |
| flight | 0x402948, 0x401330, 0x40136c, 0x401450, 0x4018d0, 0x4023d4, 0x402c28, 0x4021f8, 0x4028e4, 0x402c7c (the landscape, the ship, the sun and lens flares, the river) | notes-flight.md |
| water | 0x407dec, 0x4079f4, 0x407f90, 0x407c78 and the rest of 0x4074xx..0x408000 (the water surface, the fish, the environment map) | notes-water.md |
| twirl | 0x409ea0, 0x40a29c, 0x40a408, 0x40a678, 0x40a098, 0x40a738, 0x409e90 (the twirl of flower/jeesus1.raw, its tables) | notes-twirl.md |
| temple | 0x404464, 0x403900, 0x4039c8, 0x40461c, 0x40407c, 0x404424, 0x404668 (the temple scene, the flare, the pipes) | notes-temple.md |

The part scopes call into the engine and the core helpers: describe those calls by what goes in and what comes out
(the engine scope describes the engine). Where the routine lists above are incomplete, follow the calls: everything
a part's routines call that is not shared belongs to the part.

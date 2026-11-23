# Reverse-engineering REVE.EXE — shared brief for decompiling one scope

## Goal

*Reve* (Pulse, The Party 1995, 5th in the PC demo competition; this is the final version of May 1996; code LCA,
graphics Antony and AJT, music Shad; LCA's own extender FLAT OS and LCA's MOD-Player V3.03) will be remade in C++20 /
SDL3 / OpenGL 3.3, like the other remakes in this repository (Inside, `inside/`, is the latest). The remake renders at
any resolution in true colour: "the same as the original, but much sharper". For that it needs the exact algorithms.
Your job: decompile the routines in your scope into readable pseudo-C with every constant, so that someone who has not
seen the assembly can port them. You are read-only on everything except your own notes file and your own models
directory.

## Read first

- `reve/re/NOTES.md`: the file layout, the archive and its files, the extender, the program flow, **the script
  table**, **the clock** (the retrace-locked timer: one interrupt per screen refresh, the retrace routine; the frame
  wait: at most one drawn frame per refresh), the music, the headless run.
- `reve/re/PARTS.md`: the timeline (script entries, frames, music positions, frame rates, what is on screen).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so for
  every static table, picture, palette, mesh, string or parameter say where it is (address in the image, or the
  archive file and offset, size, format) so the remake can read it at run time.
- For the style of the result, look at `inside/re/notes-temple.md`, `clone/re/notes-parts3d.md` or
  `paper/re/notes-planes.md`.

## Files and tools (all in `reve/re`, run inside the toolbox container)

- `unpacked/reve_img.bin`: the demo's image as it runs in the headless run, at 0x20427c. **Addresses are linear
  addresses** = the program's pointers = addresses in the emulator and in snapshots. Buffers the demo allocates
  (int 0FFh AX=0101h) are outside the image: read them from snapshots. `unpacked/files/<path>`: every file of the
  archive, unpacked (`GFX\TEXTURE.RAW` is `unpacked/files/GFX/TEXTURE.RAW`).
- `rep.py` (capstone):
  ```
  cd reve/re
  toolbox run -c acme303 python3 rep.py func 0x204770     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x204770 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x25a1fb    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x205e80     # code/data containing this 32-bit value
  toolbox run -c acme303 python3 rep.py funcs | str | ints | ports
  ```
  In Python (`import rep`): `rep.rd(a, n)`, `rep.rd16/rd32/rdf/rdd(a)`, `rep.cstr(a)`, `rep.dis`, `rep.func`.
- **The code is hand-written assembler** (flat 32-bit, no C runtime). Watch for self-modifying code (immediates and
  jump targets patched at run time, e.g. the timer count at 0x20453d, the addresses patched by 0x25a1bc between
  0x25a7dd and 0x25eae9) and **code generated at run time** (unrolled loops written into buffers: read them from
  snapshots, say what generates them). There is no floating point unless your scope uses the FPU: say so if it does.
- Shared things (NOTES.md): the picture loader 0x25a1fb (ESI name, EDI destination, EAX = 1: also the palette, to
  the DAC and to 0x20613c as 6-bit values); the five 64 KB buffers [0x205e80], [0x205e84], [0x205e88], [0x205e8c],
  [0x21b140] and [0x205e90] (256 bytes), [0x205e94]; the current palette at 0x20613c; the frame wait 0x2045d0; the
  retrace counter [0x205e98]; the script entry's routines; the music position 0x2602eb.
- **Say whether your part overwrites every pixel of the frame**, which buffer it draws into, how the frame reaches
  0xa0000 (a copy of a buffer, drawn directly) and what is shown otherwise.
- Dynamic analysis: `run_reve.py` runs the whole demo headless (see its docstring and NOTES.md). Snapshots are in
  `reve/re/snaps/f<frame>.snap` every 700 refreshes (pickle: `'mem'` = 16 MB from linear 0, `'irqstacks'`, `'ctx'`,
  `'st'`, `'gus'`, `'dma'`, `'pic1'`, `'pic2'`, `'pc'`). Unpickling needs unicorn imported, so run inside the toolbox.
  Read generated tables, textures, meshes, the loaded module and generated code straight from `snap['mem']`. To run
  on from a snapshot with your own hooks, COPY `run_reve.py` into your models directory and edit the copy (never the
  shared one; a copy below `reve/re` finds `../original` by itself only if it stays two levels below `reve/`, so set
  ORIG in your copy if needed):
  ```
  toolbox run -c acme303 python3 <models>/run_reve.py --load --snap snaps/f7000.snap --out <models>/x --frames 7600
  ```
  (`--frames` is the absolute frame to stop at.) The emulator runs about 15 refreshes per second; a global code hook
  (one Python call per instruction) is far slower, so hook only the addresses you need (`mu.hook_add(UC_HOOK_CODE,
  fn, None, addr, addr)`). Do not start runs of the whole demo unless you really need one.
- Reference capture: `full.frames` (24-byte header `REVEFRM1`, then per screen refresh (70.086 Hz) the 64,000 bytes of
  VGA memory and the 768-byte DAC palette; record k = refresh k, taken at the start of the vertical retrace),
  `full.ticks` (per refresh: frame, time, PIT interrupts, GUS timer interrupts, music order, row, the main-line PC),
  `full.gus` (GUS writes stamped with the GUS timer interrupt count), `full.dram`, `full.log` (file loads with their
  buffer addresses, services). In Python: `from sheet import open_frames, rgb, indexed, read_ticks`.
  `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> f1 f2 ...` (or `--every 2 --from-time 40 --to-time
  60`) makes a contact sheet (then Read it).
- Do NOT modify anything in `reve/re` except your own notes file and your models directory. Scratch files and your
  Python models go in `reve/re/models/<scope>/` (keep the models: they are the record of how a finding was checked; add
  a short README.md there listing them). Build nothing in the shared folders.

## Time bases (state one for every animated variable: this decides how it is ported)

- **Refresh** (70.086 Hz): the part's retrace routine (script entry +8, called from the timer interrupt at each
  retrace) runs once per refresh. State it advances is stepped per refresh: give the recurrence and, where it is
  linear, the closed form in refreshes since the part began.
- **Per drawn frame**: the frame routine (script entry +0xc) runs once per frame the main loop draws, at most once per
  refresh (the frame wait 0x2045d0); slower frames make the demo skip refreshes. Anything that changes once per drawn
  frame without reading a clock is frame-rate dependent: name it, say what it looks like, and give the frame rate the
  capture has there (from `full.ticks`: count the refreshes between frames, or hook the frame routine).
- **Music position**: script entries end at a position; parts may read the position (0x2602eb) themselves.
- **Init**: what the entry's init routine (script entry +0) precomputes.
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Colours: 8 bits through a palette

Everything is drawn in 256 colours. The remake draws true colour: a pixel's colour is the palette entry of its index,
as the DAC shows it (6-bit value v → v / 63). So for every drawn pixel say how its index is made: straight from a
texture or picture, through a shade table (`GOURAUD.PAL`: which axis is the colour and which the light level, how the
level is computed), a blend table (`ANTIALIA.PAL`, `SPIRBLUR.PAL`, tables computed from the palette like 0x25a275:
what the two inputs are), index arithmetic, palette ranges. The remake will evaluate the same mapping with continuous
inputs (filtered textures, continuous light levels, true blends). Say which palette is active, where it comes from
and which palette effects the part runs (fades, flashes, cycling: per refresh or per frame).

## Pictures

The remake shows faithful high-resolution redraws of hand-drawn pictures by default and the original pixels on request
(`AGENTS.md`, "Redrawn pictures"); generated textures, maps and anything an effect reads through palette indices stay
computed from the original. So for every picture or texture your scope uses, say:
- its file, size, palette, and how it is used: where on screen, at what size, transparency (which index is not
  drawn), whether an effect reads it through palette indices or tables (then a redraw would have to keep its indices:
  say so), whether it is mapped on 3D geometry;
- for text pictures (the credits): the exact text and the look of the letters.

## Output: `reve/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its refreshes / music positions (PARTS.md);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed and how the pixel's index is made, and the formulas for every generated table, texture or
   mesh. Procedural generators must be described precisely enough to regenerate them at a higher resolution. Say
   which data is static (address, or archive file and offset, size, format);
3. the time base of every animated variable (above);
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (low-resolution grids and
   interpolation, lookup tables, integer steps, nearest-neighbour texture reads, palette quantisation, 8-bit blends),
   with what they approximate;
5. whether the part overwrites the whole frame;
6. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured frame
(or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

PARTS.md has the scope of every script entry. The table there names the routines per scope.

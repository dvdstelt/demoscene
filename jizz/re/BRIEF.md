# Reverse-engineering JIZZ.EXE — shared brief for decompiling one scope

## Goal

*JIZZ* (The Black Lotus, Wired '97 64k intro, MS-DOS; code and design Jace, music Probe, vector engine and blobs
Nix, VESA code Ballein) will be remade in C++20 / SDL3 / OpenGL 3.3, like the Paper remake in `paper/remake`. The
remake renders at any resolution in true colour: "the same as the original, but much sharper". For that it needs the
exact algorithms. Your job: decompile the routines in your scope into readable pseudo-C with every constant, so
that someone who has not seen the assembly can port them. You are read-only on everything except your own notes
file and your own scratch directory.

## Read first

- `jizz/re/NOTES.md`: file layout, DOS32, VESA (320x200, 16-bit 5:6:5 LFB, 4 pages), MXMPlay and the timer,
  program flow, the sync table, the frame-rate dependent state, how the headless run works.
- `jizz/re/PARTS.md`: the part timeline (ticks, music time, order:row, events, routines, fps, what is on screen).
- `AGENTS.md` (repository root): the rules. In particular nothing from the original may be copied into the remake's
  source, so for every static table, picture, script or string say where it is (address, size, format) so the
  remake can read it from JIZZ.EXE at run time.
- For the style of the result, look at one of Paper's notes, e.g. `paper/re/notes-planes.md`.

## Files and tools (all in `jizz/re`, run inside the toolbox container)

- `jizz_img.bin`: the unpacked program image, relocated for base 0 (program 0x2a617 bytes + zeroed BSS up to
  0x33000). Addresses in notes are offsets in this image = near pointers = linear addresses in the emulator.
- `rep.py` (capstone):
  ```
  cd jizz/re
  toolbox run -c acme303 python3 rep.py func 0x12013     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x12013 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x13d73    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x6413      # instructions containing this immediate/displacement
  toolbox run -c acme303 python3 rep.py funcs | str | ints
  ```
  In Python (`import rep`): `rep.SEG`, `rep.rd32/rd16/rdf/rdd(addr)`, `rep.cstr(addr)`, `rep.dis`, `rep.func`.
  The listing annotates float constants (`; =1000`). The code is hand-written 32-bit assembly (no C runtime, no
  fixed calling convention: read each routine's register use). All x87 maths runs in **24-bit single precision**
  (main sets the precision control to 0 at 0x18712); exact models must round like that (numpy float32 is close).
  Some calls go through registers or tables (`call [x]`, `call eax`): find their targets (a snapshot or a hook
  shows the values).
- Dynamic analysis: `run_jizz.py` runs the intro headless in unicorn (see its docstring and NOTES.md).
  Snapshots (pickle: `'mem'` = 32 MB from linear 0, `'lfb'` = 2 MB video memory, `'ctx'`, `'st'`, `'gus'`, ...)
  are in `jizz/re/snaps/t<tick>.snap` at ticks 430 (precalc done, before the mode set), 440, 1365, 2515, 3285,
  4050, 4435, 5205, 5970, 7506, 9045 (a few ticks into each part). Unpickling needs unicorn imported, so run inside
  the toolbox. Read generated tables, textures and objects straight from `snap['mem']`. To run on from a snapshot
  with your own hooks, COPY `run_jizz.py` into your scratch directory and edit the copy (never the shared one):
  ```
  toolbox run -c acme303 python3 <scratch>/run_jizz.py --load --snap snaps/t1365.snap --out <scratch>/x --ticks 1500
  ```
  It needs `rep.py` and `jizz_img.bin` (add `jizz/re` to `sys.path`). `--freeze-timer` keeps xmpGetTimer constant;
  `--part-budget ADDR=N` changes the CPU speed inside one part. A run of a few hundred ticks takes about a minute.
  Do not start runs of the whole intro (about 10 minutes) unless you really need one.
- Reference capture: `full.frames` (32-byte header, then per timer tick the visible page: 320x200 16-bit
  little-endian 5:6:5 pixels, 128000 bytes per record; record t = what was on screen at the end of tick t);
  `full.ticks` (per tick: tick, PIT count, time, order, row, event, sync index, part, flips, main EIP);
  `full.flips` (per flip: number, tick, time, first xmpGetTimer value of that frame, part). Ticks are 60.39 Hz
  player ticks from tick 434 (= music time 0) on. `jizz_reference.mp4` is the capture as a 60 fps video from the
  music start. `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> t1 t2 ...` makes a contact sheet
  (then Read it).
- `dump/*.tga`: the 17 textures as the intro's `/pandora` mode writes them (256x256 24-bit); `dump/smokeout.mxm`:
  the expanded music module (the `/mp4` mode).
- Do NOT modify anything in `jizz/re` except your own notes file. Scratch files and your Python models go in
  `jizz/re/models/<scope>/` (keep the models: they are the record of how a finding was checked; add a short
  README.md there listing them).

## Time bases (state one for every animated variable: this decides how it is ported)

- **The timer**: xmpGetTimer (0x29f44) = PIT clocks (1,193,182 Hz) since the music started, interpolated inside
  the player tick. Parts keep a start value in [0x6542]. Almost everything that moves uses it. Give formulas as
  functions of it (and say which start value they subtract).
- **The sync event** [0x6413]: set from the music position (order:row → event, table at 0x6417) at the start of
  each frame in 0x638b; parts react to it and some rewrite it.
- **Per drawn frame**: anything advanced in the main loop without the timer. Known: the TBL part's fades and colour
  pulse, the flash decay in 0x638b. If you find more, say so and give the frame rate the capture shows (full.flips).
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Output: `jizz/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its music time / order:row start and end;
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed, how colours are built (the intermediate pixel formats, the colour tables, how the
   16-bit pixel is made) and the formulas for every generated table, texture or object. Procedural generators must
   be described precisely enough to regenerate them at a higher resolution. Say which data is static in the exe
   instead (address, size, format);
3. the time base of every animated variable;
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (block interpolation, lookup
   tables, integer steps, nearest-neighbour reads, low-resolution buffers that are scaled up), with what they
   approximate;
5. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured
frame (or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines | notes file |
|---|---|---|
| core | main's set-up and buffer layout (0x186bd..0x18bdf: what each buffer pointer is), precalc tables 0x6592, 0x6b16, 0x6cdd, 0x10fa2, 0x11dfe, 0x16f4f; random generator 0x141f6; the frame loop 0x638b / 0x651f / 0x5e44 (16-bit path only); colour tables 0x1f5b8, 0x1a229, 0x1a236, 0x1a336 and the flash; timer fades 0x17979 / 0x17999 / 0x179f6; the shared 2D helpers 0x17737, 0x17727, 0x13d73, 0x13eec, 0x13f84, 0x1131b, 0x11358, 0x11396, 0x1145c, 0x1122f, 0x1151c, 0x11566, 0x11eb2, 0x12111, 0x11f73, 0x183a9: what each draws, its pixel formats | notes-core.md |
| textures | "decrunching gfx": 0x12c50, 0x12e72 and everything they call; the 17 texture scripts (where, format); which buffer each texture ends up in and which parts use it; JIZZ.DOC's "JacePeG" cruncher if it is used. A Python model that reproduces `dump/*.tga` byte for byte | notes-textures.md |
| music | the module decruncher 0x1632b (Python model that reproduces `dump/smokeout.mxm`); the MXM format; MXMPlay 0x296dc..0x2a316 complete (xmpInit, upload, xmpPlay, the IRQ0 handler, the player tick, effects, tables, xmpGetTimer, xmpGetPos); a Python player that produces the same GUS register stream as `full.gus` (mind that the fake GUS answers volume reads with the last written value) and renders a WAV through a GF1 model (`paper/re/player_ref.py`, `paper/re/gf1synth.py` and `paper/re/notes-music.md` did the same for Paper) | notes-music.md |
| engine | Nix's vector engine as the parts use it: 0x15bb0, 0x15bef, 0x15c40, 0x15401, 0x1545d, 0x154bc, 0x159fb, 0x15a59, 0x14394, 0x14567, 0x18dc4, 0x18e3b, 0x18ea1, 0x19022, 0x19118, 0x19240, 0x1966e, 0x19f78, 0x1f5c5, 0x1f5d1 and what they call: object data structures, transforms, projection, sorting / depth, polygon filling, environment mapping and shading, how a pixel is written. Generic object generators if they are shared. Treat the parts' own set-up as the part scopes' job | notes-engine.md |
| parts2d | 0x12013 "TBL" (0x11986, 0x11e33, 0x13f98; its frame-rate dependent fades), 0x1212a "jizz" logo (0x1239b, 0x123df, 0x16dc8), 0x177bf radial zoom (0x16d43), 0x17c8a field lines (0x17b2f), the end picture loop 0x18c25 | notes-parts2d.md |
| parts3d-a | 0x17003 ribbons / mirror planes / rings / insets (0x174e5, 0x17553, 0x1770b, 0x17762, 0x18009, 0x180e5, 0x18181, 0x181bc) and 0x169e7 tunnel / spinning rectangles (0x16cd3, 0x18228, 0x182e1): objects built, camera, backgrounds, insets, sync reactions | notes-parts3d-a.md |
| parts3d-b | 0x15c8b tube, 0x15d20 spiky object over clouds, 0x10fda metaballs (0x769f, 0x7aae and the blob surface code): objects, camera, backgrounds, sync reactions | notes-parts3d-b.md |

The part scopes call into the engine and the core helpers: describe those calls by what goes in and what comes out
(objects, matrices, parameters) and leave their internals to the engine and core scopes, unless you need them to
explain your part. If you find a shared routine that no scope covers, describe it in your notes and say so in your
final message.

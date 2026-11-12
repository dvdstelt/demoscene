# Reverse-engineering STARS.EXE — shared brief for decompiling one scope

## Goal

*STARS: Wonders of the World* (NoooN, 1995, MS-DOS; music Groo, 3D motion and vertices BARTi, system and special
effects KARL, script and pixels RA, sound LCA) will be remade in C++20 / SDL3 / OpenGL 3.3, like the Paper and JIZZ
remakes. The remake renders at any resolution in true colour: "the same as the original, but much sharper". For that
it needs the exact algorithms. Your job: decompile the routines in your scope into readable pseudo-C with every
constant, so that someone who has not seen the assembly can port them. You are read-only on everything except your
own notes file and your own scratch / models directory.

## Read first

- `stars/re/NOTES.md`: file layout, the loader chain and the overlay (58 files, DIET), PMODE and the services, ports,
  the three clocks (PIT callbacks, vertical retrace, music), LCA's player, how the headless run works.
- `stars/re/PARTS.md`: the timeline (frames, seconds, music order:row, the routines main calls, the PIT callback of
  each part, frame rates, what is on screen, what depends on the frame rate).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so for
  every static table, picture, palette, object, script or string say where it is (image address, or overlay file
  number and offset, size, format) so the remake can read it from STARS.EXE at run time.
- For the style of the result, look at `jizz/re/notes-parts3d-a.md` or `paper/re/notes-planes.md`.

## Files and tools (all in `stars/re`, run inside the toolbox container)

- `unpacked/stars_img.bin`: the unpacked program image (0x840b0 bytes). Addresses in notes are offsets in this
  image = the 32-bit program's near pointers = linear addresses in the emulator (it loads the image at 0).
  The 16-bit PMODE kernel at 0x7a210.. is not your concern.
- `unpacked/files/NN.bin`: the overlay's 58 files, decompressed, numbered as the program loads them (0x4a9f3 with the
  number in AX). `unpacked/files.txt` lists them. In snapshots, a loaded file lives in the 2.9 MB heap at 0x100000..
  (the run log `full.log` prints "loaded file N -> address" for every load).
- `rep.py` (capstone):
  ```
  cd stars/re
  toolbox run -c acme303 python3 rep.py func 0x11e7f     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x11e7f 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x4a830    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x4ab06     # instructions containing this immediate/displacement
  toolbox run -c acme303 python3 rep.py funcs | str | ints
  ```
  In Python (`import rep`): `rep.SEG`, `rep.rd8/rd16/rd32/rdf/rdd(addr)`, `rep.cstr(addr)`, `rep.dis`, `rep.func`.
  The code is hand-written 32-bit assembly (no C runtime, no fixed calling convention: read each routine's register
  use). Calls through registers or memory (`call [x]`, `jmp [eax*4 + table]`) need a snapshot or a hook to resolve.
  The program does not use the FPU: all maths is integer / fixed point (exact models are plain integer code).
- Shared helpers (the core scope describes them; use them by what goes in and out): 0x4a830 wait for the vertical
  sync (start, then end); 0x4abec / 0x4abfe set / clear PIT callback 1 [0x4ab06] (called 70.064 times a second
  from 0x4ac1f); 0x4a9f3 load overlay file AX → EAX; 0x17bd7 allocate EDX bytes → EAX, 0x17c8e free EAX, 0x17cd2
  resize; 0x17da3 / 0x17e04 make a data / code selector for a block; 0x17e50 show picture file EAX (palette + 320x200)
  with a black palette, 0x17e96 / 0x17eba fade by EBX per retrace for ECX retraces; 0x4a742 set palette (AL first
  colour, ECX colours, ESI 6-bit RGB); 0x4a720 scale a palette; 0x4a84e clear the screen; 0x29f80 music position
  (AX = order << 8 | row); 0x4af10 random: `s = rol32((s ^ 0x78d47e5d) * 0xd413d473, 18)`, seed dword [0x4af35],
  returns s mod EAX. VGA is at 0xa0000 - [0x17620] (= 0xa0000 in the emulator).
- Dynamic analysis: `run_stars.py` runs the demo headless in unicorn (see its docstring and NOTES.md). Snapshots
  (pickle: `'mem'` = 8 MB from linear 0, `'ctx'`, `'st'`, `'gus'`, `'dma'`, `'pal'` = 6-bit DAC, `'eip'`) are in
  `stars/re/snaps/f<frame>_<routine>.snap`, taken at the first frame after main called `<routine>` (see PARTS.md for
  the list). Unpickling needs unicorn imported, so run inside the toolbox. Read generated tables, textures and objects
  straight from `snap['mem']`. To run on from a snapshot with your own hooks, COPY `run_stars.py` into your scratch
  directory and edit the copy (never the shared one):
  ```
  toolbox run -c acme303 python3 <scratch>/run_stars.py --load --snap snaps/f5947_11e7f.snap --out <scratch>/x --frames 6300
  ```
  (`--frames` is the absolute frame number to stop at.) It needs `unpacked/stars_img.bin` and `../original/STARS.EXE`;
  it finds `stars/re` by walking up from its own folder, so a copy anywhere below `stars/re` works. `--freeze-pit` stops the PIT callbacks (what still moves is per frame or music driven);
  `--part-ips ADDR=N` changes the CPU speed inside one part; `--trace a,b,..` prints the frame at which addresses first
  run. Emulation runs at roughly 1-4x real time. Do not start runs of the whole demo (about 12 minutes) unless you
  really need one.
- Reference capture: `full.frames` (32-byte header `STARFRM1`, then per displayed frame (70.086 Hz) 64000 bytes of
  VGA memory + 768 bytes of 6-bit palette; record f = what the monitor showed in frame f), `full.ticks` (per frame:
  frame, time in s at the frame's first line, PIT ticks, GUS player ticks, order, row, main routine, PIT callback
  [0x4ab06], frames drawn so far, main EIP), `full.gus` (GUS register writes per player tick), `full.dram` (GUS DRAM),
  `full.log`. `stars_reference.mp4` is the capture as a 60 fps video from the music start.
  `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> f1 f2 ...` makes a contact sheet (then Read it).
- Do NOT modify anything in `stars/re` except your own notes file and your models directory. Scratch files and your
  Python models go in `stars/re/models/<scope>/` (keep the models: they are the record of how a finding was checked;
  add a short README.md there listing them).

## Time bases (state one for every animated variable: this decides how it is ported)

- **PIT ticks**: IRQ 0 at 1193182 / 0x4286 = 70.064 Hz; handler 0x4ac1f calls [0x4ab06] (the part's callback, see
  the PARTS table), [0x4ab0a] (debug monitor), [0x4ab0e] (no-sound music clock). Counters stepped there are time.
  Give formulas as functions of the tick count since the part installed its callback (and say where it starts).
- **Music position**: order [0x1c829] and row [0x1c828], stepped by the player on the GUS timer (40.064 ticks per
  second with speed 4 = 10.0 rows per second in the intro (orders 0..0x0a), then 46.64 ticks per second with speed 2
  = 23.3 rows per second, see NOTES.md "Music"). main and the parts compare AX = order << 8 | row
  (0x29f80) to start, end and switch phases. Give every position your part tests.
- **Vertical retrace**: 70.086 Hz. Anything stepped once per 0x4a830 call (fades, some animations) is per retrace.
  If the loop draws a frame per retrace wait, that is per drawn frame: see below.
- **Per drawn frame**: anything advanced in the part's main loop without the PIT or the music. The capture's frame
  rate (PARTS.md, 40 M instructions per second, about a 486DX2/66) is then part of the look: say so, and give the
  rate in the capture so the remake can pick a fixed step rate.
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Output: `stars/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its frames / time / music position (PARTS.md);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed, how the 8-bit colour indices and the palettes are built (the palette is part of the look:
   fades, palette rotations, shading ramps), and the formulas for every generated table, texture or object.
   Procedural generators must be described precisely enough to regenerate them at a higher resolution. Say which
   data is static in the exe or in an overlay file instead (address or file, offset, size, format);
3. the time base of every animated variable;
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (8x8-block interpolation, lookup
   tables, integer steps, nearest-neighbour reads, low-resolution buffers, 8-bit palette quantisation), with what
   they approximate;
5. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured frame
(or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines (PIT callback in brackets) | notes file |
|---|---|---|
| core | the system: entry 0x178c3, 0x31, 0x3c, memory manager 0x17b56 / 0x17d3d / 0x17bd7 / 0x17c43 / 0x17c8e / 0x17cce / 0x17cd2, selectors 0x17da3 / 0x17de6 / 0x17dfd / 0x17e04, buffer lists 0x4a870 / 0x4a883 / 0x4a894 / 0x4a8c7, exception handlers 0x17978.., IRQ handlers 0x4a610 / 0x4a620 (with their real-mode stubs), the PIT 0x4ab7d / 0x4abd4 / 0x4ac1f / 0x4abec..0x4ac14, DOS file I/O 0x4cfc8..0x4d0f1, file loading 0x4a98b / 0x4a9f3 / 0x4aa18 / 0x4aa3a / 0x4ac8b / 0x4ad47, random 0x4af10; the screen helpers 0x4a720 / 0x4a742 / 0x4a759 / 0x4a76c / 0x4a7b0 / 0x4a80b / 0x4a830 / 0x4a843 / 0x4a84e, 0x17e50 / 0x17e96 / 0x17eba (fade formulas exactly); the script: main 0x4d complete (every call, wait, picture and fade; picture 57 and the credits; the end 0x1c77e / 0x4a006 / 0x7dd), the no-sound clock 0x49ff4 / 0x4a015, the minute variant, the MDA display 0x939 / 0x83e / 0x7dd (briefly). Also: check NOTES.md's description of the loader chain and DIET against the code and `unpack.py` so a C++ unpacker can be written from it (add what is missing), and catalogue the 58 overlay files (who loads each, kind, layout as far as the core sees it, which scope describes it) | notes-core.md |
| music | LCA's MOD-Player V3.02, GUS path complete: detection 0x1ba6c / 0x1bb0a.. (ULTRASND= parsing, string at 0x29b90; the hard detection), 0x1c71b → 0x1a1b0 (module conversion, the internal pattern format, sample upload 0x1a699 / 0x1a76f / 0x1a7a6 by DMA, IRQ 0x1a8e2), 0x1c759 → 0x1a91c (start, GUS timers), the tick 0x1abb0 with the effect tables 0x1ac04 / 0x1b298, frequency / volume tables (0x28ac4.., 0x28c48..), 0x1a9a2 / 0x1a9ec / 0x1a5f7 / 0x1a66c, 0x1c77e, 0x1c7a3 / 0x29f80, 0x4a006 / 0x4ac14; the module format of file 1 (16 channels, 256-entry order table, `16CH` at 0x4b8); how the tempo becomes the timer 2 count. A Python player that reproduces `full.gus` (register writes per player tick) and `full.dram`, and renders a WAV through a GF1 model (see `paper/re/player_ref.py`, `paper/re/gf1synth.py`, `jizz/re/notes-music.md`). The SB path (0x182d5, 0x1867c..) only listed | notes-music.md |
| engine | BARTi's 3D engine 0x7871c..0x7a210 as the parts use it: 0x7871c, 0x7873a, 0x787fc, 0x788dd, 0x78905, 0x78948, 0x7898d, 0x789c4, 0x78c62, 0x78d26, 0x78ffe, 0x79111, 0x7915b, 0x7916b, 0x79483, 0x79801, 0x79808, 0x79841, 0x7984e, 0x79cbf and what they call: the object formats (`B!` files 18, 24, 45, 50, 51; `WLDD` file 28), object / scene structures (0x55836.., 0x558cc, 0x55918 ...), transforms, projection, visibility and sorting, polygon filling, shading and environment / texture mapping, how a pixel is written, the engine's tables. The parts' own set-up and motion belong to the part scopes | notes-engine.md |
| parts3d-a | 0x16945 (precalc at start: file 50 objects for 0x164dc, files 49 / 50), the cloud 0x1241b / 0x126ae / 0x123d8 (0x1278e), the bee 0x11d8e / 0x11e7f / 0x11f48 (0x12136; 0x11fa1, 0x120c4, 0x121b4, 0x122d7), the spike ball 0x65ef / 0x6534 / 0x65ad (0x6905; 0x6791, 0x6861, 0x6888, 0x696b): objects and how they are prepared, camera and object motion, backgrounds, palettes, music syncs | notes-parts3d-a.md |
| parts3d-b | the textured surfaces 0xa14d / 0xa0ca / 0xa346 (0xa4ac; 0xa3c9, 0xa491, 0xa530, 0xa57d, 0xa5c5, 0xa656, 0xa71b, 0xa729), the chrome star 0x164dc (0x16646; 0x16892, 0x168a3, 0x16acc), the torso and ring 0x16d86 / 0x16f7c / 0x174bd (0x17513; 0x16f46, 0x17084 .. 0x175ef): same as parts3d-a | notes-parts3d-b.md |
| parts2d-a | the intro 0x5b84 / 0x5a92 / 0x5b8f (0x5b91; 0x5bb0, 0x5c32..0x5c63, 0x5c97, 0x5e2d, 0x5e5b, 0x5e7a, 0x5ece, 0x5fa4, 0x6027, 0x60e0, 0x6101 .. 0x62bb: the name cards with the colour-cycling flower, files 5, 6, 7), the picture warp 0x9d17 / 0x975b / 0x9dcf (0x9932; 0x982c .. 0x9a62, 0x9d95), the tunnel 0x1064 / 0x11e8 / 0x110d (0xf85; 0x136a, 0x1158, 0x11ce, 0x1261, 0x17ed0; maps 2, 3, palette 4) | notes-parts2d-a.md |
| parts2d-b | the particles 0x13388 / 0x133ad / 0x133f1 / 0x1339d (0x1350e, 0x134e3; 0x13435, 0x1348e, 0x13579, 0x135c5, 0x138be, 0x138ff, 0x13922, 0x13a1b; text file 58), the dissolving picture 0x1528c / 0x1535c / 0x1530a (0x1544d; 0x1549b .. 0x1563d and 0x79fad, 0x7a04c, 0x7a061, 0x7a0dd, 0x7a120; files 36..41), the face 0x10b26 / 0x1089c / 0x10bd9 (0x10ae2; 0x10a86, 0x10bf3, 0x11776 .. 0x11acd; files 34, 35). These are the parts that step per frame: say exactly what does | notes-parts2d-b.md |

The part scopes call into the 3D engine and the core helpers: describe those calls by what goes in and what comes
out (objects, matrices, parameters, buffers) and leave their internals to the engine and core scopes, unless you
need them to explain your part. If you find a shared routine that no scope covers, describe it in your notes and say
so in your final message.

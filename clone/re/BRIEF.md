# Reverse-engineering CLONE.EXE — shared brief for decompiling one scope

## Goal

*clone meets clone* (Acme & Fudge, Takeover '98 64k intro, MS-DOS; design Aap and Echo, effects Echo, music Simon,
graphics Aap; texture mapper and grid filler Craft, radial blur originally Dune, VESA lib Submissive, MXMPlay
Pascal, PMODE/W Daredevil and Tran) will be remade in C++20 / SDL3 / OpenGL 3.3, like the JIZZ, STARS and DOPE
remakes. The remake renders at any resolution in true colour: "the same as the original, but much sharper". For
that it needs the exact algorithms. Your job: decompile the routines in your scope into readable pseudo-C with every
constant, so that someone who has not seen the assembly can port them. You are read-only on everything except your
own notes file and your own models directory.

## Read first

- `clone/re/NOTES.md`: file layout (PMODE/W, the "veloader" and its packed program), the runtime model, services,
  ports, video (32-bit pages and the 15-bit path), MXMPlay and the timer, main's flow, **the time base (dt)**, the
  sync table and its events, the pictures and textures, how the headless run works.
- `clone/re/PARTS.md`: the timeline (ticks, music time, order:row, events, routines, frame rate in the capture, what
  is on screen, what depends on the frame rate).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so
  for every static table, picture, palette, mesh, module part or string say where it is (address, size, format)
  so the remake can read it from CLONE.EXE at run time.
- For the style of the result, look at `jizz/re/notes-parts3d-a.md` or `paper/re/notes-planes.md`.

## Files and tools (all in `clone/re`, run inside the toolbox container)

- `unpacked/clone_img.bin`: the unpacked program, relocated for linear base 0x400000 (0x2846c bytes; the BSS, zero,
  follows up to 0xf95937). **Addresses are linear addresses** = the program's near pointers = addresses in the
  emulator and in snapshots; address A is at file offset A − 0x400000.
- `rep.py` (capstone):
  ```
  cd clone/re
  toolbox run -c acme303 python3 rep.py func 0x403530     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py dis 0x403530 40   # linear listing
  toolbox run -c acme303 python3 rep.py calls 0x402970    # call sites
  toolbox run -c acme303 python3 rep.py xref 0x420489     # code/data containing this 32-bit value
  toolbox run -c acme303 python3 rep.py funcs | str | ints | ports
  ```
  In Python (`import rep`): `rep.rd(a, n)`, `rep.rd16/rd32/rdf/rdd(a)`, `rep.cstr(a)`, `rep.dis`, `rep.func`.
  The listing annotates float / double constants (`; =0.005`) and BSS addresses (`; bss`).
- **The code is Watcom C** (register calling convention: arguments in EAX, EDX, EBX, ECX, then on the stack, the
  callee pops them with `ret n`; floats are passed on the stack as dwords) plus hand-written assembler (the player,
  the fillers, the blits: read their register use). The FPU runs with **rounding = truncate** and 64-bit precision
  (control word 0x1f7f, set at 0x4010f6); 0x4010dd, called before every `fistp`, is an empty `ret`. So every
  float → int conversion truncates toward zero; exact models must do the same. `fsin` / `fcos` of the emulator
  are computed in double precision on the host, so captured tables can differ from a real Pentium in the last
  bits.
- Shared helpers (the core scope describes them; use them by what goes in and out): 0x40100c allocate EAX bytes →
  ESI (heap; all allocations happen in the precalc, addresses in NOTES.md); 0x4010cc memset (EAX = dest, EDX =
  byte, EBX = count); 0x4043be fill a rectangle with a dword colour; 0x404002 blit a picture {pixels, w, h} at
  (EDX, EBX) to ECX, mode 0 = skip 0xffffffff pixels, mode 1 = blend with a level 0..63; 0x4045b4 per-frame
  feedback blend of two 8-bit buffers (dst = (15 dst + src) / 16); 0x404a08 expand an 8×8 block of an 8-bit
  buffer through a 256-dword palette; 0x4167b7 / 0x4034e7 float → byte through a log curve; the matrix / vector
  routines 0x404c9f (identity), 0x404cd6, 0x404d5c / 0x404e36 / 0x404f0f (rotations), 0x404feb, 0x4050e9 (the
  engine scope describes them).
- The draw target of a frame is [0xe350ec] (32-bit 0x00RRGGBB, 320 × 200, 1280 bytes per line). In the 32-bit mode
  it is the video page shown four frames earlier, in the 15-bit mode a buffer that the conversion clears: **say
  whether your part overwrites every pixel of the frame**, and what it would show otherwise.
- Dynamic analysis: `run_clone.py` runs the intro headless in unicorn (see its docstring and NOTES.md). Snapshots
  (pickle: `'mem'` = 32 MB from linear 0, `'lfb'` = 2 MB video memory, `'ctx'`, `'st'`, `'gus'`, `'eip'`) are in
  `clone/re/snaps/t<tick>.snap`, taken at the first timer tick after each part started (PARTS.md lists them;
  t11 = the start of the frame loop, all precalc done). Unpickling needs unicorn imported, so run inside the
  toolbox. Read generated tables, textures, meshes and the module straight from `snap['mem']`. To run on from a
  snapshot with your own hooks, COPY `run_clone.py` into your models directory and edit the copy (never the shared
  one; a copy anywhere below `clone/re` finds `unpacked/` by itself):
  ```
  toolbox run -c acme303 python3 <models>/run_clone.py --load --snap snaps/t6156.snap --out <models>/x --ticks 6400
  ```
  (`--ticks` is the absolute tick to stop at.) `--flip-frames` records every shown frame (light parts show hundreds
  per second); `--freeze-timer` keeps xmpGetTimer constant (dt = 0 after the first frame: what still moves is per
  frame or event driven); `--ips N` / `--part-ips PART=N` change the CPU speed (default 150 M instructions per
  second); `--trace a,b,..` prints the tick at which addresses first run. A run of a few hundred ticks takes about a
  minute. Do not start runs of the whole intro (about 17 minutes) unless you really need one.
- Reference capture: `full.frames` (32-byte header `CLONFRM1`, then per timer tick (53.99 Hz after the music
  starts) the picture on screen at the end of the tick, 320 × 200 RGB, 192,000 bytes; record t = tick t),
  `full.ticks` (per tick: tick, PIT count, time (s), order, row, player timer (PIT clocks), part, sync index,
  frames shown so far, main stage, main EIP), `full.flips` (per shown frame: number, tick, time, **the timer value
  the frame was drawn with**, t70, part, order, row), `full.gus` (GUS register writes per tick), `full.dram` (GUS
  memory). The frame on screen at tick t is flip number `full.ticks` column 9; its timer value is in `full.flips`.
  `clone_reference.mp4` is the capture as a 60 fps video from the music start.
  `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> t1 t2 ...` (or `--every 2 --from-time 40
  --to-time 60`) makes a contact sheet (then Read it).
- Do NOT modify anything in `clone/re` except your own notes file and your models directory. Scratch files and
  your Python models go in `clone/re/models/<scope>/` (keep the models: they are the record of how a finding was
  checked; add a short README.md there listing them).

## Time bases (state one for every animated variable: this decides how it is ported)

- **dt**: no part reads the timer. main computes t70 = xmpGetTimer × 70 / 1,193,182 (time in 1/70 s, double at
  0xe9572e) after every frame and dt = t70 − previous t70 (float at 0x420489); the parts advance their state by
  dt once per frame. For every animated variable give the recurrence exactly (`a += 0.005 * dt` at 0x403538), then
  - if it is linear in dt (a sum of dt times constants), give the closed form as a function of the time since the
    part began (and how the part's first frame starts it: see NOTES.md "Time base");
  - if not (damping like `x -= k x dt`, velocities that change, clamps, conditions on the value, per-frame feedback
    without dt), say so plainly and give the capture's frame rate (PARTS.md) and what the motion looks like, so the
    remake can integrate it at the original's step or find a closed form that matches.
- **Sync events**: main runs the event handlers of the sync table (0x420499) once per frame, before the part
  draws (NOTES.md lists them and the variables they set). Give every event variable your part reads and what it
  does with it.
- **Per drawn frame without dt**: anything that changes once per frame regardless of time (feedback blends,
  counters, buffer swaps). Name it; this is frame-rate dependent.
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Pictures

The remake shows faithful high-resolution redraws of hand-drawn pictures by default and the original pixels on
request (`AGENTS.md`, "Redrawn pictures"); generated textures stay computed. So for every picture, texture, palette
or bitmap font your scope uses, say:
- **hand-drawn or generated** (stored pixels in the exe = drawn by Aap, unless the code makes them; computed =
  generated, even if it uses stored parameters);
- where it is (address, size, format, palette) and how it is used: where on screen, at what size, blend or
  transparency (index 0 / 0xf0 → 0xffffffff), fades, whether an effect reads it through palette indices or as
  a texture (then it stays computed from the original, see AGENTS.md);
- for text bitmaps: the exact text and the font's look (the remake may set text in a matching font instead).
NOTES.md "Pictures and textures" lists the ten stored pictures; tell the core scope if you find more.

## Output: `clone/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its ticks / music time / order:row start and end (PARTS.md);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers and
   textures are addressed, how colours are built (8-bit intermediate buffers, palettes, the 32-bit pixel), and the
   formulas for every generated table, texture, mesh or palette. Procedural generators must be described precisely
   enough to regenerate them at a higher resolution. Say which data is static in the exe instead (address, size,
   format);
3. the time base of every animated variable (above);
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (8×8 grid interpolation,
   lookup tables, integer steps, nearest-neighbour reads, 8-bit buffers expanded through palettes, low-resolution
   buffers), with what they approximate;
5. whether the part overwrites the whole frame (32-bit pages vs 15-bit buffer, above);
6. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured
frame (or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines | notes file |
|---|---|---|
| core | entry 0x4010de; main 0x4001ab complete: precalc order, the event switch 0x400b53..0x400e7b with every handler, the part switch 0x400e80, the frame routine 0x400000, t70 / dt 0x400f76..0x400fbf (exactly, incl. the first frames and the float precision), the dead fade, the exit; Watcom runtime: heap 0x400ff4 / 0x40100c / 0x401083, 0x4010cc, int386x 0x40110c / 0x401125 / 0x40115c; Submissive's VESA lib 0x401235..0x401853 (mode search, LFB, 4F07) and the 15-bit conversion 0x4188d4; part 99 0x400079; the precalc tables 0x416806 (with 0x414f04), 0x403f10, 0x40347a, 0x4033ee; the shared 2D helpers 0x4043be, 0x404002, 0x4043ec, 0x40442c, 0x4045b4, 0x404a08, 0x4167b7, 0x4034e7. Also check NOTES.md's description of the veloader, the VE container, the decompressor and the fixups against the code and `unpack.py`, so a C++ unpacker can be written from it (add what is missing) | notes-core.md |
| gfx | everything the precalc makes that is a picture or texture: main 0x400240..0x400844 (maps [0xe35108], [0xe3510c], [0xe35110], 257 × 256 × 32 bit; colour tables 0xe6542b and 0xe35128, 256 × 257 RGB), 0x4033a7 (→ grid texture [0x4207c8] = 0xdf02ec), the grey palettes 0x4008d4..0x400946 (tables 0x41b7c2, 0x41fc1c) and 0x419d5b, the picture decoder 0x403f53 and the ten stored pictures (NOTES.md): classify each (above), where each is drawn by which part, how. A Python model that reproduces the maps and tables in `snaps/t11.snap` byte for byte (mind the truncating FPU) | notes-gfx.md |
| music | the module builder 0x414aee with the synthesisers 0x4149b4 and 0x414a38 (a Python model that reproduces the module buffer at 0x428470 in `snaps/t11.snap` and `full.dram`); the MXM module at 0x406862 (format, the song: orders, patterns, instruments, effects used); the player 0x4018d4..~0x402680 complete (xmpInit and upload, xmpPlay, the IRQ0 handler 0x4021ac, GUS update 0x4024ab, player tick 0x401e9e, effects, tables, xmpGetTimer 0x4022b3, xmpGetPos 0x401bf4, volume 0x401bde, xmpStop): compare it routine by routine with JIZZ's MXMPlay (`jizz/re/notes-music.md`, `jizz/remake/src/mxmplay.*`) and list every difference, so the remake can share or extend JIZZ's player; a Python player that reproduces `full.gus` and the order / row / timer columns of `full.ticks` (mind that the fake GUS answers volume reads with the last written value), and a WAV render through a GF1 model (`jizz/re/models/music/`, `paper/re/gf1synth.py`) | notes-music.md |
| engine | the 3D engine as parts 7..10 use it: 0x4169f8, 0x41674b, 0x416297, 0x4166dc, 0x416669, 0x416472 and the filler table at 0x427d8c (all the fillers it points to), Craft's texture mapper 0x415127 / 0x414f2f / 0x4150f7, 0x416929, 0x416b61, the matrix / vector routines 0x404c9f, 0x404cd6, 0x404d5c, 0x404e36, 0x404f0f, 0x404feb, 0x4050e9, the projection constants 0x414f04 ([0x422534], [0x422538]), the geosphere generator 0x4174c4 / 0x417420 / 0x4172c8 (octahedron at 0x427f9c / 0x427f84): object / face structures (the 0x34-byte vertices and 0x48-byte faces built by 0x416cca), transforms, projection, clipping, sorting, shading, texture coordinates, how a pixel is written. The parts' own set-up and motion belong to parts3d | notes-engine.md |
| parts3d | part 7 (0x416fce, 0x416e37, set-up 0x416cca), part 8 (0x417929, 0x4176d0, set-up 0x417563), part 9 (0x4185e5, 0x418094, 0x418445, 0x417c48, buffers 0x418402), part 10 (0x41958e, 0x418fd2, 0x4193b6, 0x418b86, buffers 0x419373): what each builds (per-frame geometry in 9 and 10), camera and object motion, backgrounds, the events they react to: [0x427f70] (a scale the engine reads at 0x416ae3 / 0x416b1f, and parts 7 and 8; set by events 7, 9, 803, 1011, 1012), 701 ([0x427f74], part 7), 801 / 803 ([0x427ff0], [0x427fe4]; part 8; 802 → [0x427fe8] is read by 0x4176d0 but no table entry fires it), 901 ([0x428014]) and 902 ([0x428140]) in part 9 | notes-parts3d.md |
| grid | Craft's grid filler 0x402970 (what the grid entries hold, how it interpolates and samples the texture [0x4207c8], light / shade) and the parts that use it: part 2 (0x4198bf), part 4 (0x4037c0), part 5 (0x403530; events 501), plus part 11 (0x419dc7, the tiles and the title box): grid formulas, motion, overlays (pictures, rectangles), the events they read: 201 ([0x428160], [0x42815c]: part 2), 401 / 403 ([0x421a7c], [0x421a84], [0x421a88]: part 4; 402 sets [0x421a80], which nothing reads), 501 ([0x421a54]: part 5), 1101 ([0x428468]: part 11) | notes-grid.md |
| particles | the 8-bit parts: part 1 (0x405527, set-up 0x4053f2; 0x405442, 0x4051d5, 0x404b0e; events 101..107, 111: [0x4224f8], [0x4224fc] (read by all three parts and their helpers), [0x422500]..[0x422514]), part 3 (0x405c45, set-up 0x405b10; 0x405b60, 0x40590c, 0x404621, 0x405f8c; event 301: [0x422518]), part 6 (0x406677, set-up 0x4065c5; 0x406615, 0x40628f, 0x40600f, 0x405162; event 601: [0x42252c]), and what they share: the radial blur 0x403df7 / 0x40407c / 0x404137 / 0x404278, the particle plotter 0x404b84 / 0x404772 / 0x40489c, 0x40507a, the palettes 0x404a3e / 0x404ad3 (0xf57394..), the 8-bit buffers 0xeaf410 (two of 64,000 bytes) and their expansion (0x404a08, the per-band palettes), the feedback 0x4045b4. These are the parts that depend most on the frame rate: say exactly what does | notes-particles.md |

The part scopes call into the engine and the core helpers: describe those calls by what goes in and what comes out
(objects, matrices, parameters, buffers) and leave their internals to the engine and core scopes, unless you need
them to explain your part. If you find a shared routine that no scope covers, describe it in your notes and say so
in your final message.

# Reverse-engineering DOPE.EXE — shared brief for decompiling one scope

## Goal

*DOPE* (Complex, The Gathering '95 PC demo; code jmagic, graphics and music jugi; GUS replay "phantom/sonic pc,
gus unit raped by jmagic") will be remade in C++20 / SDL3 / OpenGL 3.3, like the Paper and JIZZ remakes
(`paper/remake`, `jizz/remake`). The remake reads the original files at run time and renders at any resolution in
true colour: "the same as the original, but much sharper". For that it needs the exact algorithms. Your job:
decompile the routines in your scope into readable pseudo-C with every constant, so that someone who has not seen
the assembly can port them. You are read-only on everything except your own notes file and your own models folder.

## Read first

- `dope/re/NOTES.md`: file layout, the RNC-1 packer, PMODE, the loader and its player, CDPI (how the parts are
  run), the parts' common structure, the retrace-driven timing, video modes, how the headless run works.
- `dope/re/PARTS.md`: the timeline (frames, seconds, music order:row, routines, frame rate, what is on screen).
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so for
  every table, palette, picture, mesh, path or string say where it is (image, `SEG:OFF`, size, format) so the remake
  can read it from the unpacked image at run time (the remake will unpack DOPE.EXE itself, exactly like
  `unrnc.py`, and get the same `unpacked/<name>.bin` bytes).
- For the style of the result, look at `jizz/re/notes-parts3d-a.md` or `paper/re/notes-planes.md`.

## Files and tools (all in `dope/re`, run inside the toolbox container)

- `unpacked/<name>.bin`: the unpacked load modules (`loader`, `p1`..`p7`), relocated for load segment 0: the file
  offset of `SEG:OFF` is SEG*16 + OFF, and a relocated segment immediate in the code (`push 0x4527; pop es`) is the
  paragraph number in this file. All part code is in segment 0010 (file offset 0x100), the stack is 0000:0000..00FF.
  The loader's 32-bit code (player, main) is addressed as code32 offsets (file offset 0x17e0 + offset).
  `unpacked/<name>.txt` lists entry points and relocation sites.
- `rep.py` (capstone), one view per image (`loader`, `cdpi`, `loader32`, `p1`..`p7`):
  ```
  cd dope/re
  toolbox run -c acme303 python3 rep.py p2 func 371e        # recursive-descent listing (segment 0010 by default)
  toolbox run -c acme303 python3 rep.py p2 dis 0010:2580 40  # linear listing
  toolbox run -c acme303 python3 rep.py p2 calls 371e        # near call sites (and far calls)
  toolbox run -c acme303 python3 rep.py p2 xref 1928         # instructions containing this 16-bit value
  toolbox run -c acme303 python3 rep.py loader32 func 22c4   # the player's tick (32-bit code)
  toolbox run -c acme303 python3 rep.py p2 funcs | far | str | ints | ports
  ```
  In Python (`import rep`): `v = rep.View('p2')`, `v.img` (bytes), `v.rd8/rd16/rd32(file offset)`,
  `v.dis((seg, off), n)`, `v.func((seg, off))`. The code is hand-written 16-bit assembly with 386 instructions
  (32-bit registers and addressing, `fs`/`gs`), no calling convention: read each routine's register use. Byte scans
  (`xref`, `ints`, `ports`, `funcs`) also hit data; check hits with `dis`. Self-modifying code exists (immediates
  patched before loops, e.g. `add bx, 0` at p4 0010:1b09): say so where you find it.
- Dynamic analysis: `run_dope.py` (see its docstring and NOTES.md). Snapshots `snaps/part_<p>.snap` are taken where
  CDPI starts each part (a few thousand instructions into it). Load one in Python inside the toolbox (`import
  unicorn, pickle; d = pickle.load(open('snaps/part_p3.snap', 'rb'))`): `d['mem']` = 16 MB from linear 0 (the part is
  loaded at segment 0F64: its file offset X is at linear 0xF640 + X; segment S of the image is S + 0F64),
  `d['vram']` (A0000..AFFFF in chain-4 mode 13h) or `d['vga']['planes']` (4 x 64 KB in p1's unchained mode),
  `d['vga']` (registers, `dac`), `d['st']`, `d['pit']`, `d['gus']`. `--snap-at F1,F2 --snap DIR/{tag}.snap` saves
  more snapshots at chosen frames (put them in your models folder). To run on from a snapshot with your own hooks,
  COPY `run_dope.py` into your models folder and edit the copy (never the shared one; the copy finds `unpacked/` and
  `../original/` by itself):
  ```
  toolbox run -c acme303 python3 models/<scope>/run_dope.py --load snaps/part_p3.snap --out models/<scope>/x \
      --frames 12000 [--trace 0F74:2BA4] [--mips 50]
  ```
  `--trace SEG:OFF` takes linear real-mode addresses (`CS:IP` of the running part: CS = 0F74 for segment 0010).
  The emulation runs at about 0.2x real time (one minute of the demo takes five). Do not start runs of the whole
  demo; use the part snapshots.
- Reference capture: `full.frames` (32-byte header `DOPEFRM1`, then per displayed frame (70.09 Hz) 8 bytes
  <width, height, flags, 0>, the 768-byte 6-bit DAC, 320x240 indices with the visible area top left);
  `full.ticks` (per frame: frame, time, music order, row, tick countdown, music tick, part, CRTC start, start writes,
  key checks (= frames drawn by the engine parts), mode, main CS:IP); `full.gus` (GUS register writes by music
  tick); `full.dram` (the GUS memory after the upload). `toolbox run -c acme303 python3 sheet.py full.frames out.jpg
  F1 F2 ...` makes a contact sheet (then Read it); `sheet.frame(...)` gives one frame as RGB.
  `dope_reference.mp4` is the capture at 60 fps, `overview.jpg` one frame every 4 s (Read it first).
- Scratch files and your Python models go in `dope/re/models/<scope>/` (keep the models: they are the record of how a
  finding was checked; add a short README.md listing them). Do NOT modify anything else in `dope/re` except your
  own notes file.

## Time bases (state one for every animated variable: this decides how it is ported)

- **Retraces**: the per-retrace routine each part installs (called from its int 8 handler once per displayed frame,
  70.09 Hz) advances most motion, palette fades and the counters the script waits on. A remake evaluates these
  from `t * 70.086` retraces since the part (or the script step) started, interpolating between ticks.
- **Script steps**: the main script calls "run N retraces" with parameters set in between; give the step list with
  N and what each step changes (this is the part's timeline).
- **Per drawn frame**: anything advanced in the frame loop rather than in the per-retrace routine (and so depends on
  the CPU speed). Say so explicitly, with the frame rate the capture shows (key checks per second in full.ticks,
  PARTS.md).
- **Music**: the player runs on the GUS timer and nothing in the parts reads it; if you find a part that does
  (a music position, a sync byte), that is a major finding — report it.
- **Random numbers**: name the generator, its seed and when it is called, so the remake can replay the sequence.

## Output: `dope/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its frames / seconds (PARTS.md) and its script steps;
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out): how buffers are addressed, how
   colours are built (palettes, shade / blend tables and how they are generated or where they are stored), the
   formulas for every generated table, the object / mesh formats and where each mesh, path, picture and palette is
   (image, SEG:OFF, size, format);
3. the time base of every animated variable;
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (lookup-table sines,
   fixed-point steps, nearest-neighbour texture reads, integer polygon edges, 8-bit palette quantisation, low-res
   buffers), with what they approximate; and the visible quirks that are part of the look and must stay;
5. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of a routine that reproduces a captured frame
(or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

Seven scopes of similar size. Addresses are starting points found while mapping the demo (NOTES.md, PARTS.md),
not a complete list: follow the calls.

| scope | routines | notes file |
|---|---|---|
| music | the loader's player and GUS driver (`loader32`): module loader 0x2d15 (0x4aa6, 0x4b27, 0x4ae3, 0x3e88; what is kept where), start 0x2c17 / stop 0x2cff, the tick 0x22c4 with every effect (jump tables 0x2579 and 0x25c1), the GUS IRQ handler 0x4040 (timer path; ramp path 0x40d8), 0x41e9 (tempo → timer count with the remainder `[0x3f29]`, voice updates 0x421a, 0x4299), init 0x45b4 / 0x490a / 0x498b, 0x4369, 0x43d6, upload 0x4450, 0x44a8, 0x44f8, 0x4545, panning 0x4587; the MOD format as this player reads it (28 channels, 8xx panning, F-commands). A Python player that reproduces `full.gus` (register writes per music tick) from DOPE.MOD, and a WAV render through a GF1 model (start from `paper/re/player_ref.py`, `paper/re/gf1synth.py` and `jizz/re/models/music`). Note the fake GUS answers reads of 89h / 8Ah / 8Bh with its simulated ramp / position (NOTES.md) | notes-music.md |
| core | CDPI (`cdpi` view, 0131..0400) and the loader's main (code32 0xa55) as far as the remake needs them (what is on screen when, the start-up); the framework every part repeats: PIT calibration, int 8 handler, per-retrace routine skeleton, "run N retraces" loop, key check, exit / restore (p2: 1a5f, 1a21, 1abf, 1b17, 371e, 360c's wait, 43ed; give the equivalent addresses in p1, p3..p7), the Cirrus probe; **p1 completely** (0bd5..0d30: the mode-X set-up, 0b5c logo blit and where the 1024x195 logo is and its format, the palettes at 04f0 / 07f8 and the mixing 0b00 / 0b24, the scroll 0b8b / 0ba3, the split screen); the hand-overs between parts (first / last frames of each part, p2's white exit); and a check of NOTES.md's RNC-1 description (a model written from the notes only must give `unpacked/*.bin`) | notes-core.md |
| engine | the 3D engine shared by p2, p3, p5, p6, p7, documented from p2: object list 0x1810 and the scene / mesh formats (segment 2ccd and the far pointer table at 0050:0000), camera variables 0x1816..0x1838 set by the scripts, 354a / 3585 (set-up, transformation: 1b60 matrix, 1c60 vertices), 3310 (visible faces, sorting, drawing), the polygon fillers and their shading (2660 and neighbours), the tables 1967 / 1998 / 19ce, 2550 (palette scaling), 360c (frame copy and its variants by `[1904]`), the motion update in the per-retrace routine 2580. Give the address of each routine in p3, p5, p6 and p7 and say where those copies differ (the part scopes document the part-specific fillers) | notes-engine.md |
| p2 | p2's script 377f (153 steps: what each step sets), its objects (the two discs, the "C" logo and its stand), the camera paths, the credit texts (font / picture, placement, fades), "BLUR motion" (the blur buffer and how it is mixed), the palettes and their per-retrace fades, the fade to white at the end | notes-p2.md |
| p3p5 | p3 (script 2c22, 21 steps: the 7800-polygon object and where its mesh is, the "Phong" shading and how normals become colours, the nebula picture and its palette, the label) and p5 (script 30e2, 28 steps: "ENV mapping" and "BUMP glow" — their fillers, maps and palettes, the star / torus meshes, the chequerboard picture, the hand-over to p6's picture) | notes-p3p5.md |
| p4 | p4 completely: the main routine 1caa and its retrace thresholds, "FIRE clouds" (1aab: the per-pixel lookup through the tables at 07d9 / 66cd / 4f1e with self-modified offsets, how the tables are generated, the blend over p3's picture, palettes 1975 / 1999, the per-retrace routine 1a20), "FRACTAL scape" (1ee8 / 1efc: height map, projection, shading), the hand-over to p5's chequerboard, the labels | notes-p4.md |
| p6p7 | p6 (script 3951, 16 steps: the cat mesh, "SHADOW volume" — how the shadow is drawn, "material MORPH", "METAL shading", the rotating rings, the blue swirl picture, what the mouse buttons change) and p7 (script 31ca, 7 steps: "multiple LIGHT sources", the "WORLD domination" tentacle object, the horizon picture, the "dope" ending, text mode) | notes-p6p7.md |

The part scopes call into the engine and the core helpers: describe those calls by what goes in and what comes out
and leave their internals to the `engine` and `core` scopes, unless you need them to explain your part. The engine
code is duplicated in every engine part at different addresses: `engine` documents it once (from p2) and gives the
address of each routine in p3, p5, p6 and p7, and says where the copies differ. If you find a shared routine that no
scope covers, describe it in your notes and say so in your final message.

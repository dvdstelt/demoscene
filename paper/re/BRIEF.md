# Reverse-engineering PAPER.EXE — shared brief for decompiling one scope

## Goal
"Paper" (Psychic Link — code Statix, music Vic of Acme — Wired '96 64k intro, MS-DOS) will be remade in
C++/OpenGL on the engine of the 303 remake (303/remake). The remake renders at high
resolution in true colour: "same as the original, but way sharper". For that we need the exact algorithms.
Your job: decompile the routines in your scope into readable pseudo-C with every constant, so someone who has
not seen the assembly can port them. You are read-only on everything except your own notes file and your own
scratch directory.

## Read first
- paper/re/NOTES.md — layout (Tran's PMODE, code32), main sequence, what is
  known (palette fade, music counter, the plane-flock bug and its frame-rate dependence, the page-peel transition).
- paper/re/PARTS.md — the part timeline (ticks, music counter, routines).

## Files and tools
- paper_img.bin = unpacked image. All addresses in notes are code32 offsets (code32 = image offset 0xd00); in the
  emulator code32 sits at linear 0, so a code32 offset is also the emulator's linear address.
- rep.py (capstone; run inside the toolbox container):
      cd paper/re
      toolbox run -c acme303 python3 rep.py func 0x388b      # recursive-descent listing (follows jumps, stops at ret)
      toolbox run -c acme303 python3 rep.py dis 0x3884 40    # linear listing
      toolbox run -c acme303 python3 rep.py calls 0x3fb3     # call sites
      toolbox run -c acme303 python3 rep.py xref 0x1edc1     # instructions containing this immediate/displacement
  In Python (`import rep`): rep.SEG (code32 bytes), rep.rd32/rdf/rdd(addr), rep.cstr(addr), rep.dis, rep.func.
  The listing annotates float constants (`; =1000`). Note: rep.func's output may start with an unrelated
  fragment (e.g. 0x2df..0x314) — ignore lines before your start address.
- The code is hand-written 32-bit assembly (no C runtime, no calling convention: read each routine's register use).
  Common helpers: 0xaa2 random (find its algorithm), 0x240 getmem (eax bytes -> eax pointer), 0x377f/0x3793
  random in +-mask, 0xae2 / 0xb1f / 0xbaa / 0xbed / 0xc30 matrix helpers, 0x15af / 0x15c0 palette,
  sine table 65536*sin at 0x22479 (1024 entries), reciprocal table 65536/n at 0x1ee19.
- Dynamic analysis: run_paper.py runs the intro headless in unicorn (see its docstring and NOTES.md). Snapshots
  (pickle: 'mem' = 64 MB from linear 0, 'pal' = 768 bytes 6-bit DAC, 'ctx', 'tick', ...) exist at the start of
  each part in <scratch>/snaps/t<tick>.snap
  (ticks 120, 1150, 1250, 2000, 3300, 4500, 5500, 5800, 6100, 7000, 8000, 8700, 9500, 11100, 11500).
  Read generated tables / textures / object data straight from snap['mem'] (unpickling needs unicorn imported,
  so run inside the toolbox). To run on from a snapshot with your own hooks, COPY run_paper.py into your scratch
  directory and edit the copy (never edit the shared one):
      toolbox run -c acme303 python3 run_paper.py --load --snap <snap> --out <scratch>/x --ticks <end> --whole-frames
  It needs rep.py and paper_img.bin next to it (or on sys.path). A run of ~700 ticks takes ~1 minute.
- Reference capture: full.frames = per tick t, record t = 64000 bytes VGA + 768 bytes 6-bit palette (record size
  64768). full.ticks = per tick: tick, music counter [0x1ea8b], effect callback [0x1e5ef], main EIP, frames drawn.
  `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> t1 t2 ...` makes a contact sheet (then Read it).
  paper_reference.mp4 (a capture of the original made with paper/re/run_paper.py and frames2mp4.py; not in the repository) is the 70 fps video of the same.
- Do NOT modify: anything in paper/re except your own notes file, and nothing in the remake. Scratch files go in
  paper/re/models/<your scope>/.
- Don't start runs of the whole intro (13000 ticks, ~10 min) unless you really need one; use the snapshots.

## Time bases (decide and state one for every animated variable — this decides how it is ported)
- IRQ0 at 1193182/0x4295 = 70.0018 Hz runs 0x1e45: music player (0x1ed6, tempo accumulator), palette fade
  ([0x1edb5] current level, [0x1edbd] target, [0x1edb9] step per tick; 0x8000 = full; 0x15c0 uploads), and the
  per-tick effect callback [0x1e5ef]. [0x1edc1] is a tick counter some parts use for fixed-rate steps.
- Music counter [0x1ea8b] (advanced by the player, ~8.33 per second, ~8.4 ticks per step); parts end when it
  reaches [0x1ea8f].
- Anything advanced in the main loop runs per drawn frame. The emulator draws ~1.5M instructions per tick; a 1996
  PC was slower. For the plane flock the remembered look is ~34 fps. For every per-frame variable in your scope,
  say so explicitly and give the instructions per frame you measure (frames per tick column in full.ticks), so the
  remake can pick a fixed step rate.

## Output: paper/re/notes-<scope>.md
Per part / routine:
1. what it draws, in one or two sentences, with tick and music-counter start/end;
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how textures are
   addressed, how palettes are built and applied, and the formulas for every generated table/texture/object
   (procedural generators must be described precisely enough to regenerate them at higher resolution; say which
   data is static in the exe instead — address, size, format);
3. the time base of every animated variable (per tick, per frame, from the music counter) and its rate;
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (e.g. 8x8-block
   interpolation, lookup-table approximations), with what they approximate;
5. what you could not determine (mark guesses as guesses).
Check your reading against the captured frames (sheet.py) where you can. Your final message: summarise the key
findings in under 300 words and give the notes path.

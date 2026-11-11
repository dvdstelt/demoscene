# Reverse-engineering 303.EXE effect routines — shared brief

## Goal
Acme's "303" (1997 MS-DOS demo, code by Statix) is being remade in C++/OpenGL at
303/remake. Several effects were rebuilt by eye from a video capture.
We now want the exact algorithms from the original executable, so the remake can match them.
Your job: decompile the routines in your scope into readable pseudo-C with every constant,
and say precisely how the remake's corresponding scene differs. You are read-only on the remake
source: write findings only to the notes file named in your task.

## Files
- Executable: 303/original/303.EXE — Watcom C/C++ 32-bit, DOS/4GW (LE).
  Flat memory image: code object at 0x10000 (size 0x37000), data object at 0x50000.
- Data: 303/original/MUSTARD.PAK (LZW-packed files: textures *.raw = 320x200 or
  256x256 8-bit + palette, fade tables *.fad, *.gst, meshes *.dat). tools/lzw.py `read_pak` lists it.
- Remake scenes: remake/src/scenes/{intro,aap,part1,part2,part3}.cpp, shared effects remake/src/fx.{h,cpp},
  timeline remake/src/timeline.cpp, README remake/README.md.

## Tools (capstone is only installed in the toolbox container)
Run from 303/remake/tools:
    toolbox run -c acme303 python3 re303.py func 0x2d81c      # recursive-descent listing of one function
    toolbox run -c acme303 python3 re303.py dis 0x30f73 80    # linear listing, N instructions
    toolbox run -c acme303 python3 re303.py calls 0x2d81c     # call sites of a function
    toolbox run -c acme303 python3 re303.py str tunnel.raw    # string address + code xrefs
In Python (`import re303 as r`): r.func(addr), r.dis(addr, n, stop_ret), r.xrefs(addr) (code containing
the 32-bit immediate), r.calls_to(addr), r.cstr(addr), r.rd32(addr), r.DATA / r.DB (data bytes / base),
r.CODE / r.CB. `func` annotates x87 loads of data constants with their float/double value (`; =0.05`).
Read floats/doubles/tables from data yourself with struct when needed. Put any scratch scripts in
<scratch>/re/ (not in the remake).

## What is already known
- Watcom register calling convention: args in eax, edx, ebx, ecx, then stack; result in eax
  (st0 for float-returning helpers is possible — check callers). printf = 0x32c61, memset(eax=dst, edx=val, ebx=n) = 0x32c30.
- Effect routine convention: f(0x17) = init/load, f(0x48) = free, f(ptr) = render a frame into a
  320x200 8-bit buffer. The two frame buffers are 0x5fda8 and 0x6f7a8 (64000 bytes each).
  0x1bbec copies a buffer to the screen ([0x52914] = screen pointer). 0x1c490 / 0x1c503 / 0x1c3f4 are
  buffer combine/copy helpers (worth identifying: e.g. additive, through a fade table, etc.).
- Asset loading: 0x10f94(name, ...) loads a PAK file; 0x115a0 / 0x112d0 are related (palette / free).
  0x106a8(name, float) loads a fade table (*.fad). 0x10824 loads a *.gst table.
- Timing: [0x9b844] = music position = 2*(order*64 + row) - 2 + [0x149a04] (half-rows).
  [0x9b840] = position at part start rounded to a pattern. Parts end when [0x9b844] reaches a value.
  Convert with: python3 <scratch>/re/pos.py 0x840   (prints music time and video time).
  The remake's scenes are written in video time t = VT(music) (see fx.h V()/VT()).
  One row = 0.106375 s up to order 17, 0.104875 s after (orders are 64 rows; order 0 is 16 rows).
- Ticks: 0x1bc01 is called by the MIDAS timer once per vertical retrace (mode 13h, ~70 Hz). It increments
  [0x5d69c] and [0x58120], refreshes [0x9b844], then calls every callback registered with 0x10bd8(fn)
  (list at 0x8ec04, removed with 0x10c0c(fn), 0x10c0c(0) clears all) and [0x5d698]. Animation state
  advanced in those callbacks therefore runs at ~70 per second regardless of render speed; state
  advanced inside a render routine runs per rendered frame (note which one each variable uses —
  this decides how it is ported).
- Main script: 0x30aa2. Part sequence (end positions → video time):
    0x14f04 intro (neuron tunnel, opening1 credits, 303b strip)              -> ~40.9 s
    inline loop 0x30f73: Aap stripes: 0x2d458 wipeout.raw, 0x2b110, 0x2bdb4, 0x29d1c, until 0x480 -> 54.95 s
    inline loop 0x311ed: 0x182e4 blobby.raw + 0x2a34c + 0x29d1c until 0x540 (remake: robot, spoon) -> 65.17 s
    0x15734 credits.raw/phong.raw: photo + chrome spikey + credit words (remake: alex)
    inline 0x314b6: harl video + 0x2a34c until 0x680 (remake: harl)          -> 82.2 s
    inline 0x31802: 0x2c5c0 + 0x2d038 until 0x740 (remake: web)              -> 92.44 s
    0x16198 tunnel.raw (remake: rings)
    0x2e8e4 big mid-section: face, facebox, typo, blobs, wireblobs, spiral, vic, star, plasma, paranoid,
            facefloor (sub-parts, each with its own end position; textures wire.raw, bd2.raw, tex3d.raw,
            phong1.raw + faceo.dat, sphere.raw + icosa.dat, weird.raw, paranoid.lzw; fades 3d.fad,
            fish.fad, pinfx3.fad ...). Timer callbacks 0x2e534, 0x2e684, 0x2e798.
    inline 0x31b17: vs.lzw video + 0x2a34c ; inline 0x31df9: alex.lzw video + 0x2a34c (remake: dance)
    inline 0x320be: 0x2a840 final.raw (remake: icons) ; 0x10dc4 (outro)
- Routine sizes (instructions / x87 ops): 23540 84/20 (calls 2338c faceo.dat, 22be0, 22e14, 23170),
  29ae8 154/55, 26c14 220/108, 27444 149/70, 28100 155/48, 28330 128/94, 28c64 269/102,
  2902c 225/76 phong1.raw, 2b908 226/28 icosa.dat+sphere.raw, 2d81c 312/186, 16970 weird.raw,
  16b34 paranoid.lzw, 14760 56/29 (probably a sin/cos or matrix helper — identify it early).

## Output
Write a markdown notes file (path in your task) containing, per routine and per sub-part:
1. what it draws, in one or two sentences;
2. pseudo-C of the algorithm with exact constants (and table contents/formulas for any generated
   lookup tables), including how textures are addressed and how palette/fade tables are applied;
3. the time base of every animated variable (per tick at 70 Hz, per frame, or from [0x9b844]) and
   start/end positions converted to video time;
4. a concrete list of differences from the remake's scene code (file:line), most visible first;
5. what you could not determine.
Keep the notes factual; mark guesses as guesses. Your final message should summarise the key
findings in under 300 words and give the notes path.

# Reverse-engineering KORSO2.EXE — shared brief for decompiling one scope

## Goal

*The Sea Robot of Love* (Orange, The Party 1995 PC 64k intro, 3rd place; programming hoplite and dune, music dune,
font and graphics der piipo, PMODE by Tran, the player by Phantom, the 50 Hz mode by liket) will be remade in C++20 /
SDL3 / OpenGL 3.3, like the other remakes in this repository. The remake renders at any resolution in true colour:
"the same as the original, but much sharper". For that it needs the exact algorithms. Your job: decompile the
routines in your scope into readable pseudo-C with every constant, so that someone who has not seen the assembly can
port them. You are read-only on everything except your own notes file and your own models directory.

## Read first

- `okean/re/NOTES.md`: unpacking, the image layout, the runtime model, main's flow, the 50 Hz mode, **the parts'
  common structure** (set-up, the retrace timer and its callback, the main loop), the music overview, the headless
  run.
- `okean/re/PARTS.md`: the timeline (frames, music positions, what is on screen) and the snapshots.
- `AGENTS.md` (repository root): the rules. Nothing from the original may be copied into the remake's source, so
  for every static table, palette, picture, mesh, string or parameter say where it is (code32 address, size,
  format) so the remake can read it at run time.
- For the style of the result, look at `inside/re/notes-twirl.md`, `dope/re/notes-p4.md` or
  `paper/re/notes-planes.md`.

## Files and tools (all in `okean/re`, run inside the toolbox container)

- `unpacked/korso2.bin`: the program. **Addresses are code32 offsets** (the program's near pointers): file offset =
  0xec0 + address; in the emulator and in snapshots the linear address is 0x8fc0 + address.
- `rep.py` (capstone):
  ```
  cd okean/re
  toolbox run -c acme303 python3 rep.py code32 func 2c26f     # recursive-descent listing (follows jumps, stops at ret)
  toolbox run -c acme303 python3 rep.py code32 dis 2c0f9 40   # linear listing
  toolbox run -c acme303 python3 rep.py code32 calls 4cacc    # call sites
  toolbox run -c acme303 python3 rep.py code32 xref 14d1c     # instructions with this 32-bit value
  ```
  In Python: `import rep; v = rep.View('code32')`; `v.rd8/rd16/rd32(0xec0 + addr)`, `v.dis(addr, n)`,
  `v.func(addr)`.
- **The code is hand-written 32-bit assembly.** It modifies itself in places (addresses and constants patched into
  instructions by a set-up routine, e.g. 0x38469): read patched values from a snapshot, and say which instruction
  holds them so the remake can read the original immediate from the file and apply the same patch logic. Many
  branches are followed by four `nop`s (an assembler's alignment of forward jumps): ignore them.
- Dynamic analysis: `run_okean.py` runs the intro headless in unicorn (its docstring and NOTES.md). Snapshots
  (pickle: `'mem'` = 16 MB from linear 0, `'pm'` the CPU context, `'st'`, `'vga'`, `'pit'`, `'gus'`, `'voices'`) are
  in `okean/re/snaps/part_<entry>.snap`, taken where main calls each part, before its set-up. Unpickling needs
  unicorn imported, so run inside the toolbox. Read generated tables and buffers straight from `snap['mem']`
  (linear = 0x8fc0 + code32 address). To run on from a snapshot with your own hooks (trace a routine, dump a
  buffer at a given frame), COPY `run_okean.py` into your models directory and edit the copy (never the shared one;
  a copy anywhere below `okean/re` finds `unpacked/` by itself):
  ```
  toolbox run -c acme303 python3 <models>/run_okean.py --load snaps/part_3554b.snap --out <models>/x --frames 2500
  ```
  (`--frames` is the absolute frame to stop at.) The emulator does about 0.8x real time. Do not start runs of the
  whole intro (5 minutes, 700 MB) unless you really need one, and never write into `okean/re/full.*` or
  `okean/re/snaps/`.
- Reference capture: `full.frames` (32-byte header `OKEANFR1`, then per refresh (49.851 Hz) 8 bytes, the 768-byte
  DAC palette (6-bit values) and 320x240 bytes of indices, the 320x200 picture in the top left; record k = refresh k
  = program time k / 49.851 s), `full.ticks` (per refresh: frame, time, order, row, tick countdown, music ticks,
  part, start address, start writes, timer IRQs so far, mode, EIP), `full.gus` (GUS writes, stamped by the timer-2
  ticks served), `full.dram` (GUS memory). In Python: `from sheet import open_frames, frame, read_ticks`.
  `toolbox run -c acme303 python3 sheet.py full.frames <out.jpg> f1 f2 ...` (or `--every 0.5 --from 50 --to 60`)
  makes a contact sheet (then Read it).
- Do NOT modify anything in `okean/re` except your own notes file and your models directory. Scratch files and your
  Python models go in `okean/re/models/<scope>/` (keep the models: they are the record of how a finding was checked;
  add a short README.md there listing them). Large outputs (dumps, images) stay there too and are not committed.

## Time bases (state one for every animated variable: this decides how it is ported)

- **Retrace callback** (49.851 Hz, NOTES.md "The parts' common structure"): each part's callback, called from its
  IRQ 0 handler at the start of every vertical retrace. State it advances is stepped per refresh: give the
  recurrence and, where it is linear, the closed form in refreshes since the part's first callback.
- **Per drawn frame**: the part's main loop draws at most one frame per retrace. Anything that changes once per drawn
  frame without reading a counter the callback advances is frame-rate dependent: name it and say what it looks like
  on a slow machine (in the capture every refresh is drawn).
- **Music position**: order `[0x12b75]`, row word `[0x12b73]` (and the player's `[0x12b72]`): every event that waits
  for a position (part end, colour change, caption, object change): the position, and what changes.
- **Random numbers**: name the generator (e.g. the noise of the first part: seed `[0x14d1c]`, 12347 in the file,
  `x = ror(x + 0x9d, 11) ^ 0x9d`), its seed, whether it continues from the previous part, and how often it is called
  (per pixel, per frame), so the remake can replay or emulate it. For per-pixel noise, say what its statistics are
  (which indices, how many per frame), because a high-resolution remake will draw noise of the same look rather than
  the same 64,000 values.

Say also which frame of the capture shows the result of which callback count: the order of the callback, the draw
and the copy to video memory decides it (record k is video memory and the DAC at the end of refresh k, the buffer
copied in refresh k's retrace; notes-core.md section 4).

## Colours: 8 bits through a palette

Everything is drawn in 256 colours. The remake draws true colour: a pixel's colour is the palette entry of its
index, as the DAC shows it (6-bit value v -> v / 63). So for every drawn pixel say how its index is made: the
background (noise, gradients, horizon bands), the lines (single index, additive or saturating index arithmetic,
brightness by depth, anti-aliasing or glow, how overlapping lines combine, clipping), the text. Give the palette
(static address or how it is built) and every palette change (fades, colour changes at music positions). The remake
will evaluate the same mapping with continuous values (for example an intensity accumulated per pixel at high
resolution and then looked up in the palette ramp), so say exactly what the index means (an intensity on a ramp? which
range?).

## Pictures and text

The remake shows faithful high-resolution redraws of hand-drawn pictures by default and the original pixels on
request (AGENTS.md, "Redrawn pictures"); generated textures and anything an effect reads through palette indices
stay computed from the original. For every picture, font or bitmap your scope uses, say: hand-drawn (by der piipo)
or generated; its address, size and format; its palette; how it is drawn (where, transparency, blending with the
noise or the lines); for text, the exact strings with their addresses and positions on screen and the look of the
letters.

## Output: `okean/re/notes-<scope>.md`

Per part or routine:
1. what it draws, in one or two sentences, with its frames / music positions (PARTS.md);
2. pseudo-C of the algorithm with exact constants (fixed-point formats spelled out), including how buffers are
   addressed and how the pixel's index is made (above), and the formulas for every generated table or mesh.
   Procedural generators must be described precisely enough to regenerate them at a higher resolution. Say which
   data is static (address, size, format);
3. the time base of every animated variable (above), and every music-position event;
4. rendering shortcuts that a high-resolution remake should evaluate exactly instead (low-resolution grids and
   interpolation, lookup tables such as the 4096-entry sine at 0x24a3c, integer steps, 1-pixel lines, palette
   quantisation, index arithmetic), with what they approximate;
5. whether the part overwrites the whole frame;
6. what you could not determine (mark guesses as guesses).

Check your reading against the capture where you can: a Python model of the routine that reproduces a captured
frame (or a table in a snapshot) exactly is the best proof; say which ones you verified and how closely. A model
that reproduces whole frames of your part bit for bit (from a snapshot's state plus the callback count) is the goal
for every part.

Your final message: summarise the key findings in under 300 words and give the notes path.

## Scopes (who does what)

| scope | routines | notes file |
|---|---|---|
| core | main 0x100c4 (the 50 Hz mode exactly), the retrace timer 0x14bec..0x14d1b (and check that every part's copy is the same code; list each part's copy and callback address), the parts' common main-loop structure, the no-GUS fake music clock in the callbacks; **the clocks the remake needs**: from `full.ticks` and `full.gus`, the exact relation between program time, refreshes, the music ticks (19.84 ms) and the position (the time of every row; when each part's main loop leaves and the next one's first callback runs, in refreshes, relative to the row's start); which callback count each captured frame shows; the shared text printer 0x4cacc with its font (glyph widths 0x4c8c2, character map 0x4c861, pixels from 0x3cec2: the whole font, its glyphs, height, spacing, the transparent index) and every call of it (strings, positions, colours, timing); and the last part 0x4d370 with 0x3cd7c / 0x3cdd0 / 0x3cdf7 (the end texts over noise, how long each page stays, the exit) | notes-core.md |
| music | the player as the intro uses it: the MOD loader 0x13f28 (the module at 0xae0: header, orders, patterns, samples; the song: speeds, tempos, effects used), the tick 0x1343c and every effect, start 0x13eb1 / stop 0x13f18, the GUS driver 0x1433d..0x14bab (init 0x14935, the IRQ 0x1443c with its timer and ramp paths and the wait for the ramp IRQ, voice allocation, frequency, volume and pan writes, sample upload, the timer count from the tempo). Compare with DOPE's player (`dope/re/notes-music.md`, `dope/remake/src/modplay.cpp`, "ProTracker for GUS replay routine (C) 1994 Oskari Tammelin"): which routines are the same, what differs. Deliver a Python player (`models/music/`) that reproduces `full.gus` (the order of the writes and their values per tick) and the order / row columns of `full.ticks` from the module alone, a WAV render through a GF1 model (`dope/re/models/music/`, `dope/remake/src/gf1.*`), and the exact time of every tick (19.84 ms; what the parts' timer calibration, which runs with interrupts off, does to the ticks at each part start in the capture and on real hardware: late or lost ticks) | notes-music.md |
| title + objects | the first part 0x2c26f (set-up, 0x2bc7c, 0x2bf4f, 0x2c0f9, the callback 0x2c47b; the beams, the noise, the title letters: where they come from and how they appear, the palette 0x2b65c and its fade) and the second part 0x2e779 (0x2e304, 0x2e5f5 and its callback; every object, its mesh or formula, the camera, the line drawing, the background with the horizon band, the colour changes from red to orange to green) | notes-title-objects.md |
| landscapes | the third part 0x3554b (0x350c8, 0x353be, the callback) and the fifth part 0x38520 with its set-up 0x38469 (0x374da, 0x377b2, 0x3801c, 0x38331, the callback; the self-modifying code; "moving lightsource and shadow"; the captions are drawn by the core's text printer: say when and where) | notes-landscapes.md |
| morph + star | the fourth part 0x367e5 (0x363d0, 0x366bd, the callback; the morphing objects) and the sixth part 0x3c8da (0x3c128, 0x3c46e, 0x3c7b5, the callback; the web and the star) | notes-morph-star.md |
| sheet | the seventh part 0x5334f (0x52fed, 0x531fe, the callback; the curling sheet, the captions, the ending with noise and lines) | notes-sheet.md |

The part scopes call into shared code (the text printer 0x4cacc, the timer copies): describe those calls by what
goes in and what comes out (the core scope describes them). Where the routine lists above are incomplete, follow the
calls: everything a part's routines call that is not shared belongs to the part. The line drawer may be shared
between parts in copies: say so if you find the same code in another part's range.

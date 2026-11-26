# The Sea Robot of Love remake — shared brief for everyone writing code in `okean/remake`

*The Sea Robot of Love* (Orange, The Party 1995 PC 64k intro; code hoplite and dune, music dune, font and graphics der
piipo) is remade in C++20 / SDL3 / OpenGL 3.3 core. The goal: **the same as the original, but much sharper**. Same
motion, timing, composition, colours and music as the original, rendered at any resolution in true colour.

The original has been reverse engineered. Read before writing code:

- `okean/re/NOTES.md` (unpacking, layout, the parts' common structure), `PARTS.md` (the timeline);
- `okean/re/notes-core.md`, **section 4 above all** (the clocks: records, callbacks, part changes, music ticks), and
  the notes of your part: `notes-title-objects.md`, `notes-landscapes.md`, `notes-morph-star.md`, `notes-sheet.md`,
  `notes-music.md`. Every routine is pseudo-C with exact constants, and the Python models in
  `okean/re/models/<scope>/` reproduce the capture's frames byte for byte: they are the reference values;
- `AGENTS.md` in the repository root (the rules).

## The framework (read the headers)

- `src/demo.h`: `Scene` (`init`, `render(ctx, r)`), `REGISTER_SCENE`, `Ctx`, `TickStepper`, `Steps`.
  `src/scenes/morph.cpp` is a complete port of the fourth part: start from its structure.
- `src/schedule.h`, `src/timeline.cpp`: when the parts start and end, computed from the song (they reproduce the
  capture's part changes). `src/music.h`: the soundtrack and the player's positions.
- `src/okean/glow.h`: the intro's way of drawing, shared by all parts:
  - `okean::Sine` (the table at 0x24a3c as the first part completes it; `Sine::s(a)` / `c(a)`: the continuous
    curve, 1022 sin(2π a / 4096));
  - `okean::palette(exe, addr)`, `okean::scaled(pal, num, den)` (fades), `okean::kernel(exe, addr)` (a blob routine's
    8x8 kernel, read from the immediates of its 16 `add` instructions);
  - `okean::Canvas` (`ctx.shared<okean::Canvas>()`): `begin`, `noise(levels, frame)`, `setKernel`, `blob(x, y, w)`,
    `line(x1, y1, x2, y2, w, step)`, `flush`, `add`, `buffer()`, `resolve(pal)`. A float "index" buffer at the
    output resolution: noise plus blobs, each blob the kernel as a bilinear surface over its 8x8 pixel centres at its
    exact (unfloored) top-left, then looked up in the palette with linear interpolation.
- `src/okean/text.h`: `okean::Printer` (`ctx.shared<okean::Printer>()`): the shared printer's captions
  (`drawCaption(c, r, pal)`) and lines of der piipo's font (`drawLine`). Draw them after `resolve`, with the palette as
  shown (fades included).
- `src/exe.h`: `c.exe->u8/u16/s16/u32/s32/span/bytes/cstr(addr)` by code32 address.

## Time

`render(c, r)` gets `r` = a fractional record number of `okean/re/full.frames`: record k is the picture after refresh
k (the original's monitor shows it during refresh k + 1). The cue of a part runs from the refresh main calls it
(`cue->enter`) to its exit refresh (`cue->end`); the framework shows the set-up records (`enter` .. `start - 1`) black
or as the previous part's last frame, so a scene only draws `r >= c.start`.

| clock | in the remake |
|---|---|
| callbacks done before the draw that record r shows (n = r - F, notes-core 4) | `c.callbacks(r)`: evaluate the motion continuously at this n; state the original steps per callback with `TickStepper` (replay from the part's start, deterministic) |
| the palette's state: n + 1 callbacks in the title, objects, morph and landscape parts (computed after the retrace wait), n in the others | blend the palette state between whole callbacks by the fraction of r |
| the music position a callback sees (callback c = 1, 2, ... in refresh F + c - 1 at line 502) | `c.positionAtCallback(c)`, `c.callbackTime(c)` |
| the music position the draw of record F + n reads | `c.positionAtDraw(n)` |
| noise and anything else the original redraws per drawn frame | a new pattern per refresh: `canvas.noise(c, levels, uint32_t(std::floor(r)))` |

The capture drops a refresh now and then (notes-core 4, "Dropped frames"): the remake draws every refresh, n = r - F.

## Fidelity rules

- Keep: timing, motion paths, camera, composition, colours, the noise's levels and look, the order and number of
  lines, and the visible quirks that belong to the look (each notes file lists them; vertices behind the camera
  thrown far out and drawn as spokes are part of the look). Say in your report which quirks you kept and dropped.
- Evaluate exactly instead of copying low-resolution shortcuts: the sine table becomes `Sine::s` / `c` (keep its
  amplitude 1022 where a product is shifted by 10: `kAmp = 1022 / 1024`); integer projections, 8.8 steps and
  truncations become floats; blob positions are not floored; palette fades are continuous (palette x fade / 64
  without truncation); a mask or height map read per pixel is read with the same layout but evaluated at the
  output resolution where it is a picture of a shape.
- Drop artefacts that only exist because of 320x200 / 8 bit: sums past 255 that wrap and carry into the next pixel,
  the font's grey 254 reached by a sum, blobs clipped whole at x 311 / y 191 (clip at the real screen edges), the
  line drawers' 8-bit step counters for off-screen lines, 1-pixel truncation steps.
- Nothing from the original may be copied into the source: read every table, palette, mesh, map, string and kernel
  from the exe by address at run time. Code constants (multipliers, loop counts, addresses, offsets, increments) may
  appear in the source, and so may values measured from the capture. Immediates that a set-up routine patches into
  code: read the original immediate from the file and apply the same logic.

## Checking your work

- Build in your own folder: `cmake -S okean/remake -B okean/remake/build-<you> -G Ninja && cmake --build
  okean/remake/build-<you>` (inside the toolbox: `toolbox run -c acme303 ...`); `-DOKEAN_PARTS="title;objects"` builds
  only some parts. Zero warnings with g++ `-Wall`. Delete the folder when you are done.
- `okean-remake --refshots R1,R2,... --shotdir DIR --shotsize 960x720` renders records; `python3 tools/compare.py
  OUT.jpg --shots DIR --score R1 R2 ...` puts them next to the capture (`--crop X,Y,W,H` to zoom). Look at the
  images: composition, motion phase, brightness of the lines and of the noise, colours, fades, captions. The score
  is dominated by the noise (random per pixel); compare the lines by eye and with crops.
- Seeking: render the same records in different orders (e.g. `--refshots 3000,2400,3000`) and check the PNGs are
  identical.
- Speed: `okean-remake --bench T0,T1 --shotsize 2560x1920` (music time in seconds) over your part: the target is
  60+ fps (under 16 ms per frame, median well under) on a mid-range GPU; the machine's GPU here is integrated,
  so aim lower. Start-up (all parts' `init`) must stay well under 2 s in total.
- Do not edit shared files (`demo.*`, `okean/*`, `schedule.*`, `timeline.cpp`, `main.cpp`, `music.*`,
  `CMakeLists.txt`) or another agent's scenes. If you need a change in shared code, put a helper in your own scene
  file and describe the change you would make in your report. Do not commit; do not run git commands that change
  anything.

## The scenes

| scene | part | notes |
|---|---|---|
| `title` | 0x2c26f | notes-title-objects.md, title |
| `objects` | 0x2e779 | notes-title-objects.md, objects |
| `web` | 0x3554b | notes-landscapes.md, part 3 |
| `morph` | 0x367e5 | notes-morph-star.md 2 (done: `src/scenes/morph.cpp`) |
| `landscape` | 0x38520 (with the set-up 0x38469) | notes-landscapes.md, part 5 |
| `star` | 0x3c8da | notes-morph-star.md 3 |
| `sheet` | 0x5334f | notes-sheet.md |
| `end` | 0x4d370 | notes-core.md 7 |

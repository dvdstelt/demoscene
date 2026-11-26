# Paper by Psychic Link (Wired '96) — high resolution remake

A C++/OpenGL re-implementation of *Paper*, the 64k intro by Statix of Psychic Link with music by Vic of Acme,
released at Wired '96. It runs on Linux and Windows at any resolution in true colour, instead of 320×200 in
256 colours, with the original's motion, timing and music, and with high-resolution redraws of Statix's pictures on
the logo page: the pencil sketch and the inked logo of the word "Paper", and the text "present the" / "intro" (or
the original pixels, `--original-art`).

The remake reads everything from the original `PAPER.EXE` (63,845 bytes), in the original release in [`paper/original/`](../original/).

| what | where in the exe | how it is used |
|------|------------------|----------------|
| the program image | PKLITE 1.15 compressed | unpacked at start-up by `src/pklite.cpp` and checked against the known release |
| tune + 8 samples | pattern stream at 0x7a9b, raw 8-bit samples after it | played by a port of the intro's own player through a model of the GUS GF1 |
| font, logo art, ink image, cloud texture source, palettes | static data (0x18ed7, 0x1bf03, 0x196c3, 0x16ca3, 0x169a3, …) | glyphs rebuilt as distance fields; the logo page's sketch, text and finished ink redrawn (`art/`), the ink image's map kept for the fill's timing; textures regenerated at high resolution |
| 3D objects, camera keys, sketch strokes, handwriting | 0x174a3, 0x1e353, 0x1878b, 0x466f, 0x5073, 0x18a2f | drawn on the GPU with a depth buffer, 4× MSAA and resolution-scaled pencil lines |
| the random generator's seed | 0x9a0 | replayed exactly, so the jitter, oscillators, canyon texture and flock are the ones the original makes |

All addresses are offsets in the intro's 32-bit segment; the notes in [`../re/`](../re/) explain every one of them.

## Running

The program looks for `PAPER.EXE` next to itself, in its parent folders, and in an `original` folder in any of
those, so `paper/original/PAPER.EXE` is found from the build folder. Or pass `--exe FILE`. The redraws are found
the same way in an `art` folder (`paper/remake/art`); or pass `--art DIR`.

```
paper-remake [--window] [--size 1280x960] [--start SECONDS | --tick T] [--ss 2] [--loop] [--info] [--original-art]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↓/↑` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples. `--info` shows the tick and part in the title bar.
`--original-art` shows the original pixels of the logo page instead of the redraws: the sketch, the text drawn from
the intro's font, the ink image.

Offline: `--wav out.wav` writes the soundtrack; `--shots 2000,4500 --shotdir DIR --shotsize WxH` writes the
frames at those ticks to PNG; `--video out.mp4 --from T0 --to T1 [--fps 60] [--shotsize WxH]` renders a tick
range with sound through `ffmpeg`; `--bench T0,T1 --shotsize WxH` reports the GPU time per frame.

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20 and SDL3. If SDL3 is not installed, CMake downloads and builds
it (3.2.24); `-DPAPER_FETCH_DEPS=ON` always does that. No other libraries are used (`stb_image` and
`stb_image_write` are included in `third_party`).

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/paper-remake --window
```

On Fedora Atomic or without root, the same works inside a toolbox.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

This gives `build-win/paper-remake.exe` and `SDL3.dll`. Copy both anywhere, with `PAPER.EXE` next to them or in an
`original` folder next to them, and the `art` folder next to them. (The program is called `paper-remake` so it can never overwrite `PAPER.EXE`.)

**Windows (Visual Studio 2022)**: `cmake -S . -B build` then `cmake --build build --config Release` (SDL3 is
built from source). Only the MinGW build has been tried.

## How it is put together

- `src/main.cpp` options, playback loop, shots / video / bench; `src/demo.*` the framework (parts, the
  tick timeline, render-target pool); `src/timeline.cpp` the running order in 70 Hz ticks
- `src/exe.*`, `src/pklite.cpp` reading `PAPER.EXE`
- `src/music.*` the intro's tracker player (0x1ed6) and the GF1 model, rendered at load time (0.1 s); its
  audible position is the demo's clock
- `src/paper/` the core library: the random generator and its call positions (`common.h`), palettes and the
  global fade (`palette`, `fade`), font, cloud texture and paper grain, oscillators and the warp, blur, the
  page peel (`core2d.h` maps these), the redrawn pictures (`art`), and the 3D engine: exact 16.16 matrices,
  object loaders, the transform / light / draw routines on the GPU and the pencil lines (`math3d`, `obj3d`,
  `render3d`, `pencil`)
- `src/scenes/` one port per part: `title` (title and "the end") and `title_closing`, `sketch`, `scene3d`
  (coloured scene and the first page peel), `logo`, `teal` and `flock` with the shared `planes_canyon`,
  `greetings`
- `art/` the redrawn sketch, text and ink of the logo page and how they were made (`art/README.md`);
  `tools/align_art.py` makes the picture to upload, fits a redraw onto the original and writes the comparison sheet
- `tools/compare.py` puts remake frames next to the original's capture; `tools/test_pklite.cpp` and
  `tools/test_music.cpp` check the unpacker and the player against the reverse-engineering references
- `docs/BRIEF.md` the conventions every part follows (the brief the porting agents worked from); `../re/` the
  decompiled notes (pseudo-C with every constant), the emulator harness and the scripts that make the reference
  capture

## Fidelity

Every routine was decompiled, modelled in Python and checked against the original running in an emulator, then
ported and compared frame by frame with a capture of the original made in that emulator (see
[`../re/README.md`](../re/README.md) to make it yourself).

- **Timing.** The original advances almost everything in its 70.0018 Hz timer interrupt. The remake steps the
  same per-tick state exactly, from the start of each part, and interpolates between ticks, so motion is smooth
  at any refresh rate and seeking is exact. The clock is the audio position.
- **Music.** The tune is played by the intro's own player logic, driving a model of the GUS GF1 (volume ramps
  start from the voice's current volume, as on a real card). Its register writes and per-tick counter match the
  reference player exactly.
- **Randomness.** The intro's lagged-Fibonacci generator is replayed from its seed in the exe, with the call
  count at the start of every generator measured in the capture, so the letter jitter, the warps, the teal
  canyon texture and the flock are the original's.
- **The flock.** This is the only part that advances per drawn frame, so its speed depended on the PC. The
  remake steps it at a fixed 34 frames per second, the speed of the capture. (On fast machines the original
  skips the part altogether through a race on the palette fade; the remake always plays it.)
- **Sharper.** Evaluated per pixel instead of the original's 8×8-pixel interpolation grids (warps, canyon);
  colours in floating point instead of 32-step palette ramps; Gaussians instead of repeated box blurs;
  distance-field text; anti-aliased polygons and resolution-scaled pencil strokes; the ink fill grows smoothly
  along the original's fronts; the page peel is a continuous fold instead of 25 strips.
- **Pictures.** The pencil sketch, the text and the finished ink of the logo page are shown as faithful
  high-resolution redraws by default (see below); `--original-art` shows the original pixels, interpolated
  smoothly, and the text from the font.
- **Kept on purpose.** The teal plane's shading flipping while its light vector overflows, the flock camera's
  odd motion (a bug that subtracts all three camera coordinates from one axis), the streaky decay of the
  closing "1996" glow (made by rounding in 150 blur passes, so it uses the original's integer blur, upscaled),
  the soft title letters, the handwriting without sag.

Known differences:

- The flock's planes drift up to about 8 steps from the capture late in the part: the capture's frame rate
  varied between 33 and 37 fps, the remake's step rate is fixed.
- The greetings surface is smoother than the original, whose fine streaks came from 8-bit texture sampling.
- The pencil outlines on the small flock planes are a little heavier than the original's.
- Parts are aligned with the capture to within one tick (14 ms).
- The redrawn sketch has cleaner lines of one weight than the original's pencil pixels, and its soft grey strokes
  are smudges along one line where the original has bands of light pixels (`art/README.md`).
- The text on the logo page is sharp: the original blurs its 1-bit letters once (sigma 1 pixel, its anti-aliasing at
  320x200), the remake draws the redrawn letters anti-aliased at the output resolution. Their edges are therefore a
  little lighter than the original's, where the letter's blur darkens the paper around it; the shadow is built as
  the original builds it and matches it.
- The redrawn ink is a little glossier than the original's: white highlights where the original's lightest grey is
  0x41, and a thin darker outline. The ink image's holes (pixels without ink inside the letters, where the original
  shows the paper) are ink. After the fill the sketch is hidden within 1 original pixel of the ink, so of the
  original's 50 visible pencil pixels only the "stx" signature stays.
- With `--original-art`, the ink's lightest greys (the art's anti-aliasing and a few highlights) let the page show
  through, with a faint trace of the interpolated sketch under them; the original draws those pixels in light grey
  ink over the sketch. With the redraw the sketch is hidden there (see below).

## The redrawn pictures on the logo page

The logo part (0x5623 / 0x572f) has two hand-made 224-pixel-wide images: the pencil sketch of "Paper" (0x1bf03,
224x83, 4-bit darkness) and the ink image that the flood fill colours (0x196c3, 224x92, 4-bit palette indices
0x41..0x4f, 0 = no ink), and it draws "present the" and "intro" with the intro's 1-bit font. They are drawn only by
`src/scenes/logo.cpp`; the first page peel (`scene3d.cpp`) and the teal part show the page through
`ctx.renderPart("logo")`. The paper grain is computed.

- **The sketch** is a picture shown as it is, so it is redrawn: `art/sketch.png` is an AI redraw made with ChatGPT's
  image generation from the original pixels and fitted onto them ([`art/README.md`](art/README.md) has the prompt,
  the attempts and the fit). It is a grey shade map over the sketch's frame (grey g = page value v = 63 g / 255) that
  the page pass reads trilinearly and shades with the paper grain exactly as it shades the original's
  v = 63 - 4 n. The original's ink covers its whole sketch except 50 pixels (the "stx" signature and single pixels
  at the ink's edge), so with the redraw the page pass also hides the sketch wherever the fill has reached an ink
  pixel of any colour, the light greys included (channel G of the per-frame fill texture), with an anti-aliased
  edge on the smooth contour of the ink pixels. `--original-art`, or a missing `art` folder, draws the original's
  darkness levels as before (Catmull-Rom, clamped to the nearest texels' range).
- **The text** is redrawn as a sharp coverage mask: `art/text.png` is an AI redraw made with ChatGPT's image
  generation from the 1-bit sharp copy in 0x5623's 320x80 text buffer, fitted onto it (every letter within a
  fraction of an original pixel). The original blurs its sharp copy once (sigma 1 pixel), which at 320x200 is
  anti-aliasing but at 2560x1920 a blur about 8 pixels wide, and its 32-pixel glyphs are too coarse to scale
  cleanly. The page pass draws the letters at the mask's 0.5 contour, anti-aliased at the output resolution, with
  no blur. The shadow is built as the original builds it, from the same mask: drawn at (+4, +3) at half strength
  into the text buffer target and blurred as the original's five blur calls (sigma sqrt(5) original pixels);
  T = min(1, shadow + letters), then the page value 63 - 63.75 T and the paper grain as before.
- **The ink** is redrawn as the finished picture: `art/ink.png` is an AI redraw of the inked logo (colour with
  alpha), fitted onto the ink image and given its body colour 0x4b. The fill only decides when each pixel appears,
  so the remake keeps that from the original: it runs 0x572f's fill on the original's map for every ink pixel's
  arrival tick R, extends R to a field of 4 x 4 samples per original pixel (each sample takes the region, one per
  seed, of its nearest original ink pixel and the Gaussian-weighted mean, sigma 0.6 px, of that region's ticks
  around it, so neighbouring letters never mix) and shows the redraw where R < tick + 0.5 with an anti-aliased
  front. At whole ticks that is the original's pixel set {R <= n}; between ticks the front moves on smoothly along
  the original's fronts. The page pass also hides the sketch within 1 original pixel of the ink wherever the ink
  has arrived: every stroke of the sketch redraw except the signature lies that close to the ink redraw, and in
  the original the ink pixels cover all but 50 pencil pixels.

With `--original-art` (or without the files) the text is drawn from the font as before (the core font's anti-aliased
outlines into a 320x80 intensity buffer, the shadow and the sharp copy blurred as the original blurs them) and the
ink image is upscaled; these frames are the same as before the text and ink redraws.

- **The ink image** (with `--original-art`): its values are palette indices that 0x572f writes pixel by pixel as
  the fill arrives. The remake upscales it at start-up from the original's values by splitting it into what the
  indices are: the outline (ink or not) as the smooth 0.5 contour of an interpolated coverage field in which the
  light greys count as partial coverage (they are the art's anti-aliasing against the paper), so the edge lies
  where the drawn edge is; the body and the greys as interpolated shading; and the red edges 0x4c..0x4f as a class
  of their own, shown where their interpolated indicator is the strongest. A literal per-index upscale (each
  index's smoothed indicator field, the strongest wins, tried with Catmull-Rom, B-spline and Gaussian kernels)
  gives smooth boundaries between the indices but turns the one-pixel grey anti-aliasing along every edge, a
  different grey on every pixel of a diagonal, into a chain of round grey blobs; it was not used. This upscale is
  still blobby at high resolutions (smooth contours of 1-pixel steps, single-pixel highlights and red specks),
  which is why the default mode shows the redraw.

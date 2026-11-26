# 303 by Acme (X'97) — high resolution remake

A C++/OpenGL re-implementation of Acme's *303*, the PC demo that won the X'97 demo competition
(code: Statix, music and vocals: Vic, graphics/design: Aap). It runs on Linux and Windows at any
resolution in true colour, instead of 320×200 in 256 colours, and it plays the original soundtrack.

The remake loads the original data files directly, from the original release in [`303/original/`](../original/).

| file | contents | how it is used |
|------|----------|----------------|
| `MUSTARD.PAK` | 45 LZW-compressed files: palettised textures, fonts, 3D objects, fade tables | textures upscaled on the GPU, fonts turned into signed distance fields, meshes rendered with smooth normals |
| `GARLIC.PAK` | 30-channel PolyTracker (PTM) module | played with libopenmpt (8-tap sinc interpolation), the whole mix attenuated by 4.9 dB so its loudest moments (+3.9 dBFS) do not clip |
| `KETCHUP.PAK` | MPEG-1 layer II stream with Vic's vocals and drum loops | decoded with minimp3 and put back into the module's sample slots 48–90, as Statix's MIDAS hack did |
| `ALEX/HARL/VS/PARANOID.LZW` | video clips (custom 4×4 DCT codec, YIQ) | decoded in floating point and converted with the original YIQ matrix, with no 16-level dithering |
| `303.EXE` | the program (Watcom C/C++, DOS/4GW LE, 414,153 bytes) | loaded with its fixups at start-up and checked by size and hash; the lyric and song tables, the credit layout, the web and icon tables, the jitter table and the marching-cubes case table are read from it by address |

## Running

The program looks for the original files next to itself, in its parent folders, and in an `original` folder in
any of those, so `303/original/` is found from the build folder. Or pass `--data DIR`.

```
acme303 [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↑/↓` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples, for supersampled antialiasing.

Analysis helpers: `--wav out.wav` renders the soundtrack, and `--shots 10,20.5 --shotdir DIR --shotsize WxH`
writes frames at given music times to PNG. `--video out.mp4 [--from S] [--to S] [--fps 60] [--shotsize WxH] [--vcodec "ffmpeg args"]`
renders the demo with its sound through `ffmpeg`, frame by frame (see `docs/videos.md` in the repository).
`--bench T0,T1 --shotsize WxH` renders music times T0..T1 at 60 frames per second offscreen and reports the time
per frame (median, 95th percentile and the five slowest frames, including waits for a restored video frame).

## Redrawn pictures and restored video

The remake can show high-resolution redraws of MUSTARD.PAK's pictures and restored frames of the video clips from
an `art` folder: next to the program or in its parents, or `remake/art` there (so `303/remake/art` is found from
the build folder), or `--art DIR`. `--original-art` shows the original pixels and frames. Without an `art` folder,
or with an empty one, the demo looks exactly as with `--original-art`.

- `art/<name>.png` (or `.jpg`): a redraw of `<name>.raw` (`final.png`, `tex3d.png`), RGB or RGBA, any resolution,
  covering the file's whole pixel grid; a stacked file (`tex3d`, `bd2`, `blobby`, `tunnel`) is one picture with
  its tiles stacked as in the file. Files without a palette (`final`, `opening1`, `303b`, `credits`) are read as
  their luminance. It replaces the picture wherever a part reads it as colours (`Ctx::tex`); parts that read
  palette indices (the alex and vic photos, the fonts, `wire`, `phong1`) keep the original and say so in the code,
  and `Ctx::redraw(name)` gives them the redraw to convert. The typo and wire blobs parts read `bd2` that way:
  where the original looks the picture's index up in wirecut.fad, they blend the redraw's colour towards white by
  the same amount in true colour. Pictures read at an explicit mip level inside per-pixel loops (`final`, `303b`,
  `credits`, the metaballs' env map) use the level a larger redraw needs, and level 0 for the original.
- `art/video/<CLIP>/NNNN.png` (or `.jpg`): a restored frame of `<CLIP>.LZW` (ALEX, HARL, VS, PARANOID), one file per
  original frame numbered from `0000`, any resolution with the original's 5:3 shape (160x96). The parts draw it
  through the same mapping, ripple and fades as the decoded frame; frames without a file keep the decoded one.
  Frames are decoded on worker threads two ahead of the one shown, and the first three frames of every clip are
  kept ready, so a clip starts without waiting (also PARANOID when the face floor starts it again). After a seek
  into a clip the first frame waits for its decode (about 7 ms at 1280x768).

`art/` holds AI redraws (ChatGPT image generation, from the original pixels) of 13 of the 15 pictures worth a
redraw, 12 files, 7.1 MB: the hexagon plates and the gold ball (`tex3d`, tiles 1 and 0; the generated tiles 2–5
are the original scaled up), the intro's threads (`neuron`), "statix vic acme" (`opening1`) and the 303 strip
(`303b`), the icons part's lettering, icons and MC-303 photo (`final`), the credit words (`credits`), the env maps
(`phong`, `sphere`, `tunnel` tile 1), the rings' wall (`tunnel` tile 0), the blobby background (`blobby` tile 0;
the mask, tile 1, is the original), the paranoid texture (`weird`) and the typo pictures (`bd2`). The photos of
Alex and Vic keep their original pixels (photos of real people), and so do the gradient stripes (`wipeout`,
`tex3d` tile 5). Textures that wrap join at their edges as the originals do (seamless, except `weird`, whose
original edges do not match either), and every line, icon, glyph and word the exe addresses by position is at its
pixels.

`art/video/` holds the four clips restored with SeedVR2, an AI video restoration model, at 1280x768: the frames the
parts show plus a few (HARL 0–121, VS 0–60, ALEX 0–26, PARANOID 0–30), 241 JPEGs, 32 MB.
[`art/README.md`](art/README.md) says how the redraws and the restored clips were made, with which prompts and
settings, how well each redraw fits the original, and what to look at; `tools/align_art.py` makes the uploads,
fits the redraws and writes them here.

[`../re/notes-pictures.md`](../re/notes-pictures.md) lists every picture and clip, how the parts read them, which
ones a redraw can replace and what it has to keep (tileable textures, environment-map discs, element positions).

## Building

Requirements: a C++20 compiler, CMake ≥ 3.20, SDL3, libopenmpt. If SDL3 or libopenmpt is not installed,
CMake downloads and builds them (SDL 3.2.24, libopenmpt 0.8.8 pinned by hash). Pass
`-DACME303_FETCH_DEPS=ON` to always do that.

**Linux (Fedora)**

```
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel libopenmpt-devel
cmake -S . -B build -G Ninja && cmake --build build
./build/acme303 --window
```

On Fedora Atomic or without root, the same works inside a toolbox (`toolbox create acme303`, then install the
packages above with `sudo dnf` inside it).

**Windows (Visual Studio 2022)**

```
cmake -S . -B build
cmake --build build --config Release
```

SDL3 and libopenmpt are built from source automatically, and `SDL3.dll` is copied next to `acme303.exe`.
With vcpkg (`vcpkg install sdl3 libopenmpt`) the installed packages are used instead.

**Windows from Linux (MinGW cross-compile, Fedora)**

```
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-winpthreads-static
mingw64-cmake -S . -B build-win -G Ninja && cmake --build build-win
```

## How it is put together

- `src/exe.*`: loads `303.EXE` (LE objects and fixups) and reads it by address
- `src/assets.*`: the LZW variant (Nelson's LZW15V), the PAK directory, the video codec, the three mesh formats
- `src/art.*`: the art folder (redrawn pictures, restored video frames), decoded with stb_image
- `src/audio.*`: rebuilds the PTM with the vocal samples, playback and timing through SDL3
- `src/exefx.*`: building blocks ported from the executable and shared by several parts: the palette system
  (fades, flashes, negative strobes, brightness kicks), Watcom `rand()`, the sphere-ray grid, soft additive
  lines, the lyric renderer, and the inverted thumbnail inset
- `src/scenes/*.cpp`: one port per part. `intro`, `aap`, `blobby`; `mid_face` (rings tunnel, flowers, relief
  face); `mid_blobs` (face sheet, typo, metaballs, wire blobs); `mid_spiral` (photo and credit parts, spiral,
  tentacle star, plasma); `late` (harl, web, paranoid, face floor, dance, icons, ending)
- `src/timeline.cpp`: running order; `src/fx.*`, `src/demo.*`, `src/render.*`: engine and shared effects
- `tools/`: the reverse-engineering helpers (LE/DOS4GW loader with fixups, capstone disassembly helper,
  LZW decoder, video frame dumper, module row timing), `video_art.py`, which makes the SeedVR2 uploads and
  turns the restored clips into `art/video/`, and `align_art.py`, which makes the uploads for the picture redraws,
  fits each redraw onto the original and writes it to `art/`
- [`../re/`](../re/): the decompiled notes for every part (pseudo-C with all constants), the briefs the agents
  worked from, and the comparison scripts

## Fidelity

Everything is ported from the executable, routine by routine, then checked side by side against a capture of
the original running on a Pentium MMX. The remake evaluates per pixel what the original computed on an
8x8-pixel grid, keeps colours in floating point instead of 256 palette entries, and draws lines, contours and
edges anti-aliased, but the algorithms, constants, motion and timing are the original's.

The original schedules everything on the module position (`[0x9b844]` = 2 × (order × 64 + row) − 2, `musicPos()`
in `fx.cpp`) and animates in 70 Hz vertical-retrace callbacks. The remake replays those callbacks from the start
of each part every frame, so seeking is exact, and blends the state towards the next tick (camera, motion, palette
fades), so the picture moves smoothly at any refresh rate instead of stepping at 70 Hz. Sudden changes, such as
flashes, still happen on their tick. In the capture the picture leads the sound by about 0.5 s at the start,
shrinking to zero by about 2 minutes; `captureLead()` holds the measured offsets so that effects line up with the
capture.

What is not exact, and why:

- **Randomness.** Many choices come from `rand()` (Aap's compositions, the blobby mode switches, lyric jumps
  and inversions, web tiles, blob wobble, icon layout). The sequence depends on the machine's frame rate, so
  every run of the original differed. The remake uses fixed seeds, and scripts the realisation seen in the
  capture where it matters (mode switches, lyric and word positions, ball paths, which were searched for).
- **Frame rate.** The original applies feedback (trails, ghosting, spin) per rendered frame and never waits
  for vsync; the remake runs those at fixed rates estimated from the capture (35–140 fps depending on the part).
- **Palette effects.** Fade tables snap to the nearest palette colour. The remake lerps in true colour and only
  reproduces the hue shifts where they are visible (the purple lyric smears in the blobby part).
- **Known differences.** The plasma section's camera roll and the look of its balls in places. The web
  words, which the code colours red and violet, but the capture shows in blue and yellow (the remake follows
  the capture).
- **Restored video.** By default the clips are AI restorations (SeedVR2) of the 160x96 frames, not the frames
  themselves. They keep each frame's content, position, colours and timing, but the model invents fine detail the
  original does not have: faces, hands and textures are plausible guesses, not the real people's faces
  (`art/README.md`, "What to look at"). `--original-art` shows the decoded frames.

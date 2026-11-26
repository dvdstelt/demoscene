# 303 by Acme (X'97) — high resolution remake, first version

> **This is the first version**, kept as a record. It was rebuilt by eye from a video capture of the original,
> after decoding the data files exactly. The effects are close reinterpretations, not the original's algorithms.
> The current remake, ported from the executable routine by routine, is in [`../remake/`](../remake/).
> [`../docs/making-of.md`](../docs/making-of.md) describes both.

A C++/OpenGL re-implementation of Acme's *303*, the PC demo that won the X'97 demo competition
(code: Statix, music and vocals: Vic, graphics/design: Aap). It runs on Linux and Windows at any
resolution in true colour, instead of 320×200 in 256 colours, and it plays the original soundtrack.

The remake loads the original data files directly, from the original release in [`303/original/`](../original/).

| file | contents | how it is used |
|------|----------|----------------|
| `MUSTARD.PAK` | 45 LZW-compressed files: palettised textures, fonts, 3D objects, fade tables | textures upscaled on the GPU, fonts turned into signed distance fields, meshes rendered with smooth normals |
| `GARLIC.PAK` | 30-channel PolyTracker (PTM) module | played with libopenmpt (8-tap sinc interpolation) |
| `KETCHUP.PAK` | MPEG-1 layer II stream with Vic's vocals and drum loops | decoded with minimp3 and put back into the module's sample slots 48–90, as Statix's MIDAS hack did |
| `ALEX/HARL/VS/PARANOID.LZW` | video clips (custom 4×4 DCT codec, YIQ) | decoded in floating point and converted with the original YIQ matrix, with no 16-level dithering |
| `303.EXE` | the program (Watcom C/C++, DOS/4GW LE, 414,153 bytes) | loaded with its fixups at start-up and checked by size and hash; the lyric words are read from it by address |

## Running

The program looks for the original files next to itself, in its parent folders, and in an `original` folder in
any of those, so `303/original/` is found from the build folder. Or pass `--data DIR`.

```
acme303 [--window] [--size 1280x960] [--start SECONDS] [--ss 2] [--loop]
```

Keys: `Esc` quit, `Space` pause, `←/→` seek ±5 s, `↑/↓` seek ±30 s, `F`/`F11` toggle fullscreen.
`--ss 2` renders at twice the resolution and downsamples, for supersampled antialiasing.

Analysis helpers: `--wav out.wav` renders the soundtrack, and `--shots 10,20.5 --shotdir DIR --shotsize WxH`
writes frames at given music times to PNG.

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
- `src/assets.*` — the LZW variant (Nelson's LZW15V), the PAK directory, the video codec, the three mesh formats
- `src/audio.*` — rebuilds the PTM with the vocal samples, playback and timing through SDL3
- `src/demo.*`, `src/render.*`, `src/fx.*` — resources, text, the shared effects (warps, radial blur, ray-traced hex corridor with metaballs and the plasma sphere, environment-mapped meshes)
- `src/scenes/*.cpp` — the 22 parts; `src/timeline.cpp` — running order and lyrics
- `tools/` — the reverse-engineering helpers: an LE/DOS4GW loader with fixups, a capstone-based disassembly helper, the LZW decoder in Python, and a video frame dumper

The timeline was measured on a capture of the original running on a Pentium MMX. That capture plays 0.126%
slow and starts 1.4 s into the music; the soundtrack was aligned to it by onset cross-correlation. `V()` in `fx.h` converts.

## Fidelity notes

The file formats, video codec, palette maths, vocal sample mapping and music are recovered exactly from the
executable. The effects were rebuilt from the capture, not disassembled routine by routine. Some parts are
close reinterpretations rather than exact replicas: the compositions in Aap's stripe section, the Julia-set
"flowers", the words in the scribble-web section, the spiral and tentacle motion, and the plasma sphere.

# Notice

## The original productions

This repository contains remakes of demoscene productions made by other people. The originals, including their
code, graphics, music, samples, vocals and text, are the work and property of their authors. This repository does
not include them, and the license in [LICENSE.md](LICENSE.md) does not cover them.

- **303** by Acme (X'97, 1997): code Statix, music and vocals Vic, graphics and design Aap, with Harlequin as the
  breakdancer.
- **Paper** by Psychic Link (Wired '96, 1996): code, graphics and design Statix, music Vic of Acme. It uses PMODE by
  Tran.
- **JIZZ** by The Black Lotus (Wired '97, 1997): code, design and graphics Jace, music Probe, vector engine and
  metaballs Nix, VESA code Ballein, "jizz" logo Sjaak of Trepaan. It uses the DOS32 extender by Adam Seychell and
  MXMPlay by Niklas Beisert. Its documentation allows copying it for non-commercial use only, in its complete and
  original form; this repository does not include or change it.

The remakes read the original files at run time. You need your own copy of each original release:
`scripts/fetch-originals.sh` (or `fetch-originals.ps1` on Windows) downloads it from scene.org. The source code
here is a re-implementation written from an analysis of the originals. Data such as images, music, meshes, tables
and text is never copied into it.

This project is not affiliated with or endorsed by the original authors. It is a non-commercial tribute: nobody
may sell it, charge for it, or make money from it in any other way. If you are one of the original authors and
want something changed or removed, please open an issue.

## Third-party code included here

| file | license |
|------|---------|
| `*/third_party/stb_image_write.h` (Sean Barrett) | public domain or MIT |
| `303/*/third_party/minimp3.h` (lieff) | CC0 1.0 |

Downloaded during the build when not installed: SDL3 (zlib license) and libopenmpt (BSD-3-Clause).

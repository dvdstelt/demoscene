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
- **DOPE** by Complex (The Gathering '95, 1995): code jmagic, graphics and music jugi, music replay routine Phantom
  of Sonic PC (GUS part adapted by jmagic), routine boxes sketched by jugi and pixelled by Reward, Media Labs logo
  Reward; 3D objects by jugi and M. Hakulinen. It uses PMODE by Tran.
- **STARS: Wonders of the World** by NoooN (1995): music GROO, 3D motion and vertices BARTi, system and special
  effects KARL, script and pixels RA, sound code LCA. It uses PMODE by Tran.
- **clone meets clone** by Acme and Fudge (Takeover '98, 1998): design Aap and Echo, effects Echo, music Simon,
  graphics Aap; additional credits Assa (graphics help and network solutions), Craft (texture mapper, grid
  filler), Dune (original radial blur), Submissive (VESA library) and Unreal (segment BSS). It uses PMODE/W by
  Daredevil and Tran and MXMPlay by Pascal (Niklas Beisert).
- **Inside** by CNCD (The Gathering '96, 1996): code Wili, Jouni and Juliet, graphics Destop, music Yolk and
  Legend, additional graphics Santtu. It uses DSMI by Otto Chrons (Virtual Visions), PMODE/W by Thomas Pytel and
  the SurRender 3D engine by Hybrid.

The remakes read the original files at run time. You need your own copy of each original release:
`scripts/fetch-originals.sh` (or `fetch-originals.ps1` on Windows) downloads it from scene.org. The source code
here is a re-implementation written from an analysis of the originals. Data such as images, music, meshes, tables
and text is never copied into it.

The remakes also include redrawn versions of the originals' hand-drawn pictures (in each `<demo>/remake/art/`):
high-resolution AI redraws made from the original pictures, faithful to them, and shown instead of the original
pixels unless `--original-art` is given. 303's remake also includes its video clips restored at high resolution with
an AI video model (`303/remake/art/video/`), shown the same way. They are derived from the original artists' work and
footage, which stay theirs, and fall under the same non-commercial terms as everything else here. Each folder's
README names the artist of every picture.

This project is not affiliated with or endorsed by the original authors. It is a non-commercial tribute: nobody
may sell it, charge for it, or make money from it in any other way. If you are one of the original authors and
want something changed or removed, please open an issue.

## Third-party code included here

| file | license |
|------|---------|
| `*/third_party/stb_image_write.h`, `*/third_party/stb_image.h` (Sean Barrett) | public domain or MIT |
| `303/*/third_party/minimp3.h` (lieff) | CC0 1.0 |

Downloaded during the build when not installed: SDL3 (zlib license) and libopenmpt (BSD-3-Clause).

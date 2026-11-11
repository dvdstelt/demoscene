# Texture generator models (scope "textures", see ../../notes-textures.md)

Run inside the toolbox from `jizz/re`:

| file | what it does |
|---|---|
| `texgen.py` | Python model of the driver 0x12c50 and the script interpreter 0x12e72. `python3 models/textures/texgen.py` builds all 17 textures (about 8 s) and compares the 16 `/pandora` TGA files with `dump/*.tga` and the 17 final buffers with `snaps/t440.snap`: all byte-identical. |
| `scripts.py` | lists the 17 scripts op by op (address, bytes, decoded arguments); `compact()` gives the one-line form used in the notes. |
| `emu.py` | runs the original interpreter 0x12e72 on one script in unicorn (memory from `snaps/t430.snap`, progress print patched to `ret`, fninit + 24-bit precision as main does) and records B0..B3 before every op. |
| `test_ops.py` | compares `texgen.py` with `emu.py` on a made-up script that covers the ops and argument ranges the 17 scripts do not use (0x0d, 0x16, 0x1e, 0x25, negative scales, amplitude > 127, alpha 0, ...); with `all` also the 17 scripts, op by op on a mismatch. Result: identical. |

The models read the scripts and pictures from `jizz_img.bin` by address; they contain no data from the original.

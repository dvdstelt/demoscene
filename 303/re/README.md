# 303 — reverse-engineering notes

What was learned from `303.EXE` (Watcom C/C++, DOS/4GW LE: code object at 0x10000, data object at 0x50000).
Addresses are linear addresses in that image. [`../docs/making-of.md`](../docs/making-of.md) tells the story.

| file | contents |
|---|---|
| [BRIEF.md](BRIEF.md) | the brief the decompiling agents worked from: conventions, the main script, the clock, the part sequence |
| [notes-aap.md](notes-aap.md) | Aap's stripe compositions (0x30eb1–0x310ca) and blobby (0x31142–0x31304) |
| [notes-mid1.md](notes-mid1.md) | the rings tunnel (0x16198) and the first sub-parts of 0x2e8e4: flowers, face, face box |
| [notes-mid2.md](notes-mid2.md) | 0x2e8e4's middle sub-parts: typo, blobs, wire blobs |
| [notes-mid3.md](notes-mid3.md) | spiral, Vic, star, plasma (0x2f6c9–0x30060) |
| [notes-late.md](notes-late.md) | web, Harlequin, paranoid, face floor, dance, icons, outro (sections 3 and 4 also as [notes-late-part3.md](notes-late-part3.md) and [notes-late-part4.md](notes-late-part4.md)) |
| [notes-pictures.md](notes-pictures.md) | every file in MUSTARD.PAK and the four video clips: what they show, how each part reads them, which ones a redraw or restored frames can replace |
| [PORT.md](PORT.md) | the brief the porting agents worked from: the remake's engine, timing and building blocks |
| `compare.sh`, `grab.sh` | comparison sheets: capture frames next to remake frames. Set `CAPTURE` to a video capture of the original |
| `pos.py`, `rows.txt` | converts the music position counter to music and video time |
| `tables.py` | example fit of a `.gst` blend table |

The disassembly tools live in [`../remake/tools/`](../remake/tools/): `le_load.py` (LE loader with fixups),
`re303.py` (capstone: `func`, `dis`, `calls`, `str`, xrefs; needs `pip install capstone`), `lzw.py` (PAK reader),
`dumpvideo.cpp` (every video frame as PNG, decoded like the remake). They expect the original files in
[`../original/`](../original/). Picture and video dumps go to `dump/` (ignored by Git, see notes-pictures.md).

The notes mention scratch folders (`scratchpad/re/...`) with listings and small simulations. Those were temporary
and are not kept. Everything needed to redo them is in the notes and the tools.

The lyric words are not copied into the notes: they refer to them by table entry (`w0`–`w9` for the table at
0x57dc0, `s0`–`s8` for the entries of the sung table at 0x57f30 that the dance part uses).

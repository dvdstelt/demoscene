# Making of the Reve remake

Reve was the eighth remake, and like the ones before it went straight to the executable. Claude Code agents did the
work, directed by me, Dennis van der Stelt; I supplied the original release and left the decisions to the agents.
[../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working.

## Unpacking

`REVE.EXE` is one file of 999,999 bytes: a small real-mode program followed by an archive. The program is protected
by Infiny Loader (the same loader as STARS, here in 64 layers of a self-decrypting copy loop) and unpacks LCA's own
DOS extender, "FLAT OS V1.0". The archive, "LCA's Packer", holds 36 files compressed with LZW: the demo itself
(`REVE.LFL`, a flat 32-bit program with a relocation table), the module, 18 pictures, palettes and blend tables, and
six 3D worlds with their animations. `re/unpack.py` runs the loaders in unicorn up to the extender's first
instruction, unpacks every file with a Python LZW and checks each against the extender's own decompressor run in
unicorn; the remake has the same LZW in C++ (`remake/src/unpack.cpp`) and checks the file by hash.

## Running it headless

Earlier remakes skipped the extender: the emulator loaded the unpacked program and answered its DPMI or DOS calls
itself. FLAT OS is LCA's own, with its own interface (int 0FFh), so `re/run_reve.py` runs all of `REVE.EXE` instead:
the loaders, the extender and the demo. The extender looks for HIMEM, sees that the processor is in real mode (no
EMM386), and switches to protected mode by itself, with its own descriptor tables, interrupt table and PIC setup;
unicorn emulates those instructions as a processor would. The runner answers only what DOS, the BIOS and HIMEM
would, and fakes the hardware: the interrupt controllers, the timer, the VGA and a Gravis UltraSound. Interrupts
go through the extender's own interrupt table, so its interrupt handling, its calls to real-mode DOS and its file
loading all run as written.

Two things had to be found first. Unicorn's 16-bit mode resets the instruction pointer to 16 bits whenever the
emulation is resumed, which broke as soon as the extender ran 32-bit code above 64 KB; the runner uses unicorn's
32-bit mode started in real mode instead. And the capture crashed after 108 seconds, in the middle of the 3D
engine's face sort: the sort is recursive, the demo's stack is the extender's 2 KB stack, and the extender keeps
four 512-byte interrupt stacks directly below it. A deep sort reaches into them, and an interrupt arriving at that
moment overwrites the sort's return addresses. That can happen on a real PC as well, depending on when the
interrupt comes. The runner moves the interrupt stacks out of the way, which changes nothing the demo computes.

The demo's clock is the screen refresh. Its timer interrupt adjusts its own period until it fires just before every
vertical retrace, waits for the retrace and then runs the current part's per-refresh routine; the main loop draws at
most one frame per refresh. The music runs on the GUS's own timer. The capture holds one record per refresh with the
screen and the palette, the music position, the script entry and the number of frames drawn, plus every GUS register
write and snapshots every 10 seconds to resume from.

## Decompiling

Four agents decompiled the demo in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)), one scope each:
the music, the 3D engine with its nine 3D entries, and the 2D entries in two groups; the core (the clock, the
script, the frame wait) was done first, for the brief. Each wrote a notes file with every routine as pseudo-C with all
its constants, and Python models that reproduce what the original computes, checked against the capture and the
snapshots: the GUS register stream, the engine's frames pixel for pixel, the strobes' flashes record by record.

What came out:

- **A script table runs the demo.** Thirty entries of 20 bytes: an init, a done routine, a routine the timer calls
  once per refresh, a routine the main loop calls once per drawn frame, and the music position at which the entry
  ends. The inits load their files when the entry starts, which takes 1 to 12 refreshes; the music keeps playing.
- **LCA's MOD-Player V3.03** is a later version of the player LCA wrote for NoooN's STARS (V3.02), whose port in
  this repository was the starting point: the same structure, with a different module converter and sample records.
- **The 3D engine** loads its worlds (objects, faces, normals, bounding boxes) and plays their animations as a key
  frame plus 16.16 deltas per refresh. It sorts the objects and each object's faces with a recursive quicksort on the
  depth of a face's first vertex, lights vertices with one formula of the angle and the distance, and draws through
  `GOURAUD.PAL`'s 16 shade levels. The blur that pouët's commenters noticed is one pass through `ANTIALIA.PAL`, a
  256 × 256 table of averages: each pixel becomes the average of itself and its neighbours above, below and to the
  left, in three table lookups.
- **The 2D effects are tables too.** The strobes count down per frame and show the negative through `NEGATIF.PAL`, a
  table of the palette entry nearest to each inverse colour (computed at run time for the later strobes). The twirls
  build 120 ring lists of picture pixels and rotate each ring's colours. The dot spirals blend the screen with a
  zoomed copy of itself through `SPIRBLUR.PAL`. The rotozoom writes a quarter of the pixels per frame and lets the
  rest stand.

## Porting

The framework is Inside's, with the clock in screen refreshes. The shared pieces came first:

- the unpacker (the archive's LZW and the program's 2,937 relocations) and the timeline. Each entry starts on the
  refresh on which the capture shows its first retrace call, and the gap after the previous entry shows what the
  capture shows there: the last frame held, black where the next init clears the screen, or, for two entries whose
  init draws their first frame, that frame;
- the music: the STARS port adapted to V3.03. Its 111,241 GUS register writes, the GUS memory, the GUS timer
  interrupts and the music positions match the capture exactly (`remake/tools/test_music.cpp`);
- the still pictures and the title, and the picture store with its redraws.

Then three agents ported the parts at the same time: the 3D engine and its entries, the first 2D entries (WIZ, the
twirls, the dot spirals) and the others (the strobes, the planes). Each checked its work with side-by-side sheets
against the capture (`remake/tools/compare.py`), with checks that seeking gives the same frames, and with timings at
2560 × 1920. The engine has the original's integer arithmetic as well (`REVE_ENGINE=original`): of 120 capture
records of the 3D parts, 101 match it to the pixel, and most of the others were captured while a frame was being
written.

Some effects needed more than an exact evaluation. WIZ's rotozoom leaves each pixel class from a different frame,
which shows as a fine checker of the two layers; the remake averages the classes at their own times, which halves
the difference to the capture against drawing one time. The dot spirals' feedback blend loses 3/8 of a colour level
per frame on average to the table's truncation; without that loss the echoes stay too long and the frame is a
quarter too bright. The blur's table does not map every colour to itself in flat areas, which turns PART4's beige
letters white; the remake gives each index that colour before blurring.

At the end the whole demo was rendered forwards and backwards (71 frames, byte-identical) and timed at
2560 × 1920: 3.4 ms per frame on the median on an integrated Intel GPU. Start-up takes under 0.4 s.

## The redraws

Reve draws its pictures as they are, and none of them is a photo: the title, the four names in the credits, the
strobed and still pictures, WIZ, the two twirled pictures and the three textures of the planes. They were redrawn
with ChatGPT's image generation from the original pixels, in three conversations (the pictures, the lettering, the
textures) so that related pictures match. `remake/tools/align_art.py` makes the uploads and fits each redraw onto the
original. ChatGPT refused nothing.

Most pictures came out right at the first attempt. The title took three: the generator drew the mirrored R from
straight diagonals, then from round arcs, and got it right when the prompt described its strokes one by one. Its
letters and the names' are fitted one by one, as the generator placed some half a pixel off. The names first came
back with the pixel steps of their curves; when the second LCA came back smooth, I asked for all four names in that
style. TOM.RAW holds the same drawing as MRTOMMY, so it is cut from that redraw. WIZ, a small sprite, came back with
smoother shapes and lighter antennas than the original, which is close enough at the size it is shown.

`TEXTURE.RAW`, the sheet of tiles the 3D parts shade through tables, has a 4× Real-ESRGAN upscale instead, as
Inside's textures do; its tile of noise blocks turned into blobs, so that tile keeps its pixels.
[`remake/art/README.md`](../remake/art/README.md) has the prompts, the attempts and the fits. `--original-art`
shows the original pixels.

## Decisions

For this remake I left the decisions to the agents. The main ones:

- **Clock.** The remake's time is the screen refresh, as a fraction, and every part steps its state exactly as the
  original does per refresh and blends within a refresh.
- **Part starts.** The inits take as long as they take on the capture's machine; the remake takes those times from
  the capture, so every entry starts on the refresh on which it starts there.
- **Visibility.** The engine keeps the painter's order. A depth buffer would change 0.4 to 4.7 % of the pixels, with
  whole surfaces swapping.
- **The blur** is the same average in colour, each tap one original pixel wide, so it softens the frame as much as
  the original's at any resolution.
- **Frame-rate effects.** The dot spirals are shown one frame per refresh, not blended, because their look is the
  stroboscopic effect of the spiral at 70 frames per second. FIN's trail is stepped once per refresh, as the capture
  draws it.
- **Negatives** are the exact inverse colours, so a redraw can be inverted too; the original's nearest palette
  entries shift the hue a little.
- **Sound.** The remake plays at 48 kHz and normalises the mix to −1 dBFS after the card's high-pass.

# How the remakes were made

The remakes were made in November 2026, over a few days, by [Claude Code](https://claude.com/claude-code)
(Anthropic's coding agent) working with me, Dennis van der Stelt. I brought the original releases and my memory of
how the demos looked and sounded, and I decided what was right. Claude Code did the reverse engineering and wrote
the code. This page describes the way of working. Each production's making-of, `<demo>/docs/making-of.md` (for
example [Paper's](../paper/docs/making-of.md)), has the details.

## The goal

My aim was a remake that is "the same as the original, but way sharper". The original runs at 320×200 in 256
colours. The remake should run at any resolution in true colour, keep the original's motion, timing, composition,
colours and music, and run on Windows and Linux. (JIZZ already ran in hicolor, 65,536 colours, at 320×200, and clone
meets clone in true colour; for them the gain is resolution and exact maths.) I also wanted the hand-drawn pictures
sharper, which code alone cannot do; see [Redrawn pictures](#redrawn-pictures).

## From watching to reading the code

The first attempt, [303 version 1](../303/remake-v1/), worked from a video. The data files were decoded exactly, and
the effects were rebuilt by watching the capture. It looked right at first glance, but I saw that many effects were
not the real ones. The opening in particular was a different effect altogether.

From then on the executable was the source of truth:

1. **Get the code out.** Load the executable as DOS would. 303 needed an LE loader with relocations. Paper was
   PKLITE-compressed, so its own decompression stub was run in an emulator. JIZZ is a compressed DOS32 program,
   so the DOS32 kernel's own loader routine was run in an emulator. STARS is encrypted by Infiny Loader and
   compressed with PKLITE, with 58 DIET-compressed data files appended; the loader stub and PKLITE's decompressor
   were run in an emulator and the files unpacked with a decompressor checked against the program's own. DOPE is a
   chain of executables in one file, eight of them packed with Rob Northen's ProPack; every packer stub was run in
   an emulator. clone meets clone is a Watcom C program packed into a "VE" container that a small LE loader under
   PMODE/W unpacks; the loader's own code was run in an emulator. Inside is a Watcom C program in PMODE/W's own
   compressed format, whose decompressor was run in an emulator. From JIZZ on, a Python reimplementation was
   checked against the emulated original; the remakes do the same unpacking in C++ at
   start-up. Then disassemble with capstone.
2. **Map the skeleton.** Find the main script, the clock (what advances per timer tick, per drawn frame, or per
   music row), the calling conventions, the asset loaders and the shared helpers. This is written down once, in a
   brief that every later agent reads.
3. **Get a ground truth.** For every production but 303, the whole production runs headless
   in an emulator (unicorn, with fake VGA or VESA, timer and sound-card hardware), recording the exact screen,
   palette and sound-card writes for every tick or displayed frame. STARS runs at the speed of a 486DX2/66, the
   machine its NFO recommends, clone meets clone at about the 133 MHz Pentium its INFO asks for, and Inside at
   150 million instructions per second, the fast Pentium its README asks for. DOPE needs
   two emulated CPUs that share memory: one in real mode for its chain loader and its parts, one in 32-bit mode
   for the music player. For 303, a video capture of the original running on a Pentium MMX
   served as the reference.
4. **Decompile in parallel.** Split the program into scopes (one part, or the shared start-up code) and give each
   to an agent. Each agent writes a notes file:
   - what the code draws;
   - pseudo-C with every constant;
   - the time base of every animated variable;
   - the quirks a viewer would notice;
   - what is still unknown.

   For every production but 303, every agent also built a Python model and checked it against the capture.
5. **Port in parallel.** Shared building blocks first (palette system, random generator, 3D engine, music), then
   one agent per part. Each agent owns its own files and builds in its own folder, so they don't get in each
   other's way. A second brief fixes the conventions: time, screen coordinates, colours, how state is stepped.
6. **Compare, then fix.** Every part is compared side by side with the reference at the same moments, on contact
   sheets of original next to remake. The checks are not done by eye alone: the music is checked by its register
   writes and rendered samples, and the timing by measuring when part boundaries happen.
7. **Integrate.** One coordinating session assembles the parts, checks every part boundary, checks that seeking
   is exact, measures performance, renders videos and writes the documentation.

## Redrawn pictures

Effects can be recomputed at any resolution, but a picture drawn pixel by pixel cannot: scaled up, it stays 320×200.
For every remake, I asked for high-resolution versions of the hand-drawn pictures (title cards, backgrounds,
labels, logos, sprites and textures that are drawn as they are), faithful to the originals. They were made like
this:

1. **Upload the real pixels.** Each original picture, with the palette the demo shows it with, is scaled up with
   nearest-neighbour to its display shape (a 320×200 screen to 4:3, a texture with square texels), so the image generator sees the exact pixels.
   STARS's sprites and clone meets clone's glyphs are laid out on a flat key colour that stands for transparency.
2. **Ask for a faithful redraw.** ChatGPT's image generation got the same prompt for every picture, with a short
   description of what it shows: the same composition, pose, proportions, framing, colours and text, letter for
   letter; only the resolution and the detail change; nothing added, removed, moved, cropped or zoomed. Related
   pictures were made in one conversation so they match in style. When a result moved text, dropped a signature
   or changed a word, a second or third attempt said what to correct.
3. **Align and measure.** `tools/align_art.py` in each remake fits the redraw onto the original (scale in x and y
   and an offset), writes it covering exactly the original's frame, and measures how well it matches: the colour
   difference in the original's pixel grid and the correlation of their detail. For DOPE it can also match the
   redraw's colours to the original's where the generator made them darker or shifted their hue; for clone meets
   clone it fits every word of a caption on its own, where the generator spaced the words a little differently.

The redraws live in `<demo>/remake/art/` with a README that names the original artist and gives, per picture, the
prompt, the attempts and the measured fit (for example [JIZZ's](../jizz/remake/art/README.md)). Generated textures,
maps and anything an effect reads through palette indices stay computed from the original; where an effect colours a
grey picture (JIZZ multiplies its logo with computed clouds), only the grey picture is redrawn; where a picture holds
a photo of a person (the scanned faces in Inside's credits), only the drawn rest of it is. ChatGPT refused one
picture, the third of STARS's three women; it was redrawn with the open image-edit model Qwen-Image-Edit on Comfy
Cloud instead, with the other two redraws as style references. The remakes show the redraws by default and the
original pixels with `--original-art`; palette fades are colour multiplies, so they apply to both.

[redrawing-pictures.md](redrawing-pictures.md) is the recipe for the next picture: what gets a redraw, the upload,
the ChatGPT prompt, what to do when ChatGPT refuses or falls short (the Comfy Cloud workflow is saved in
[comfyui/](comfyui/)), and fitting and checking the result.

## What made it work

- **The notes are the hand-off.** A decompiling agent and a porting agent never talk; the notes file is the
  contract between them. Writing every constant down, with addresses, made the ports checkable line by line.
- **A ground truth you can query.** The emulator capture answered questions like "what does this variable hold
  at tick 5483?" directly, so findings didn't rest on reading the code alone. It also found things no video shows.
  For example, Paper's flock part is skipped on fast PCs, which is why so many videos of Paper lack it.
- **Time is the hard part.** Demos mix three clocks: the timer interrupt, the drawn frame and the music position.
  Getting every variable onto the right clock decides whether a port matches at all. Replaying per-tick state
  from the start of each part makes seeking exact and keeps every frame a pure function of time. clone meets clone
  has no frame-rate limit at all: it moves everything by the time since the last frame, and what it does once per
  frame (its particles) is stepped at the emulator capture's own frame times.
- **Human judgement where data runs out.** Several decisions came from me rather than from the code: the 303
  opening was wrong, the Paper flock was missing, and the flock moved too fast. The original's flock speed depends
  on the PC, and I picked 34 fps from three test renders. For JIZZ and STARS I left most decisions to the
  agents; they are listed in their making-ofs.
- **Nothing copied.** The remakes read every table, image, string, mesh and sample from the original files at run
  time, by address. The source is a re-implementation, and the originals stay their authors' property. The
  redrawn pictures are new pictures made from the originals; they are kept apart in `art/` and stay under the
  originals' non-commercial terms.

## Tools

The tools are described in [reverse-engineering.md](reverse-engineering.md). Rendering the videos is described in
[videos.md](videos.md).

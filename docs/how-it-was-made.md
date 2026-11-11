# How the remakes were made

Both remakes were made in November 2026, over about two days, by [Claude Code](https://claude.com/claude-code)
(Anthropic's coding agent) working with Dennis van der Stelt. Dennis brought the original releases and his memory
of how the demos looked and sounded, and he decided what was right. Claude Code did the reverse engineering and
wrote the code. This page describes the way of working. The making-of pages for [303](../303/docs/making-of.md)
and [Paper](../paper/docs/making-of.md) have the details for each production.

## The goal

The aim, in Dennis's words, was a remake that is "the same as the original, but way sharper". The original runs at
320×200 in 256 colours. The remake should run at any resolution in true colour, keep the original's motion,
timing, composition, colours and music, and run on Windows and Linux.

## From watching to reading the code

The first attempt, [303 version 1](../303/remake-v1/), worked from a video. The data files were decoded exactly,
and the effects were rebuilt by watching the capture. It looked right at first glance, but Dennis saw that many
effects were not the real ones. The opening in particular was a different effect altogether.

From then on the executable was the source of truth:

1. **Get the code out.** Load the executable as DOS would. 303 needed an LE loader with relocations. Paper was
   PKLITE-compressed, so its own decompression stub was run in an emulator. Then disassemble with capstone.
2. **Map the skeleton.** Find the main script, the clock (what advances per timer tick, per drawn frame, or per
   music row), the calling conventions, the asset loaders and the shared helpers. This is written down once, in a
   brief that every later agent reads.
3. **Get a ground truth.** For Paper, the whole intro runs headless in an emulator (unicorn, with fake VGA, timer
   and sound-card hardware), recording the exact screen, palette and sound-card writes for every tick. For 303, a
   video capture of the original running on a Pentium MMX served as the reference.
4. **Decompile in parallel.** Split the program into scopes (one part, or the shared start-up code) and give each
   to an agent. Each agent writes a notes file:
   - what the code draws;
   - pseudo-C with every constant;
   - the time base of every animated variable;
   - the quirks a viewer would notice;
   - what is still unknown.

   For Paper, every agent also built a Python model and checked it against the capture.
5. **Port in parallel.** Shared building blocks first (palette system, random generator, 3D engine, music), then
   one agent per part. Each agent owns its own files and builds in its own folder, so they don't get in each
   other's way. A second brief fixes the conventions: time, screen coordinates, colours, how state is stepped.
6. **Compare, then fix.** Every part is compared side by side with the reference at the same moments, on contact
   sheets of original next to remake. The checks are not done by eye alone: the music is checked by its register
   writes and rendered samples, and the timing by measuring when part boundaries happen.
7. **Integrate.** One coordinating session assembles the parts, checks every part boundary, checks that seeking
   is exact, measures performance, renders videos and writes the documentation.

## What made it work

- **The notes are the hand-off.** A decompiling agent and a porting agent never talk; the notes file is the
  contract between them. Writing every constant down, with addresses, made the ports checkable line by line.
- **A ground truth you can query.** The emulator capture answered questions like "what does this variable hold
  at tick 5483?" directly, so findings didn't rest on reading the code alone. It also found things no video shows.
  For example, Paper's flock part is skipped on fast PCs, which is why so many videos of Paper lack it.
- **Time is the hard part.** Demos mix three clocks: the timer interrupt, the drawn frame and the music position.
  Getting every variable onto the right clock decides whether a port matches at all. Replaying per-tick state
  from the start of each part makes seeking exact and keeps every frame a pure function of time.
- **Human judgement where data runs out.** Several decisions came from Dennis rather than from the code: the 303
  opening was wrong, the Paper flock was missing, and the flock moved too fast. The original's flock speed depends
  on the PC, and Dennis picked 34 fps from three test renders.
- **Nothing copied.** The remakes read every table, image, string, mesh and sample from the original files at run
  time, by address. The source is a re-implementation, and the originals stay their authors' property.

## Tools

The tools are described in [reverse-engineering.md](reverse-engineering.md). Rendering the videos is described in
[videos.md](videos.md).

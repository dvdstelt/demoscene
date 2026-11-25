# Making of the Vivid Experiment remake

Vivid Experiment went straight to the executable, like the remakes before it. Claude Code agents did the work,
directed by me, Dennis van der Stelt; I supplied the original release and left the decisions to the agents.
[../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working.

## Unpacking

`VIVID.EXE` is PMODE/W 1.22 with a Watcom C program in PMODE/W's own compressed format, like Inside's, so Inside's
unpacker worked with new load addresses: it runs the stub's self-decompression and the extender's decompressor in the
unicorn emulator on all nine compressed streams and checks an independent Python decompressor against it
(`re/unpack.py`). The program has three objects instead of two. The first is 4.2 MB, of which 177 KB is code: the rest
is the demo's data, three archives with a 128-entry directory each (name, size, offset), linked into the program
(`re/files.py`). One of the strings next to them, "EXAMPLE DEMO SCRIPT FILE", heads the two scripts that run the demo.
The remake unpacks the program in C++ (`remake/src/unpack.cpp`), checks it by hash and reads the archives as the
program does.

## Running it headless

Inside's emulator harness also needed little more than new addresses (`re/run_vivid.py`). The demo finds the
Gravis UltraSound through the `ULTRASND` environment variable, and MIDAS uses it without its interrupt or DMA: it
writes every sample byte into the card's memory through one port. The capture holds 27,690 records, one per screen
refresh, with every GUS register write, every script line with the refresh it ran in, and snapshots at every effect.
The 3D scenes run slowly in the emulator, so the whole demo took about two hours to capture. The program has a
switch of its own that starts at part II (`vivid.exe 2`), so a second run captured part II alone in parallel, and
the agents of part II's effects could start before the full capture had got there.

## Decompiling

Six agents decompiled the demo in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)): the core (the
script, the pictures, the palette, the zoom credits and the clock), the music, the bitmap effects, the bump mapping,
the water scene and the 3D engine. Each wrote a notes file with pseudo-C and every constant, and Python models that
reproduce the capture: every record of every bitmap effect, every draw of the three bump mappings and of the water
scene, the 3D scenes' pages pixel for pixel, the opening's palette in every record, and all 372,039 GUS register writes
and the GUS memory.

What came out:

- **A line acts, then waits.** Each script line does its work first and then waits until the music reaches its
  position, so what a line draws appears when the line before it stops waiting. The wait ends when the order is at
  least POS *and* the row at least PAT. A line copied between the two slide sections waits for row 63 of order 34;
  the second time it runs, the music is in order 46, so it waits for row 63 of order 46.
- **The opening's texts are index arithmetic.** The texts are added to the sky's indices, shifted up by 32 per
  level, and the sky's palette repeats itself four times; fading to a second palette whose upper copies are grey
  makes the text appear, without drawing anything.
- **The tables are formulas.** The bitmap effects read a texture through two per-pixel tables stored as files;
  eighteen of the nineteen regenerate byte for byte from distances, angles and sine wobbles.
- **Water in the bump mapping.** Two sets of ring waves make a water level; where it is above the height picture the
  pixel is water, elsewhere land, so Finland's coast moves with the waves. The waves never restart: the last bump
  mapping begins with all of the earlier ones' time in its waves.
- **The glass heart is opaque.** Its environment map is a picture of a glassy sphere, and an orange heart behind it
  is drawn at a sixteenth of its size into a copy of that map.
- **MIDAS keeps two clocks on one timer chip**: the player's ticks and an interrupt just before each retrace, which
  counts the demo's frames. Ticks that fall due just before a retrace are late, so the songs run up to 0.25 % slow;
  a remake that ignored this would drift half a second by the end of part I. The demo forces mono, and the second
  module's last two orders point to patterns the file does not have, which MIDAS plays as empty: the demo ends on one.
- **The capture's card was too simple.** The harness's GUS answered every volume read with the same value, so every
  volume ramp in the capture starts from it. The register stream is still the original's for that card; the remake
  models a real one.

## Porting

The framework is Inside's. The new core library reads the scripts and times every line against the music
(`remake/src/vivid/timeline.*`), and keeps the palette the screen shows as the original's DAC: the scripts' palettes
and fades, the pictures' own and the effects', stepped only where the original's loops step them. The scenes draw
indices through that palette, so every fade and flash comes out right without the scenes knowing about it.

Then six agents ported the scenes and the music, from a shared brief ([`re/PORT.md`](../re/PORT.md)): the pictures
and the zoom credits, the bitmap effects, the bump mapping, the water, the 3D scenes, and MIDAS. Until the music port
landed, the scenes ran on a stand-in that timed the rows from the modules' speed and tempo alone; with the port, all
43 of the scripts' waits end on the refresh they end on in the capture, and `tools/test_music` matches every register
write. Each scene was compared with the capture in side-by-side sheets and crops (`remake/tools/compare.py`), checked
for seeking (78 frames over the whole demo, rendered forwards and backwards, are identical) and timed at 2560 × 1920:
a median of 3.2 ms per frame for the whole demo on an integrated GPU.

## The redraws

The demo shows four kinds of picture as they are: its own lettering, borrowed art, photos of people, and captions.

- **Redrawn**: the opening's lettering (`alkutext.pcx`), the six names of the credits, and the winged man at the end
  of part I, with ChatGPT's image generation from the original pixels (`remake/tools/align_art.py` makes the uploads
  and fits the redraws). The lettering and the names came right at the first attempt, the names three to an upload;
  they are kept as coverage masks, because the demo draws them through its palette arithmetic, and the zoom draws the
  names from a distance field of the redraw, so their edges stay sharp when a letter fills the screen. The winged man,
  a fantasy painting in Boris Vallejo's style, came back a little redder and darker and was matched to the original's
  colours.
- **Not redrawn**: the photos of women in the title picture, the greetings and the bitmap effects (photos of real
  people); the slides' pin-ups; and the captions on the slides, which quote a song's lyrics. I wanted the pin-ups in
  high resolution too, censored for YouTube, so the first went to ChatGPT with its nude part covered by a mosaic.
  ChatGPT refused it for its similarity to third-party content: the pin-ups are a professional illustrator's work that
  the demo copied, so they keep their pixels. The captions are drawn sharper instead from their own pixels: their
  lightness, scaled up and cut at a threshold, gives smooth script at any resolution.
- **Censoring.** For the YouTube video, `--censor` covers the nude parts of the title picture and of two pin-ups with
  a mosaic of 6 × 6 original pixels; the remake shows the pictures as the demo does unless it is given.

[`remake/art/README.md`](../remake/art/README.md) has the prompts, the attempts and the fits.

## Decisions

For this remake I left the decisions to the agents. The main ones:

- **Clock.** The remake's time is the screen refresh, as a fraction. Everything the original counts in refreshes is
  evaluated at that fraction; the offsets and waves that carry over from one effect to the next are replayed from the
  script.
- **Load times.** The remake runs every script command in no time, as an infinitely fast PC would. The music and the
  waits are the original's; only the 3D scenes, whose set-up took the emulated PC 5 and 14 refreshes, start earlier.
- **Tables.** Where a table is a formula, the remake evaluates the formula per pixel; where it is not (one bitmap
  table and the three 2 × 2 tables), it is interpolated and keeps its own noise.
- **Smoothing.** The textures, maps and reliefs are filtered in colour. The bump mapping's water had a scratchy texture
  that came from its integer wave level; the remake evaluates the level exactly, so the water is smooth. The textures'
  own drawn detail stays.
- **Visibility.** The 3D scenes keep the original's near rule, which drops whole triangles of the landscape as the
  camera skims the hills, and the bands its wrapped mapping coordinates draw; each object resolves its own faces with
  a depth buffer.
- **Sound.** The register writes are the original's; the mix is mono, as the demo plays it, through a model of a real
  card.

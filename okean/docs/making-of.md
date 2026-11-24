# Making of the remake of The Sea Robot of Love

The Sea Robot of Love was the ninth remake, and the fourth 64k intro after Paper, JIZZ and clone meets clone. Like
the ones before it, it went straight to the executable. Claude Code agents did the work, directed by me, Dennis van
der Stelt; I supplied the original release and left the decisions to the agents.
[../../docs/how-it-was-made.md](../../docs/how-it-was-made.md) describes the general way of working.

## Unpacking

`KORSO2.EXE` is a single MZ executable packed with Rob Northen's ProPack, the same packer and stub as DOPE's parts, so
DOPE's unpacker worked unchanged: it runs the packer's own stub in the unicorn emulator, decodes the stream with an
independent Python decoder, and checks that both give the same 345,399 bytes (`re/unpack.py`). Inside is Tran's PMODE
3.x and one 32-bit program written in assembly: main, a ProTracker player with a GUS driver, eight parts, and the
music module itself, an 8-channel MOD stored in the program. The remake has the decoder in C++ (`remake/src/rnc.cpp`)
and checks the unpacked program by hash.

## Running it headless

`re/run_okean.py` runs the program in unicorn without DOS or PMODE: one 32-bit CPU, PMODE's few services answered in
Python, and DOPE's models of the VGA, the timer chip and the Gravis UltraSound. The first run hung after two parts.
The player's timer interrupt sends its end-of-interrupt early, enables interrupts and then waits, inside the
interrupt, for the card's volume-ramp interrupt to switch a voice to its new sample; an emulator that runs every
handler to completion before the next can never deliver that second interrupt. The harness now takes interrupts as
the CPU does, on the running code's stack, with a model of the interrupt controller's request and in-service bits, so
handlers nest as on real hardware. The whole intro then took about five minutes to capture: 9,109 records, one per
screen refresh, with every GUS register write and snapshots at every part.

The capture showed the intro's clock at once. The program switches the VGA to its own 50 Hz mode (625 lines at
49.85 Hz), and every part locks its timer to that refresh and moves everything once per refresh. A run with `-M`,
which keeps the standard 70 Hz mode, ends every part at the same music position but runs the motion 40 % faster.
The remake follows the 50 Hz mode.

## Decompiling

Six agents decompiled the intro in parallel from a shared brief ([`re/BRIEF.md`](../re/BRIEF.md)): the core (main,
the timer, the clocks, the text printer and the end part), the music, and four pairs of parts. Each wrote a notes
file with pseudo-C and every constant, and Python models that reproduce the capture. They all got there: every
drawn frame of every part byte for byte from the part's starting values and its callback count, the landscape's
generated height map, and the player's 70,027 GUS register writes and the GUS memory.

What came out:

- **Everything is added light.** Each frame starts as random noise; lines are rows of 8×8 brightness patterns added
  into it every two or four pixels; the sum indexes a palette ramp from black through one colour to white. The
  patterns are not data: they are the immediates of sixteen `add` instructions in each part's own copy of the line
  drawer.
- **The title's letters are never drawn.** A 1-bit picture of the lettering decides which stamps of the light beams
  are left out, so the words appear as shadows.
- **"Moving lightsource and shadow"** has no light source: the landscape's dark areas are the lines its floating
  horizon hides.
- **The music decides everything.** Parts end on a music order or on a fade that a music position starts, and the
  captions come from one printer that turns a page every 16 rows. The core agent worked out the exact rule: a part
  ends in the refresh in which its exit test sees the position, and the next one's first callback comes three
  refreshes later, after its timer calibration, which keeps interrupts off long enough to lose one music tick.
- **Dropped frames.** The capture repeats a frame now and then, every 1.8 seconds or so, where a new row's notes
  keep the CPU in the GUS interrupt over the retrace: the player's wait inside the interrupt again.

## Porting

The framework is Inside's, with DOPE's packer and GF1 model. I wrote the shared pieces first and ported one part,
the morph, as the example:

- `src/schedule.cpp` computes from the song alone when every music tick is served (the GUS timer's grid, the ticks
  the calibrations hold off and lose) and when every part starts and ends. It reproduces the capture's 1,494 row
  times and all eight part changes but one, which a dropped frame moved by a refresh.
- `src/okean/glow.*`, the intro's way of drawing on the GPU: a float buffer at the output resolution with the noise,
  every blob of every line as a small textured quad at its exact position, the kernel read from the code, and the
  palette lookup with interpolation between entries.
- `src/okean/text.*`, the caption printer and der piipo's font, whose outline takes each part's own dark colour.

Then four agents ported the music and the other six parts at the same time, from a shared brief
([`re/PORT.md`](../re/PORT.md)). The music port reproduces the capture's register stream exactly and renders the
soundtrack in about 60 ms. Each part was compared with the capture in side-by-side sheets (`remake/tools/compare.py`)
and crops, checked for seeking (the same frames rendered forwards and backwards are identical) and timed at
2560 × 1920.

## Decisions

The main ones:

- **Clock.** The remake's time is the 50 Hz refresh, as a fraction. The parts' motion is evaluated in closed form at
  the fractional callback count; nothing replays the capture.
- **The title's letters.** The first port drew the letters as crisp black shapes. The original's are soft: a pixel
  gets the light of every stamp that covers it unless that stamp's corner lies on a letter, so the mask is blurred by
  the stamp. The remake averages the mask along the stamps that cover each pixel, which gives the original's soft
  shadows with smooth edges, and the 1-bit mask needs no redraw.
- **Noise.** Each part's noise keeps its levels and changes every refresh, drawn at a third of an original pixel:
  fine snow at any resolution rather than 320×200 squares.
- **The landscape's hidden lines.** A continuous horizon per output column hid lines the original shows (its
  horizon only rises at the points it draws), so the port tests each drawn point at eight columns per original
  pixel and keeps the original's gaps.
- **Quirks.** Vertices behind the camera keep the original's projection, which throws them far out as spokes: that is
  the look of the star part's web. The 8-bit artefacts go: sums that wrap past 255, the step counters that cut long
  lines short, blobs clipped whole at the screen edge, the title's one-frame vertical bar.
- **Sound.** The player's register writes are the original's; the song stays a third of a semitone sharp, as the
  card played it.

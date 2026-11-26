# The Sea Robot of Love: what its makers achieved

*The Sea Robot of Love* by the Finnish group Orange came 3rd in the PC intro competition at The Party 1995 in
Fredericia, Denmark (27–29 December 1995), with 521 points, behind *Lasse Reinbong* by Cubic Team and $een (1,424)
and *Illumination* by Yodel (564) ([results][res], [demozoo][dzprod]). Its info file credits the programming to
hoplite and dune, the sound effects and music to dune, the font and graphics to der piipo, PMODE to Tran, the player
to Phantom, and "the 50hz mode" to liket. It is a 64 KB intro: one 65,358-byte executable, `KORSO2.EXE`.

This page draws on our reverse engineering of the executable ([`../re/`](../re/)) and on what the scene wrote about
the intro.

## Orange and the robots

Orange was a young PC group from the Helsinki area. In 1995 its core was hoplite (code), der piipo (graphics) and dune
(music), and late that year most of Jeskola! joined ([demozoo][dzgroup]). The intro belongs to a series: *Robot*
(The Party 1994), *Who Else Can Upgrade A Robot* (Abduction 1995, 1st), this one, and *You Am I You Am The Robot*
(The Gathering 1996, 1st). Orange's own index on scene.org files the robot intros as `korso.zip`, `okean.zip` and
`korso100.zip`, and the executable here is `KORSO2.EXE` ([00index][index]); no source we found says what "korso" or
"okean" stands for. Dune scored the winning PC demo of the same party as well, EMF and Plant's *Caero*
([demozoo][dzdune]). Orange went on to win at The Party 1996 with the intro *Deesbab* and, with CNCD, at The Gathering
1997 with *CNCD vs. Orange* ([pouët][pouetgroup]).

## Fifty frames per second

By default the intro reprograms the VGA: mode 13h with one character clock more per line (808 dots) and 625 lines
per frame instead of 449, of which the same 400 are shown. That is 25.175 MHz / (808 × 625) = **49.85 frames per
second** instead of 70.09 ([notes-core.md](../re/notes-core.md), section 1). With `-M` on the command line the screen
stays at 70 Hz. The same register table appears in Complex's *Xtal* two months earlier, whose text file credits the
mode to liket as "reverse-engineered" ([research notes][xtal]); the 50 Hz modes had a reputation for upsetting
monitors, and this one is still a known test case for VGA timings on LCD screens ([VOGONS][vogons]).

Every part then locks its timer to that refresh. It measures one frame with the PIT, loads the count less 0xc0
(161 µs early), and in each interrupt it checks whether the retrace has already begun, lowers the count by 7 if it
has and raises it by 1 if not, waits for the retrace, reloads the count and calls the part's retrace routine. That
routine moves everything by one step, 49.85 times per second; the main loop only draws, at most one frame per
refresh. So the motion is tied to the refresh, and with `-M` the intro runs 40 % faster while the music keeps its
tempo: the parts follow the music position, and the music plays on the Gravis UltraSound's own timer.

## Light from adding

Almost everything on screen is drawn the same way ([notes-morph-star.md](../re/notes-morph-star.md), section 1). Each
frame starts as noise: 64,000 random bytes from a small generator (`x = ror(x + 0x9d, 11) ^ 0x9d`), masked to the
part's 8, 16 or 32 darkest palette entries. Lines are then drawn as rows of small "blobs": an 8×8 pattern of
brightness values is added into the buffer every two or four pixels along the line, with sixteen 32-bit `add`
instructions whose immediates are the pattern. Where lines cross or bunch up the sums climb, and each part's palette
is a ramp from black through its colour (red, green, violet, magenta) to white. The result is the intro's look: soft,
glowing wireframes in TV snow, built from integer additions with no blur pass and no lighting.

The title turns this around. 256 beams of light radiate from an ellipse around the centre, each a row of 8×8
stamps adding one, and a 1-bit picture of the words "the sea ROBOT of love" in the program decides where stamps are
left out. The letters are never drawn: they appear as shadows in the light.

## The parts

- **Objects**: cogs with eight teeth, a 64-vertex mesh built from the sine table, copied eight times into a ribbed
  ball and a flying ring, five times into a tube, over a background of two sine waves per row; the palette blends
  from red to green on the music.
- **The canyon**: a 16×16 grid flown over a 200×16 map stored in the program, banking on the heading of 32 frames
  earlier, that cross-fades to red and flies off.
- **The morph**: one sphere of 32 rings and 16 meridians whose radius two travelling sine waves ripple; nothing
  switches, the shape is never the same twice. It builds up edge by edge, eight edges per refresh.
- **The landscape**: a 256×256 height map made at start-up by recursive midpoint displacement and smoothing, seen
  through a 32×32 window, its edges sorted near to far and drawn with a floating horizon so that ridges hide what is
  behind them. The caption says "moving lightsource and shadow", but there is no light source in the code: the dark
  areas are the hidden lines. Two angles advanced every refresh and never read suggest one was planned.
- **The star**: a tunnel of 16 rings of 8 points streaming towards the viewer; the far rings overlap into a white
  star, and vertices behind the camera are thrown far out and become the spokes of a web.
- **The sheet**: a 16×16 grid with a ring wave running outwards over it, tilted and swinging, which falls away at
  the end while the palette cross-fades to grey noise.
- **The end**: white text on grey noise, in der piipo's font: 26 hand-drawn lowercase letters, 38 pixels high, with a
  dark outline and single grey pixels on the curves.

The captions ("hmmm", "float robot float", "thy shall be upgraded", ...) come from one printer that every part calls
once per frame. It shows one page per 16 rows of the music: the parts do not choose their texts, the song does.

## 64 kilobytes

The executable is packed with Rob Northen's ProPack, which unpacks it to 345,399 bytes, most of them buffers full of
zeros. Inside is Tran's PMODE 3.x and one 32-bit program written in assembly. The music is a complete 8-channel
ProTracker module stored in the program itself, about 63 KB unpacked; only a quarter of the sine table is stored,
and the first part mirrors it into the rest. The landscape is generated, the canyon map is 3,200 bytes, and the
font sheet is 12 KB of mostly empty pixels that pack well.

## The music player

Phantom is Oskari Tammelin, later the author of Jeskola Buzz ([demozoo][dzphantom]). His GUS player is the same family
as the one in Complex's *DOPE*, with a different driver ([notes-music.md](../re/notes-music.md)). It runs from GUS
timer 2 every 19.84 ms, uses 16 of the card's voices, and starts every note by ramping the old one down with the
volume-ramp interrupt switched on; the timer interrupt then waits inside itself, with interrupts enabled, for that
ramp interrupt. It computes pitches with a divisor of 37 where the card at 16 voices runs 37.68 Hz per step, so the
whole song plays about a third of a semitone sharp. In the last seconds the end part lowers the player's master
volume together with the picture.

## Hidden and left over

- The printer's pages go on past the end of the intro: one more, "take hash man", is never reached.
- Main writes the graphics controller's two mode-13h values to port 3C3h instead of 3CEh; they were the values
  already set.
- The star part builds and projects a second set of 128 vertices every frame that no edge uses.
- The printer would print digits through its own variables; no text has any.

## How it was received

Phoenix of Hornet gave it the highest overall rating among The Party's 64 KB intros in DemoNews 113 (January 1996):
dune's music was not the usual techno, the snow gave it a TV-like feel, and it ticked none of his list of clichés
of the time. He called it "smooth and artistic, with mostly meshed vectors" ([DemoNews 113][dn113]). On pouët it has
63 thumbs up and none down; commenters remember its sayings, which people repeated on IRC for months, and its style
from simple means ([pouët][pouetprod]).

## Sources

- [res]: The Party 1995 results file, <https://files.scene.org/view/parties/1995/theparty95/results.txt>
- [dzprod]: Demozoo, The Sea Robot of Love, <https://demozoo.org/productions/7690/>
- [dzgroup]: Demozoo, Orange, <https://demozoo.org/groups/487/>
- [dzdune]: Demozoo, Dune, <https://demozoo.org/sceners/486/>
- [dzphantom]: Demozoo, Phantom, <https://demozoo.org/sceners/6774/>
- [index]: Orange's index on scene.org, <https://files.scene.org/view/demos/groups/orange/00index>
- [pouetgroup]: pouët, Orange, <https://www.pouet.net/groups.php?which=30>
- [pouetprod]: pouët, sea robot of love, <https://www.pouet.net/prod.php?which=1303>
- [dn113]: Hornet DemoNews 113, <https://resources.openmpt.org/resources/demonews113.txt>
- [xtal]: Complex's Xtal on Demozoo, <https://demozoo.org/productions/28488/> (the credit to liket is quoted from
  XTAL.TXT by a machine-written analysis, <https://demo2.jeskola.net/xtal-analysis.html>; not checked against the
  file)
- [vogons]: VOGONS, "LCD monitor VGA compatibility database", <https://www.vogons.org/viewtopic.php?t=44877>

[res]: https://files.scene.org/view/parties/1995/theparty95/results.txt
[dzprod]: https://demozoo.org/productions/7690/
[dzgroup]: https://demozoo.org/groups/487/
[dzdune]: https://demozoo.org/sceners/486/
[dzphantom]: https://demozoo.org/sceners/6774/
[index]: https://files.scene.org/view/demos/groups/orange/00index
[pouetgroup]: https://www.pouet.net/groups.php?which=30
[pouetprod]: https://www.pouet.net/prod.php?which=1303
[dn113]: https://resources.openmpt.org/resources/demonews113.txt
[xtal]: https://demozoo.org/productions/28488/
[vogons]: https://www.vogons.org/viewtopic.php?t=44877

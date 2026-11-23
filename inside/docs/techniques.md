# Inside: what its makers achieved

*Inside* by CNCD won the PC demo competition at The Gathering 1996 in Hamar, Norway (2–6 April 1996), with 173
points; *The Control* by Coma and *Ninja II* by Melon Dezign and Scoop shared second place with 69 ([results][res]).
Twenty demos were entered, at the first Gathering held in the Vikingskipet, the speed-skating hall of the 1994
Olympics ([demozoo][dzparty]). The README, dated 3 April, credits the code to wili, jouni and juliet, "graphics and
all" to destop, the music to yolk and legend and additional graphics to santtu, with DSMI by Otto Chrons and PMODE/W
by Thomas Pytel. Hybrid's own page about the demo adds two credits: compression by Aleks Jakulin and photography by
Michael Schwartz ([Hybrid, 1996][hyinside]).

This page draws on our reverse engineering of the executable ([`../re/`](../re/)) and on what the scene and Hybrid
wrote about the demo.

## CNCD, Hybrid and SurRender

CNCD (Carillon & Cyberiad) formed on the Amiga in 1993, when the Finnish groups Carillon and Cyberiad merged
([demozoo][dzcncd]). In 1995 they won the Amiga demo competition at The Party with *Closer*; Inside was their first
PC win, which Hornet's DemoNews called "a worthy first notch in CNCD's PC belt" ([DemoNews 122][dn122]).

Two of its coders also wrote its 3D engine. Hybrid Ltd was a Helsinki company in the Cable Factory that made
interactive video, motion capture and 3D software, and its SurRender engine names wili and jouni as its authors
([Hybrid, 1996][hysr]). The executable carries the version it was built with: "SurRender P5 version 1.02 (Apr 01
1996)", built the day before the party began. Hybrid presented Inside as a demonstration of the engine, all of it
rendered in real time, offered a Windows 95 friendly build (`inside95.exe`, on DOS/4GW) and released the demo's
source code without the engine ([Hybrid, 1996][hyinside]).

SurRender went on: by September 1996 it was at version 1.10 and offered for licensing, with a core in protected-mode
assembly, a run-time footprint under 200 KB and back ends for 3Dfx's Glide and DirectX. Around 1999 it became
SurRender 3D, a C++ library over Direct3D, OpenGL, Glide or software, used for *Supreme Snowboarding*, *Test Drive
Off-Road 3*, *NFL Blitz 2000* on the PC and *Starlancer* ([Hybrid's news][hynews]). Hybrid discontinued it in 2002
and moved to visibility software and mobile graphics; NVIDIA bought Hybrid Graphics in March 2006 ([NVIDIA][nvidia]).
In 2002 wili, jouni and santtu still worked there ([Hybrid, 2002][hystaff]).

## Everything runs on the screen refresh

At start-up the demo measures the monitor's refresh with the timer chip, twice until two measurements agree, and
programs the timer to interrupt four times per refresh: once at the vertical retrace, then three times at 0.24 of a
frame. Everything that moves runs in that interrupt ([NOTES.md](../re/NOTES.md)): DSMI's music player, which advances
its time by 35 units per interrupt; one step of the palette fade; the copy of a finished frame to the screen; and the
current part's tick routine, once per refresh. The main loop only draws, as fast as the machine allows, so the frame
rate varies (48 frames per second in the intro on an emulated fast Pentium, 62 under water) while the motion keeps to
the refresh.

The running order is a table of 12-byte entries in the program: a music order, a row and a routine. After every drawn
frame, the main loop runs the entries the music has reached. The routines start and end the parts and fire the white
flashes, inverse flashes and fades that mark the beats.

## Translucency from tables

In 256 colours, nothing can be mixed on screen; it can only be looked up. SurRender has six rasterizers and every one
is a table lookup ([notes-engine.md](../re/notes-engine.md)). Texels go through shade tables (a palette index per
light level and colour) for the lit surfaces, and through 64 KB tables indexed by the texel and by the palette index
already on screen for everything translucent: the temple's pillars (`temple.int`, a 50 % mix), the glass fish
(`water.int`, the same), the shadow in the photo room (`.sdw`), the flares and the glowing pipes (in the flight,
`flare.shd` adds half the flare's colour to what is behind) and the credits' water mask and names, which add a level
to the tunnel. The engine sorts faces by depth instead of using a depth buffer, lights vertices with point lights,
and places its sprites ("billboards") with the horizontal focal length in both directions, so they sit 1.2 times too
far from the centre line vertically. Our Python model of the engine reproduces the original's frames pixel for pixel.

The flower part even reads a LightWave scene, `tunnel1.lws`, with its own parser at start-up, and flies its camera
along the motion with a cardinal spline, sampled once per LightWave frame.

## Pictures and photos

Destop's logos and lettering are drawn: the twisted "INSIDE" title and the five names in the credits. Much of the
rest is photographic: the intro's collage, the people standing on the warping backgrounds and lying in the turning
room (with a shadow cast through a table), the faces scanned into the names, the blurred room seen through the water,
the aerial photo the flight's landscape is textured with. Each part has its own 256-colour palette, and most switch
to it under a white flash.

## Packing, data and sound

`INSIDE.EXE` is a Watcom C program under PMODE/W 1.22, compressed in PMODE/W's own format: 113 KB that unpack to 141
KB of code and data, with 617 KB more for its variables and stack. The data, `INSIDE.DAT`, is one LZSS block that
unpacks to 2.7 MB with a directory of 93 files: pictures, palettes, shade and blend tables, SurRender objects and the
LightWave scene. The demo checks for 5.5 MB of free memory before it starts.

The music plays through DSMI, Otto Chrons' Digital Sound and Music Interface, written with Jussi Lahdenniemi and
licensed to games such as Disney's *Lion King* and *Aladdin* for the PC ([DSMI on GitHub][dsmi]). Its module format
is AMF; the release also includes the tune as a MOD, its "original edit". `SETUP.EXE` writes `SOUND.CFG`, and the
demo loads the chosen card's driver from a `.DSD` file at run time: Gravis UltraSound, Sound Blaster, Pro Audio
Spectrum, Windows Sound System, Aria, or no sound. The remake plays it with a port of DSMI's player and of its GUS
driver.

## Hidden and left over

- Pressing S saves the screen as `shot<n>.tga` (0x400c40; also in the [hidden parts list][hidden]).
- A sixth name picture, SANTTU, is loaded with the other five and never shown.
- The twirl part has a spiral unwrap of its picture that nothing calls, and the program has a debug font that is
  switched off.
- DemoNews noted that with less than 1 MB of memory on the GUS the intro's picture never stops filling.

## How it was received

DemoNews 122 (26 April 1996) gave it 9 for effects, 8 for design, 6 for music and 8 overall, and noted the "rapper
guys" with their shadows over the effects, the fluid dynamics and the landscape with its lens-flaring sun. In the
same issue its two zips were the two most downloaded files on ftp.cdrom.com, and they stayed in the top ten of demos
through September 1996 ([DemoNews 122][dn122], [DemoNews 129][dn129]). On pouët it has about 190 comments, most of
them about its style, Destop's graphics and the music ([pouët][pouet]). It is on the *MindCandy Volume 1* DVD of PC
demos (2002), Melon Dezign parodied it as *Outside* at The Gathering 1997, and CNCD won the same competition that
year with *CNCD vs. Orange* ([demozoo][dzprod]).

## Sources

- Our notes: [NOTES.md](../re/NOTES.md), [notes-core.md](../re/notes-core.md),
  [notes-engine.md](../re/notes-engine.md), [notes-music.md](../re/notes-music.md), [PARTS.md](../re/PARTS.md),
  [making-of.md](making-of.md), [remake README](../remake/README.md)
- README.TXT and FILE_ID.DIZ, in the release archive: <https://files.scene.org/view/parties/1996/gathering96/demo/inside.zip>
- The Gathering 1996 results (by yitz of Proxima): <https://files.scene.org/view/parties/1996/gathering96/results.txt>
- Demozoo, The Gathering 1996: <https://demozoo.org/parties/42/>; Inside: <https://demozoo.org/productions/18030/>;
  CNCD: <https://demozoo.org/groups/26/>
- pouët, *Inside* and its comments: <https://www.pouet.net/prod.php?which=74>
- DemoNews 122 (April 1996): <https://resources.openmpt.org/resources/demonews122.txt>
- DemoNews 129 (September 1996): <https://resources.openmpt.org/resources/demonews129.txt>
- The Quasi-Official List of Demo Hidden Parts, Keys, and Tricks: <https://files.scene.org/view/mags/hidden_parts/secret20.txt>
- Hybrid, "SurRender demonstration by CNCD: Inside" (1996):
  <https://web.archive.org/web/19961206151344/http://hybrid.org:80/surrender/inside.html>
- Hybrid, SurRender (1996): <https://web.archive.org/web/19961206150028/http://hybrid.org:80/surrender/index.html>
- Hybrid, SurRender 3D news (1999–2001):
  <https://web.archive.org/web/20010409022442/http://www.surrender3d.com:80/news/ss.html>
- Hybrid Graphics, personnel (2002): <https://web.archive.org/web/20020408075811/http://surrender3d.com:80/personnel.html>
- NVIDIA's announcement of the acquisition (2006): <https://www.webwire.com/ViewPressRel.asp?aId=11512>
- Otto Chrons, DSMI: <https://github.com/ochrons/dsmi>

[res]: https://files.scene.org/view/parties/1996/gathering96/results.txt
[dzparty]: https://demozoo.org/parties/42/
[dzprod]: https://demozoo.org/productions/18030/
[dzcncd]: https://demozoo.org/groups/26/
[pouet]: https://www.pouet.net/prod.php?which=74
[dn122]: https://resources.openmpt.org/resources/demonews122.txt
[dn129]: https://resources.openmpt.org/resources/demonews129.txt
[hidden]: https://files.scene.org/view/mags/hidden_parts/secret20.txt
[hyinside]: https://web.archive.org/web/19961206151344/http://hybrid.org:80/surrender/inside.html
[hysr]: https://web.archive.org/web/19961206150028/http://hybrid.org:80/surrender/index.html
[hynews]: https://web.archive.org/web/20010409022442/http://www.surrender3d.com:80/news/ss.html
[hystaff]: https://web.archive.org/web/20020408075811/http://surrender3d.com:80/personnel.html
[nvidia]: https://www.webwire.com/ViewPressRel.asp?aId=11512
[dsmi]: https://github.com/ochrons/dsmi


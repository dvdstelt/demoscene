// The pictures scene: what VIVID.EXE's script draws straight into VGA memory between the effects
// (vividexp/re/notes-core.md 7), with the palette the DAC holds:
//   - part I: the sky (taivas.pcx) fading in from black, its white flash at 1:00; the three texts ("in the year 1996",
//     "a doomsday production", "at assembly": alkutext.pcx rectangles added to the sky with mode 14) fading in and
//     out through taivas.pal <-> taivas2.pal; the credits' labels in the top band and the white flashes between the
//     names (the window keeps the zoom's last frame); the label erased and the cut fade to white; the title
//     (vivid.pcx) fading in from white; the slides (synknai1..4.pcx) with their captions (synktxt1..4.pcx rectangles)
//     appearing one by one and fading to black; the winged man (siipiman.pcx) fading in from white; the clear screen;
//   - part II: the white flash over the water's last frame before the first bump, and the greetings (greets.pcx)
//     fading in from white.
// Ports InitPCX 0x401884, DeinitPCX 0x401c20, PutPCXSYNC 0x401c2c, ClearSCR 0x402784 and the fades' effect on the
// colours (0x4020b8, 0x401ed0); the drawing is shared with the zoom credits in pictures_vga.h, which says what is
// evaluated exactly instead of the original's shortcuts and how the redraws are used.
//
// Kept from the original, as the script makes them: the descenders of "a doomsday production" cut off (its rectangle
// takes rows 0..20 of letters that reach row 23); the "music" label erased by a copy of empty rows; the band
// rectangle of 320 columns at x = 5 clipped to 315; the jump to white when the title's fade starts from white.pal
// although the cut fade to white had reached only 6 of its 45 steps; the captions copied opaquely, rectangle by
// rectangle. PutPCXSYNC reads every slot with the width of the last picture decoded (one global); the scripts always
// decode a 320-wide picture last before a copy, so the remake reads each picture with its own width.
#include "demo.h"
#include "scenes/pictures_vga.h"

namespace {

class PicturesScene : public Scene {
public:
    void init(Ctx& c) override { c.shared<vga::Memory>(); }
    void render(Ctx& c, double r) override { c.shared<vga::Memory>().draw(c, r); }
};

}  // namespace

REGISTER_SCENE(PicturesScene, "pictures");

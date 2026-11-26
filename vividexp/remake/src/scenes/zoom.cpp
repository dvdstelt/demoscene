// The zoom credits (RunZOOMSYNC 0x402644; vividexp/re/notes-core.md 6): a 1280 x 200 name (doom, mri, dice, damac,
// swallow, wode.pcx) flies at the camera over the sky (taivas.pcx, slot 0 of InitZOOM 0x402460) inside the letterbox
// window, rows b .. 199 - b of the line's `b` (28); the bands keep what VGA memory held (the sky's black bands and
// the label of pictures.cpp). The line's numbers give the distance z0 (900), the size a (200) and the change of z
// per refresh dz (-3.5): z(n) = max(z0 + dz n, 1), n the refreshes since the line started (fractional), and the
// name is drawn at s = a / (2 z) screen pixels per picture pixel, its centre (640, 100) on the screen's (160, 100)
// (zoomFrame 0x402494). Index 0 is transparent (zoomBlit 0x40ccb8), the grey edge pixels opaque.
//
// Evaluated exactly instead of the original's shortcuts: the integer rectangle (truncated x0, y0) and the 16.16
// nearest-neighbour stepping that drops the start fraction (letter edges moving in blocks of up to s pixels) become
// the continuous transform, with the name filtered (the original's pixels as sharp-edged texels, or the redraw's
// coverage, its edge from a signed distance field when magnified); z steps per refresh in the original and is
// evaluated at the fractional refresh. The drawing is shared with the pictures scene (pictures_vga.h): during the
// white flashes between the names the window keeps the zoom's last frame.
//
// Not reproduced: the capture's records that still show the previous zoom frame (852 of 2,856: the emulated CPU
// took longer than a refresh to draw the larger names) and the one record caught in the middle of the copy.
#include "demo.h"
#include "scenes/pictures_vga.h"

namespace {

class ZoomScene : public Scene {
public:
    void init(Ctx& c) override { c.shared<vga::Memory>(); }
    void render(Ctx& c, double r) override { c.shared<vga::Memory>().draw(c, r); }
};

}  // namespace

REGISTER_SCENE(ZoomScene, "zoom");

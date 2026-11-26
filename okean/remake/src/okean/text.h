// The intro's text (okean/re/notes-core.md sections 5..7): der piipo's font (code32 0x3cec2: 26 lowercase glyphs, 38
// rows, widths at 0x4c8c2) and the shared printer 0x4cacc, whose pages (0x4c8e8.., one per 16 rows of the music)
// the parts draw as captions on row 162; the end part prints its own pages with its own line spacing.
//
// A glyph has three layers: the body (index 216, white in every part's palette), single edge pixels at the steps of
// curves (254, the grey (33, 33, 33), i.e. 33/63 of the body) and a 1-pixel outline (7: each part's own dark colour).
// The font is a picture ("font", pictures.h: the 285 x 38 sheet; a redraw by default): white body, red outline, on
// black, so a pixel's body coverage is its green, its outline coverage red minus green, and the three layers keep the
// part's colours 216 and 7 as its palette fades.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "demo.h"

namespace okean {

class Printer {
public:
    explicit Printer(Ctx& c);

    // The caption page the shared printer draws into the frame that record r of the current part shows: its lines
    // (empty if it shows none). A page per 16 rows (rows 0, 16, 32, and 48 in order 0); the draw of record r prints
    // the page the previous draw's call chose from the position it read (the printer advances after printing), so a
    // caption shows from the second frame after its row's tick. `line`: where in the refreshes the part calls the
    // printer, as scan lines after the start of the refresh before the record's (the draw of record r starts at line
    // 504 of refresh r - 1): in the capture about 830 in the landscape part (line 205 of refresh r: its draw is
    // slow), 591 in the star part, 597 in the sheet part (models/core/runs/p*.ev, the `print` events).
    std::vector<std::string> caption(Ctx& c, double r, double line = 504.0) const;
    // Draws that caption into ctx.out (the part's resolved picture), centred on row 162, in the colours `pal` gives
    // entries 7 and 216 (the part's palette as shown, fades included).
    void drawCaption(Ctx& c, double r, const std::array<Vec3, 256>& pal, double line = 504.0);
    // One line of text with the printer's layout: left edge x = 160 - floor(w / 2) (w = the sum of the advances, a
    // space is 4), the glyph cells' top at y (38 rows high).
    void drawLine(Ctx& c, const std::string& line, float y, const std::array<Vec3, 256>& pal);
    int width(const std::string& line) const;   // w in 320x200 pixels

    // The page that starts at code32 address `addr` ('\r' starts a line, '@' ends the page), as lines.
    std::vector<std::string> page(uint32_t addr) const;

private:
    const OkeanExe* exe_;
    std::array<int, 26> w_{}, x_{};          // glyph widths and their columns in the sheet
    std::vector<uint32_t> pages_;            // the shared printer's pages from 0x4c8e8
    Shader shader_;
};

}  // namespace okean

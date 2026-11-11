// The running order, as main (0x186bd) calls the parts one after the other; each part returns when the sync event
// reaches its end value, i.e. at a music position (jizz/re/PARTS.md). The original changes part at the first frame
// after the position is reached (up to two frames late); the remake changes on the row.
#include "demo.h"

const std::vector<CueDef>& timeline() {
    static const std::vector<CueDef> t = {
        {0x0000, 0x0100, "tbl"},      // 0x12013: "TBL" behind a burst of light rays, ends in a white flash
        {0x0100, 0x0400, "ribbons"},  // 0x17003: ribbons over cloud planes, the mirror plane with rings, insets
        {0x0400, 0x0600, "tunnel"},   // 0x169e7: the tunnel, then white rectangles over a radial blur
        {0x0600, 0x0800, "tube"},     // 0x15c8b: the yellow tube object with pink spikes
        {0x0800, 0x0900, "logo"},     // 0x1212a: the "jizz" logo over a star-shaped warp
        {0x0900, 0x0b00, "spiky"},    // 0x15d20: the spiky object over moving clouds
        {0x0b00, 0x0d00, "zoom"},     // 0x177bf: the rotating radial zoom
        {0x0d00, 0x1100, "field"},    // 0x17c8a: the field-line patterns
        {0x1100, 0x1500, "blobs"},    // 0x10fda: the gold metaballs
        {0x1500, -1, "end"},          // 0x18c25: "~JIZZ~ TBL'97" until the sync table ends (0x15:0x1c)
    };
    return t;
}

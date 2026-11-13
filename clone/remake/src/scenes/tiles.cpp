// Part 11 (0x419dc7, 08:20..0c:00): 8 x 6 flat tiles of 40 x 24 pixels (rows 24..167) in the colours of the table
// 0x428164 (entries 8..55, cmc::tileColour), each fading in at its own flickering rate; a black box (tiles 3..6 of
// rows 2..3) with the title "clone meets clone"; from event 1101 (0b:00) everything fades to black in 2.2 s and the
// title to a 1/64 ghost. clone/re/notes-grid.md section 4, the title's level notes-gfx.md 7.4.
//
// Evaluated exactly instead of the original's shortcuts: the tile colours without the truncation to 8 bits (half a
// step lower, the steps' mean; the clamp at 255 stays), the edges of the tiles anti-aliased (each pixel takes the tiles it covers by area), the title
// blend at a continuous level. The tile clock and the fade are closed forms of the part's clock (timer only: the
// part has no frame-rate dependence).
#include <algorithm>
#include <cmath>

#include "clone/core.h"
#include "exe.h"
#include "scenes/grid.h"

namespace {
constexpr int kCols = 8, kRows = 6;
constexpr int kFirstEntry = 24;          // dword index of the first tile in 0x428164 (entry 8)
constexpr uint32_t kFade0 = 0x428464;    // the fade's initial value (1.0)

// Each pixel: the area-weighted sum of the tiles under its footprint (black outside the 8 x 6 tiles).
const char* kTilesFS = R"(
in vec2 vga;
out vec4 o;
uniform vec3 tiles[48];   // row by row, display-space colours
vec3 tile(int col, int row) {
    return col < 0 || col > 7 || row < 0 || row > 5 ? vec3(0.0) : tiles[row * 8 + col];
}
void main() {
    vec2 hw = 0.5 * fwidth(vga);
    vec2 lo = (vga - hw - vec2(0.0, 24.0)) / vec2(40.0, 24.0), hi = (vga + hw - vec2(0.0, 24.0)) / vec2(40.0, 24.0);
    ivec2 a = ivec2(floor(lo)), b = min(ivec2(floor(hi)), a + 3);
    vec3 sum = vec3(0.0);
    for (int j = a.y; j <= b.y; j++)
        for (int i = a.x; i <= b.x; i++) {
            vec2 w = max(vec2(0.0), min(hi, vec2(i + 1, j + 1)) - max(lo, vec2(i, j)));
            sum += tile(i, j) * (w.x * w.y);
        }
    o = vec4(sum / max((hi.x - lo.x) * (hi.y - lo.y), 1e-12), 1.0);
}
)";

struct TilesScene : Scene {
    Shader sh;

    void init(Ctx&) override { sh.build(gridfill::kGridVS, kTilesFS, "tiles"); }

    void render(Ctx& c, double t) override {
        double clock = 0.05 * c.clock(t);  // [0xf73934] += dt * 0.05
        double t1101 = c.eventTime(1101);  // fade [0x428464] -= dt * 0.0065 from 0b:00, >= 0
        double fade = cmc::Ramp{t1101, c.exe->f32(kFade0), -0.0065, 0.0, 1e9}.at(c, std::max(t, t1101));
        float col[kCols * kRows * 3];
        for (int row = 0; row < kRows; row++)
            for (int cl = 0; cl < kCols; cl++) {
                int k = kFirstEntry + 3 * (row * kCols + cl);
                double red = c.exe->s32(0x428164 + 4 * uint32_t(k));  // the flicker's frequency: the tile's red value
                double b = std::min(1.0, (std::sin(red + red * clock * 0.01) * 0.3 + 0.7) * (clock * 0.13));
                Vec3 v = cmc::tileColour(*c.exe, k) * float(b * fade);
                if (cl > 2 && cl < 7 && row > 1 && row < 4) v = Vec3(0, 0, 0);  // the black box behind the title
                // min(255, trunc(value * 0.0125 * 255 * b * fade)): continuous through the centres of the steps
                auto ch = [](float x) { return std::clamp(x - 0.5f / 255.0f, 0.0f, 1.0f); };
                float* p = &col[3 * (row * kCols + cl)];
                p[0] = ch(v.x);
                p[1] = ch(v.y);
                p[2] = ch(v.z);
            }
        setBlend(Blend::None);
        sh.use();
        sh.set("rect", 0.0f, 0.0f, 320.0f, 200.0f);
        gl::Uniform3fv(sh.loc("tiles"), kCols * kRows, col);
        drawQuad();
        c.overlay("title_clone_meets_clone", 141, 104, cmc::tilesTitleLevel(c, t));
    }
};
}  // namespace

REGISTER_SCENE(TilesScene, "tiles");

// The last part (0x4d370, okean/re/notes-core.md section 7): grey noise over the whole screen with the end texts in
// der piipo's font, a page per order (rows 0..32; blank from row 33), and from 30:32 the fade to black, after which
// the intro sets text mode. Ports the main loop 0x4d389 (noise, the page select 0x4d3c8, the palette 0x4cd64 x fade /
// 32 written before the retrace wait, the printer inlined at 0x4d481 with its 28-row line spacing) and the callback
// 0x4d645 (the fade).
//
// Time: n = the callbacks before the draw (record F + n); the palette uses the fade after n callbacks. The page a
// frame shows was selected by the previous frame's draw, from the position it read after its noise fill.
#include <algorithm>
#include <array>
#include <cmath>

#include "demo.h"
#include "exe.h"
#include "okean/glow.h"
#include "okean/text.h"

namespace {

constexpr uint32_t kPalette = 0x4cd64;
// the pages (0x4d364 and the selects at 0x4d3c8): the initial page, orders 28, 29, 30, and the blank page
constexpr uint32_t kPageFirst = 0x4cca3, kPage28 = 0x4ccd9, kPage29 = 0x4cd0f, kPage30 = 0x4cd33, kPageBlank = 0x4cca0;
constexpr int kLineStep = 28;   // 0x2300 / 320

class EndScene : public Scene {
public:
    void init(Ctx& c) override { pal_ = okean::palette(*c.exe, kPalette); }

    void render(Ctx& c, double r) override {
        auto& canvas = c.shared<okean::Canvas>();
        auto& printer = c.shared<okean::Printer>();
        double nf = std::max(0.0, c.callbacks(r));
        Steps st = stepsOf(nf);
        canvas.begin(c);
        canvas.noise(c, 32, uint32_t(std::floor(r)));
        double fade = this->fade(c, st.n) * (1.0 - st.frac) + this->fade(c, st.n + 1) * st.frac;
        auto pal = okean::scaled(pal_, fade, 32.0);
        canvas.resolve(c, pal);

        // the page: selected by the previous draw (the initial one for the first)
        uint32_t page = st.n == 0 ? kPageFirst : select(c.positionAtDraw(st.n - 1));
        float y = 0;
        for (const std::string& line : printer.page(page)) {
            if (!line.empty()) printer.drawLine(c, line, y, pal);
            y += kLineStep;
        }
    }

private:
    // 0x4d3c8: orders 28..30 choose their page, any row past 32 the blank one; otherwise the page stays (the
    // initial one in order 27)
    static uint32_t select(int pos) {
        int order = pos / 64, row = pos % 64;
        uint32_t p = kPageFirst;
        if (order == 28) p = kPage28;
        if (order == 29) p = kPage29;
        if (order == 30) p = kPage30;
        if (row > 32) p = kPageBlank;
        return p;
    }

    // The fade after n callbacks (0x4d645): 32 until a callback sees 30:32 or later in order 30, then one less per
    // callback, down to 0.
    double fade(Ctx& c, int n) const {
        int f = 32;
        for (int cb = std::max(1, n - 40); cb <= n; cb++) {
            int pos = c.positionAtCallback(cb);
            if (pos / 64 == 30 && pos % 64 >= 32) {
                // count the callbacks since the first one that saw it
                int first = cb;
                while (first > 1) {
                    int p = c.positionAtCallback(first - 1);
                    if (!(p / 64 == 30 && p % 64 >= 32)) break;
                    first--;
                }
                f = std::max(0, 32 - (n - first + 1));
                break;
            }
        }
        return f;
    }

    std::array<Vec3, 256> pal_{};
};

}  // namespace

REGISTER_SCENE(EndScene, "end");

// main's picture sequences (scenes "ornament" and "credits"): ports main 0x4d's script around the pictures
// (stars/re/notes-core.md sections 2 and 2.2) with 0x17e50 (show a 0xfd00 picture with a black palette), 0x17e96 /
// 0x17eba (fade in / out: n + 1 palette writes, one per retrace) and 0x4a720 (the palette scale).
//
//   ornament: picture 57 between the textured surface and the star: shown black, faded in once the position passes
//             0x5500 (+4 per retrace, 64 steps), out after 0x5800 (-4, 64), then cleared;
//   credits:  54 and 53 (swapped on odd minutes), 55, 52, 10, 56: each shown black right after the previous fade-out,
//             faded in when its position passes (0x9500, 0x9600, 0x9700, 0x9800, 0x9a00 (+8 per retrace), 0x9a20)
//             and out at once (the first after 0x9519), the last (56, "NOOON 50 $") held 200 retraces and faded
//             out by -2 per retrace over 128.
// The fades are colour multiplies, evaluated continuously (the level through the middle of the retrace steps), so
// they apply to the redraws (art/) and to the original pixels (--original-art) alike.
#include <vector>

#include "stars/core.h"

namespace {

using namespace stars;

// One picture of a sequence: shown from `shown`, faded in from fadeIn (main's fade_17e96 called at that time) and
// out from fadeOut (the call of fade_out_17eba).
struct Shown {
    int file;
    double shown;
    Fade in, out;
};

class PictureSequence : public Scene {
public:
    void render(Ctx& c, double t) override {
        const Shown* cur = nullptr;
        for (const Shown& s : seq_)
            if (t >= s.shown) cur = &s;
        if (!cur) return;
        double level = t < cur->out.t0 - 1.0 / kRetraceHz ? cur->in.level(t) : cur->out.level(t);
        if (level <= 0) return;
        c.drawPicture(c.picture(cur->file), {.mul = fadeMul(level)});
    }

protected:
    // main's fade_17e96(step, n) called at tc: n + 1 writes from the current level, one per retrace
    static Fade fade(double tc, double from, double step, int n) { return mainFade(tc, from, step, n); }
    static double writes(int n) { return (n + 1) / kRetraceHz; }  // how long main's fade takes
    std::vector<Shown> seq_;
};

class Ornament : public PictureSequence {
public:
    void init(Ctx& c) override {
        std::vector<Cue> q = buildTimeline(c);
        double start = 0;
        for (const Cue& k : q)
            if (k.scene == "ornament") start = k.start;
        double in = std::max(start, c.afterPos(0x5500));
        double out = std::max(in + writes(64), c.afterPos(0x5800));
        seq_.push_back({57, start, fade(in, 0, 4, 64), fade(out, 256, -4, 64)});
        c.picture(57);  // load now
    }
};

class Credits : public PictureSequence {
public:
    void init(Ctx& c) override {
        std::vector<Cue> q = buildTimeline(c);
        double t = 0;
        for (const Cue& k : q)
            if (k.scene == "credits") t = k.start;
        int a = 54, b = 53;  // [4] = minute & 1: odd minutes show BARTi's credit first
        if (c.minute & 1) std::swap(a, b);
        struct Step { int file; int pos; double step; int n; int outPos; };
        const Step steps[] = {{a, 0x9500, 4, 64, 0x9519}, {b, 0x9600, 4, 64, -1}, {55, 0x9700, 4, 64, -1},
                              {52, 0x9800, 4, 64, -1},    {10, 0x9a00, 8, 32, -1}};
        for (const Step& s : steps) {
            double shown = t;
            double in = std::max(t, c.afterPos(s.pos));
            double out = in + writes(s.n);
            if (s.outPos >= 0) out = std::max(out, c.afterPos(s.outPos));
            seq_.push_back({s.file, shown, fade(in, 0, s.step, s.n), fade(out, 256, -4, 64)});
            t = out + writes(64);
        }
        // 56: in at 0x9a20, then 20 retraces, the music stops, 180 retraces, fade out by -2 over 128 retraces
        double in = std::max(t, c.afterPos(0x9a20));
        double out = in + writes(64) + (20 + 180) / kRetraceHz;
        seq_.push_back({56, t, fade(in, 0, 4, 64), fade(out, 256, -2, 128)});
        for (const Shown& s : seq_) c.picture(s.file);  // load now
    }
};

}  // namespace

REGISTER_SCENE(Ornament, "ornament");
REGISTER_SCENE(Credits, "credits");

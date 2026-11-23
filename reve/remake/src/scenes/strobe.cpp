// The strobes (reve/re/notes-parts2d-b.md section 5): a whole-screen picture flashed for one frame at a time on
// black, alternately as it is and as its negative, every 16 frames, then 15, 14 ...; the next script entry holds the
// picture.
//
//   scene "pulse"  entry 5 (init 0x204aef, frame 0x204b2c), entry 6 (frame 0x204bd8)   GFX\PULSE.RAW
//   scene "gene"   entry 20 (init 0x205924, frame 0x205950), entry 21 (frame 0x2059fc) GFX\GENE.RAW
//   scene "bouge"  entry 27 (init 0x205ad8, frame 0x205b04), entry 28 (frame 0x2059fc) GFX\BOUGE.RAW
//
// The frame routines, per drawn frame:  count -= step;  if (count > 0) black;  else { count = 16000; step += 5;
// flag ^= 1; flag ? the picture : the picture through a table }.  The table maps each index to the palette entry
// nearest to its inverse colour (entry 5: GFX\NEGATIF.PAL, which is that table for PULSE's palette; entries 20 and
// 27: computed by 0x25a5ff); the remake shows the exact inverse in colour, 1 - c, so a redraw works too.
//
// The counter steps per drawn frame; the capture draws one frame per refresh in all three entries, so the remake
// steps it once per refresh: frame F (1, 2, ...) is shown from refresh s + F - 1 to s + F, s = the refresh from which
// the capture runs the entry's frames (measured below). Every flash lasts one refresh.
#include <cmath>
#include <vector>

#include "demo.h"
#include "exe.h"

namespace {
struct Strobe {
    const char* scene;
    const char* picture;     // pictures.cpp
    int entry;               // the strobe's script entry; entry + 1 holds the picture
    uint32_t flag, count, step;   // the counter variables (their initial values are in the image)
    uint32_t reload;         // `mov dword ptr [count], imm32` (imm32 at +6): 16000
    uint32_t inc;            // `add dword ptr [step], imm8` (imm8 at +6): 5
};
// The cue starts at the entry's first retrace call S (timeline.cpp: 2737, 14027 and 16722 in the capture, after the
// init loaded the picture; entries 20 and 27 also compute their table): frame F shows from S + F - 1.
// clang-format off
constexpr Strobe kStrobes[] = {
    {"pulse", "pulse",  5, 0x204bcc, 0x204bd0, 0x204bd4, 0x204b4f, 0x204b59},
    {"gene",  "gene",  20, 0x2059f0, 0x2059f4, 0x2059f8, 0x205973, 0x20597d},
    {"bouge", "bouge", 27, 0x205ba4, 0x205ba8, 0x205bac, 0x205b27, 0x205b31},
};
// clang-format on
// The holding entry (6, 21, 28: empty init) draws its first frame after its first retrace: the picture from the
// cue's start + 1 (3138, 14426, 17113 in the capture); until then the strobe's frames go on (black there).
constexpr double kHoldDelay = 1;

enum Kind : uint8_t { kBlack, kPicture, kNegative };

struct StrobeScene : Scene {
    const Strobe* s;
    std::vector<uint8_t> kinds;   // kinds[F - 1]: what frame F shows
    explicit StrobeScene(const Strobe* s_) : s(s_) {}

    void init(Ctx& c) override {
        int32_t count = c.exe->s32(s->count), step = c.exe->s32(s->step), flag = c.exe->s32(s->flag);
        const int32_t reload = c.exe->s32(s->reload + 6), inc = c.exe->s8(s->inc + 6);
        kinds.resize(1 << 14);
        for (uint8_t& k : kinds) {
            count -= step;
            if (count > 0) {
                k = kBlack;
            } else {
                count = reload;
                step += inc;
                flag ^= 1;
                k = flag ? kPicture : kNegative;
            }
        }
    }

    void render(Ctx& c, double r) override {
        const int e = c.cue->entry;
        const bool hold = e != s->entry;
        const Picture& p = c.picture(s->picture);
        if (hold && r >= c.start + kHoldDelay) {
            c.drawPicture(p, {});
            return;
        }
        double first = hold ? c.script()[e - 1].start : c.start;
        if (r < first) return;   // the init: black
        double f = std::floor(r - first);
        uint8_t k = f < double(kinds.size()) ? kinds[size_t(f)] : kBlack;
        if (k == kPicture) c.drawPicture(p, {});
        else if (k == kNegative) c.drawPicture(p, {.mul = Vec3(-1, -1, -1), .add = Vec3(1, 1, 1)});
    }
};

struct Registrar {
    Registrar() {
        for (const Strobe& s : kStrobes) sceneRegistry()[s.scene] = [p = &s] { return (Scene*)new StrobeScene(p); };
    }
} s_registrar;
}  // namespace

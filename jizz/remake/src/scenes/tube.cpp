// The tube object, part 0x15c8b (06:00..08:00, jizz/re/notes-parts3d-b.md section 3): one white page (0x17737),
// then Nix's engine scene 1 (script 0x157fa: a yellow bumpy ring, a pink ball with six spikes, inside a sphere with
// map2) seen from camera path 1 (0x15bef / 0x159fb / 0x154bc), with the timer fade from white 0x17979(0x4ac51b) /
// 0x17999 over it. The colour tables are the plain ones (E 0, S 0x100, F 0) left by main.
#include "demo.h"
#include "jizz/core.h"
#include "jizz/engine.h"
#include "jizz/textures.h"

namespace {

using namespace jizz;

constexpr int kStart = 0x0600;         // event 600 at 06:00
constexpr uint32_t kFadeRate = 0x4ac51b;  // 0x15ced: 0.75 s from white
// The white page 0x17737 is one drawn frame; the part reads its start timer after flipping it. One frame at the
// capture's 23 fps (measured lag of the start timer behind 06:00: 43.1 ms).
constexpr double kWhitePage = 1.0 / 23.0;
constexpr int kDetail = 6;  // segments per original segment (same parametric shapes, smooth silhouettes)

class TubeScene : public Scene {
public:
    void init(Ctx& c) override {
        jizz::prepare(c);
        tex_ = c.shared<Textures>().slotIds(false);
        scene_.build(*c.exe, 1, kDetail);
        c.shared<Engine>();
    }

    void render(Ctx& c, double t) override {
        const double t0 = c.positionTime(kStart) + kWhitePage;  // [0x6542] = -xmpGetTimer(), fade_start
        if (t < t0) {                                           // 0x17737: [0x637c] = 0xffdf, flipped
            c.clear(kWhite);
            return;
        }
        const double clocks = c.timer(t) - c.timer(t0);
        EngineCamera cam = scene_.camera(clocks, false);  // 0x15bef with [0x15c3f] = 0
        EngineShading sh;
        sh.tex = tex_;
        sh.E = 0;
        sh.S = 256;
        sh.F = 0;
        c.clear(kBlack);  // never visible: the background sphere covers the frame
        c.shared<Engine>().render(c, scene_, cam, EngineViewport::full(), sh);
        timerFade(c, t, t0, kFadeRate);  // 0x17999
    }

private:
    EngineScene scene_;
    std::array<GLuint, 18> tex_{};
};

}  // namespace

REGISTER_SCENE(TubeScene, "tube");

// The spiky object over the clouds, part 0x15d20 (09:00..0b:00, jizz/re/notes-parts3d-b.md section 4): Nix's engine
// scene 3 (script 0x15973: a sphere with 24 lobes and 24 dents, map4, inside a sphere of clouds, map8; built by the
// logo part 0x1212a in the original) seen from camera path 3 with the local time starting at 1,000,000 PIT clocks,
// the colour tables 0x1a336(-40) (0x15d48) and the timer fade from white 0x17979(0x4dd47a) / 0x17999.
#include "demo.h"
#include "jizz/core.h"
#include "jizz/engine.h"
#include "jizz/textures.h"

namespace {

using namespace jizz;

constexpr int kStart = 0x0900;            // event 900 at 09:00
constexpr double kClockBase = 1000000.0;  // 0x15d20: [0x6542] = 1000000 - xmpGetTimer()
constexpr uint32_t kFadeRate = 0x4dd47a;  // 0x15d93: 0.72 s from white
constexpr int kDetail = 8;                // segments per original segment (the lobes are 8 half-periods high)

class SpikyScene : public Scene {
public:
    void init(Ctx& c) override {
        jizz::prepare(c);
        tex_ = c.shared<Textures>().slotIds(false);
        scene_.build(*c.exe, 3, kDetail);
        c.shared<Engine>();
    }

    void render(Ctx& c, double t) override {
        const double t0 = c.positionTime(kStart);
        const double clocks = c.timer(t) - c.timer(t0) + kClockBase;
        EngineCamera cam = scene_.camera(clocks, false);  // 0x15d20 clears the wobble flag [0x15c3f] set by 0x1212a
        EngineShading sh;
        sh.tex = tex_;
        sh.E = -40;  // 0x15d48: 0x1f5c5(-40)
        sh.S = 256;
        sh.F = 0;
        c.clear(kBlack);  // never visible: the cloud sphere covers the frame
        c.shared<Engine>().render(c, scene_, cam, EngineViewport::full(), sh);
        timerFade(c, t, t0, kFadeRate);  // 0x17999 (fade tables towards white, 0x1131b)
    }

private:
    EngineScene scene_;
    std::array<GLuint, 18> tex_{};
};

}  // namespace

REGISTER_SCENE(SpikyScene, "spiky");

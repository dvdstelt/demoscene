// Dumps the core library's CPU-side exact data for comparison with emulator snapshots (scratch test, not shipped).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "exe.h"
#include "paper/common.h"
#include "paper/fade.h"
#include "paper/font.h"
#include "paper/osc.h"
#include "paper/palette.h"
#include "paper/peel.h"
#include "paper/textures.h"
#include "paper/warp.h"
using namespace paper;
template <class T> void w(const std::string& n, const std::vector<T>& v) {
    FILE* f = std::fopen(n.c_str(), "wb"); std::fwrite(v.data(), sizeof(T), v.size(), f); std::fclose(f);
}
int main(int argc, char** argv) {
    PaperExe exe; std::string err;
    if (!exe.loadImage(argv[1], &err)) { std::printf("load: %s\n", err.c_str()); return 1; }
    w("cloud256.bin", cloudTexture256(exe));
    w("cloudhi.bin", cloudTextureHi(exe, 1024));
    w("grain.bin", paperGrainBytes(exe));
    w("grainf.bin", paperGrainSmooth(exe));
    FontData fd(exe);
    std::vector<uint8_t> font(29 * 2048, 0);
    for (int g = 0; g < 29; g++) { font[g * 2048] = uint8_t(fd.glyph(g).width);
        for (int i = 0; i < 32 * 60; i++) font[g * 2048 + 1 + i] = fd.glyph(g).bits[i] ? 255 : 0; }
    w("font.bin", font);
    // title warp grids for ticks 110..125 (u, v int32)
    OscSet t = OscSet::title(exe);
    std::vector<int32_t> grids;
    for (int tick = 110; tick < 126; tick++)
        for (auto& v : warpGridExact(warpFieldExact(t, tick), WarpShift::title())) { grids.push_back(v.u); grids.push_back(v.v); }
    w("titlegrids.bin", grids);
    OscSet g = OscSet::greetings(exe);
    std::vector<int32_t> gg;
    for (int tick = 7990; tick < 8006; tick++)
        for (auto& v : warpGridExact(warpFieldExact(g, tick), WarpShift::greetings())) { gg.push_back(v.u); gg.push_back(v.v); }
    w("watergrids.bin", gg);
    // oscillator phases at snapshot ticks (state after IRQ tick-1)
    std::vector<int32_t> ph;
    for (auto [set, tick] : {std::pair<OscSet, int>{t, 119}, {OscSet::teal(exe), 6099}, {OscSet::teal(exe), 6999}, {g, 7999}})
        for (int k = 0; k < 72; k++) { ph.push_back(set.phaseAt(k, tick)); ph.push_back(set.init[k].amp); ph.push_back(set.init[k].speed); }
    w("osc.bin", ph);
    // hue palettes for angles 0..1023, shift 11 and 12
    std::vector<uint8_t> hp;
    for (int s = 11; s <= 12; s++) for (int a = 0; a < 1024; a++) { auto p = huePalette6(a, s); hp.insert(hp.end(), p.begin(), p.end()); }
    w("hue.bin", hp);
    std::vector<int32_t> fl;
    for (int k = 0; k <= 12236; k++) fl.push_back(fadeLevelAt(k));
    w("fade.bin", fl);
    // continuous vs exact control points: max deviation over the title
    double md = 0;
    for (int tick = 106; tick < 1079; tick++) {
        WarpField a = warpFieldExact(t, tick), b = warpField(t, tick);
        for (int R = 0; R < 3; R++) for (int K = 0; K < 3; K++) md = std::max({md, std::abs(a.x[R][K] - b.x[R][K]), std::abs(a.y[R][K] - b.y[R][K])});
    }
    std::printf("title control points: max |exact - continuous| = %.2f\n", md);
    double mn = 0;
    OscSet te = OscSet::teal(exe);
    for (int tick = 6018; tick < 7741; tick++) for (int f0 : {0, 8, 16}) mn = std::max(mn, std::abs(double(noise4Exact(te, f0, tick)) - noise4(te, f0, tick)));
    std::printf("teal noise4: max |exact - continuous| = %.2f\n", mn);
    std::printf("peel p: 5482 %d 5483 %d 5546 %d 5547 %d 5610 %d | teal 6018 %d 6145 %d\n", peelProgressAt(5482, kPeelSceneFirstTick),
        peelProgressAt(5483, kPeelSceneFirstTick), peelProgressAt(5546, kPeelSceneFirstTick), peelProgressAt(5547, kPeelSceneFirstTick),
        peelProgressAt(5610, kPeelSceneFirstTick), peelProgressAt(6018, kPeelTealFirstTick), peelProgressAt(6145, kPeelTealFirstTick));
    for (auto [str, x, y, stride] : {std::tuple<std::string, int, int, int>{"psychic link", 187, 6, 640}, {"present the\x01intro", 16, 1, 320}}) {
        std::printf("layout '%s':", str.c_str());
        for (auto& gp : layoutText(fd, str, x, y, stride)) std::printf(" %c(%g,%g)", 'a' + gp.glyph, gp.x, gp.y);
        std::printf("\n");
    }
    std::printf("advance 'present the' %d, 'intro' %d\n", textAdvance(fd, "present the"), textAdvance(fd, "intro"));
    // big "1996" as 0xfb2 draws it, compare with the snapshot's [0x1ede1] before blurring is impossible; dump bytes
    std::vector<uint8_t> big(64000, 0);
    int x = 2;
    for (char ch : std::string("{||}")) x = fd.drawBytesBig(big.data(), 320, 200, 320, x, 9, ch);
    w("big1996.bin", big);
    return 0;
}

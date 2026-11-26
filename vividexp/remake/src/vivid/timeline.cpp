// The running order and the palette (timeline.h): the scripts replayed against the songs' positions.
#include "vivid/timeline.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

#include "exe.h"
#include "music.h"

namespace vivid {

namespace {
// The script names of the scripts and the modules: read from VIVID.EXE (main 0x4001a0 passes these strings).
constexpr uint32_t kScriptName[2] = {0x82010a, 0x82011b};   // "script/demo1.scr", "script/demo2.scr"

// The Run commands and the scene that draws each.
const std::map<Cmd, std::string>& runScenes() {
    static const std::map<Cmd, std::string> m = {
        {Cmd::RunBITMAPEFEK, "bitmap"}, {Cmd::RunBUMPEFEK, "bump"}, {Cmd::RunZOOMSYNC, "zoom"},
        {Cmd::WaterEffect, "water"},    {Cmd::GenericEffect, "generic"},
    };
    return m;
}
// The Init commands that set the effects up.
bool isInit(Cmd c) {
    return c == Cmd::InitBITMAPEFEK || c == Cmd::InitBUMP || c == Cmd::InitZOOM || c == Cmd::WaterInit ||
           c == Cmd::GenericInit;
}
// Commands that wait for their music position (POS given): the Run commands, WaitSYNC and PutPCXSYNC. InitFADE
// never waits, whatever its POS says (notes-core.md 3).
bool waits(const ScriptLine& l) {
    return l.posGiven && (runScenes().count(l.cmd) || l.cmd == Cmd::WaitSYNC || l.cmd == Cmd::PutPCXSYNC);
}
// Loops that step the palette fade once per refresh (0x401ed0): the waits, the zoom, the bitmap and bump effects.
// The water and generic scenes do not.
bool stepsFade(const ScriptLine& l) {
    return waits(l) && l.cmd != Cmd::WaterEffect && l.cmd != Cmd::GenericEffect;
}
// The ini keys the water (0x405bd0) and generic (0x408cec) inits read the palette with, from their own strings.
constexpr uint32_t kWaterPalette = 0x820612, kWaterEnvMap = 0x8205fb;        // "Palette", "EnvironmentMap"
constexpr uint32_t kGenericPalette = 0x820f52, kGenericBackPic = 0x820ed4;   // "Palette", "BackgroundPic"

// A value of a "key=value" ini file ('#' starts a comment), or "".
std::string iniValue(std::span<const uint8_t> f, const std::string& key) {
    std::string text(f.begin(), f.end()), line;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find_first_of("\r\n", p);
        if (e == std::string::npos) e = text.size();
        line = text.substr(p, e - p);
        p = e + 1;
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        size_t eq = line.find('=', a);
        if (eq == std::string::npos) continue;
        std::string k = line.substr(a, eq - a);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (k != key) continue;
        std::string v = line.substr(eq + 1);
        size_t b = v.find_first_not_of(" \t"), c = v.find_last_not_of(" \t");
        return b == std::string::npos ? "" : v.substr(b, c - b + 1);
    }
    return "";
}
}  // namespace

std::vector<TimedLine> timeScript(const VividExe& exe, int part, const Soundtrack& music) {
    std::vector<ScriptLine> script = parseScript(exe, exe.file(exe.cstr(kScriptName[part - 1])), part);
    const Soundtrack::Song& song = music.song[part - 1];
    std::vector<TimedLine> out;
    double t = song.startRefresh;
    for (ScriptLine& l : script) {
        TimedLine tl{l, t, t};
        if (waits(l)) {   // the wait loop: at least one refresh, then until order >= POS and row >= PAT
            double e = song.reached(l.pos, l.pat, t);
            tl.end = std::isfinite(e) ? e : std::max(t, song.stopRefresh);
        }
        t = tl.end;
        out.push_back(std::move(tl));
    }
    return out;
}

bool Timeline::build(const VividExe& exe, const Soundtrack& music, std::string* err) {
    lines_.clear();
    segs_.clear();
    pal_.clear();
    stepping_.clear();
    Palette black{};
    pal_.push_back({-1e9, black, black, 0, 0, 255});
    for (int part = 0; part < 2; ++part) {
        std::vector<TimedLine> timed;
        try {
            timed = timeScript(exe, part + 1, music);
        } catch (const std::exception& e) {
            if (err) *err = e.what();
            return false;
        }
        const Soundtrack::Song& song = music.song[part];
        double t = song.startRefresh;
        // main sets the whole palette to black before and after each script (0x401fbc)
        pal_.push_back({t, black, black, 0, 0, 255});
        int init = -1;
        Segment pics{"pictures", part + 1, t, t, -1, -1};
        for (TimedLine& tl : timed) {
            const ScriptLine& l = tl.line;
            const Cmd c = l.cmd;
            const int idx = int(lines_.size());
            // palette events and the refreshes in which fades step
            if (stepsFade(l) && tl.end > tl.start) stepping_.push_back({tl.start, tl.end});
            try {
                if (c == Cmd::LoadPAL) {
                    Palette p = toColours(loadPal(exe, l.arg(0)));
                    pal_.push_back({tl.start, p, p, 0, 0, 255});
                } else if (c == Cmd::InitFADE) {
                    Palette a = toColours(loadPal(exe, l.arg(0))), b = toColours(loadPal(exe, l.arg(1)));
                    pal_.push_back({tl.start, a, b, std::max(1, l.argInt(2)), l.argInt(3), l.argInt(4)});
                } else if (c == Cmd::InitPCX) {
                    Indexed p = loadPicture(exe, l.arg(1));
                    if (l.argInt(2) == 0) {
                        Palette c6 = toColours(p.pal);
                        pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                    }
                } else if (c == Cmd::InitBITMAPEFEK && l.argInt(1) == 0) {
                    // InitBITMAPEFEK 0x400ae4 sets the whole palette from the first texture's raw palette (the
                    // colour table's for two 1 x 1 planes), entries 192..255 from the background picture's first 64
                    // colours, and writes the DAC (notes-bitmap.md 3, 8). The ini: TYPE PLANES BPIC x, BPICNAME
                    // DTABNAME x x, P1..P4, T1..T8, whitespace-separated
                    std::span<const uint8_t> ini = exe.file(l.arg(0));
                    std::istringstream words(std::string(ini.begin(), ini.end()));
                    std::vector<std::string> w;
                    for (std::string x; w.size() < 12 && words >> x;) w.push_back(x);
                    if (w.size() < 12) throw std::runtime_error("cannot read " + l.arg(0));
                    const int type = std::atoi(w[0].c_str()), planes = std::atoi(w[1].c_str());
                    const bool bpic = std::atoi(w[2].c_str()) == 1;
                    std::span<const uint8_t> raw = exe.file(type == 1 && planes == 2 ? w[5] : w[8]);
                    std::array<uint8_t, 768> p6{};
                    for (size_t k = 0; k < 768 && 32 + k < raw.size(); ++k) p6[k] = uint8_t(raw[32 + k] >> 2);
                    if (bpic) {
                        std::span<const uint8_t> b = exe.file(w[4]);
                        for (size_t k = 0; k < 64 * 3 && 32 + k < b.size(); ++k) p6[192 * 3 + k] = uint8_t(b[32 + k] >> 2);
                    }
                    Palette c6 = toColours(p6);
                    pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                } else if (c == Cmd::InitBUMP && l.argInt(1) == 0) {
                    // InitBUMP's flag goes to InitPCX for the light picture, the 7th word of its ini: 0 sets that
                    // picture's palette at once (notes-bump.md 2, 8)
                    std::span<const uint8_t> ini = exe.file(l.arg(0));
                    std::istringstream words(std::string(ini.begin(), ini.end()));
                    std::string w, light;
                    for (int k = 0; k < 7 && words >> w; ++k) light = w;
                    Palette c6 = toColours(loadPicture(exe, light).pal);
                    pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                } else if (c == Cmd::WaterEffect && init >= 0) {
                    // the water sets its environment map's palette (or the ini's Palette=) before its first frame
                    // (notes-water.md, 0x413b70); its Init line names the ini
                    std::span<const uint8_t> ini = exe.file(lines_[size_t(init)].line.arg(1));
                    std::string palFile = iniValue(ini, exe.cstr(kWaterPalette));
                    Palette c6 = palFile.empty() ? toColours(loadPicture(exe, iniValue(ini, exe.cstr(kWaterEnvMap))).pal)
                                                 : toColours(loadPal(exe, palFile));
                    pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                } else if (c == Cmd::GenericEffect && init >= 0) {
                    // the 3D scenes send the ini's Palette= (else the background picture's) to the DAC at the start
                    // of GenericEffect (notes-generic.md 9)
                    std::span<const uint8_t> ini = exe.file(lines_[size_t(init)].line.arg(1));
                    std::string palFile = iniValue(ini, exe.cstr(kGenericPalette));
                    Palette c6 = !palFile.empty() ? toColours(loadPal(exe, palFile))
                                                  : toColours(loadPicture(exe, iniValue(ini, exe.cstr(kGenericBackPic))).pal);
                    pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                }
            } catch (const std::exception& e) {
                if (err) *err = std::string("script line ") + std::to_string(l.line) + ": " + e.what();
                return false;
            }
            // segments
            if (isInit(c)) init = idx;
            auto run = runScenes().find(c);
            if (run != runScenes().end()) {
                if (pics.end > pics.start) segs_.push_back(pics);
                segs_.push_back({run->second, part + 1, tl.start, tl.end, idx, init});
                pics = Segment{"pictures", part + 1, tl.end, tl.end, -1, -1};
            } else {
                pics.end = tl.end;
            }
            t = tl.end;
            lines_.push_back(std::move(tl));
        }
        if (pics.end > pics.start) segs_.push_back(pics);
        pal_.push_back({t, black, black, 0, 0, 255});
        end_ = std::max(t, song.stopRefresh);
    }
    // the segments fill the time from the first line to the end
    for (size_t i = 0; i + 1 < segs_.size(); ++i) segs_[i].end = std::max(segs_[i].end, segs_[i + 1].start);
    std::stable_sort(pal_.begin(), pal_.end(), [](const PalEvent& a, const PalEvent& b) { return a.at < b.at; });
    return true;
}

const Segment* Timeline::segmentAt(double r) const {
    for (const Segment& s : segs_)
        if (r >= s.start && r < s.end) return &s;
    return nullptr;
}

double Timeline::steppedRefreshes(double from, double to) const {
    double n = 0;
    for (const auto& [a, b] : stepping_) n += std::max(0.0, std::min(b, to) - std::max(a, from));
    return n;
}

Palette Timeline::paletteAt(size_t ev, double r) const {
    // Event ev is the last one at or before r. A set holds from its refresh on. A fade (0x4020b8) does not write the
    // DAC; the wait loops write it right after each retrace and then step (0x401ed0), so at the j-th refresh a
    // stepping loop sees the DAC shows p1 + min(j - 1, steps) / steps * (p2 - p1) (evaluated in float, between
    // refreshes as a fraction: the original's truncated 16.16 steps are dropped); before the first such refresh it
    // still shows what it showed before the fade (notes-core.md 5).
    const PalEvent& e = pal_[ev];
    if (e.steps <= 0) return e.from;
    double j = steppedRefreshes(e.at, r);
    if (j < 1.0) return ev > 0 ? paletteAt(ev - 1, r) : e.from;
    Palette out = e.from;
    float s = float(std::clamp((j - 1.0) / e.steps, 0.0, 1.0));
    for (int i = std::max(0, e.first); i <= std::min(255, e.last); ++i)
        out[size_t(i)] = e.from[size_t(i)] * (1 - s) + e.to[size_t(i)] * s;
    return out;
}

Palette Timeline::palette(double r) const {
    size_t ev = 0;
    for (size_t i = 0; i < pal_.size(); ++i)
        if (pal_[i].at <= r) ev = i;
    return paletteAt(ev, r);
}

}  // namespace vivid

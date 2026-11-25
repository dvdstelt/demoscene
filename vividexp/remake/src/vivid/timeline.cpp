// The running order and the palette (timeline.h): the scripts replayed against the songs' positions.
#include "vivid/timeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "exe.h"
#include "music.h"

namespace vivid {

namespace {
// The script names of the scripts and the modules: read from VIVID.EXE (main 0x4001a0 passes these strings).
constexpr uint32_t kScriptName[2] = {0x82010a, 0x82011b};   // "script/demo1.scr", "script/demo2.scr"

// The Run commands and the scene that draws each.
const std::map<std::string, std::string>& runScenes() {
    static const std::map<std::string, std::string> m = {
        {"RunBITMAPEFEK", "bitmap"}, {"RunBUMPEFEK", "bump"}, {"RunZOOMSYNC", "zoom"},
        {"WaterEffect", "water"},    {"GenericEffect", "generic"},
    };
    return m;
}
// The Init command that sets up each scene's effect.
const std::map<std::string, std::string>& initScenes() {
    static const std::map<std::string, std::string> m = {
        {"InitBITMAPEFEK", "bitmap"}, {"InitBUMP", "bump"}, {"InitZOOM", "zoom"},
        {"WaterInit", "water"},       {"GenericInit", "generic"},
    };
    return m;
}
// Commands that wait for their music position (POS given): the Run commands, WaitSYNC and PutPCXSYNC.
bool waits(const ScriptLine& l) {
    const std::string& c = l.command();
    return l.posGiven && (runScenes().count(c) || c == "WaitSYNC" || c == "PutPCXSYNC");
}
}  // namespace

std::vector<TimedLine> timeScript(const VividExe& exe, int part, const Soundtrack& music) {
    std::vector<ScriptLine> script = parseScript(exe.file(exe.cstr(kScriptName[part - 1])), part);
    const Soundtrack::Song& song = music.song[part - 1];
    std::vector<TimedLine> out;
    double t = song.startRefresh;
    for (ScriptLine& l : script) {
        TimedLine tl{l, t, t};
        if (waits(l)) {
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
            const std::string& c = l.command();
            const int idx = int(lines_.size());
            // palette events
            try {
                if (c == "LoadPAL") {
                    Palette p = toColours(loadPal(exe, l.arg(0)));
                    pal_.push_back({tl.start, p, p, 0, 0, 255});
                } else if (c == "InitFADE") {
                    Palette a = toColours(loadPal(exe, l.arg(0))), b = toColours(loadPal(exe, l.arg(1)));
                    pal_.push_back({tl.start, a, b, std::max(1, l.argInt(2)), l.argInt(3), l.argInt(4)});
                } else if (c == "InitPCX") {
                    Indexed p = loadPicture(exe, l.arg(1));
                    if (l.argInt(2) == 0) {
                        Palette c6 = toColours(p.pal);
                        pal_.push_back({tl.start, c6, c6, 0, 0, 255});
                    }
                }
            } catch (const std::exception& e) {
                if (err) *err = std::string("script line ") + std::to_string(l.line) + ": " + e.what();
                return false;
            }
            // segments
            if (initScenes().count(c)) init = idx;
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

Palette Timeline::palette(double r) const {
    // the last event at or before r; a fade runs from `from` to `to` over `steps` refreshes for its entries
    const PalEvent* e = &pal_.front();
    for (const PalEvent& p : pal_)
        if (p.at <= r) e = &p;
    Palette out = e->from;
    if (e->steps > 0) {
        float s = float(std::clamp((r - e->at) / e->steps, 0.0, 1.0));
        for (int i = std::max(0, e->first); i <= std::min(255, e->last); ++i)
            out[size_t(i)] = e->from[size_t(i)] * (1 - s) + e->to[size_t(i)] * s;
    }
    return out;
}

}  // namespace vivid

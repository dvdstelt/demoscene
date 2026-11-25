// test_music: checks Vivid Experiment's soundtrack code (src/xmplay.cpp, src/gusdev.cpp, src/gf1.cpp, src/music.cpp)
// against the capture (see vividexp/re/notes-music.md and vividexp/re/models/music), and measures the render.
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [--orig DIR] [--rate N] [--bench N] [--wav FILE] [--out-gus FILE]
//   --re DIR        vividexp/re: the captures (full.gus, full.dram, full.ticks, full.script and part2.*); default: the
//                   vividexp/re of the build (VIVID_RE_DIR). Checks, exactly, with the capture's fake GUS, the runs
//                   as the captures played them (part I's music stops after the retrace at which its script ends,
//                   part II starts 17 interrupts after the next retrace; the ends are the last lines of
//                   full.script / part2.script):
//                     full.gus / part2.gus    every register write with its timer-interrupt stamp, in order
//                     full.dram / part2.dram  the GUS memory at the end (the samples of part II over part I's)
//                     positions               the order and row the capture's main line had copied at every record
//                                             (full.ticks / part2.ticks): a position the player had at most 20
//                                             refreshes before (the copy lags while the main line is busy)
//                   then, on the remake's timeline (renderSoundtrack):
//                     waits                   every WaitSYNC / PutPCXSYNC line of full.script ends at the refresh
//                                             the capture logs for the next line (Song::reached from the line's
//                                             frame); the effects' lines (which poll once per drawn frame) only
//                                             reported
//                     part ends               the parts end where the capture's scripts do
//                   and the card's model (as on hardware) against render.py's counts.
//   --orig DIR      VIVID.EXE, EXPRMNT1.XM and EXPRMNT2.XM (default DIR/../original)
//   --rate N        output rate of the render (default 48000);  --bench N  time renderSoundtrack N times
//   --wav FILE      write the render as a 16-bit stereo WAV;    --out-gus FILE  write the full run's fake-GUS log
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "exe.h"
#include "music.h"

namespace {

constexpr double kPit = 1193182.0;
constexpr double kVga = 25175000.0 / (800.0 * 449.0);
const double kFrame = kPit / kVga;
const double kLine = kFrame / 449.0;

bool fileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool exists(const std::string& path) { return bool(std::ifstream(path)); }

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v;
    size_t a = 0;
    while (a < s.size()) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        v.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return v;
}

std::vector<std::string> words(const std::string& line) {
    std::istringstream ss(line);
    return {std::istream_iterator<std::string>(ss), std::istream_iterator<std::string>()};
}

double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

bool ok = true;
std::string firstFailure;

void report(bool good, const std::string& what, const std::string& detail) {
    std::printf("%-10s %s: %s\n", good ? "OK" : "MISMATCH", what.c_str(), detail.c_str());
    if (!good && firstFailure.empty()) firstFailure = what + ": " + detail;
    ok &= good;
}

std::string fmt(const char* f, ...) {
    char b[1024];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

bool writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::vector<uint8_t> h(44);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) h[at + size_t(i)] = uint8_t(v >> (8 * i)); };
    auto put16 = [&](size_t at, uint32_t v) { for (int i = 0; i < 2; i++) h[at + size_t(i)] = uint8_t(v >> (8 * i)); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    std::memcpy(&h[0], "RIFF", 4);
    put32(4, 36 + bytes);
    std::memcpy(&h[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, uint32_t(rate));
    put32(28, uint32_t(rate) * 4);
    put16(32, 4);
    put16(34, 16);
    std::memcpy(&h[36], "data", 4);
    put32(40, bytes);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(h.data()), 44);
    f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
    return bool(f);
}

double retrace(int64_t k) { return double(k) * kFrame + 412.0 * kLine + 1e-4; }

// A line of a capture's .script: "frame time_s part line POS PAT COMMAND args"
struct ScriptLine {
    int frame = 0, part = 0, line = 0, pos = -1, pat = -1;
    double time = 0;
    std::string command;
};

std::vector<ScriptLine> readScript(const std::string& path) {
    std::vector<ScriptLine> out;
    std::ifstream f(path);
    std::string s;
    while (std::getline(f, s)) {
        if (s.empty() || s[0] == '#') continue;
        const std::vector<std::string> w = words(s);
        if (w.size() < 7) continue;
        ScriptLine l;
        l.frame = std::atoi(w[0].c_str());
        l.time = std::atof(w[1].c_str());
        l.part = std::atoi(w[2].c_str());
        l.line = std::atoi(w[3].c_str());
        if (w[4] != "xxx") l.pos = std::atoi(w[4].c_str());
        if (w[5] != "xxx") l.pat = std::atoi(w[5].c_str());
        l.command = w[6];
        out.push_back(l);
    }
    return out;
}

// The refresh at which the last line of each part of a capture's script runs (after the last wait ends).
std::map<int, int> partEnds(const std::vector<ScriptLine>& script) {
    std::map<int, int> e;
    for (const ScriptLine& l : script) e[l.part] = l.frame;
    return e;
}

// The capture's register log against the remake's, line by line.
void compareGus(const std::string& name, const std::string& path, const std::string& mine) {
    std::vector<uint8_t> raw;
    if (!fileBytes(path, raw)) return report(false, name, "cannot read " + path);
    const std::vector<std::string> ref = splitLines(std::string(raw.begin(), raw.end())), got = splitLines(mine);
    size_t i = 0;
    while (i < ref.size() && i < got.size() && ref[i] == got[i]) i++;
    const bool same = i == ref.size() && i == got.size();
    report(same, name,
           same ? fmt("%zu register writes identical, with their interrupt stamps (last stamp %s)", ref.size(),
                      ref.empty() ? "-" : ref.back().substr(0, ref.back().find(' ')).c_str())
                : fmt("capture %zu, remake %zu writes; identical for the first %zu; at line %zu: capture '%s', "
                      "remake '%s'",
                      ref.size(), got.size(), i, i + 1, i < ref.size() ? ref[i].c_str() : "-",
                      i < got.size() ? got[i].c_str() : "-"));
}

void compareDram(const std::string& name, const std::string& path, const std::vector<uint8_t>& mine) {
    std::vector<uint8_t> ref;
    if (!fileBytes(path, ref)) return report(false, name, "cannot read " + path);
    size_t diff = 0, first = 0;
    for (size_t k = 0; k < std::min(mine.size(), ref.size()); k++)
        if (mine[k] != ref[k] && diff++ == 0) first = k;
    report(diff == 0 && mine.size() == ref.size(), name,
           fmt("%zu of %zu bytes differ%s", diff, ref.size(), diff ? fmt(" (first at %#zx)", first).c_str() : ""));
}

// player.py's check_positions: the position the capture's main line had copied (0x400104) at each record against
// the player's, at the record and at the retrace before it; and how many refreshes ago the player left it.
void comparePositions(const std::string& name, const std::string& path, const MusicTrace& tr,
                      const std::vector<int>& startIrq) {
    std::ifstream f(path);
    if (!f) return report(false, name, "cannot read " + path);
    std::vector<double> times;
    for (const MusicTrace::Tick& t : tr.ticks) times.push_back(t.clock);
    auto lastTick = [&](double t) { return int(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1; };
    std::string s;
    size_t n = 0, atRecord = 0, atRetrace = 0, either = 0, never = 0;
    double maxLag = 0;
    int maxLagRecord = -1, firstNever = -1;
    while (std::getline(f, s)) {
        if (s.empty() || s[0] == '#') continue;
        const std::vector<std::string> w = words(s);
        if (w.size() < 6) continue;
        const int k = std::atoi(w[0].c_str()), o = std::atoi(w[2].c_str()), r = std::atoi(w[3].c_str());
        const int part = std::atoi(w[5].c_str());
        const double tr0 = double(k + 1) * kFrame;
        const int i = lastTick(tr0);
        const int start = part >= 1 && size_t(part) <= startIrq.size() ? startIrq[size_t(part - 1)] : 0;
        if (i < 0 || tr.ticks[size_t(i)].irq < start) continue;
        n++;
        const MusicTrace::Tick& a = tr.ticks[size_t(i)];
        const int i2 = lastTick(retrace(k));
        const bool same1 = a.order == o && a.row == r;
        const bool same2 = i2 >= 0 && tr.ticks[size_t(i2)].order == o && tr.ticks[size_t(i2)].row == r;
        atRecord += same1;
        atRetrace += same2;
        either += same1 || same2;
        double lag = -1;
        for (int j = i; j >= 0 && tr0 - times[size_t(j)] < 20 * kFrame; j--)
            if (tr.ticks[size_t(j)].order == o && tr.ticks[size_t(j)].row == r) {
                lag = j == i ? 0.0 : (tr0 - times[size_t(j) + 1]) / kFrame;
                break;
            }
        if (lag < 0) {
            if (never++ == 0) firstNever = k;
        } else if (lag > maxLag) {
            maxLag = lag;
            maxLagRecord = k;
        }
    }
    report(n > 0 && never == 0, name,
           fmt("%zu records with the music playing: the copied position is the player's at the record in %zu, at the "
               "retrace before it in %zu, one of the two in %zu; the others are older copies, at most %.2f refreshes "
               "old (record %d); never the player's in the 20 refreshes before: %zu%s",
               n, atRecord, atRetrace, either, maxLag, maxLagRecord, never,
               never ? fmt(" (first: record %d)", firstNever).c_str() : ""));
}

void checkCapture(const VividExe& exe, const std::vector<uint8_t>& xm1, const std::vector<uint8_t>& xm2,
                  const std::string& reDir, const std::string& outGus) {
    // ---- the whole demo ----
    const std::map<int, int> ends = partEnds(readScript(reDir + "/full.script"));
    if (!ends.count(1) || !ends.count(2)) return report(false, "full.script", "no lines of parts 1 and 2");
    const MusicPart full[2] = {{xm1, 4, 31, ends.at(1)}, {xm2, ends.at(1) + 1, 17, ends.at(2)}};
    auto t0 = std::chrono::steady_clock::now();
    const MusicTrace tr = traceMusic(exe, full, true);
    std::printf("traceMusic (whole demo, fake GUS, parts ending at records %d and %d): %zu ticks, stops after "
                "interrupts %d and %d, %.1f ms\n",
                ends.at(1), ends.at(2), tr.ticks.size(), tr.stopIrq[0], tr.stopIrq[1], since(t0) * 1e3);
    if (!outGus.empty()) {
        std::ofstream f(outGus, std::ios::binary);
        f << tr.gus;
    }
    compareGus("full.gus", reDir + "/full.gus", tr.gus);
    compareDram("full.dram", reDir + "/full.dram", tr.dram);
    comparePositions("positions", reDir + "/full.ticks", tr, {31, tr.stopIrq[0] + 17});

    // ---- vivid.exe 2: part II alone ----
    const std::map<int, int> ends2 = partEnds(readScript(reDir + "/part2.script"));
    if (ends2.empty()) return report(false, "part2.script", "no lines");
    const MusicPart part2[1] = {{xm2, 4, 17, ends2.begin()->second}};
    const MusicTrace tr2 = traceMusic(exe, part2, true);
    compareGus("part2.gus", reDir + "/part2.gus", tr2.gus);
    compareDram("part2.dram", reDir + "/part2.dram", tr2.dram);
    comparePositions("part2 positions", reDir + "/part2.ticks", tr2, {17});

    // ---- the card's model (as on hardware): render.py writes 320,835 registers and runs 137,313 wait passes ----
    const MusicTrace hw = traceMusic(exe, full, false);
    report(hw.writes == 320835 && hw.passes == 137313, "card model",
           fmt("%lld register writes, %lld passes of the ramp waits (render.py: 320,835 and 137,313)",
               (long long)hw.writes, (long long)hw.passes));
}

// The remake's positions against the capture's script: every wait that polls once per refresh ends where the
// capture's next line runs.
void checkWaits(const Soundtrack& s, const std::string& reDir) {
    const std::vector<ScriptLine> script = readScript(reDir + "/full.script");
    size_t n = 0, bad = 0, effects = 0;
    int firstBad = -1, worstEffect = 0, worstLine = -1;
    std::string detail;
    for (size_t i = 0; i + 1 < script.size(); i++) {
        const ScriptLine& l = script[i];
        const ScriptLine& next = script[i + 1];
        if (l.pos < 0 || next.part != l.part || l.part < 1 || l.part > 2) continue;
        const bool polls = l.command == "WaitSYNC" || l.command == "PutPCXSYNC";
        const bool effect = l.command.rfind("Run", 0) == 0 || l.command == "WaterEffect" || l.command == "GenericEffect";
        if (!polls && !effect) continue;
        // the first poll: at the retrace after the next frame-counter change (the refresh interrupt S clocks after
        // the retrace before the line)
        const double t = l.time * kPit, tick = retrace(l.frame - 1) + double((0x3b6 * 34049 / 1000) >> 1);
        const double from = t < tick ? l.frame - 1 : l.frame;
        const double e = s.song[l.part - 1].reached(l.pos, l.pat, from);
        const int d = int(next.frame - e);
        if (polls) {
            n++;
            if (d != 0 && bad++ == 0) {
                firstBad = l.line;
                detail = fmt("part %d line %d (%s %03d %03d at %d): remake %.0f, capture %d", l.part, l.line,
                             l.command.c_str(), l.pos, l.pat, l.frame, e, next.frame);
            }
        } else {
            effects++;
            if (std::abs(d) > std::abs(worstEffect)) {
                worstEffect = d;
                worstLine = l.line;
            }
        }
    }
    report(n > 0 && bad == 0, "waits",
           bad ? fmt("%zu of %zu differ; first: %s", bad, n, detail.c_str())
               : fmt("all %zu WaitSYNC / PutPCXSYNC lines of full.script end at the capture's refresh; the %zu effect "
                     "lines (polling per drawn frame) end up to %d refreshes after the position (line %d)",
                     n, effects, worstEffect, worstLine));
    (void)firstBad;
}

void measure(const Soundtrack& s) {
    const size_t frames = s.pcm.size() / 2;
    int peak = 0;
    double sum = 0, sq = 0;
    for (size_t i = 0; i < s.pcm.size(); i += 2) {
        const int v = s.pcm[i];
        peak = std::max(peak, std::abs(v));
        sum += v;
        sq += double(v) * v;
    }
    std::printf("  level: peak %d (%.2f dBFS), RMS %.1f dBFS, DC %.2f LSB; channels identical (mono)\n", peak,
                20 * std::log10(std::max(peak, 1) / 32767.0),
                10 * std::log10(std::max(sq, 1.0) / double(std::max<size_t>(frames, 1)) / (32767.0 * 32767.0)),
                sum / double(std::max<size_t>(frames, 1)));
    std::printf("  RMS per 10 s (dBFS):");
    for (size_t a = 0, k = 0; a < frames; a += size_t(s.rate) * 10, k++) {
        const size_t b = std::min(frames, a + size_t(s.rate) * 10);
        double e = 0;
        for (size_t f = a; f < b; f++) e += double(s.pcm[2 * f]) * s.pcm[2 * f];
        const double rms = std::sqrt(e / double(b - a)) / 32767.0;
        if (k % 10 == 0) std::printf("\n   ");
        std::printf(" %6.1f", rms > 0 ? 20 * std::log10(rms) : -999.0);
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string reDir, origDir, wavOut, outGus;
    int rate = 48000, bench = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--re") reDir = next();
        else if (a == "--orig") origDir = next();
        else if (a == "--wav") wavOut = next();
        else if (a == "--rate") rate = std::atoi(next().c_str());
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else if (a == "--out-gus") outGus = next();
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
#ifdef VIVID_RE_DIR
    if (reDir.empty() && exists(std::string(VIVID_RE_DIR) + "/full.gus")) reDir = VIVID_RE_DIR;
#endif
    if (reDir.empty())
        for (const char* d : {"re", "../re", "../../re", "vividexp/re"})
            if (exists(std::string(d) + "/full.gus")) {
                reDir = d;
                break;
            }
    if (origDir.empty()) origDir = (reDir.empty() ? std::string("re") : reDir) + "/../original";

    VividExe exe;
    std::string err;
    if (!exe.load(origDir + "/VIVID.EXE", &err)) {
        std::fprintf(stderr, "%s/VIVID.EXE: %s\n", origDir.c_str(), err.c_str());
        return 1;
    }
    std::vector<uint8_t> xm1, xm2;
    if (!fileBytes(origDir + "/EXPRMNT1.XM", xm1) || !fileBytes(origDir + "/EXPRMNT2.XM", xm2)) {
        std::fprintf(stderr, "cannot read EXPRMNT1.XM and EXPRMNT2.XM in %s (use --orig)\n", origDir.c_str());
        return 1;
    }
    std::printf("originals: %s; capture: %s\n", origDir.c_str(), reDir.empty() ? "(none)" : reDir.c_str());

    try {
        if (!reDir.empty()) checkCapture(exe, xm1, xm2, reDir, outGus);

        // the parts end where the scripts' last waits end: the capture's last wait lines, played on the remake's
        // positions from the frames the capture logs for them
        std::map<int, ScriptLine> lastWait;
        std::map<int, int> ends;
        if (!reDir.empty()) {
            const std::vector<ScriptLine> script = readScript(reDir + "/full.script");
            for (const ScriptLine& l : script)
                if (l.pos >= 0) lastWait[l.part] = l;
            ends = partEnds(script);
        }
        const ScriptEnd scriptEnd = [&](int part, const Soundtrack& m) {
            auto it = lastWait.find(part);
            if (it == lastWait.end()) throw std::runtime_error("no capture script to end the parts");
            return m.song[part - 1].reached(it->second.pos, it->second.pat, it->second.frame);
        };
        Soundtrack s;
        double best = 1e9;
        for (int b = 0; b < bench; b++) {
            const auto t0 = std::chrono::steady_clock::now();
            s = renderSoundtrack(exe, xm1, xm2, rate, scriptEnd);
            best = std::min(best, since(t0));
        }
        const size_t frames = s.pcm.size() / 2;
        std::printf("renderSoundtrack: %zu frames at %d Hz (%.3f s: refresh %.0f to %.0f); part II from record %.0f; "
                    "best of %d: %.1f ms\n",
                    frames, s.rate, double(frames) / s.rate, s.song[0].startRefresh, s.endRefresh,
                    s.song[1].startRefresh, bench, best * 1e3);
        if (!reDir.empty()) {
            report(s.song[0].stopRefresh == ends[1] && s.song[1].stopRefresh == ends[2] &&
                       s.song[1].startRefresh == ends[1] + 12,
                   "part ends",
                   fmt("part I ends at record %.0f (capture %d), part II starts in %.0f and ends at %.0f (capture %d)",
                       s.song[0].stopRefresh, ends[1], s.song[1].startRefresh, s.song[1].stopRefresh, ends[2]));
            checkWaits(s, reDir);
        }
        measure(s);
        if (!wavOut.empty()) report(writeWav(wavOut, s.pcm, s.rate), "WAV", "written to " + wavOut);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    if (ok) std::printf("ALL MATCH\n");
    else std::printf("MISMATCH (first: %s)\n", firstFailure.c_str());
    return ok ? 0 : 1;
}

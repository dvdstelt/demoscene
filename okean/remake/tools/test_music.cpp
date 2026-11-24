// test_music: checks the soundtrack code (src/modplay.cpp, src/gf1.cpp, src/music.cpp) and the tick times of
// src/schedule.cpp against the capture and the Python model okean/re/models/music/okeanplay.py (see
// okean/re/notes-music.md and the README in okean/re/models/music).
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [options]
//   --re DIR        okean/re: reads DIR/../original/KORSO2.EXE and the capture DIR/full.gus, full.dram, full.ticks
//                   (default: the source tree's okean/re)
//   --exe F, --gus F, --dram F, --ticks F   override single files ("" skips the checks that need it)
//   --pywav F       the mix at the GF1's own rate against the WAV of `okeanplay.py --wav F` (16-bit, 0.9 / peak;
//                   default: DIR/models/music/okean_music.wav if it exists, "" skips it)
//   --wav F         write the soundtrack (48 kHz stereo 16-bit)
//   --out-gus F     write the register log of the capture-schedule run
//   --bench N       time the start-up's music work (songTicks, buildSchedule, renderSoundtrack) N times (best)
//
// Checks:
//   1. the player with the capture's interrupt service times (the `irq` lines of full.gus): the GUS register log from
//      the GUS init to the stop line by line against full.gus, the GUS DRAM after the upload against full.dram, and
//      the music position (order, row, tick countdown) after every timer interrupt against full.ticks;
//   2. songTicks (the positions the schedule is computed from) against the positions of that run;
//   3. schedule.cpp: its tick times against the capture's service times of the timer interrupts, and its master
//      volume steps against the end part's callback replayed on the capture's tick times (as okeanplay.py does);
//   4. the player on the remake's schedule (schedule.cpp's tick times, ramp IRQs at the card model's ramp ends, the
//      schedule's master volume and stop): its register log against full.gus without the irq lines' times;
//   5. the soundtrack: render time, level, clipping, silence; per-tick RMS against okeanplay.py's WAV;
//   6. the mix at the card's own rate on okeanplay.py's hardware schedule against its WAV, sample by sample.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "exe.h"
#include "gf1.h"
#include "modplay.h"
#include "music.h"
#include "schedule.h"

#ifndef OKEAN_RE_DIR
#define OKEAN_RE_DIR "../../re"
#endif

namespace {

constexpr double kTick = 62 * 320e-6;                    // GUS timer 2 at count 0xc2
constexpr double kFrame = 808 * 625 / 25.175e6;          // okeanplay.py FRAME: one refresh of the 50 Hz mode
constexpr double kRetrace = 502 * 808 / 25.175e6;        // RETRACE: the callbacks, 16.112 ms into the refresh
constexpr uint32_t kMasterAddr = 0x12b8c;
constexpr uint32_t kBrightAddr = 0x3ceac;                // the end part's brightness, counted down with the master

bool readBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

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

std::vector<std::string> logLines(const std::string& path) {
    std::vector<uint8_t> raw;
    std::vector<std::string> v;
    if (!readBytes(path, raw)) return v;
    for (std::string& s : splitLines(std::string(raw.begin(), raw.end())))
        if (!s.empty() && s[0] != '#') v.push_back(std::move(s));
    return v;
}

double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

bool ok = true;

void report(bool good, const char* what, const std::string& detail) {
    std::printf("%-9s %s: %s\n", good ? "OK" : "MISMATCH", what, detail.c_str());
    std::fflush(stdout);
    ok &= good;
}

std::string fmt(const char* f, ...) {
    char b[2048];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

bool writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };  // (little-endian hosts)
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(bytes));
    return bool(f);
}

bool readWav16(const std::string& path, std::vector<int16_t>& out) {
    std::vector<uint8_t> raw;
    if (!readBytes(path, raw) || raw.size() < 12) return false;
    size_t at = 12;
    while (at + 8 <= raw.size()) {
        uint32_t sz;
        std::memcpy(&sz, &raw[at + 4], 4);
        if (!std::memcmp(&raw[at], "data", 4)) {
            size_t len = std::min<size_t>(sz, raw.size() - at - 8);
            out.resize(len / 2);
            std::memcpy(out.data(), &raw[at + 8], out.size() * 2);
            return true;
        }
        at += 8 + sz + (sz & 1);
    }
    return false;
}

// the `irq` lines of a full.gus-format log: kind and time
std::vector<modplay::CaptureIrq> readIrqs(const std::vector<std::string>& lines) {
    std::vector<modplay::CaptureIrq> out;
    for (const std::string& ln : lines) {
        int tick;
        char kind;
        double t;
        if (std::sscanf(ln.c_str(), "%d irq %c %lf", &tick, &kind, &t) == 3) out.push_back({kind, t});
    }
    return out;
}

// a log line without the time of an irq line
std::string noTime(const std::string& s) {
    const size_t i = s.find(" irq ");
    return i == std::string::npos ? s : s.substr(0, i + 6);
}

// The end part's callback (0x4d6b1..0x4d70b) once per refresh, as okeanplay.py's class Fade models it: at the
// retrace of refresh k (k x FRAME + RETRACE), while the order [0x12b75] is 0x1e and the row word >= 0x20, the
// brightness [0x3ceac] (32) counts down and with it the master volume [0x12b8c]; otherwise the brightness counts back
// up to 32. The position at a callback is the one after the last tick served at or before it. Returns the decrements
// (time, master from then on) up to time `end`.
std::vector<std::pair<double, int>> fadeSteps(const std::vector<double>& tickTime, const std::vector<int>& pos,
                                              int master0, int bright0, double end) {
    std::vector<std::pair<double, int>> out;
    int cnt = bright0, master = master0;
    for (long k = 0;; k++) {
        const double tc = double(k) * kFrame + kRetrace;
        if (tc >= end) break;
        const auto it = std::upper_bound(tickTime.begin(), tickTime.end(), tc);
        const size_t n = size_t(it - tickTime.begin());
        const int p = n ? pos[n - 1] : 0;
        if (p % 64 >= 0x20 && p / 64 == 0x1e) {
            if (cnt) {
                cnt--;
                if (master > 0) out.push_back({tc, --master});
            }
        } else if (cnt != 0x20) {
            cnt++;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string re = OKEAN_RE_DIR, exePath, gusRef, dramRef, ticksRef, pyWav, outWav, outGus;
    bool setExe = false, setGus = false, setDram = false, setTicks = false, setPyWav = false;
    int bench = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--re") re = next();
        else if (a == "--exe") exePath = next(), setExe = true;
        else if (a == "--gus") gusRef = next(), setGus = true;
        else if (a == "--dram") dramRef = next(), setDram = true;
        else if (a == "--ticks") ticksRef = next(), setTicks = true;
        else if (a == "--pywav") pyWav = next(), setPyWav = true;
        else if (a == "--wav") outWav = next();
        else if (a == "--out-gus") outGus = next();
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
    if (!setExe) exePath = re + "/../original/KORSO2.EXE";
    if (!setGus) gusRef = re + "/full.gus";
    if (!setDram) dramRef = re + "/full.dram";
    if (!setTicks) ticksRef = re + "/full.ticks";
    if (!setPyWav) {
        pyWav = re + "/models/music/okean_music.wav";
        if (!std::ifstream(pyWav)) pyWav.clear();
    }
    OkeanExe exe;
    std::string err;
    if (!exe.load(exePath, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const int master0 = int(exe.u32(kMasterAddr)), bright0 = int(exe.u32(kBrightAddr));

    // the song's positions (what schedule.cpp works from) and the schedule
    SongTicks song;
    IntroSchedule sched;
    try {
        song = songTicks(exe, 9400);
        sched = buildSchedule(song);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "songTicks / buildSchedule: %s\n", e.what());
        return 1;
    }

    // ---- 1. the player with the capture's interrupt service times ----
    const std::vector<std::string> ref = gusRef.empty() ? std::vector<std::string>{} : logLines(gusRef);
    const std::vector<modplay::CaptureIrq> irqs = readIrqs(ref);
    std::vector<double> capTick;  // the capture's service time of player tick j (timer interrupt j + 4)
    {
        int n = 0;
        for (const auto& q : irqs)
            if (q.kind == 't' && ++n > modplay::kStartAfter) capTick.push_back(q.time);
    }
    std::vector<std::pair<double, int>> capFade;  // the end part's fade on the capture's tick times
    modplay::Run run;
    if (!irqs.empty()) {
        capFade = fadeSteps(capTick, song.pos, master0, bright0, irqs.back().time + 1.0);  // (all 32 steps)
        gus::Card card;
        std::string log;
        log.reserve(size_t(4) << 20);
        card.log = &log;
        modplay::Options opt;
        opt.capture = &irqs;
        opt.master = [&](double t) {
            int m = master0;
            for (const auto& s : capFade)
                if (s.first < t) m = s.second;
            return m;
        };
        auto t0 = std::chrono::steady_clock::now();
        try {
            modplay::run(exe, card, opt, run);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "player: %s\n", e.what());
            return 1;
        }
        std::printf("player (capture schedule): %d interrupts, %zu timer, %zu log bytes in %.1f ms\n", run.irqs,
                    run.pos.size(), log.size(), since(t0) * 1e3);
        if (!outGus.empty()) std::ofstream(outGus, std::ios::binary).write(log.data(), std::streamsize(log.size()));
        const std::vector<std::string> mine = splitLines(log);
        size_t n = std::min(mine.size(), ref.size()), i = 0;
        while (i < n && mine[i] == ref[i]) i++;
        if (i == n && mine.size() == ref.size())
            report(true, "full.gus", fmt("%zu lines (register and port writes, irq lines with their times), identical "
                                         "(GUS init to stop)", ref.size()));
        else
            report(false, "full.gus", fmt("%zu vs %zu lines, first difference at line %zu: '%s' vs '%s'", mine.size(),
                                          ref.size(), i + 2, i < mine.size() ? mine[i].c_str() : "-",
                                          i < ref.size() ? ref[i].c_str() : "-"));
        report(run.early == 0 && card.lost == 7 && run.notes == 2494 && run.ramps == 2494, "interrupts",
               fmt("%d after the start, %d served more than 1.5 us after their event (max %.3f ms), %d before it; %d "
                   "timer overflows lost; %d note starts, %d ramps; master volume %d -> %d in %zu steps (%.4f .. "
                   "%.4f s)",
                   run.irqs - modplay::kStartAfter, run.late, run.maxLate * 1e3, run.early, card.lost, run.notes,
                   run.ramps, master0, capFade.empty() ? master0 : capFade.back().second, capFade.size(),
                   capFade.empty() ? 0.0 : capFade.front().first, capFade.empty() ? 0.0 : capFade.back().first));
        if (!dramRef.empty()) {
            std::vector<uint8_t> d;
            if (!readBytes(dramRef, d)) {
                report(false, "full.dram", "cannot read " + dramRef);
            } else {
                size_t diff = 0, m = std::min(d.size(), card.dram().size());
                for (size_t k = 0; k < m; k++) diff += d[k] != card.dram()[k];
                report(diff == 0 && d.size() == card.dram().size(), "full.dram",
                       fmt("%zu bytes, %zu differ (samples 0..0x%x, probe bytes 0x3ffff, 0x7ffff, 0xbffff, 0xfffff)",
                           d.size(), diff, unsigned(run.dramEnd)));
            }
        }
    } else if (!gusRef.empty()) {
        report(false, "full.gus", "cannot read " + gusRef);
    }
    if (!ticksRef.empty() && !irqs.empty()) {
        std::ifstream f(ticksRef);
        if (!f) {
            report(false, "full.ticks", "cannot read " + ticksRef);
        } else {
            std::string line;
            size_t n = 0, bad = 0;
            long firstBad = -1;
            while (std::getline(f, line)) {
                if (line.empty() || line[0] == '#') continue;
                long frame, order, row, cnt, g;
                double ts;
                if (std::sscanf(line.c_str(), "%ld %lf %ld %ld %ld %ld", &frame, &ts, &order, &row, &cnt, &g) != 6)
                    continue;
                if (g < modplay::kStartAfter || size_t(g) > run.pos.size()) continue;
                n++;
                const modplay::Position c{int(order), int(row), int(cnt)};
                if (run.pos[size_t(g - 1)] != c && !(g == modplay::kStartAfter && run.prestart == c))
                    if (bad++ == 0) firstBad = frame;
            }
            report(n > 0 && bad == 0, "full.ticks",
                   fmt("position (order, row, countdown) after the last timer interrupt of %zu capture records, %zu "
                       "differ (first: record %ld)", n, bad, firstBad));
        }
    }

    // ---- 2. songTicks against the run ----
    if (!run.pos.empty()) {
        size_t n = 0, bad = 0;
        for (size_t j = 0; j < song.pos.size() && j + modplay::kStartAfter < run.pos.size(); j++, n++) {
            const modplay::Position& p = run.pos[j + modplay::kStartAfter];
            bad += song.pos[j] != p.order * 64 + p.row;
        }
        size_t rows = 0;
        for (size_t j = 0; j < n; j++) rows += song.newRow[j];
        report(bad == 0, "songTicks", fmt("%zu player ticks (%zu rows) against the capture-schedule run: %zu differ",
                                          n, rows, bad));
    }

    // ---- 3. schedule.cpp against the capture ----
    const MusicSchedule& ms = sched.music;
    if (!capTick.empty()) {
        size_t n = std::min(ms.tickTime.size(), capTick.size()), over = 0, early = 0, known = 0;
        double maxd = 0;
        std::string list;
        for (size_t j = 0; j < n; j++) {
            const double d = capTick[j] - ms.tickTime[j];
            // The capture dropped the star part's last frame (notes-core.md 4, "Dropped frames"): its main loop saw
            // order 23 one refresh late, so the sheet's timer calibration, which holds a tick off and loses one,
            // started a refresh later (6893 instead of 6892). The remake draws every refresh; ticks 6961 and 6962
            // (interrupts 6965, 6966) differ by that refresh.
            if (j == 6961 || j == 6962) {
                known++;
                continue;
            }
            if (std::fabs(d) > 0.2e-3) {
                over++;
                list += fmt(" tick %zu (interrupt %zu): capture %.6f s, schedule %.6f s;", j, j + 4, capTick[j],
                            ms.tickTime[j]);
            } else {
                maxd = std::max(maxd, std::fabs(d));
            }
            early += d < -1e-6;  // (the capture can only be later: its CPU latency)
        }
        report(ms.tickTime.size() == capTick.size() && over == 0 && early == 0, "schedule",
               fmt("%zu tick times (capture: %zu player ticks) against the capture's service times: %zu within %.3f "
                   "ms, %zu over 0.2 ms,%s %zu earlier in the capture than in the schedule, %zu not compared (the "
                   "capture's dropped frame at the star part's exit); stop at %.6f s (refresh %d)",
                   ms.tickTime.size(), capTick.size(), n - over - known, maxd * 1e3, over, list.c_str(), early, known,
                   ms.stopTime, sched.exitRefresh));
        std::string parts;
        for (const PartSchedule& p : sched.parts)
            parts += fmt(" %s %d/%d/%d", p.scene.c_str(), p.enter, p.first, p.end);
        std::printf("          parts (enter / first callback / exit refresh):%s\n", parts.c_str());
        size_t same = 0;
        double maxt = 0;
        for (size_t k = 0; k < std::min(ms.master.size(), capFade.size()); k++) {
            same += ms.master[k].second == capFade[k].second;
            maxt = std::max(maxt, std::fabs(ms.master[k].first - capFade[k].first));
        }
        report(ms.master.size() == capFade.size() && same == capFade.size() && maxt < 1e-9, "fade",
               fmt("%zu master volume steps (the end part's callback on the capture's tick times: %zu), %zu with the "
                   "same value, times within %.2g s",
                   ms.master.size(), capFade.size(), same, maxt));
    }

    // ---- 4. the player on the remake's schedule against full.gus (without the irq times) ----
    if (!ref.empty()) {
        gus::Card card;
        std::string log;
        log.reserve(size_t(4) << 20);
        card.log = &log;
        modplay::Options opt;
        opt.tickTime = &ms.tickTime;
        opt.stopTime = ms.stopTime;
        opt.master = [&](double t) {
            int m = master0;
            for (const auto& s : ms.master)
                if (s.first < t) m = s.second;
            return m;
        };
        modplay::Run r;
        modplay::run(exe, card, opt, r);
        const std::vector<std::string> mine = splitLines(log);
        size_t n = std::min(mine.size(), ref.size()), i = 0;
        while (i < n && noTime(mine[i]) == noTime(ref[i])) i++;
        const bool same = i == n && mine.size() == ref.size();
        report(same && r.early == 0, "remake schedule",
               same ? fmt("ramp IRQs at the card's ramp ends, ticks at schedule.cpp's times: %zu lines identical to "
                          "full.gus but for the irq times; %d interrupts served more than 1.5 us after their event "
                          "(max %.3f ms), %d before it; %d timer overflows lost",
                          mine.size(), r.late, r.maxLate * 1e3, r.early, card.lost)
                    : fmt("%zu vs %zu lines, first difference at line %zu: '%s' vs '%s'", mine.size(), ref.size(), i + 2,
                          i < mine.size() ? mine[i].c_str() : "-", i < ref.size() ? ref[i].c_str() : "-"));
    }

    // ---- 5. the soundtrack ----
    Soundtrack s;
    double best = 1e9, bestRender = 1e9;
    for (int b = 0; b < bench; b++) {
        auto t0 = std::chrono::steady_clock::now();
        try {
            SongTicks st = songTicks(exe, 9400);
            IntroSchedule sc = buildSchedule(st);
            auto t1 = std::chrono::steady_clock::now();
            s = renderSoundtrack(exe, st, sc.music, 48000);
            bestRender = std::min(bestRender, since(t1));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "render: %s\n", e.what());
            return 1;
        }
        best = std::min(best, since(t0));
    }
    const size_t frames = s.pcm.size() / 2;
    std::printf("soundtrack: %zu frames at %d Hz (%.3f s, to the stop at %.6f s), %zu rows; songTicks + "
                "buildSchedule + renderSoundtrack best of %d: %.1f ms (render %.1f ms, %u threads)\n",
                frames, s.rate, double(frames) / s.rate, s.endTime, s.rowTime.size(), bench, best * 1e3,
                bestRender * 1e3, std::thread::hardware_concurrency());
    if (!outWav.empty()) report(writeWav(outWav, s.pcm, s.rate), "WAV", "wrote " + outWav);
    {
        int peak = 0;
        size_t clipped = 0;
        double sumL = 0, sumR = 0;
        for (size_t i = 0; i < s.pcm.size(); i++) {
            const int v = s.pcm[i];
            peak = std::max(peak, std::abs(v));
            clipped += std::abs(v) >= 32767;
            (i & 1 ? sumR : sumL) += double(v) * v;
        }
        const double rmsL = std::sqrt(sumL / double(frames)), rmsR = std::sqrt(sumR / double(frames));
        // longest stretch after tick 0 whose 50 ms RMS stays below -60 dBFS
        const size_t win = size_t(s.rate / 20);
        size_t quiet = 0, longest = 0, at = 0;
        const size_t t0 = size_t(std::ceil(s.schedule.tickTime[0] * s.rate));
        for (size_t w0 = t0; w0 + win <= frames; w0 += win) {
            double e = 0;
            for (size_t k = w0; k < w0 + win; k++)
                e += double(s.pcm[2 * k]) * s.pcm[2 * k] + double(s.pcm[2 * k + 1]) * s.pcm[2 * k + 1];
            if (std::sqrt(e / double(2 * win)) < 32.767) {
                quiet += win;
                if (quiet > longest) longest = quiet, at = w0 + win - quiet;
            } else {
                quiet = 0;
            }
        }
        int pre = 0;  // before tick 0
        for (size_t k = 0; k < t0 && 2 * k + 1 < s.pcm.size(); k++)
            pre = std::max(pre, std::max(std::abs(int(s.pcm[2 * k])), std::abs(int(s.pcm[2 * k + 1]))));
        const double pk = 20 * std::log10(peak / 32767.0);
        report(pk > -1.05 && pk < -0.95 && clipped == 0 && longest < size_t(s.rate) && pre < 2, "level",
               fmt("peak %d (%.2f dBFS) after the 5 Hz high-pass, %zu samples at full scale, RMS left %.1f dBFS, right "
                   "%.1f dBFS, longest stretch below -60 dBFS %.2f s (at %.2f s), before tick 0 at most %d",
                   peak, pk, clipped, 20 * std::log10(rmsL / 32767.0), 20 * std::log10(rmsR / 32767.0),
                   double(longest) / s.rate, double(at) / s.rate, pre));
    }

    // ---- 6. okeanplay.py's WAV: the hardware schedule (ticks on the timer's grid, no holds, no lost ticks), from the
    // first player tick to one tick period after timer interrupt 9203, GF1 at its own 38,587.5 Hz ----
    if (!pyWav.empty()) {
        std::vector<int16_t> pref;
        if (!readWav16(pyWav, pref)) {
            report(false, "okeanplay.py WAV", "cannot read " + pyWav);
        } else {
            MusicSchedule hw;
            const int ticks = modplay::kCaptureTicks - modplay::kStartAfter;
            for (int j = 0; j < ticks; j++) hw.tickTime.push_back(double(j + 4) * kTick);
            hw.master = fadeSteps(hw.tickTime, song.pos, master0, bright0, double(ticks + 4) * kTick);
            MixSpec spec;
            spec.rate = gus::kGf1Clock / 16;
            spec.origin = 62 * 320e-6;  // okeanplay.py: frame 0 = the first timer interrupt
            spec.stopAtOverflow = true;
            auto t2 = std::chrono::steady_clock::now();
            const MusicMix mx = renderMix(exe, hw, spec);
            const double dt = since(t2);
            // okeanplay.py --wav: from the frame of the first player tick, int16(clip(x / peak * 0.9) * 32767)
            // (truncated), peak over the WAV's frames
            const size_t f0 = size_t(std::ceil((mx.timerServed[modplay::kStartAfter] - spec.origin) * spec.rate));
            const size_t total = mx.pcm.size() / 2;
            double pk = 0;
            for (size_t i = 2 * f0; i < mx.pcm.size(); i++) pk = std::max(pk, double(std::fabs(mx.pcm[i])));
            const size_t n = std::min(pref.size(), mx.pcm.size() - 2 * f0);
            size_t same = 0, off1 = 0, worst = 0, at = 0;
            for (size_t i = 0; i < n; i++) {
                const double x = std::clamp(double(mx.pcm[2 * f0 + i]) / pk * 0.9, -1.0, 1.0) * 32767.0;
                const size_t d = size_t(std::abs(int(x) - pref[i]));
                same += d == 0;
                off1 += d == 1;
                if (d > worst) worst = d, at = i;
            }
            report(pref.size() == 2 * (total - f0) && worst <= 1, "okeanplay.py WAV",
                   fmt("%zu vs %zu samples at %.4f Hz (%.1f ms), mix peak %.6f; %zu identical, %zu off by 1 LSB, max "
                       "%zu LSB (frame %zu)",
                       2 * (total - f0), pref.size(), spec.rate, dt * 1e3, pk, same, off1, worst, at / 2));

            // the soundtrack against the WAV tick by tick: RMS over one tick period from each player tick on (the
            // soundtrack's ticks at the schedule's times, the WAV's on the timer grid from its frame 0), the WAV
            // through the soundtrack's 5 Hz high-pass. The renders differ where the schedule holds a tick at a part
            // start: the notes of the held tick sound up to 15 ms later relative to the ticks after it, until their
            // voices get new notes.
            std::vector<double> wav(pref.size());
            {
                const double c = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / spec.rate);
                for (int ch = 0; ch < 2; ch++) {
                    double x1 = 0, y1 = 0;
                    for (size_t i = size_t(ch); i < pref.size(); i += 2) {
                        const double x = pref[i], y = x - x1 + c * y1;
                        x1 = x;
                        y1 = y;
                        wav[i] = y;
                    }
                }
            }
            std::vector<double> a, b;
            for (int j = 0; j < ticks && size_t(j) < s.schedule.tickTime.size(); j++) {
                auto rms = [](auto* p, size_t f1, size_t f2, size_t nf) {
                    double e = 0;
                    size_t m = 0;
                    for (size_t k = f1; k < f2 && k < nf; k++, m++)
                        e += double(p[2 * k]) * p[2 * k] + double(p[2 * k + 1]) * p[2 * k + 1];
                    return m ? std::sqrt(e / double(2 * m)) : 0.0;
                };
                const double c0 = double(j + 4) * kTick - mx.timerServed[modplay::kStartAfter];
                const double t0 = s.schedule.tickTime[size_t(j)];
                a.push_back(rms(wav.data(), size_t(std::max(0.0, c0 * spec.rate)), size_t((c0 + kTick) * spec.rate),
                                wav.size() / 2));
                b.push_back(rms(s.pcm.data(), size_t(t0 * s.rate), size_t((t0 + kTick) * s.rate), frames));
            }
            double ma = 0, mb = 0;
            for (size_t k = 0; k < a.size(); k++) ma += a[k], mb += b[k];
            ma /= double(a.size());
            mb /= double(b.size());
            double sab = 0, saa = 0, sbb = 0;
            for (size_t k = 0; k < a.size(); k++) {
                sab += (a[k] - ma) * (b[k] - mb);
                saa += (a[k] - ma) * (a[k] - ma);
                sbb += (b[k] - mb) * (b[k] - mb);
            }
            const double corr = sab / std::sqrt(saa * sbb);
            std::vector<double> ratio;
            for (size_t k = 0; k < a.size(); k++)
                if (a[k] > 300 && b[k] > 300) ratio.push_back(20 * std::log10(b[k] / a[k]));
            std::sort(ratio.begin(), ratio.end());
            auto q = [&](double p) { return ratio.empty() ? 0.0 : ratio[size_t(p * double(ratio.size() - 1))]; };
            report(corr > 0.99, "tick RMS",
                   fmt("the 48 kHz soundtrack against okeanplay.py's WAV, RMS per player tick over %zu ticks: "
                       "correlation %.5f; level difference (soundtrack - WAV) median %+.2f dB, 1 %% %+.2f, 99 %% %+.2f "
                       "(the two are normalised to their own peaks)",
                       a.size(), corr, q(0.5), q(0.01), q(0.99)));
        }
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

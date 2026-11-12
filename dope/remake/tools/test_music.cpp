// test_music: checks the soundtrack code (src/modplay.cpp, src/gf1.cpp, src/music.cpp) against the capture and the
// Python model dope/re/models/music/dopeplay.py (see dope/re/notes-music.md).
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [options]
//   --re DIR        dope/re: reads DIR/unpacked/loader.bin, DIR/../original/DOPE.MOD and the capture DIR/full.gus,
//                   full.dram, full.ticks (default: the source tree's dope/re)
//   --loader F, --module F, --gus F, --dram F, --ticks F   override single files ("" skips a check)
//   --ideal-gus F   the register log of `dopeplay.py --ideal --out X` (X.gus): the C++ player with the interrupts at
//                   the card's events (no capture latency, no holds) against it, line by line
//   --pywav F       the mix at the GF1's own rate against the WAV of `dopeplay.py --wav F` (16-bit, 0.9 / peak)
//   --wav F         write the soundtrack (44.1 kHz stereo 16-bit)
//   --out-gus F     write the register log of the capture-schedule run
//   --bench N       time renderSoundtrack N times (best)
//   --no-onsets     skip the note-onset check (28 single-voice renders)
//
// Checks: with the capture's interrupt service times (the `irq` lines of full.gus) the GUS register log from the GUS
// init to the stop line by line against full.gus, the GUS DRAM after the upload against full.dram, and the music
// position (order, row, tick countdown) after every timer interrupt against full.ticks; the soundtrack's tick times
// and positions against the capture's; the interface; the level, clipping and silence of the soundtrack; and that the
// notes start in the audio where the player starts them.
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
#include <string>
#include <vector>

#include "gf1.h"
#include "modplay.h"
#include "music.h"

#ifndef DOPE_RE_DIR
#define DOPE_RE_DIR "../../re"
#endif

namespace {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
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

double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

bool ok = true;

void report(bool good, const char* what, const std::string& detail) {
    std::printf("%-10s %s: %s\n", good ? "OK" : "MISMATCH", what, detail.c_str());
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
    if (!readFile(path, raw) || raw.size() < 12) return false;
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

// the `irq` lines of a full.gus-format log: kind, time, and for ramp IRQs the voices their `r0d 3` writes stop
std::vector<modplay::CaptureIrq> readIrqs(const std::vector<std::string>& lines) {
    std::vector<modplay::CaptureIrq> out;
    for (const std::string& ln : lines) {
        if (ln.empty() || ln[0] == '#') continue;
        int tick, voice;
        char kind;
        double t;
        unsigned val;
        char reg[8];
        if (std::sscanf(ln.c_str(), "%d irq %c %lf", &tick, &kind, &t) == 3) {
            out.push_back({kind, t, {}});
        } else if (!out.empty() && out.back().kind == 'r' &&
                   std::sscanf(ln.c_str(), "%d v%d %7s %x", &tick, &voice, reg, &val) == 4 && !std::strcmp(reg, "r0d") &&
                   val == 3) {
            out.back().order.push_back(voice);
        }
    }
    return out;
}

std::vector<std::string> logLines(const std::string& path) {
    std::vector<uint8_t> raw;
    std::vector<std::string> v;
    if (!readFile(path, raw)) return v;
    for (std::string& s : splitLines(std::string(raw.begin(), raw.end())))
        if (!s.empty() && s[0] != '#') v.push_back(std::move(s));
    return v;
}

// a card that logs and remembers the note starts: per tick the voices whose sample a ramp IRQ switched (reg 0x0b)
// and the volume the update then writes (reg 0x09)
class LogCard : public gus::Card {
public:
    struct Note {
        int tick, voice;
        double sw = 0;     // card time of the switch (the ramp IRQ)
        int vol = -1;      // 12-bit volume written by the update (-1: none)
        double when = 0;   // card time of the update
    };
    std::vector<Note> notes;

protected:
    void received(int voice, int reg, uint32_t d) override {
        if (reg == 0x0b) {
            notes.push_back({tick, voice, now()});
        } else if (reg == 0x09) {
            for (size_t i = notes.size(); i-- > 0 && notes[i].tick == tick;)
                if (notes[i].voice == voice) {
                    notes[i].vol = int(d >> 4) & 0xfff;
                    notes[i].when = now();
                }
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string re = DOPE_RE_DIR, loaderPath, modPath, gusRef, dramRef, ticksRef, idealGus, pyWav, outWav, outGus;
    bool setLoader = false, setMod = false, setGus = false, setDram = false, setTicks = false, onsets = true;
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
        else if (a == "--loader") loaderPath = next(), setLoader = true;
        else if (a == "--module") modPath = next(), setMod = true;
        else if (a == "--gus") gusRef = next(), setGus = true;
        else if (a == "--dram") dramRef = next(), setDram = true;
        else if (a == "--ticks") ticksRef = next(), setTicks = true;
        else if (a == "--ideal-gus") idealGus = next();
        else if (a == "--pywav") pyWav = next();
        else if (a == "--wav") outWav = next();
        else if (a == "--out-gus") outGus = next();
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else if (a == "--no-onsets") onsets = false;
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
    if (!setLoader) loaderPath = re + "/unpacked/loader.bin";
    if (!setMod) modPath = re + "/../original/DOPE.MOD";
    if (!setGus) gusRef = re + "/full.gus";
    if (!setDram) dramRef = re + "/full.dram";
    if (!setTicks) ticksRef = re + "/full.ticks";
    std::vector<uint8_t> loader, mod;
    if (!readFile(loaderPath, loader) || !readFile(modPath, mod)) {
        std::fprintf(stderr, "cannot read %s or %s\n", loaderPath.c_str(), modPath.c_str());
        return 1;
    }

    // ---- the player with the capture's interrupt service times ----
    const std::vector<std::string> ref = gusRef.empty() ? std::vector<std::string>{} : logLines(gusRef);
    const std::vector<modplay::CaptureIrq> irqs = readIrqs(ref);
    std::vector<double> capTimer;  // the capture's service time of each timer interrupt
    for (const auto& q : irqs)
        if (q.kind == 't') capTimer.push_back(q.time);
    LogCard card;
    modplay::Run run;
    if (!irqs.empty()) {
        std::string log;
        log.reserve(size_t(8) << 20);
        card.log = &log;
        modplay::Options opt;
        opt.capture = &irqs;
        auto t0 = std::chrono::steady_clock::now();
        try {
            modplay::run(mod, loader, card, opt, run);
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
                                          ref.size(), i + 1, i < mine.size() ? mine[i].c_str() : "-",
                                          i < ref.size() ? ref[i].c_str() : "-"));
        report(run.early == 0 && card.lost == 7 && card.reordered == 7 && run.stale == 0, "schedule",
               fmt("%d interrupts after the start, %d served more than 1.5 us after their event (max %.3f ms), %d "
                   "before it; %d timer overflows lost, %d ramp IRQs with the capture's queue order, %d stale ramp "
                   "IRQs; %d note starts",
                   run.irqs - 2, run.late, run.maxLate * 1e3, run.early, card.lost, card.reordered, run.stale,
                   run.notes));
    } else if (!gusRef.empty()) {
        report(false, "full.gus", "cannot read " + gusRef);
    }
    if (!dramRef.empty() && !irqs.empty()) {
        std::vector<uint8_t> d;
        if (!readFile(dramRef, d)) {
            report(false, "full.dram", "cannot read " + dramRef);
        } else {
            size_t diff = 0, n = std::min(d.size(), card.dram().size());
            for (size_t i = 0; i < n; i++) diff += d[i] != card.dram()[i];
            report(diff == 0 && d.size() == card.dram().size(), "full.dram",
                   fmt("%zu bytes, %zu differ (samples 0..0x%x, probe bytes 0x7ffff, 0xbffff, 0xfffff)", d.size(), diff,
                       unsigned(run.dramEnd)));
        }
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
                if (g < 1 || size_t(g) > run.pos.size()) continue;
                n++;
                const modplay::Position& p = run.pos[size_t(g - 1)];
                if (p.order != order || p.row != row || p.countdown != cnt)
                    if (bad++ == 0) firstBad = frame;
            }
            report(n > 0 && bad == 0, "full.ticks",
                   fmt("position (order, row, countdown) after the last timer interrupt of %zu capture frames, %zu "
                       "differ (first: frame %ld)", n, bad, firstBad));
        }
    }

    // ---- the hardware schedule against dopeplay.py --ideal ----
    if (!idealGus.empty()) {
        const std::vector<std::string> iref = logLines(idealGus);
        if (iref.empty()) {
            report(false, "ideal", "cannot read " + idealGus);
        } else {
            gus::Card c;
            std::string log;
            c.log = &log;
            modplay::Options opt;
            opt.ticks = int(std::count_if(iref.begin(), iref.end(),
                                          [](const std::string& s) { return s.find(" irq t ") != std::string::npos; }));
            modplay::Run r;
            modplay::run(mod, loader, c, opt, r);
            const std::vector<std::string> mine = splitLines(log);
            size_t n = std::min(mine.size(), iref.size()), i = 0;
            while (i < n && mine[i] == iref[i]) i++;
            report(i == n && mine.size() == iref.size(), "ideal",
                   i == n && mine.size() == iref.size()
                       ? fmt("interrupts at the card's events: %zu lines identical to dopeplay.py --ideal", mine.size())
                       : fmt("%zu vs %zu lines, first difference at line %zu: '%s' vs '%s'", mine.size(), iref.size(),
                             i + 1, i < mine.size() ? mine[i].c_str() : "-", i < iref.size() ? iref[i].c_str() : "-"));
        }
    }

    // ---- the soundtrack ----
    Soundtrack s;
    double best = 1e9;
    for (int b = 0; b < bench; b++) {
        auto t0 = std::chrono::steady_clock::now();
        try {
            s = renderSoundtrack(mod, loader, 44100);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "render: %s\n", e.what());
            return 1;
        }
        best = std::min(best, since(t0));
    }
    const size_t frames = s.pcm.size() / 2;
    std::printf("renderSoundtrack: %zu frames at %d Hz (%.5f s), %zu player ticks, best of %d: %.1f ms\n", frames,
                s.rate, double(frames) / s.rate, s.tickTimes.size(), bench, best * 1e3);
    if (!outWav.empty()) report(writeWav(outWav, s.pcm, s.rate), "WAV", "wrote " + outWav);

    // tick times against the capture: music time = capture time - 0.1104; player tick k = timer interrupt k + 3
    if (capTimer.size() > 3) {
        size_t n = 0, same = 0, worse = 0;
        double maxd = 0;
        for (size_t k = 0; k < s.tickTimes.size() && k + 2 < capTimer.size(); k++) {
            const double c = std::round((capTimer[k + 2] - 0.1104) * 1e6) / 1e6;
            const double d = c - s.tickTimes[k];
            n++;
            same += d == 0;
            worse += d < 0;  // (the capture can only be later: its CPU latency)
            maxd = std::max(maxd, std::fabs(d));
        }
        size_t posBad = 0;
        for (size_t k = 0; k < s.positions.size() && k + 2 < run.pos.size(); k++)
            posBad += s.positions[k] != (run.pos[k + 2].order << 8 | run.pos[k + 2].row);
        report(worse == 0 && maxd < 0.6e-3 && posBad == 0, "tick times",
               fmt("%zu player ticks against the capture's timer interrupts: %zu at the same microsecond, the others "
                   "earlier by at most %.3f ms (the capture's CPU latency), %zu later; positions: %zu differ; music "
                   "behind the GUS clock at the end by %.2f ms (7 lost overflows)",
                   n, same, maxd * 1e3, worse, posBad,
                   (s.timeOfTick(22202) - (38.74688 + (22202 - 1730) * 0.02048 + 0.01984)) * 1e3));
    }
    // the closed form of notes-music 7.2 without holds
    {
        Soundtrack t;
        MixSpec spec;
        spec.holds.clear();
        spec.rate = 1000;  // (only the times are used)
        double pk = 0;
        renderMix(mod, loader, spec, t, &pk);
        size_t bad = 0;
        for (size_t k = 0; k < t.tickTimes.size(); k++) {
            const double c = k <= 2 ? 0.01984 * double(k) : k <= 1730 ? 0.03968 + double(k - 2) * 0.0224
                                                                      : 38.74688 + double(k - 1730) * 0.02048;
            bad += std::fabs(t.tickTimes[k] - 0.01984 - c) > 1e-9;
        }
        report(bad == 0, "closed form", fmt("%zu player ticks without holds against T(k) of notes-music 7.2: %zu differ",
                                             t.tickTimes.size(), bad));
    }

    // interface
    {
        std::string bad;
        if (s.timeOfTick(0) != 0.01984) bad += " timeOfTick(0)";
        if (s.positionAt(0.0) != 0x003f || s.positionAt(0.01983) != 0x003f || s.positionAt(0.01984) != 0x0000)
            bad += " positionAt";
        if (s.timeOfPosition(0x0000) != 0.01984) bad += " timeOfPosition(0)";
        // every position change: timeOfPosition gives the tick that reads the row
        int prev = s.pos0;
        size_t changes = 0;
        for (size_t k = 0; k < s.positions.size(); k++) {
            const int p = s.positions[k];
            if (p == prev) continue;
            changes++;
            const double t = s.timeOfPosition(p);
            if (t != s.tickTimes[k] || s.positionAt(t) != p || s.positionAt(std::nextafter(t, -1.0)) != prev) {
                if (bad.size() < 200) bad += fmt(" %04x", p);
            }
            prev = p;
        }
        const double end = s.timeOfTick(int(s.tickTimes.size()) - 1);
        report(bad.empty(), "interface",
               fmt("timeOfTick / positionAt / timeOfPosition at %zu position changes; order 03 row 00 at %.5f s, 6d:28 "
                   "(the capture's last row) at %.5f s, captureEnd %.5f s, last tick %.5f s (%04x)%s%s",
                   changes, s.timeOfPosition(0x0300), s.timeOfPosition(0x6d28), s.captureEnd, end, s.positions.back(),
                   bad.empty() ? "" : "; wrong:", bad.c_str()));
    }

    // level, clipping, silence
    {
        int peak = 0;
        size_t clipped = 0;
        double sum = 0, sumL = 0, sumR = 0;
        for (size_t i = 0; i < s.pcm.size(); i++) {
            const int v = s.pcm[i];
            peak = std::max(peak, std::abs(v));
            clipped += std::abs(v) >= 32767;
            sum += double(v) * v;
            (i & 1 ? sumR : sumL) += double(v) * v;
        }
        const double rms = s.pcm.empty() ? 0 : std::sqrt(sum / double(s.pcm.size()));
        const double rmsL = std::sqrt(sumL / double(frames)), rmsR = std::sqrt(sumR / double(frames));
        // longest stretch after tick 0 whose 50 ms RMS stays below -60 dBFS (up to the capture's stop)
        const size_t win = size_t(s.rate / 20), stop = std::min(frames, size_t(s.captureEnd * s.rate));
        size_t quiet = 0, longest = 0, at = 0;
        for (size_t w0 = size_t(s.timeOfTick(0) * s.rate); w0 + win <= stop; w0 += win) {
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
        // before tick 0: silence
        int pre = 0;
        for (size_t k = 0; k < size_t(s.timeOfTick(0) * s.rate) && 2 * k + 1 < s.pcm.size(); k++)
            pre = std::max(pre, std::max(std::abs(int(s.pcm[2 * k])), std::abs(int(s.pcm[2 * k + 1]))));
        Soundtrack tt;
        double mixPeak = 0;
        MixSpec spec;
        renderMix(mod, loader, spec, tt, &mixPeak);
        report(peak > 29000 && peak < 32767 && clipped == 0 && rms > 1000 && longest < size_t(s.rate) && pre < 2,
               "level",
               fmt("mix peak %.4f (1 = one voice at full scale, hard panned) -> peak %d (%.2f dBFS) after the 5 Hz "
                   "high-pass, %zu samples at full scale, RMS %.0f (%.1f dBFS; left %.1f, right %.1f), longest stretch "
                   "below -60 dBFS %.2f s (at %.2f s), before tick 0 at most %d",
                   mixPeak, peak, 20 * std::log10(peak / 32767.0), clipped, rms, 20 * std::log10(rms / 32767.0),
                   20 * std::log10(rmsL / 32767.0), 20 * std::log10(rmsR / 32767.0), double(longest) / s.rate,
                   double(at) / s.rate, pre));
    }

    // note onsets, voice by voice: a new note's voice ramps down to GF1 volume 0x400 (-72 dB), is switched to the new
    // sample there by the ramp IRQ, and the update after the last ramp of the tick jumps to the new volume (notes-music
    // 9.4). The player is run once more on the soundtrack's schedule (the holds of music.cpp: the load until 0.110398
    // s and captureHolds(); music time = card time - 0.1104) to know when each switch and update comes. Onset = the
    // first frame from the switch on (up to 20 ms) above -60 dB of one voice's full scale: it must not come before the
    // update. Notes with a new volume of at least 0xc00 (-24 dB); a sample that starts with near-zero bytes sounds
    // later than its update.
    if (onsets) {
        constexpr double kZero = 0.1104;
        LogCard nc;
        modplay::Options opt;
        opt.ticks = int(s.tickTimes.size()) + modplay::kStartAfter;
        opt.holds.push_back({0.0, 0.110398});
        for (const IrqHold& h : captureHolds()) opt.holds.push_back({h.from + kZero, h.to + kZero});
        modplay::Run nr;
        modplay::run(mod, loader, nc, opt, nr);
        size_t diff = 0;
        for (size_t k = 0; k < s.tickTimes.size(); k++)
            diff += std::round((nr.timerServed[k + 2] - kZero) * 1e6) / 1e6 != s.tickTimes[k];
        const int rate = 44100;
        const double thr = std::pow(10.0, -60.0 / 20.0);
        const long reach = long(0.020 * rate);
        const bool debug = std::getenv("DOPE_ONSET_DEBUG") != nullptr;
        std::vector<double> upd, offs;  // update after the tick, onset after the update (ms)
        size_t total = 0, none = 0;
        auto t1 = std::chrono::steady_clock::now();
        for (int v = 0; v < 28; v++) {
            Soundtrack tt;
            double pk = 0;
            MixSpec spec;
            spec.rate = rate;
            spec.voices = 1u << v;
            const std::vector<float> mix = renderMix(mod, loader, spec, tt, &pk);
            const long nf = long(mix.size() / 2);
            auto amp = [&](long f) {
                return std::max(std::fabs(mix[size_t(2 * f)]), std::fabs(mix[size_t(2 * f + 1)]));
            };
            for (const LogCard::Note& n : nc.notes) {
                const int k = n.tick - 3;
                if (n.voice != v || n.vol < 0xc00 || k < 0 || k >= int(tt.tickTimes.size())) continue;
                const double t = tt.tickTimes[size_t(k)], u = n.when - kZero;
                const long c = long(std::ceil((n.sw - kZero) * rate));  // the switch applies from this frame
                if (c + reach >= nf) continue;
                total++;
                upd.push_back((u - t) * 1e3);
                long at = -1;
                for (long f = c; f <= c + reach; f++)
                    if (amp(f) > thr) {
                        at = f;
                        break;
                    }
                if (at < 0) {
                    none++;
                } else {
                    const double o = (double(at) / rate - u) * 1e3;
                    offs.push_back(o);
                    if (debug && (o < 0 || o > 0.25))
                        std::printf("  onset %+.3f ms after the update (%.3f ms after the tick): voice %d, tick %d "
                                    "(position %04x), volume %03x\n", o, (u - t) * 1e3, v, n.tick,
                                    tt.positions[size_t(k)], unsigned(n.vol));
                }
            }
        }
        std::sort(offs.begin(), offs.end());
        std::sort(upd.begin(), upd.end());
        auto q = [](const std::vector<double>& x, double p) { return x.empty() ? 0.0 : x[size_t(p * double(x.size() - 1))]; };
        // (an onset can be one output frame before the update's time: writes apply from the first frame at or after)
        const size_t early = size_t(std::count_if(offs.begin(), offs.end(), [](double o) { return o < -0.023; }));
        const size_t near = size_t(std::count_if(offs.begin(), offs.end(), [](double o) { return o >= -0.023 && o < 0.25; }));
        report(diff == 0 && !offs.empty() && double(near) >= 0.9 * double(offs.size()) && early == 0 && none * 100 < total,
               "onsets",
               fmt("%zu note starts at volume >= 0xc00 (28 single-voice renders, %.1f s); their updates come %.3f.."
                   "%.3f ms after the tick (median %.3f); onsets found for %zu: %.2f %% within [-1 frame, +0.25 ms) "
                   "of the update (median %+.3f ms), %zu earlier, %zu later (1 %% %+.3f, 99 %% "
                   "%+.3f, max %+.3f ms: samples starting near zero); %zu without an onset; schedule: %zu tick times "
                   "differ from the soundtrack's",
                   total, since(t1), q(upd, 0), q(upd, 1), q(upd, 0.5), offs.size(),
                   100.0 * double(near) / double(std::max<size_t>(1, offs.size())), q(offs, 0.5), early,
                   offs.size() - near - early, q(offs, 0.01), q(offs, 0.99), q(offs, 1), none, diff));
    }

    // the mix at the GF1's own frame rate against dopeplay.py --wav (hardware schedule without holds, from tick 0)
    if (!pyWav.empty()) {
        std::vector<int16_t> pref;
        if (!readWav16(pyWav, pref)) {
            report(false, "dopeplay.py WAV", "cannot read " + pyWav);
        } else {
            Soundtrack tt;
            double pk = 0;
            MixSpec spec;
            spec.rate = gus::kGf1Clock / 28;
            spec.holds.clear();
            spec.loadHold = false;
            spec.ticks = modplay::kCaptureTicks;
            spec.fromTickZero = true;
            auto t2 = std::chrono::steady_clock::now();
            const std::vector<float> mix = renderMix(mod, loader, spec, tt, &pk);
            const double dt = since(t2);
            // dopeplay.py's WAV is int16(clip(x / peak * 0.9) * 32767) (truncated), peak over the WAV's frames
            const size_t n = std::min(pref.size(), mix.size());
            size_t same = 0, off1 = 0, worst = 0, at = 0;
            for (size_t i = 0; i < n; i++) {
                const double x = std::clamp(double(mix[i]) / pk * 0.9, -1.0, 1.0) * 32767.0;
                const size_t d = size_t(std::abs(int(x) - pref[i]));
                same += d == 0;
                off1 += d == 1;
                if (d > worst) worst = d, at = i;
            }
            report(pref.size() == mix.size() && worst <= 1, "dopeplay.py WAV",
                   fmt("%zu vs %zu samples at %.4f Hz (%.1f ms), mix peak %.6f; %zu identical, %zu off by 1 LSB, max "
                       "%zu LSB (frame %zu)",
                       mix.size(), pref.size(), spec.rate, dt * 1e3, pk, same, off1, worst, at / 2));
        }
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

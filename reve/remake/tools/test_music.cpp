// test_music: checks the soundtrack code (src/lcaplay.cpp, src/gf1.cpp, src/music.cpp) against the capture and the
// Python model reve/re/models/music/lcaplay.py (see reve/re/notes-music.md).
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [options]
//   --re DIR        reve/re: reads DIR/unpacked/reve_img.bin, DIR/unpacked/files/ZIC/PULSE.MOD and the capture
//                   DIR/full.gus, full.dram, full.ticks (default: the source tree's reve/re)
//   --exe F         take the program image and the module from REVE.EXE instead (exe.cpp, as the remake does)
//   --image F, --module F, --gus F, --dram F, --ticks F   override single files ("" skips a check)
//   --pywav F       the mix at the GF1's own rate against the WAV of `lcaplay.py --wav` (16-bit, 0.9 / peak)
//   --wav F         write the soundtrack (48 kHz stereo 16-bit)
//   --out-gus F     write the register log
//   --bench N       time renderSoundtrack N times (best)
//
// Checks: the GUS register log from the card detection to main's stop line by line against full.gus, the GUS DRAM
// after the upload against full.dram, per capture record (full.ticks: frame, time, PIT interrupts, GUS timer
// interrupts, order, row) the GUS timer interrupts before its retrace and the position the timer interrupt sees there
// (Soundtrack::positionAt), the script entries' switch refreshes (refreshOfPosition), and the soundtrack's level,
// clipping and note onsets.
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
#include <utility>
#include <vector>

#include "exe.h"
#include "gf1.h"
#include "lcaplay.h"
#include "music.h"

#ifndef REVE_RE_DIR
#define REVE_RE_DIR "../../re"
#endif

namespace {

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
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
    if (!readAll(path, raw) || raw.size() < 12) return false;
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

// a card that logs and remembers the note starts of the ticks: a position write (reg 0x0b) to a voice, and whether
// a frequency write (reg 0x01) to that voice follows in the same tick (notes with periods below the FC table have
// none and stay silent)
class LogCard : public gus::Card {
public:
    struct Note {
        int tick, voice;  // tick: the player tick (0-based; its writes carry the stamp tick + 1)
        bool fc;
    };
    std::vector<Note> notes;
    bool armed = false;  // from tick 0 on

protected:
    void received(int voice, int reg, uint32_t) override {
        if (!armed) return;
        const bool same = !notes.empty() && notes.back().tick == tick - 1 && notes.back().voice == voice;
        if (reg == 0x0b && !same) notes.push_back({tick - 1, voice, false});
        else if (reg == 0x01 && same) notes.back().fc = true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string re = REVE_RE_DIR, exePath, imagePath, modulePath, gusRef, dramRef, ticksRef, pyWav, outWav, outGus;
    bool setImage = false, setModule = false, setGus = false, setDram = false, setTicks = false;
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
        else if (a == "--exe") exePath = next();
        else if (a == "--image") imagePath = next(), setImage = true;
        else if (a == "--module") modulePath = next(), setModule = true;
        else if (a == "--gus") gusRef = next(), setGus = true;
        else if (a == "--dram") dramRef = next(), setDram = true;
        else if (a == "--ticks") ticksRef = next(), setTicks = true;
        else if (a == "--pywav") pyWav = next();
        else if (a == "--wav") outWav = next();
        else if (a == "--out-gus") outGus = next();
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
    if (!setImage) imagePath = re + "/unpacked/reve_img.bin";
    if (!setModule) modulePath = re + "/unpacked/files/ZIC/PULSE.MOD";
    if (!setGus) gusRef = re + "/full.gus";
    if (!setDram) dramRef = re + "/full.dram";
    if (!setTicks) ticksRef = re + "/full.ticks";
    std::vector<uint8_t> image, module;
    if (!exePath.empty()) {
        ReveExe exe;
        std::string err;
        if (!exe.load(exePath, &err)) {
            std::fprintf(stderr, "%s: %s\n", exePath.c_str(), err.c_str());
            return 1;
        }
        image.assign(exe.image().begin(), exe.image().end());
        const auto mod = exe.file("ZIC\\PULSE.MOD");
        module.assign(mod.begin(), mod.end());
        std::printf("REVE.EXE: image %zu bytes, ZIC\\PULSE.MOD %zu bytes\n", image.size(), module.size());
    } else if (!readAll(imagePath, image) || !readAll(modulePath, module)) {
        std::fprintf(stderr, "cannot read %s or %s\n", imagePath.c_str(), modulePath.c_str());
        return 1;
    }

    // ---- the soundtrack: its times, positions and the stop ----
    Soundtrack s;
    double best = 1e9;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < bench; b++) {
        t0 = std::chrono::steady_clock::now();
        try {
            s = renderSoundtrack(module, image, 48000);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "render: %s\n", e.what());
            return 1;
        }
        best = std::min(best, since(t0));
    }
    const size_t frames = s.pcm.size() / 2;
    std::printf("renderSoundtrack: %zu frames at %d Hz (%.5f s), %zu ticks, best of %d: %.1f ms\n", frames, s.rate,
                double(frames) / s.rate, s.tickTimes.size(), bench, best * 1e3);
    std::printf("startRefresh %.7f, endRefresh %.7f (music time %.6f s), last end position %04x at refresh %.0f\n",
                s.startRefresh, s.endRefresh, s.timeOf(s.endRefresh), lastEndPosition(image),
                s.refreshOfPosition(lastEndPosition(image)));
    if (!outWav.empty()) report(writeWav(outWav, s.pcm, s.rate), "WAV", "wrote " + outWav);

    // ---- the player with a logging card, from the detection to main's stop ----
    LogCard card;
    std::string log;
    log.reserve(size_t(4) << 20);
    card.log = &log;
    lca::Run run;
    const int64_t stopUs = int64_t(std::floor(s.timeOf(s.endRefresh) * 1e6));
    t0 = std::chrono::steady_clock::now();
    try {
        lca::run(
            module, image, card,
            [&](lca::Run& r, int64_t us) {
                if (us <= stopUs) return false;
                r.stopUs = stopUs;
                return true;
            },
            run, [&](int k, int64_t) { card.armed |= k == 0; });
    } catch (const std::exception& e) {
        std::fprintf(stderr, "player: %s\n", e.what());
        return 1;
    }
    std::printf("player: %zu ticks, %zu log bytes in %.1f ms\n", run.pos.size(), log.size(), since(t0) * 1e3);
    if (!outGus.empty()) std::ofstream(outGus, std::ios::binary).write(log.data(), std::streamsize(log.size()));
    report(run.pos.size() == s.tickTimes.size(), "ticks",
           fmt("%zu ticks before the stop, the soundtrack's %zu", run.pos.size(), s.tickTimes.size()));
    if (!gusRef.empty()) {
        std::vector<uint8_t> raw;
        if (!readAll(gusRef, raw)) {
            report(false, "full.gus", "cannot read " + gusRef);
        } else {
            std::vector<std::string> a = splitLines(log), b = splitLines(std::string(raw.begin(), raw.end()));
            size_t n = std::min(a.size(), b.size()), i = 0;
            while (i < n && a[i] == b[i]) i++;
            if (i == n && a.size() == b.size())
                report(true, "full.gus", fmt("%zu register-write lines, identical (detection to stop)", a.size()));
            else
                report(false, "full.gus", fmt("%zu vs %zu lines, first difference at line %zu: '%s' vs '%s'", a.size(),
                                              b.size(), i + 1, i < a.size() ? a[i].c_str() : "-",
                                              i < b.size() ? b[i].c_str() : "-"));
        }
    }
    if (!dramRef.empty()) {
        std::vector<uint8_t> ref;
        if (!readAll(dramRef, ref)) {
            report(false, "full.dram", "cannot read " + dramRef);
        } else {
            size_t diff = 0, n = std::min(ref.size(), card.dram().size());
            for (size_t i = 0; i < n; i++) diff += ref[i] != card.dram()[i];
            report(diff == 0 && ref.size() == card.dram().size(), "full.dram",
                   fmt("%zu bytes, %zu differ", ref.size(), diff));
        }
    }
    if (!ticksRef.empty()) {
        std::ifstream f(ticksRef);
        if (!f) {
            report(false, "full.ticks", "cannot read " + ticksRef);
        } else {
            // per record k (the retrace of refresh k): the GUS timer interrupts counted before it (column 4) and the
            // position in memory there (columns 5, 6). The emulator delivers an interrupt only between its slices,
            // so a tick whose overflow came while the demo's timer interrupt was waiting for this retrace (interrupts
            // of lower priority held) has not run yet when the record is taken: the record then shows the position
            // before that tick ("late"), where hardware runs the tick right after the timer interrupt's `sti`, before
            // its call of 0x2602eb.
            std::string line;
            size_t n = 0, badIrq = 0, late = 0, bad = 0;
            long firstBadIrq = -1, firstBad = -1;
            std::vector<std::pair<long, int>> recs;  // (frame, position) of the capture
            while (std::getline(f, line)) {
                if (line.empty() || line[0] == '#') continue;
                long frame, pit, g, order, row;
                double ts;
                if (std::sscanf(line.c_str(), "%ld %lf %ld %ld %ld %ld", &frame, &ts, &pit, &g, &order, &row) != 6)
                    continue;
                n++;
                const int capPos = int(order << 8 | row);
                recs.push_back({frame, capPos});
                const double t = s.timeOf(double(frame));
                const long k = long(std::lower_bound(s.tickTimes.begin(), s.tickTimes.end(), t) - s.tickTimes.begin());
                if (k != g && double(frame) < s.endRefresh) {
                    if (badIrq++ == 0) firstBadIrq = frame;
                }
                const int p = s.positionAt(double(frame));
                if (p != capPos) {
                    const int before = k >= 2 ? s.positions[size_t(k - 2)] : s.pos0;
                    if (k >= 1 && capPos == before) late++;
                    else if (bad++ == 0) firstBad = frame;
                }
            }
            report(n > 0 && badIrq == 0, "interrupts",
                   fmt("GUS timer interrupts before the retrace of %zu records, %zu differ (first: record %ld)", n,
                       badIrq, firstBadIrq));
            report(n > 0 && bad == 0, "positions",
                   fmt("positionAt(record) against the position in memory at %zu records: %zu identical, %zu late in "
                       "the capture (the tick's interrupt held by the timer interrupt), %zu other differences (first: "
                       "record %ld)",
                       n, n - late - bad, late, bad, firstBad));
            // the script entries' switches: the first record whose position is at or past the entry's end
            constexpr uint32_t kScript = 0x205ed0;
            std::string sw;
            int nsw = 0, diff = 0;
            for (uint32_t a = kScript;; a += 0x14) {
                const size_t o = a - lca::kImageBase;
                uint32_t init, end;
                std::memcpy(&init, &image[o], 4);
                if (!init) break;
                std::memcpy(&end, &image[o + 0x10], 4);
                long cap = -1;
                for (const auto& r : recs)
                    if (r.second >= int(end)) {
                        cap = r.first;
                        break;
                    }
                const double mine = s.refreshOfPosition(int(end));
                nsw++;
                if (cap < 0 || mine != double(cap)) {
                    diff++;
                    sw += fmt(" %04x: %.0f vs %ld;", end, mine, cap);
                }
            }
            report(diff == 0, "switches",
                   fmt("refreshOfPosition(end position) of %d script entries against the first record at or past it: "
                       "%d differ%s",
                       nsw, diff, sw.c_str()));
        }
    }

    // level, clipping, silence
    {
        int peak = 0;
        size_t clipped = 0;
        double sum = 0;
        for (int16_t v : s.pcm) {
            peak = std::max(peak, std::abs(int(v)));
            clipped += std::abs(int(v)) >= 32767;
            sum += double(v) * v;
        }
        const double rms = s.pcm.empty() ? 0 : std::sqrt(sum / double(s.pcm.size()));
        // longest stretch from tick 0 to the last script entry's end whose 50 ms RMS stays below -60 dBFS (the song
        // fades out at its end, F00 stops the player and main holds the title picture for 210 refreshes: the pcm's
        // last seconds are silent, as in the original)
        const size_t win = size_t(s.rate / 20);
        const size_t lastEnd = size_t(s.timeOf(s.refreshOfPosition(lastEndPosition(image))) * s.rate);
        size_t quiet = 0, longest = 0, at = 0;
        for (size_t w0 = size_t(s.tickTimes[0] * s.rate); w0 + win <= std::min(frames, lastEnd); w0 += win) {
            double e = 0;
            for (size_t k = w0; k < w0 + win; k++)
                e += double(s.pcm[2 * k]) * s.pcm[2 * k] + double(s.pcm[2 * k + 1]) * s.pcm[2 * k + 1];
            if (std::sqrt(e / double(2 * win)) < 32.767) {
                quiet += win;
                if (quiet > longest) longest = quiet, at = w0;
            } else {
                quiet = 0;
            }
        }
        Soundtrack tt;
        double mixPeak = 0;
        renderMix(module, image, s.rate, tt, &mixPeak);
        // the tail: from the last 50 ms window above -60 dBFS to the end
        size_t tail = frames;
        for (size_t w1 = frames; w1 >= win; w1 -= win) {
            double e = 0;
            for (size_t k = w1 - win; k < w1; k++)
                e += double(s.pcm[2 * k]) * s.pcm[2 * k] + double(s.pcm[2 * k + 1]) * s.pcm[2 * k + 1];
            if (std::sqrt(e / double(2 * win)) >= 32.767) break;
            tail = w1 - win;
        }
        report(peak > 29000 && peak < 32767 && clipped == 0 && rms > 1000 && longest < size_t(s.rate),
               "level", fmt("mix peak %.4f (1 = one voice at full scale, hard panned; before the high-pass): peak %d "
                            "(%.2f dBFS), %zu samples at full scale, RMS %.0f (%.1f dBFS); longest stretch below -60 "
                            "dBFS before the last entry's end %.2f s (at %.2f s); below -60 dBFS from %.2f s to the "
                            "end (%.2f s)",
                            mixPeak, peak, 20 * std::log10(peak / 32767.0), clipped, rms,
                            20 * std::log10(rms / 32767.0), double(longest) / s.rate, double(at) / s.rate,
                            double(tail) / s.rate, double(frames) / s.rate));
    }

    // note onsets, voice by voice: a note starts on the voice of its channel's pair that was ramped down to -84 dB
    // at the channel's note before, so in a render of that voice alone its first sound is clear. Onset = the first
    // frame within +-20 ms of the tick that is above -50 dB of one voice's full scale after 5 ms below it.
    {
        const int rate = 48000;
        const double thr = std::pow(10.0, -50.0 / 20.0);
        const long quiet = long(0.005 * rate), reach = long(0.020 * rate);
        std::vector<double> offs;
        size_t total = 0, none = 0;
        auto t1 = std::chrono::steady_clock::now();
        for (int v = 0; v < 16; v++) {
            Soundtrack tt;
            double pk = 0;
            const std::vector<float> mix = renderMix(module, image, rate, tt, &pk, 1u << v);
            const long frames = long(mix.size() / 2);
            auto amp = [&](long f) {
                return std::max(std::fabs(mix[size_t(2 * f)]), std::fabs(mix[size_t(2 * f + 1)]));
            };
            for (const LogCard::Note& n : card.notes) {
                if (n.voice != v || !n.fc || n.tick >= int(tt.tickTimes.size())) continue;
                const double t = tt.tickTimes[size_t(n.tick)];
                const long c = std::lround(t * rate);
                if (c - reach - quiet < 0 || c + reach >= frames) continue;
                total++;
                long run = 0, at = -1;
                for (long f = c - reach - quiet; f <= c + reach; f++) {
                    if (amp(f) <= thr) {
                        run++;
                        continue;
                    }
                    if (run >= quiet && f >= c - reach) {
                        at = f;
                        break;
                    }
                    run = 0;
                }
                if (at < 0) none++;
                else offs.push_back((double(at) / rate - t) * 1e3);
            }
        }
        std::sort(offs.begin(), offs.end());
        const size_t in3 = size_t(std::count_if(offs.begin(), offs.end(), [](double o) { return o >= 0 && o < 3.0; }));
        auto q = [&](double p) { return offs.empty() ? 0.0 : offs[size_t(p * double(offs.size() - 1))]; };
        report(!offs.empty() && offs.front() >= 0 && double(in3) >= 0.9 * double(offs.size()), "onsets",
               fmt("%zu note starts with a frequency (16 single-voice renders, %.1f s): %zu sound from %.2f ms after "
                   "their tick (median %.2f, 95 %% %.2f, max %.2f ms), %.1f %% within 3 ms; %zu without an onset in "
                   "+-20 ms (volume 0, quiet sample start or still sounding)",
                   total, since(t1), offs.size(), q(0), q(0.5), q(0.95), q(1),
                   100.0 * double(in3) / double(offs.size()), none));
    }

    // the mix at the GF1's own frame rate against lcaplay.py --wav
    if (!pyWav.empty()) {
        std::vector<int16_t> ref;
        if (!readWav16(pyWav, ref)) {
            report(false, "lcaplay.py WAV", "cannot read " + pyWav);
        } else {
            Soundtrack tt;
            double pk = 0;
            const double rate = gus::kGf1Clock / 16;
            std::vector<float> mix = renderMix(module, image, rate, tt, &pk);
            // lcaplay.py's WAV starts at the timer start and is int16(clip(x / peak * 0.9) * 32767) (truncated)
            const size_t first = 0;
            const size_t n = std::min(ref.size(), mix.size() - 2 * first);
            size_t same = 0, off1 = 0, worst = 0, at = 0;
            double pk2 = 0;
            for (size_t i = 2 * first; i < mix.size(); i++) pk2 = std::max(pk2, double(std::fabs(mix[i])));
            for (size_t i = 0; i < n; i++) {
                const double x = std::clamp(double(mix[2 * first + i]) / pk2 * 0.9, -1.0, 1.0) * 32767.0;
                const int q = int(x);
                const size_t d = size_t(std::abs(q - ref[i]));
                same += d == 0;
                off1 += d == 1;
                if (d > worst) worst = d, at = i;
            }
            report(ref.size() == mix.size() - 2 * first && worst <= 1, "lcaplay.py WAV",
                   fmt("%zu vs %zu samples at %.2f Hz, peak %.6f; %zu identical, %zu off by 1 LSB, max %zu LSB (frame "
                       "%zu)",
                       mix.size() - 2 * first, ref.size(), rate, pk2, same, off1, worst, at / 2));
        }
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

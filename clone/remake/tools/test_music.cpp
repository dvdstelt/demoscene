// test_music: checks the soundtrack code (src/modsynth.cpp, src/mxmplay.cpp, src/gf1.cpp, src/music.cpp) against the
// capture and the Python models in clone/re/models/music (see clone/re/notes-music.md), and measures the render.
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [--exe CLONE.EXE | --image clone_img.bin] [checks]
//   --exe FILE       the original, unpacked by exe.cpp (default: DIR/../original/CLONE.EXE if it is there)
//   --image FILE     the unpacked program image instead (default otherwise: DIR/unpacked/clone_img.bin)
//   --re DIR         clone/re: the capture (full.dram, full.gus, full.ticks); default: the clone/re of the build
//                    (CLONE_RE_DIR), else the first of re, ../re, ../../re, clone/re that has them. Checks, exactly:
//                      DRAM       GUS DRAM after the upload (the module's 8-bit and synthesised 16-bit samples)
//                      full.gus   the register log with the capture's fake GUS, line by line
//                      positions  order, row and timer after every IRQ against full.ticks columns 4-6
//   --module REF     the module buffer against a dump of 0x428470 from snaps/t11.snap (models/music/dumpmod.py)
//   --real-gus REF   the register log with the card model's reads against `mxmplay.py --real-volume-read --out`
//   --mix-wav REF    the synthesiser against `mxmplay.py --wav`: the mix at the card's own 34,300 Hz with every
//                    write on the card's frame grid (renderMix cardGrid), quantised like mxmplay.py: at most 1 LSB
//   --rate N         output rate of the render (default 48000);  --bench N  time renderSoundtrack N times
//   --wav FILE       write the render as a 16-bit stereo WAV
//   --measure        level, clipping, DC, loudness per order, and the note starts of every channel rendered alone
//                    (the restart writes of the card-model register stream against the onsets in the audio)
//   --out-gus FILE   write the fake-GUS register log;  --out-module FILE  write the module buffer
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
#include <vector>

#include "exe.h"
#include "modsynth.h"
#include "music.h"

namespace {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const std::string& path, const void* d, size_t n) {
    std::ofstream f(path, std::ios::binary);
    f.write(static_cast<const char*>(d), std::streamsize(n));
    return bool(f);
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

void compareLogs(const char* what, const std::string& mine, const std::string& refPath) {
    std::vector<uint8_t> raw;
    if (!readFile(refPath, raw)) return report(false, what, "cannot read " + refPath);
    std::vector<std::string> a = splitLines(mine), b = splitLines(std::string(raw.begin(), raw.end()));
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n && a[i] == b[i]) i++;
    if (i == n && a.size() == b.size()) return report(true, what, fmt("%zu lines, identical", a.size()));
    report(false, what, fmt("%zu vs %zu lines, first difference at line %zu: '%s' vs '%s'", a.size(), b.size(), i + 1,
                            i < a.size() ? a[i].c_str() : "-", i < b.size() ? b[i].c_str() : "-"));
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

bool readWav(const std::string& path, std::vector<int16_t>& pcm, int& rate) {
    std::vector<uint8_t> raw;
    if (!readFile(path, raw) || raw.size() < 12) return false;
    size_t at = 12;
    rate = 0;
    while (at + 8 <= raw.size()) {
        uint32_t sz;
        std::memcpy(&sz, &raw[at + 4], 4);
        if (!std::memcmp(&raw[at], "fmt ", 4) && at + 16 <= raw.size()) std::memcpy(&rate, &raw[at + 12], 4);
        if (!std::memcmp(&raw[at], "data", 4)) {
            const size_t len = std::min<size_t>(sz, raw.size() - at - 8);
            pcm.resize(len / 2);
            std::memcpy(pcm.data(), &raw[at + 8], pcm.size() * 2);
            return true;
        }
        at += 8 + sz + (sz & 1);
    }
    return false;
}

constexpr double kPit = 1193182.0;

// ---- --measure: the note starts of every channel ----
// A new note stops the voice (a ramp down to near silence, which the player waits for) and restarts it at the
// sample start + offset with a ramp up; a retrigger restarts it without the stop. For every restart write (r0b) in
// the card-model register stream at IRQ k (music time (k - 2) x 22,100 / 1,193,182 s) the channel is rendered alone
// and the onset searched: the frame within [t - 1 ms, t + 3 ms] where the envelope (the largest magnitude within
// 0.25 ms) is smallest (the stop), then the first frame after it whose magnitude reaches a quarter of the next 20 ms'
// peak. Expected: the ramp wait (up to 1.6 ms) plus the attack of the sample and its volume ramp.
void measureOnsets(std::span<const uint8_t> image, int rate) {
    const MusicTrace tr = traceMusic(image, false);
    std::vector<std::vector<int>> starts(32);  // per voice: IRQ k of the new notes (stop + restart)
    int retrigs = 0;
    {
        const std::vector<std::string> lines = splitLines(tr.gus);
        int stopTick[32];
        std::fill(stopTick, stopTick + 32, -1);
        for (const std::string& l : lines) {
            int tick, v;
            unsigned val;
            char reg[8];
            if (std::sscanf(l.c_str(), "%d v%d r%2s %x", &tick, &v, reg, &val) != 4 || v < 0 || v >= 32 || tick - 10 < 1)
                continue;
            if (!std::strcmp(reg, "00") && (val & 3) == 3) stopTick[v] = tick;  // the GUS update's stop of a new note
            if (std::strcmp(reg, "0b") != 0) continue;
            if (stopTick[v] == tick) starts[size_t(v)].push_back(tick - 10);
            else retrigs++;  // Rxy: a restart without the stop and ramp
        }
    }
    int total = 0, found = 0, quiet = 0, outliers = 0;
    std::vector<double> dts;
    for (int v = 0; v < 18; v++) {
        if (starts[size_t(v)].empty()) continue;
        Soundtrack times;
        double peak = 0;
        const std::vector<float> mix = renderMix(image, rate, times, &peak, 1u << v);
        const int64_t frames = int64_t(mix.size() / 2);
        auto mag = [&](int64_t f) { return f < 0 || f >= frames ? 0.0 : std::max(std::fabs(mix[size_t(2 * f)]), std::fabs(mix[size_t(2 * f + 1)])) + 0.0; };
        int vt = 0, vf = 0, vq = 0;
        double vmin = 1e9, vmax = -1e9;
        for (int k : starts[size_t(v)]) {
            const double t = double(k - 2) * 22100.0 / kPit;
            const int64_t f0 = int64_t(std::floor((t - 0.001) * rate)), f1 = int64_t(std::ceil((t + 0.003) * rate));
            if (f1 + int64_t(0.02 * rate) >= frames || f0 < 0) continue;
            vt++;
            const int64_t w = std::max<int64_t>(1, rate / 4000);  // envelope: the largest magnitude within 0.25 ms
            auto env = [&](int64_t f) {
                double e = 0;
                for (int64_t j = f - w; j <= f + w; j++) e = std::max(e, mag(j));
                return e;
            };
            int64_t dip = f0;
            double dipEnv = env(f0);
            for (int64_t f = f0 + 1; f <= f1; f++)
                if (const double e = env(f); e < dipEnv) {
                    dip = f;
                    dipEnv = e;
                }
            double pk = 0;
            for (int64_t f = dip; f < dip + int64_t(0.02 * rate); f++) pk = std::max(pk, mag(f));
            if (pk < 1e-3) {  // the note is nearly silent (1e-3 = -60 dB of one voice at full scale)
                vq++;
                continue;
            }
            int64_t on = dip;
            while (mag(on) < 0.25 * pk) on++;
            const double dt = (double(on) / rate - t) * 1e3;
            if ((dt < 0 || dt > 12) && outliers++ < 12)
                std::printf("    channel %d, IRQ %d (row %03x): onset %+.2f ms, dip %.2e, peak %.2e\n", v, k,
                            tr.pos[size_t(k - 2)], dt, mag(dip), pk);
            dts.push_back(dt);
            vmin = std::min(vmin, dt);
            vmax = std::max(vmax, dt);
            vf++;
        }
        std::printf("  channel %2d: %4d new notes, %4d onsets at %+.2f .. %+.2f ms from their IRQ, %d nearly silent\n", v, vt, vf,
                    vf ? vmin : 0.0, vf ? vmax : 0.0, vq);
        total += vt;
        found += vf;
        quiet += vq;
    }
    std::sort(dts.begin(), dts.end());
    auto q = [&](double p) { return dts.empty() ? 0.0 : dts[size_t(p * double(dts.size() - 1))]; };
    std::printf("  all: %d new notes (and %d retriggers, not measured), %d onsets (%d nearly silent): onset - IRQ "
                "instant min %.2f, median %.2f, 99%% %.2f, max %.2f ms\n",
                total, retrigs, found, quiet, q(0), q(0.5), q(0.99), q(1));
}

void measure(const Soundtrack& s) {
    const size_t frames = s.pcm.size() / 2;
    int peak = 0;
    size_t clipped = 0;
    double sum[2] = {0, 0}, sq = 0;
    for (size_t i = 0; i < s.pcm.size(); i++) {
        const int v = s.pcm[i];
        peak = std::max(peak, std::abs(v));
        clipped += std::abs(v) >= 32767;
        sum[i & 1] += v;
        sq += double(v) * v;
    }
    std::printf("  level: peak %d (%.2f dBFS), %zu samples at full scale, RMS %.1f dBFS, DC L %.2f R %.2f LSB\n", peak,
                20 * std::log10(peak / 32767.0), clipped, 10 * std::log10(sq / double(s.pcm.size()) / (32767.0 * 32767.0)),
                sum[0] / double(frames), sum[1] / double(frames));
    // loudness per order (from the music time the order's first row is heard to the next one)
    std::printf("  RMS per order (dBFS):");
    int col = 0;
    for (int o = 0;; o++) {
        const double a = s.timeOfPosition(o << 8), b = s.timeOfPosition((o + 1) << 8);
        if (!std::isfinite(a)) break;
        const double tb = std::isfinite(b) ? b : s.endTime();
        // a row is heard one tick after it is read
        const double p = 22100.0 / kPit;
        const size_t fa = size_t(std::max(0.0, (a + p) * s.rate)), fb = std::min(frames, size_t(std::max(0.0, (tb + p) * s.rate)));
        double e = 0;
        for (size_t f = fa; f < fb; f++) e += double(s.pcm[2 * f]) * s.pcm[2 * f] + double(s.pcm[2 * f + 1]) * s.pcm[2 * f + 1];
        const double rms = fb > fa ? std::sqrt(e / double(2 * (fb - fa))) / 32767.0 : 0;
        if (col++ % 12 == 0) std::printf("\n   ");
        std::printf(" %02x:%6.1f", o, rms > 0 ? 20 * std::log10(rms) : -999.0);
    }
    std::printf("\n");
    // silence at the start (nothing before row 00:00 is heard)
    size_t first = 0;
    while (first < frames && std::abs(s.pcm[2 * first]) < 2 && std::abs(s.pcm[2 * first + 1]) < 2) first++;
    std::printf("  first sample above 1 LSB at frame %zu (%.4f s)\n", first, double(first) / s.rate);
}

}  // namespace

int main(int argc, char** argv) {
    std::string reDir, exePath, imagePath, modRef, realRef, mixRef, wavOut, outGus, outModule;
    int rate = 48000, bench = 1;
    bool doMeasure = false;
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
        else if (a == "--exe") exePath = next();
        else if (a == "--image") imagePath = next();
        else if (a == "--module") modRef = next();
        else if (a == "--real-gus") realRef = next();
        else if (a == "--mix-wav") mixRef = next();
        else if (a == "--wav") wavOut = next();
        else if (a == "--rate") rate = std::atoi(next().c_str());
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else if (a == "--measure") doMeasure = true;
        else if (a == "--out-gus") outGus = next();
        else if (a == "--out-module") outModule = next();
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
#ifdef CLONE_RE_DIR
    if (reDir.empty() && exists(std::string(CLONE_RE_DIR) + "/full.gus")) reDir = CLONE_RE_DIR;
#endif
    if (reDir.empty())
        for (const char* d : {"re", "../re", "../../re", "clone/re"})
            if (exists(std::string(d) + "/full.gus")) {
                reDir = d;
                break;
            }
    const std::string base = reDir.empty() ? std::string("re") : reDir;
    if (exePath.empty() && imagePath.empty()) {
        if (exists(base + "/../original/CLONE.EXE")) exePath = base + "/../original/CLONE.EXE";
        else imagePath = base + "/unpacked/clone_img.bin";
    }
    std::vector<uint8_t> image;
    std::string err;
    CloneExe exe;
    if (!exePath.empty()) {
        if (!exe.load(exePath, &err)) {
            std::fprintf(stderr, "%s: %s\n", exePath.c_str(), err.c_str());
            return 1;
        }
        image.assign(exe.image().begin(), exe.image().end());
        std::printf("image: %s, unpacked (%zu bytes)\n", exePath.c_str(), image.size());
    } else if (!readFile(imagePath, image)) {
        std::fprintf(stderr, "cannot read %s (use --exe, --image or --re)\n", imagePath.c_str());
        return 1;
    } else {
        std::printf("image: %s (%zu bytes)\n", imagePath.c_str(), image.size());
    }

    try {
        // ---- the module ----
        auto t0 = std::chrono::steady_clock::now();
        const std::vector<uint8_t> mod = modsynth::buildModule(image);
        std::printf("buildModule: %zu bytes in %.1f ms\n", mod.size(), since(t0) * 1e3);
        if (!outModule.empty()) writeFile(outModule, mod.data(), mod.size());
        if (!modRef.empty()) {
            std::vector<uint8_t> ref;
            if (!readFile(modRef, ref)) {
                report(false, "module", "cannot read " + modRef);
            } else {
                size_t diff = 0, first = 0;
                for (size_t i = 0; i < std::min(ref.size(), mod.size()); i++)
                    if (ref[i] != mod[i] && diff++ == 0) first = i;
                report(diff == 0 && ref.size() == mod.size(), "module",
                       fmt("%zu bytes vs reference %zu; %zu differ (first at %#zx)", mod.size(), ref.size(), diff, first));
            }
        }

        // ---- the player with the capture's fake GUS ----
        if (!reDir.empty() || !outGus.empty()) {
            t0 = std::chrono::steady_clock::now();
            const MusicTrace tr = traceMusic(image, true);
            std::printf("traceMusic (fake GUS): %zu IRQs in %.1f ms\n", tr.pos.size(), since(t0) * 1e3);
            if (!outGus.empty()) writeFile(outGus, tr.gus.data(), tr.gus.size());
            if (!reDir.empty()) {
                std::vector<uint8_t> ref;
                if (!readFile(reDir + "/full.dram", ref)) report(false, "DRAM", "cannot read " + reDir + "/full.dram");
                else report(ref == tr.dram, "DRAM", ref == tr.dram ? "identical (1 MB)" : "differs");
                compareLogs("full.gus", tr.gus, reDir + "/full.gus");
                std::ifstream f(reDir + "/full.ticks");
                std::string line;
                size_t n = 0, bad = 0;
                long firstBad = -1;
                while (std::getline(f, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    long tick, pit, order, row, timer;
                    double ts;
                    if (std::sscanf(line.c_str(), "%ld %ld %lf %ld %ld %ld", &tick, &pit, &ts, &order, &row, &timer) != 6)
                        continue;
                    const long k = tick - 10;
                    if (k < 1 || size_t(k) > tr.pos.size()) continue;
                    n++;
                    const int p = tr.pos[size_t(k - 1)];
                    if ((p >> 8) != order || (p & 0xff) != row || tr.timer[size_t(k - 1)] != timer)
                        if (bad++ == 0) firstBad = tick;
                }
                report(n == tr.pos.size() && n > 0 && bad == 0, "positions",
                       fmt("order, row and timer after %zu of %zu IRQs compared with full.ticks, %zu differ (first at "
                           "tick %ld)",
                           n, tr.pos.size(), bad, firstBad));
            }
        }
        if (!realRef.empty()) {
            const MusicTrace tr = traceMusic(image, false);
            compareLogs("GF1-read log", tr.gus, realRef);
        }

        // ---- the soundtrack ----
        Soundtrack s;
        double best = 1e9;
        for (int b = 0; b < bench; b++) {
            t0 = std::chrono::steady_clock::now();
            s = renderSoundtrack(image, rate);
            best = std::min(best, since(t0));
        }
        const size_t frames = s.pcm.size() / 2;
        std::printf("renderSoundtrack: %zu frames at %d Hz (%.4f s of music time), %zu IRQs; best of %d: %.1f ms\n",
                    frames, s.rate, s.endTime(), s.irqTimes.size(), bench, best * 1e3);
        std::printf("  IRQ 1 at %.6f s, IRQ 2 at %.6f s, tick 0 at %.6f s; order 0c heard from %.4f s, 3c:3f read at "
                    "%.4f s, position at the end %03x\n",
                    s.irqTimes[0], s.irqTimes[1], s.timeOfTick(0), s.timeOfPosition(0x0c00) + 22100.0 / kPit,
                    s.timeOfPosition(0x3c3f), s.positionAt(s.endTime()));
        if (!wavOut.empty()) report(writeWav(wavOut, s.pcm, s.rate), "WAV", "written to " + wavOut);
        if (doMeasure) {
            measure(s);
            measureOnsets(image, rate);
        }

        if (!mixRef.empty()) {
            std::vector<int16_t> ref;
            int refRate = 0;
            if (!readWav(mixRef, ref, refRate)) {
                report(false, "mix WAV", "cannot read " + mixRef);
            } else {
                Soundtrack times;
                double peak = 0;
                const std::vector<float> mix = renderMix(image, refRate, times, &peak, 0xffffffffu, true);
                // mxmplay.py's WAV starts at IRQ 1 and is int16(clip(x / peak * 0.9) * 32767) (truncating); frame j
                // of it is frame j - lead here, lead = the card frames from IRQ 1 to IRQ 2 (635 at 34,300 Hz)
                const int64_t lead = int64_t(std::floor(22100.0 * refRate / kPit));
                const int64_t n = std::min<int64_t>(int64_t(ref.size() / 2) - lead, int64_t(mix.size() / 2));
                double se = 0, ss = 0;
                int64_t same = 0, maxLsb = 0, at = 0;
                for (int64_t f = 0; f < n; f++)
                    for (int c = 0; c < 2; c++) {
                        const double a = double(mix[size_t(2 * f + c)]) / peak * 0.9;
                        const int q = int(std::clamp(a, -1.0, 1.0) * 32767.0), b = ref[size_t(2 * (f + lead) + c)];
                        se += double(q - b) * (q - b);
                        ss += double(b) * b;
                        same += q == b;
                        if (std::abs(q - b) > maxLsb) {
                            maxLsb = std::abs(q - b);
                            at = f;
                        }
                    }
                report(n > 0 && maxLsb <= 1, "mix WAV",
                       fmt("on the card's frame grid, %lld frames at %d Hz: %lld of %lld samples identical, max %lld "
                           "LSB (at %.4f s), RMS difference %.1f dB below the signal",
                           (long long)n, refRate, (long long)same, (long long)(2 * n), (long long)maxLsb,
                           double(at) / refRate, 10 * std::log10(ss / std::max(se, 1e-30))));
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

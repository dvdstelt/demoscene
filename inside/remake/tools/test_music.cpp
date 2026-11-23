// test_music: checks Inside's soundtrack code (src/amp.cpp, src/gusdrv.cpp, src/gf1.cpp, src/music.cpp) against the
// capture and the Python models in inside/re/models/music (see inside/re/notes-music.md), and measures the render.
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--re DIR] [--orig DIR] [checks]
//   --re DIR         inside/re: the capture (full.gus, full.ticks, full.dram); default: the inside/re of the build
//                    (INSIDE_RE_DIR), else the first of re, ../re, ../../re, inside/re that has them. Checks, exactly,
//                    with the capture's fake GUS and its lost callback (interrupt 12117):
//                      start-up   the writes of SDIInitDevice .. the uploads, in order (their stamps depend on the
//                                 machine's speed: text only)
//                      full.gus   every music and exit write with its timer-interrupt stamp
//                      exit       the demo exits after the interrupt of the capture's last write
//                      positions  order and row after the interrupts of every full.ticks record that has them
//                      DRAM       the uploaded samples and their layout (full.dram holds them unsigned: the
//                                 capture's DMA did not invert the MSB, notes-music 13.1)
//                    and, on the remake's timeline (no lost callback), the row starts against notes-core 1.3's
//                    closed form and the exit callback.
//   --orig DIR       INSIDE.EXE, EYE.AMF and SDIGUS.DSD (default DIR/../original)
//   --mix-wav REF    the synthesiser against `models/music/render.py REF --skip ''`: the mix at the card's own
//                    25,725 Hz with every write on the card's frame grid (renderMix cardGrid), quantised like
//                    render.py, up to the exit: at most 1 LSB
//   --rate N         output rate of the render (default 48000);  --bench N  time renderSoundtrack N times
//   --wav FILE       write the render as a 16-bit stereo WAV
//   --out-gus FILE   write the fake-GUS register log
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "exe.h"
#include "music.h"

namespace {

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

std::string afterStamp(const std::string& l) {
    const size_t p = l.find(' ');
    return p == std::string::npos ? l : l.substr(p + 1);
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
    if (!fileBytes(path, raw) || raw.size() < 12) return false;
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

constexpr double kPitHz = 1193182.0;
constexpr double kRefreshHz = 25175000.0 / (800.0 * 449.0);
constexpr int kLostIrq = 12117;  // the capture's lost callback (notes-music 4)

// notes-core 1.3: song row R starts in callback floor((1200 + 768 R) / 35) + 1 (order 36: three times as long)
int rowStartFormula(int R) {
    const long long t = 1200 + 768LL * std::min(R, 2304) + 2304LL * std::max(0, R - 2304);
    return int(t / 35) + 1;
}

// refresh (relative to record 0 of the capture) of timer interrupt i (notes-music 9)
double irqRefresh(int i) {
    const int m = (i - 1) / 4, j = (i - 1) % 4 + 1;
    const double frame = kPitHz / kRefreshHz;
    return j == 4 ? 16 + m - 37.0 / 449.0 : 15 + m + (4085.0 * j - 37.0 * frame / 449.0) / frame;
}

void checkCapture(const InsideExe& exe, const std::vector<uint8_t>& amf, const std::vector<uint8_t>& dsd,
                  const std::string& reDir, const std::string& outGus) {
    auto t0 = std::chrono::steady_clock::now();
    const int lost[] = {kLostIrq};
    const MusicTrace tr = traceMusic(exe, amf, dsd, true, lost);
    std::printf("traceMusic (fake GUS, callback of interrupt %d lost): %zu interrupts in %.1f ms\n", kLostIrq,
                tr.pos.size(), since(t0) * 1e3);
    if (!outGus.empty()) {
        std::ofstream f(outGus, std::ios::binary);
        f << tr.gus;
    }
    if (reDir.empty()) return;

    // ---- the register log ----
    std::vector<uint8_t> raw;
    if (!fileBytes(reDir + "/full.gus", raw)) {
        report(false, "full.gus", "cannot read " + reDir + "/full.gus");
    } else {
        const std::vector<std::string> ref = splitLines(std::string(raw.begin(), raw.end()));
        const std::vector<std::string> mine = splitLines(tr.gus);
        const size_t ns = tr.startupLines;
        size_t i = 0;
        while (i < ns && i < ref.size() && afterStamp(ref[i]) == afterStamp(mine[i])) i++;
        auto stamp = [&](size_t k) { return k < ref.size() ? ref[k].substr(0, ref[k].find(' ')) : std::string("-"); };
        report(i == ns && ns <= ref.size(), "start-up",
               i == ns ? fmt("%zu writes identical (capture stamps %s..%s)", ns, stamp(0).c_str(),
                             stamp(ns - 1).c_str())
                       : fmt("first difference at line %zu: capture '%s', remake '%s'", i + 1,
                             i < ref.size() ? ref[i].c_str() : "-", mine[i].c_str()));
        size_t j = ns;
        while (j < ref.size() && j < mine.size() && ref[j] == mine[j]) j++;
        const bool same = j == ref.size() && j == mine.size();
        report(same, "full.gus",
               same ? fmt("%zu music and exit writes identical, with their interrupt stamps (%zu lines in all)",
                          ref.size() - ns, ref.size())
                    : fmt("capture %zu, remake %zu music writes; identical for the first %zu; at line %zu: capture "
                          "'%s', remake '%s'",
                          ref.size() - ns, mine.size() - ns, j - ns, j + 1, j < ref.size() ? ref[j].c_str() : "-",
                          j < mine.size() ? mine[j].c_str() : "-"));
        const int lastStamp = ref.empty() ? -1 : std::atoi(ref.back().c_str());
        report(tr.exitIrq == lastStamp, "exit",
               fmt("after the callback of interrupt %d (callback %d; the capture's last write: %d)", tr.exitIrq,
                   tr.exitCallback, lastStamp));
    }

    // ---- the music positions ----
    {
        std::ifstream f(reDir + "/full.ticks");
        std::string line;
        size_t n = 0, bad = 0;
        long firstBad = -1;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::vector<std::string> c{std::istream_iterator<std::string>(ss), std::istream_iterator<std::string>()};
            if (c.size() < 12) continue;
            const long rec = std::atol(c[0].c_str()), order = std::atol(c[2].c_str()), row = std::atol(c[3].c_str());
            const long irqs = std::atol(c[10].c_str());
            if (order < 0) continue;
            n++;
            const long k = irqs - tr.firstIrq;
            const int mine = k >= 0 && size_t(k) < tr.pos.size() ? tr.pos[size_t(k)] : 0;
            if (mine != order * 64 + row && bad++ == 0) firstBad = rec;
        }
        report(n > 0 && bad == 0, "positions",
               fmt("order and row of %zu full.ticks records compared, %zu differ (first: record %ld)", n, bad,
                   firstBad));
    }

    // ---- DRAM ----
    std::vector<uint8_t> dram;
    if (!fileBytes(reDir + "/full.dram", dram)) {
        report(false, "DRAM", "cannot read " + reDir + "/full.dram");
    } else {
        std::vector<uint8_t> mine = tr.dram;  // the capture's DMA stored the bytes as they are: undo the XOR
        size_t bytes = 0;
        for (size_t u = 0; u + 1 < tr.uploads.size(); u += 2)
            for (uint32_t k = 0; k < tr.uploads[u + 1]; k++) {
                mine[(tr.uploads[u] + k) & 0xfffff] ^= 0x80;
                bytes++;
            }
        size_t diff = 0, first = 0;
        for (size_t k = 0; k < std::min(mine.size(), dram.size()); k++)
            if (mine[k] != dram[k] && diff++ == 0) first = k;
        report(diff == 0 && mine.size() == dram.size(), "DRAM",
               fmt("%zu samples, %zu bytes; %zu of %zu DRAM bytes differ (first at %#zx)", tr.uploads.size() / 2,
                   bytes, diff, dram.size(), first));
    }
}

// The card's model against the fake card on the capture's timeline (notes-music 8: on hardware the same 140,095
// writes go out, 30,384 values differ, and the ramp waits let the GF1 run in 924 of the 52,172 music interrupts),
// then the remake's timeline (no lost callback): the row starts against notes-core 1.3 and the exit.
void checkTimeline(const InsideExe& exe, const std::vector<uint8_t>& amf, const std::vector<uint8_t>& dsd) {
    const int lost[] = {kLostIrq};
    const MusicTrace fake = traceMusic(exe, amf, dsd, true, lost), hw = traceMusic(exe, amf, dsd, false, lost);
    const std::vector<std::string> a = splitLines(fake.gus), b = splitLines(hw.gus);
    size_t sameReg = 0, diffVal = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        const size_t ka = a[i].find_last_of(' '), kb = b[i].find_last_of(' ');
        if (a[i].substr(0, ka) != b[i].substr(0, kb)) continue;
        sameReg++;
        diffVal += a[i] != b[i];
    }
    // (the exit's waits, in the last interrupt, are counted apart: render.py does not run the exit)
    int waits = 0, maxWait = 0;
    const size_t music = hw.waited.empty() ? 0 : hw.waited.size() - 1;
    for (size_t i = 0; i < music; i++) {
        waits += hw.waited[i] > 0;
        maxWait = std::max(maxWait, hw.waited[i]);
    }
    report(a.size() == b.size() && sameReg == a.size() && diffVal == 30384 && waits == 924, "card model",
           fmt("%zu writes, %zu of them to the same register in the same interrupt as the fake card's, %zu values "
               "differ; the ramp waits ran the card 1..%d frames in %d of the first %zu music interrupts (the last "
               "one, with the exit: %d frames)",
               b.size(), sameReg, diffVal, maxWait, waits, music, music ? hw.waited.back() : 0));

    const MusicTrace tr = traceMusic(exe, amf, dsd, false, {});
    size_t bad = 0;
    int firstBad = -1;
    for (size_t R = 0; R < tr.rowStart.size(); R++)
        if (tr.rowStart[R] != rowStartFormula(int(R)) && bad++ == 0) firstBad = int(R);
    report(bad == 0 && tr.rowStart.size() == 2329, "row starts",
           fmt("%zu rows (0:00 .. %d:%02d) on the remake's timeline, %zu differ from notes-core 1.3 (first: row %d); "
               "row 0:00 in callback %d",
               tr.rowStart.size(), int(tr.rowStart.size() - 1) / 64, int(tr.rowStart.size() - 1) % 64, bad, firstBad,
               tr.rowStart.empty() ? -1 : tr.rowStart[0]));
    report(tr.exitCallback == tr.rowStart.back(), "remake exit",
           fmt("after callback %d (interrupt %d), the start of the last row: end at record %d", tr.exitCallback,
               tr.exitIrq, (tr.exitCallback + 150) / 4 + 1));
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
                20 * std::log10(std::max(peak, 1) / 32767.0), clipped,
                10 * std::log10(std::max(sq, 1.0) / double(s.pcm.size()) / (32767.0 * 32767.0)),
                sum[0] / double(frames), sum[1] / double(frames));
    std::printf("  RMS per order (dBFS):");
    for (int o = 0; o < 37; o++) {
        // callback M runs in refresh (M + 150) / 4
        const double a = s.timeOf(s.rowCallback(o * 64) / 4.0 + 37.5);
        const double b = s.timeOf(s.rowCallback(o * 64 + 64) / 4.0 + 37.5);
        const size_t fa = size_t(std::max(0.0, a * s.rate));
        const size_t fb = std::isfinite(b) ? std::min(frames, size_t(b * s.rate)) : frames;
        double e = 0;
        for (size_t f = 2 * fa; f < 2 * fb; f++) e += double(s.pcm[f]) * s.pcm[f];
        const double rms = fb > fa ? std::sqrt(e / double(2 * (fb - fa))) / 32767.0 : 0;
        if (o % 12 == 0) std::printf("\n   ");
        std::printf(" %2d:%6.1f", o, rms > 0 ? 20 * std::log10(rms) : -999.0);
    }
    std::printf("\n");
    size_t first = 0;
    while (first < frames && std::abs(s.pcm[2 * first]) < 2 && std::abs(s.pcm[2 * first + 1]) < 2) first++;
    // row 0:00 sounds in callback rowStart[0] = 35: interrupt 125, refresh 46.16 of the capture
    const double row0 = (irqRefresh(int(s.rowStart.empty() ? 0 : s.rowStart[0]) + 90) - 37.0) / kRefreshHz;
    std::printf("  first sample above 1 LSB at frame %zu (%.4f s of music time; row 0:00's interrupt at %.4f s)\n",
                first, double(first) / s.rate, row0);
}

void compareMix(const InsideExe& exe, const std::vector<uint8_t>& amf, const std::vector<uint8_t>& dsd,
                const std::string& refPath) {
    std::vector<int16_t> ref;
    int refRate = 0;
    if (!readWav(refPath, ref, refRate)) return report(false, "mix WAV", "cannot read " + refPath);
    Soundtrack times;
    double peak = 0;
    const std::vector<float> mix = renderMix(exe, amf, dsd, refRate, times, &peak, 0xffffffffu, true);
    const MusicTrace tr = traceMusic(exe, amf, dsd, false, {});
    // render.py: frame 0 = record 0, int16(clip(x / peak * 0.9) * 32767) (truncating), no exit: compare up to the
    // card frame of the exit's interrupt
    const int64_t exitFrame = int64_t(std::floor(irqRefresh(tr.exitIrq) / kRefreshHz * refRate));
    const int64_t n = std::min<int64_t>({int64_t(ref.size() / 2), int64_t(mix.size() / 2), exitFrame});
    double se = 0, ss = 0;
    int64_t same = 0, maxLsb = 0, at = 0;
    for (int64_t f = 0; f < n; f++)
        for (int c = 0; c < 2; c++) {
            const double a = double(mix[size_t(2 * f + c)]) / peak * 0.9;
            const int q = int(std::clamp(a, -1.0, 1.0) * 32767.0), b = ref[size_t(2 * f + c)];
            se += double(q - b) * (q - b);
            ss += double(b) * b;
            same += q == b;
            if (std::abs(q - b) > maxLsb) {
                maxLsb = std::abs(q - b);
                at = f;
            }
        }
    report(n > 0 && maxLsb <= 1, "mix WAV",
           fmt("on the card's frame grid, %lld frames at %d Hz (to the exit at %.3f s; peak %.4f): %lld of %lld "
               "samples identical, max %lld LSB (at %.4f s), RMS difference %.1f dB below the signal",
               (long long)n, refRate, double(exitFrame) / refRate, peak, (long long)same, (long long)(2 * n),
               (long long)maxLsb, double(at) / refRate, 10 * std::log10(ss / std::max(se, 1e-30))));
}

}  // namespace

int main(int argc, char** argv) {
    std::string reDir, origDir, mixRef, wavOut, outGus;
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
        else if (a == "--mix-wav") mixRef = next();
        else if (a == "--wav") wavOut = next();
        else if (a == "--rate") rate = std::atoi(next().c_str());
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else if (a == "--out-gus") outGus = next();
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
#ifdef INSIDE_RE_DIR
    if (reDir.empty() && exists(std::string(INSIDE_RE_DIR) + "/full.gus")) reDir = INSIDE_RE_DIR;
#endif
    if (reDir.empty())
        for (const char* d : {"re", "../re", "../../re", "inside/re"})
            if (exists(std::string(d) + "/full.gus")) {
                reDir = d;
                break;
            }
    if (origDir.empty()) origDir = (reDir.empty() ? std::string("re") : reDir) + "/../original";

    InsideExe exe;
    std::string err;
    if (!exe.load(origDir + "/INSIDE.EXE", &err)) {
        std::fprintf(stderr, "%s/INSIDE.EXE: %s\n", origDir.c_str(), err.c_str());
        return 1;
    }
    std::vector<uint8_t> amf, dsd;
    if (!fileBytes(origDir + "/EYE.AMF", amf) || !fileBytes(origDir + "/SDIGUS.DSD", dsd)) {
        std::fprintf(stderr, "cannot read EYE.AMF and SDIGUS.DSD in %s (use --orig)\n", origDir.c_str());
        return 1;
    }
    std::printf("originals: %s; capture: %s\n", origDir.c_str(), reDir.empty() ? "(none)" : reDir.c_str());

    try {
        checkCapture(exe, amf, dsd, reDir, outGus);
        checkTimeline(exe, amf, dsd);

        Soundtrack s;
        double best = 1e9;
        for (int b = 0; b < bench; b++) {
            const auto t0 = std::chrono::steady_clock::now();
            s = renderSoundtrack(exe, amf, dsd, rate);
            best = std::min(best, since(t0));
        }
        const size_t frames = s.pcm.size() / 2;
        std::printf("renderSoundtrack: %zu frames at %d Hz (%.4f s: refresh %.0f to %.0f); best of %d: %.1f ms\n",
                    frames, s.rate, double(frames) / s.rate, s.startRefresh, s.endRefresh, bench, best * 1e3);
        measure(s);
        if (!wavOut.empty()) report(writeWav(wavOut, s.pcm, s.rate), "WAV", "written to " + wavOut);
        if (!mixRef.empty()) compareMix(exe, amf, dsd, mixRef);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

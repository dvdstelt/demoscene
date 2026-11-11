// test_music: checks the soundtrack code (src/mp4synth.cpp, src/mxmplay.cpp, src/gf1.cpp) against the capture and
// the Python models in jizz/re/models/music (see jizz/re/notes-music.md).
//
// Build: cmake --build <build dir> --target test_music   (not part of the default build)
//
// Usage: test_music [--exe JIZZ.EXE | --image jizz_img.bin] [checks]
//   --module REF     the expanded module against dump/smokeout.mxm (equal bytes, REF's tail all zero)
//   --gus REF        fake-GUS register log against full.gus (line by line)
//   --dram REF       GUS DRAM after the upload against full.dram
//   --ticks REF      xmpGetPos after every IRQ against columns 4 and 5 of full.ticks (IRQ k = log tick 433 + k)
//   --real-gus REF   register log with the GF1 model's volume reads against `mxmplay.py --real-volume-read --out`
//   --f64 REF        the soundtrack PCM against mxmplay.py's raw float64 stereo mix (scaled by 0.9 / its peak)
//   --wav REF        the soundtrack PCM against the 16-bit WAV of `mxmplay.py --wav`
//   --out-gus FILE   write the fake-GUS register log;  --out-module FILE  write the expanded module
//   --bench N        time renderSoundtrack N times (default 1)
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
#include "mp4synth.h"
#include "mxmplay.h"

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
    char b[512];
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

struct Stats {
    double peak = 0, rms = 0;
    size_t at = 0;
};

template <class F> Stats diffStats(size_t n, F diffAt) {
    Stats s;
    double sum = 0;
    for (size_t i = 0; i < n; i++) {
        double e = diffAt(i);
        sum += e * e;
        if (std::fabs(e) > s.peak) {
            s.peak = std::fabs(e);
            s.at = i;
        }
    }
    s.rms = n ? std::sqrt(sum / double(n)) : 0;
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::string exePath, imagePath, modRef, gusRef, dramRef, ticksRef, realRef, f64Ref, wavRef, outGus, outModule;
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
        if (a == "--exe") exePath = next();
        else if (a == "--image") imagePath = next();
        else if (a == "--module") modRef = next();
        else if (a == "--gus") gusRef = next();
        else if (a == "--dram") dramRef = next();
        else if (a == "--ticks") ticksRef = next();
        else if (a == "--real-gus") realRef = next();
        else if (a == "--f64") f64Ref = next();
        else if (a == "--wav") wavRef = next();
        else if (a == "--out-gus") outGus = next();
        else if (a == "--out-module") outModule = next();
        else if (a == "--bench") bench = std::max(1, std::atoi(next().c_str()));
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }
    JizzExe exe;
    std::string err;
    if (!imagePath.empty() ? !exe.loadImage(imagePath, &err) : !exe.load(exePath.empty() ? "../original/JIZZ.EXE" : exePath, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    // ---- the module ----
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> mod = expandModule(exe);
    std::printf("expandModule: %zu bytes in %.1f ms\n", mod.size(), since(t0) * 1e3);
    if (!outModule.empty()) writeFile(outModule, mod.data(), mod.size());
    if (!modRef.empty()) {
        std::vector<uint8_t> ref;
        if (!readFile(modRef, ref)) {
            report(false, "module", "cannot read " + modRef);
        } else {
            size_t n = std::min(ref.size(), mod.size()), diff = 0, first = n;
            for (size_t i = 0; i < n; i++)
                if (ref[i] != mod[i] && diff++ == 0) first = i;
            bool tailZero = std::all_of(ref.begin() + long(n), ref.end(), [](uint8_t b) { return b == 0; });
            report(diff == 0 && ref.size() >= mod.size() && tailZero, "module",
                   fmt("%zu bytes vs reference %zu; %zu differ (first at %zu); reference tail of %zu bytes %s", mod.size(),
                       ref.size(), diff, first, ref.size() - n, tailZero ? "all zero" : "NOT zero"));
        }
    }

    // ---- the player with the capture's fake GUS ----
    if (!gusRef.empty() || !dramRef.empty() || !ticksRef.empty() || !outGus.empty()) {
        MusicTrace tr;
        t0 = std::chrono::steady_clock::now();
        if (!traceSoundtrack(exe, true, tr, &err)) {
            std::fprintf(stderr, "trace: %s\n", err.c_str());
            return 1;
        }
        std::printf("traceSoundtrack (fake GUS): %zu IRQs in %.1f ms\n", tr.pos.size(), since(t0) * 1e3);
        if (!outGus.empty()) writeFile(outGus, tr.gus.data(), tr.gus.size());
        if (!gusRef.empty()) compareLogs("full.gus", tr.gus, gusRef);
        if (!dramRef.empty()) {
            std::vector<uint8_t> ref;
            if (!readFile(dramRef, ref)) report(false, "DRAM", "cannot read " + dramRef);
            else report(ref == tr.dram, "DRAM", ref == tr.dram ? "identical (1 MB)" : "differs");
        }
        if (!ticksRef.empty()) {
            std::ifstream f(ticksRef);
            std::string line;
            size_t n = 0, bad = 0;
            long firstBad = -1;
            while (std::getline(f, line)) {
                if (line.empty() || line[0] == '#') continue;
                long tick, pit, order, row;
                double ts;
                if (std::sscanf(line.c_str(), "%ld %ld %lf %ld %ld", &tick, &pit, &ts, &order, &row) != 5) continue;
                long k = tick - 433;
                if (k < 1 || size_t(k) > tr.pos.size()) continue;
                n++;
                int p = tr.pos[size_t(k - 1)];
                if ((p >> 8) != order || (p & 0xff) != row) {
                    if (bad++ == 0) firstBad = tick;
                }
            }
            report(n == tr.pos.size() && bad == 0, "positions",
                   fmt("%zu of %zu IRQs compared with full.ticks, %zu differ (first at tick %ld)", n, tr.pos.size(), bad,
                       firstBad));
        }
    }
    if (!realRef.empty()) {
        MusicTrace tr;
        if (!traceSoundtrack(exe, false, tr, &err)) {
            std::fprintf(stderr, "trace: %s\n", err.c_str());
            return 1;
        }
        compareLogs("GF1-read log", tr.gus, realRef);
    }

    // ---- the soundtrack ----
    Soundtrack s;
    double best = 1e9;
    for (int b = 0; b < bench; b++) {
        t0 = std::chrono::steady_clock::now();
        if (!renderSoundtrack(exe, s, &err)) {
            std::fprintf(stderr, "render: %s\n", err.c_str());
            return 1;
        }
        best = std::min(best, since(t0));
    }
    const size_t frames = s.pcm.size() / 2;
    std::printf("renderSoundtrack: %zu frames at %.1f Hz (%.3f s), %zu rows, timer0 %.0f, length %.4f s; best of %d: "
                "%.1f ms\n",
                frames, s.rate, double(frames) / s.rate, s.rows.size(), s.timer0, s.length, bench, best * 1e3);
    for (const auto& r : s.rows)
        if ((r.pos & 0xff) == 0) std::printf("  order %02x at %8.3f s (timer %.3f s)\n", r.pos >> 8, r.time,
                                             (s.timer0 + r.time * 1193182.0) / 1193182.0);
    std::printf("  last row %02x:%02x at %.4f s\n", s.rows.back().pos >> 8, s.rows.back().pos & 0xff, s.rows.back().time);

    if (!f64Ref.empty()) {
        std::vector<uint8_t> raw;
        if (!readFile(f64Ref, raw)) {
            report(false, "f64 mix", "cannot read " + f64Ref);
        } else {
            std::vector<double> ref(raw.size() / 8);
            std::memcpy(ref.data(), raw.data(), ref.size() * 8);
            double pk = 0;
            for (double v : ref) pk = std::max(pk, std::fabs(v));
            size_t n = std::min(ref.size(), s.pcm.size());
            Stats d = diffStats(n, [&](size_t i) { return double(s.pcm[i]) - ref[i] / pk * 0.9; });
            report(ref.size() == s.pcm.size() && d.peak < 1e-6, "f64 mix",
                   fmt("%zu vs %zu samples; reference peak %.17g; scaled difference max %.3g (frame %zu) RMS %.3g",
                       s.pcm.size(), ref.size(), pk, d.peak, d.at / 2, d.rms));
        }
    }
    if (!wavRef.empty()) {
        std::vector<uint8_t> raw;
        size_t at = 12, data = 0, len = 0;
        if (readFile(wavRef, raw))
            while (at + 8 <= raw.size()) {
                uint32_t sz;
                std::memcpy(&sz, &raw[at + 4], 4);
                if (!std::memcmp(&raw[at], "data", 4)) {
                    data = at + 8;
                    len = std::min<size_t>(sz, raw.size() - data);
                    break;
                }
                at += 8 + sz + (sz & 1);
            }
        if (!data) {
            report(false, "WAV", "cannot read " + wavRef);
        } else {
            std::vector<int16_t> ref(len / 2);
            std::memcpy(ref.data(), &raw[data], ref.size() * 2);
            size_t n = std::min(ref.size(), s.pcm.size()), same = 0;
            int maxLsb = 0;
            for (size_t i = 0; i < n; i++) {  // mxmplay.py: int16(clip(x, -1, 1) * 32767), truncating
                int q = int(double(std::clamp(s.pcm[i], -1.0f, 1.0f)) * 32767.0);
                maxLsb = std::max(maxLsb, std::abs(q - ref[i]));
                same += q == ref[i];
            }
            Stats d = diffStats(n, [&](size_t i) { return double(s.pcm[i]) - ref[i] / 32767.0; });
            report(ref.size() == s.pcm.size() && maxLsb <= 1, "WAV",
                   fmt("%zu vs %zu samples; quantised like mxmplay.py: %zu identical, max %d LSB; float difference max "
                       "%.3g RMS %.3g (1 LSB = %.3g)",
                       s.pcm.size(), ref.size(), same, maxLsb, d.peak, d.rms, 1 / 32767.0));
        }
    }
    std::printf("%s\n", ok ? "ALL MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

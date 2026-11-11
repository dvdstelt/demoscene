// test_music: checks src/music.cpp against paper/re/player_ref.py (the reference port of Paper's player + GF1).
//
// Build (toolbox acme303, from paper/remake; use src/pklite_null.cpp while src/pklite.cpp does not exist):
//   g++ -std=c++20 -O2 -Wall -Isrc -o test_music tools/test_music.cpp src/music.cpp src/exe.cpp src/pklite.cpp
//       $(pkg-config --cflags --libs sdl3)
//
// Usage: test_music [--image paper_img.bin | --exe PAPER.EXE] [options]
//   --bench N          time Music::load N times (default 3)
//   --gus OUT          write the GF1 register-write log in player_ref.py's .gus text format
//   --cmp-gus REF      compare that log with REF (player_ref.py --real-volume-read --out x -> x.gus)
//   --ticks REF        compare counter(t) with column 2 of REF (paper/re/full.ticks or x.ticks)
//   --countdown OUT    write "tick counter countdown" per tick
//   --pcm OUT          write pcm() as raw float32 interleaved stereo
//   --cmp-f64 REF      compare pcm() with REF: raw float64 interleaved stereo mix of player_ref.py before its
//                      normalisation (scaled here by 0.9 / peak(REF), as player_ref.py --wav does)
//   --cmp-wav REF      compare pcm() with a 16-bit WAV written by player_ref.py --wav (paper/re/full.wav)
//   --play SECONDS     play through SDL from --from TICK (default 0), printing the clock; tests pause and seek
//   --clock SECONDS    exercise the clock without a device (SDL_AUDIO_DRIVER forced to a missing driver)
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

struct MusicTest {
    static bool render(Music& m, const PaperExe& x, std::string* err, std::string* log) { return m.render(x, err, log); }
};

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

double seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

void sleepMs(int ms) { SDL_Delay(Uint32(ms)); }

struct Diff {
    double rms = 0, peak = 0;
    size_t at = 0;
};

// Difference statistics of two interleaved stereo signals (1.0 = full scale).
template <class F> Diff diffStats(size_t n, F diffAt) {
    Diff d;
    double s = 0;
    for (size_t i = 0; i < n; i++) {
        double e = diffAt(i);
        s += e * e;
        if (std::fabs(e) > d.peak) {
            d.peak = std::fabs(e);
            d.at = i;
        }
    }
    d.rms = n ? std::sqrt(s / double(n)) : 0;
    return d;
}

int playTest(Music& m, double secs, double from) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::printf("play: SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("play: audio driver %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
    m.seekTick(from);
    bool dev = m.startPlayback();
    std::printf("play: startPlayback -> %s\n", dev ? "device open" : "no device (timer clock)");
    double last = -1;
    int backwards = 0, polls = 0, stalls = 0;
    double maxDev = 0;  // largest |clock step - wall step| between two polls in steady playback, in ms
    auto t0 = std::chrono::steady_clock::now(), tl = t0, steady = t0 + std::chrono::milliseconds(1000);
    double t0tick = m.tick();
    bool paused = false, seeked = false;
    int k = 0;
    while (seconds(t0, std::chrono::steady_clock::now()) < secs) {
        auto now = std::chrono::steady_clock::now();
        double el = seconds(t0, now);
        if (!paused && el > secs * 0.6 && el < secs * 0.75) {
            std::printf("play: pause at tick %.3f\n", m.tick());
            m.setPaused(true);
            paused = true;
        }
        if (paused && el >= secs * 0.75) {
            std::printf("play: resume at tick %.3f\n", m.tick());
            m.setPaused(false);
            paused = false;
            if (!seeked) {
                seeked = true;
                double to = m.tick() + 3 * Music::kTickHz;
                std::printf("play: seek to %.3f\n", to);
                m.seekTick(to);
                last = -1;
            }
            steady = now + std::chrono::milliseconds(1000);
        }
        double t = m.tick();
        if (last >= 0 && t < last) backwards++;
        if (last >= 0 && !paused && tl >= steady) {  // steady playback: compare the clock step with the wall step
            polls++;
            if (t == last) stalls++;
            maxDev = std::max(maxDev, std::fabs((t - last) / Music::kTickHz - seconds(tl, now)) * 1e3);
        }
        last = t;
        tl = now;
        if (++k % 100 == 0)
            std::printf("  wall %6.3f s  tick %9.3f  counter %4d  countdown %2d  ended %d%s\n", el, t,
                        m.counter(int(t)), m.rowCountdown(int(t)), int(m.ended()), m.paused() ? "  (paused)" : "");
        sleepMs(5);
    }
    double t1tick = m.tick();
    std::printf("play: %.3f ticks in %.3f s wall (incl. pause and +%.0f-tick seek), backwards steps %d, ended %d\n",
                t1tick - t0tick, seconds(t0, std::chrono::steady_clock::now()), 3 * Music::kTickHz, backwards,
                int(m.ended()));
    std::printf("play: steady playback: %d polls, %d without progress, max |clock step - wall step| %.2f ms\n",
                polls, stalls, maxDev);
    m.stopPlayback();
    SDL_Quit();
    return backwards ? 1 : 0;
}

int clockTest(Music& m, double secs) {
    // force "no audio device": an audio driver that does not exist
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "no-such-driver");
    SDL_Init(0);
    m.seekTick(100);
    bool dev = m.startPlayback();
    std::printf("clock: startPlayback -> %s\n", dev ? "device open (unexpected)" : "false (no device)");
    auto t0 = std::chrono::steady_clock::now();
    double a = m.tick();
    sleepMs(int(secs * 400));
    double b = m.tick();
    double w1 = seconds(t0, std::chrono::steady_clock::now());
    m.setPaused(true);
    double c = m.tick();
    sleepMs(int(secs * 200));
    double d = m.tick();
    m.setPaused(false);
    auto t2 = std::chrono::steady_clock::now();
    sleepMs(int(secs * 400));
    double e = m.tick();
    double w2 = seconds(t2, std::chrono::steady_clock::now());
    m.seekTick(Music::kEndTick - 10);
    double f = m.tick();
    bool endedBefore = m.ended();
    sleepMs(300);
    double g = m.tick();
    std::printf("clock: running %.3f ticks in %.3f s (%.3f Hz); paused: %.6f -> %.6f; resumed %.3f ticks in %.3f s;\n"
                "       seek to %d -> %.3f, ended %d; 0.3 s later %.3f, ended %d\n",
                b - a, w1, (b - a) / w1, c, d, e - d, w2, Music::kEndTick - 10, f, int(endedBefore), g,
                int(m.ended()));
    bool ok = !dev && std::fabs((b - a) / w1 - Music::kTickHz) < 2 && d == c && !endedBefore && m.ended();
    m.stopPlayback();
    SDL_Quit();
    std::printf("clock: %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string image = "../re/paper_img.bin", exePath, gusOut, gusRef, ticksRef, countdownOut, pcmOut, f64Ref, wavRef;
    int bench = 3;
    double play = 0, from = 0, clock = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--image") image = next();
        else if (a == "--exe") exePath = next();
        else if (a == "--bench") bench = std::atoi(next().c_str());
        else if (a == "--gus") gusOut = next();
        else if (a == "--cmp-gus") gusRef = next();
        else if (a == "--ticks") ticksRef = next();
        else if (a == "--countdown") countdownOut = next();
        else if (a == "--pcm") pcmOut = next();
        else if (a == "--cmp-f64") f64Ref = next();
        else if (a == "--cmp-wav") wavRef = next();
        else if (a == "--play") play = std::atof(next().c_str());
        else if (a == "--from") from = std::atof(next().c_str());
        else if (a == "--clock") clock = std::atof(next().c_str());
        else {
            std::fprintf(stderr, "unknown option %s (see the comment at the top of tools/test_music.cpp)\n", a.c_str());
            return 2;
        }
    }

    PaperExe exe;
    std::string err;
    if (!(exePath.empty() ? exe.loadImage(image, &err) : exe.load(exePath, &err))) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    int fails = 0;

    Music music;
    double best = 1e9, sum = 0;
    for (int k = 0; k < std::max(1, bench); k++) {
        auto t0 = std::chrono::steady_clock::now();
        if (!music.load(exe, &err)) {
            std::fprintf(stderr, "load: %s\n", err.c_str());
            return 1;
        }
        double s = seconds(t0, std::chrono::steady_clock::now());
        best = std::min(best, s);
        sum += s;
    }
    const std::vector<float>& pcm = music.pcm();
    size_t frames = pcm.size() / 2;
    float peak = 0;
    for (float v : pcm) peak = std::max(peak, std::fabs(v));
    std::printf("load: %zu frames at %.1f Hz (%.3f s), peak %.6f; load time best %.3f s, mean %.3f s over %d\n",
                frames, music.rate(), double(frames) / music.rate(), double(peak), best, sum / std::max(1, bench),
                std::max(1, bench));

    if (!gusOut.empty() || !gusRef.empty()) {
        Music m2;
        std::string log;
        if (!MusicTest::render(m2, exe, &err, &log)) {
            std::fprintf(stderr, "render: %s\n", err.c_str());
            return 1;
        }
        if (!gusOut.empty()) {
            std::ofstream(gusOut, std::ios::binary) << log;
            std::printf("gus: wrote %s\n", gusOut.c_str());
        }
        if (!gusRef.empty()) {
            std::vector<uint8_t> ref;
            if (!readFile(gusRef, ref)) {
                std::fprintf(stderr, "cannot read %s\n", gusRef.c_str());
                return 1;
            }
            std::vector<std::string> a = splitLines(std::string(ref.begin(), ref.end())), b = splitLines(log);
            size_t n = std::min(a.size(), b.size()), d = 0;
            while (d < n && a[d] == b[d]) d++;
            bool same = d == n && a.size() == b.size() && ref.size() == log.size();
            if (same)
                std::printf("gus: %zu lines, %zu bytes, identical to %s\n", a.size(), log.size(), gusRef.c_str());
            else {
                std::printf("gus: DIFFER: %zu vs %zu lines, first difference at line %zu: '%s' vs '%s'\n", a.size(),
                            b.size(), d + 1, d < a.size() ? a[d].c_str() : "(eof)", d < b.size() ? b[d].c_str() : "(eof)");
                fails++;
            }
        }
    }

    if (!ticksRef.empty()) {
        std::ifstream f(ticksRef);
        std::string line;
        int compared = 0, bad = 0, firstBad = -1;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            long t, c;
            if (!(ss >> t >> c)) continue;
            if (t < 0 || t >= Music::kEndTick) continue;
            compared++;
            if (music.counter(int(t)) != c) {
                if (firstBad < 0) firstBad = int(t);
                bad++;
            }
        }
        std::printf("ticks: %d ticks compared with %s, %d differ", compared, ticksRef.c_str(), bad);
        if (bad) std::printf(" (first at tick %d)", firstBad);
        std::printf("\n");
        if (bad || compared != Music::kEndTick) fails++;
    }

    if (!countdownOut.empty()) {
        std::ofstream f(countdownOut);
        for (int t = 0; t < Music::kEndTick; t++) f << t << ' ' << music.counter(t) << ' ' << music.rowCountdown(t) << '\n';
        std::printf("countdown: wrote %s\n", countdownOut.c_str());
    }

    if (!pcmOut.empty()) {
        std::ofstream(pcmOut, std::ios::binary).write(reinterpret_cast<const char*>(pcm.data()),
                                                       std::streamsize(pcm.size() * sizeof(float)));
        std::printf("pcm: wrote %s (float32, stereo, %.1f Hz)\n", pcmOut.c_str(), music.rate());
    }

    if (!f64Ref.empty()) {
        std::vector<uint8_t> raw;
        if (!readFile(f64Ref, raw)) {
            std::fprintf(stderr, "cannot read %s\n", f64Ref.c_str());
            return 1;
        }
        std::vector<double> ref(raw.size() / sizeof(double));
        std::memcpy(ref.data(), raw.data(), ref.size() * sizeof(double));
        double rp = 0;
        for (double v : ref) rp = std::max(rp, std::fabs(v));
        double scale = 0.9 / rp;  // player_ref.py --wav: pcm / peak * 0.9
        size_t n = std::min(ref.size(), pcm.size());
        Diff d = diffStats(n, [&](size_t i) { return double(pcm[i]) - ref[i] * scale; });
        std::printf("f64: reference %zu frames, mix peak %.17g (gain 0.9/peak = %.14f); vs pcm(): RMS diff %.3g, "
                    "peak diff %.3g (frame %zu)%s\n",
                    ref.size() / 2, rp, scale, d.rms, d.peak, d.at / 2,
                    ref.size() == pcm.size() ? "" : "  LENGTH DIFFERS");
        if (ref.size() != pcm.size() || d.peak > 1e-6) fails++;
    }

    if (!wavRef.empty()) {
        std::vector<uint8_t> w;
        if (!readFile(wavRef, w) || w.size() < 44) {
            std::fprintf(stderr, "cannot read %s\n", wavRef.c_str());
            return 1;
        }
        // find the "data" chunk
        size_t p = 12, data = 0, len = 0;
        while (p + 8 <= w.size()) {
            uint32_t sz;
            std::memcpy(&sz, &w[p + 4], 4);
            if (std::memcmp(&w[p], "data", 4) == 0) {
                data = p + 8;
                len = std::min<size_t>(sz, w.size() - data);
                break;
            }
            p += 8 + sz + (sz & 1);
        }
        size_t n = std::min(len / 2, pcm.size());
        auto s16 = [&](size_t i) { int16_t v; std::memcpy(&v, &w[data + 2 * i], 2); return v; };
        Diff d = diffStats(n, [&](size_t i) { return double(pcm[i]) - s16(i) / 32767.0; });
        // the same 16-bit conversion as player_ref.py: (x * 32767).astype(int16) truncates towards zero
        size_t same = 0;
        for (size_t i = 0; i < n; i++)
            if (int16_t(double(pcm[i]) * 32767.0) == s16(i)) same++;
        std::printf("wav: %zu frames in %s; vs pcm(): RMS diff %.3g, peak diff %.3g (1 LSB = %.3g); after the same "
                    "int16 conversion %zu of %zu samples identical (%.4f%%)%s\n",
                    len / 4, wavRef.c_str(), d.rms, d.peak, 1 / 32767.0, same, n, 100.0 * double(same) / double(n),
                    len / 2 == pcm.size() ? "" : "  LENGTH DIFFERS");
        if (len / 2 != pcm.size() || d.peak > 1.01 / 32767.0) fails++;
    }

    if (clock > 0) fails += clockTest(music, clock);
    if (play > 0) fails += playTest(music, play, from);

    std::printf("%s\n", fails ? "FAILURES" : "OK");
    return fails ? 1 : 0;
}

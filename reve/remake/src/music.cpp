// Renders Reve's soundtrack: LCA's MOD-Player V3.03 (lcaplay.cpp) plays ZIC\PULSE.MOD on the GUS from the timer
// start to main's stop (0x2602c6, after the last script entry), and every register write is recorded at the output
// frame of its tick (writes of one tick apply at the tick's instant, the first frame at or after it); the GF1 model
// (gf1.cpp) then renders the voices from these writes. The voices' state is advanced once through the whole song
// without output to take a snapshot every block of frames, and the blocks are mixed in parallel from their snapshots,
// so the result does not depend on the number of threads. Positions and times: reve/re/notes-music.md section 5.
// See music.h.
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "exe.h"
#include "gf1.h"
#include "lcaplay.h"

namespace {

constexpr double kLevel = 0.8912509381337456;  // output peak: -1 dBFS
constexpr int64_t kBlock = 1 << 15;   // frames per parallel block

struct Write {
    int64_t frame;
    uint8_t voice, reg;
    uint16_t d;
};

// The card of the render: every write the GF1 receives, at the current output frame.
class Recorder : public gus::Card {
public:
    std::vector<Write> writes;
    int64_t frame = 0;

protected:
    void received(int voice, int reg, uint32_t d) override {
        writes.push_back({frame, uint8_t(voice), uint8_t(reg), uint16_t(d)});
    }
};

int64_t frameAt(int64_t us, double rate) {  // first frame at or after the instant
    return int64_t(std::ceil(double(us) * rate / 1e6));
}

void fillTimes(const lca::Run& r, Soundtrack& s) {
    s.tickTimes.resize(r.tickUs.size());
    for (size_t k = 0; k < r.tickUs.size(); k++) s.tickTimes[k] = double(r.tickUs[k]) / 1e6;
    s.positions = r.pos;
    s.pos0 = r.pos0;
    s.reached.resize(r.pos.size());
    int m = r.pos0;
    for (size_t k = 0; k < r.pos.size(); k++) s.reached[k] = m = std::max(m, r.pos[k]);
}

// The first refresh whose retrace comes after music time t: the retrace of refresh k is at music time
// (k - startRefresh) / kRefreshHz.
double firstRetraceAfter(double startRefresh, double t) {
    return std::floor(startRefresh + t * capture::kRefreshHz) + 1.0;
}

// main's stop (0x2602c6): kStopAfterLastEntry refreshes after the retrace at which the timer interrupt sees the last
// script entry's end position, decided before every tick from the run so far (the position after each tick is known
// when it has run).
class MainStop {
public:
    MainStop(int lastEnd, double startRefresh, double* endRefresh)
        : lastEnd_(lastEnd), startRefresh_(startRefresh), endRefresh_(endRefresh) {}
    bool operator()(lca::Run& run, int64_t nextUs) {
        if (run.pos0 >= lastEnd_) throw std::runtime_error("Reve music: the script ends before the music starts");
        if (stopUs_ < 0) {
            if (run.pos.empty() || run.pos.back() < lastEnd_) return false;
            const double t = double(run.tickUs.back()) / 1e6;  // the first tick at or past the end position
            *endRefresh_ = firstRetraceAfter(startRefresh_, t) + capture::kStopAfterLastEntry;
            stopUs_ = int64_t(std::floor((*endRefresh_ - startRefresh_) / capture::kRefreshHz * 1e6));
        }
        if (nextUs <= stopUs_) return false;
        run.stopUs = stopUs_;
        return true;
    }

private:
    int lastEnd_;
    double startRefresh_;
    double* endRefresh_;
    int64_t stopUs_ = -1;
};

struct Snapshot {
    gus::Gf1 g;
    size_t next;  // first write at or after the block start
};

// fn(item, thread) for items 0..n-1 on up to 32 threads (the calling one included)
template <class F> void parallelFor(int64_t n, int threads, F&& fn) {
    std::atomic<int64_t> next{0};
    auto worker = [&](int t) {
        for (int64_t i; (i = next.fetch_add(1)) < n;) fn(i, t);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; t++) pool.emplace_back(worker, t);
    worker(0);
    for (std::thread& t : pool) t.join();
}

int threadCount(int64_t items) {
    return int(std::clamp<int64_t>(std::thread::hardware_concurrency(), 1, std::clamp<int64_t>(items, 1, 32)));
}

struct Mix {
    std::unique_ptr<float[]> pcm;  // interleaved stereo
    int64_t frames = 0;
    double peak = 0;
};

Mix mixSong(std::span<const uint8_t> module, std::span<const uint8_t> image, double rate, Soundtrack& times,
            uint32_t voices) {
    if (!(rate > 0)) throw std::runtime_error("Reve music: bad output rate");
    // ---- the player, recording the writes at their frames ----
    Recorder card;
    card.writes.reserve(size_t(1) << 18);
    lca::Run run;
    times.startRefresh = capture::kTimerStartPit * capture::kRefreshHz / capture::kPitHz - capture::kRetraceLine;
    times.endRefresh = 0;
    lca::run(module, image, card, MainStop(lastEndPosition(image), times.startRefresh, &times.endRefresh), run,
             [&](int, int64_t us) { card.frame = frameAt(us, rate); });
    fillTimes(run, times);
    const std::vector<Write>& w = card.writes;
    // the render ends where the stop's writes take effect (they are not applied)
    const int64_t total = std::max<int64_t>(0, frameAt(run.stopUs, rate));

    // ---- snapshots: the GF1 state at every block start ----
    const int64_t nb = (total + kBlock - 1) / kBlock;
    std::vector<Snapshot> snaps;
    snaps.reserve(size_t(nb));
    gus::Gf1 g(card.dram(), rate);
    g.mixed = voices;
    size_t e = 0;
    int64_t f = 0;
    for (int64_t b = 0; b < nb; b++) {
        const int64_t b0 = b * kBlock;
        for (; e < w.size() && w[e].frame < b0; e++) {
            g.advance(w[e].frame - f);
            f = w[e].frame;
            g.write(w[e].voice, w[e].reg, w[e].d);
        }
        g.advance(b0 - f);
        f = b0;
        snaps.push_back({g, e});
    }

    // ---- the blocks, in parallel ----
    Mix m;
    m.frames = total;
    m.pcm.reset(new float[size_t(total) * 2]);  // (not zeroed: every frame is written once)
    std::vector<double> peaks(size_t(nb), 0.0);
    const int nt = threadCount(nb);
    struct Scratch {
        std::vector<double> L, R, dcL, dcR;
    };
    std::vector<Scratch> scratch(static_cast<size_t>(nt));
    parallelFor(nb, nt, [&](int64_t b, int t) {
        Scratch& sc = scratch[size_t(t)];
        if (sc.L.empty()) {
            sc.L.resize(kBlock);
            sc.R.resize(kBlock);
            sc.dcL.resize(kBlock + 1);
            sc.dcR.resize(kBlock + 1);
        }
        double *L = sc.L.data(), *R = sc.R.data(), *dcL = sc.dcL.data(), *dcR = sc.dcR.data();
        const int64_t b0 = b * kBlock, b1 = std::min(total, b0 + kBlock);
        const int n = int(b1 - b0);
        std::fill(L, L + n, 0.0);
        std::fill(R, R + n, 0.0);
        std::fill(dcL, dcL + n + 1, 0.0);
        std::fill(dcR, dcR + n + 1, 0.0);
        gus::Gf1 v = snaps[size_t(b)].g;
        size_t i = snaps[size_t(b)].next;
        int64_t at = b0;
        while (at < b1) {
            const int64_t to = i < w.size() && w[i].frame < b1 ? std::max(at, w[i].frame) : b1;
            if (to > at) {
                const int64_t k = at - b0;
                // the constant part of [at, to) goes to dc[k] and comes off at dc[k + (to - at)]
                v.mix(int(to - at), L + k, R + k, dcL + k, dcR + k);
                at = to;
            }
            for (; i < w.size() && w[i].frame <= at && at < b1; i++) v.write(w[i].voice, w[i].reg, w[i].d);
        }
        double accL = 0, accR = 0, pk = 0;
        float* o = m.pcm.get() + size_t(b0) * 2;
        for (int k = 0; k < n; k++) {
            accL += dcL[k];
            accR += dcR[k];
            const double l = L[k] + accL, r = R[k] + accR;
            pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
            o[2 * k] = float(l);
            o[2 * k + 1] = float(r);
        }
        peaks[size_t(b)] = pk;
    });
    m.peak = peaks.empty() ? 0.0 : *std::max_element(peaks.begin(), peaks.end());
    return m;
}

}  // namespace

std::vector<float> renderMix(std::span<const uint8_t> module, std::span<const uint8_t> image, double rate,
                             Soundtrack& times, double* peak, uint32_t voices) {
    Mix m = mixSong(module, image, rate, times, voices);
    if (peak) *peak = m.peak;
    return std::vector<float>(m.pcm.get(), m.pcm.get() + m.frames * 2);
}

// The card's line output is AC-coupled: a one-shot sample that ends on a non-zero byte leaves its voice at a
// constant level, which the output capacitors of a real GUS block. A one-pole high-pass at 5 Hz models that.
static double blockDC(float* pcm, int64_t frames, double rate) {
    const float a = float(std::exp(-2.0 * 3.14159265358979323846 * 5.0 / rate));
    double peak = 0;
    for (int ch = 0; ch < 2; ch++) {
        float x1 = 0, y1 = 0;
        for (int64_t i = 0; i < frames; i++) {
            float& v = pcm[i * 2 + ch];
            const float y = v - x1 + a * y1;
            x1 = v;
            y1 = y;
            v = y;
            peak = std::max(peak, double(std::fabs(y)));
        }
    }
    return peak;
}

int lastEndPosition(std::span<const uint8_t> image) {
    // the script table 0x205ed0: entries of 0x14 bytes {init, done, retrace routine, frame routine, end position},
    // ended by a zero dword (reve/re/NOTES.md "The script table")
    constexpr uint32_t kScript = 0x205ed0, kEntry = 0x14;
    auto u32 = [&](uint32_t a) {
        const size_t o = size_t(a - lca::kImageBase);
        if (a < lca::kImageBase || o + 4 > image.size()) throw std::runtime_error("REVE.LFL: no script table");
        return uint32_t(image[o] | image[o + 1] << 8 | image[o + 2] << 16 | uint32_t(image[o + 3]) << 24);
    };
    int end = -1;
    for (uint32_t a = kScript; u32(a) != 0; a += kEntry) {
        end = int(u32(a + 0x10));
        if (a > kScript + 64 * kEntry) throw std::runtime_error("REVE.LFL: script table without an end");
    }
    if (end < 0) throw std::runtime_error("REVE.LFL: empty script table");
    return end;
}

Soundtrack renderSoundtrack(const ReveExe& exe, int rate) {
    return renderSoundtrack(exe.file("ZIC\\PULSE.MOD"), exe.image(), rate);
}

Soundtrack renderSoundtrack(std::span<const uint8_t> module, std::span<const uint8_t> image, int rate) {
    Soundtrack s;
    s.rate = rate;
    Mix m = mixSong(module, image, double(rate), s, 0xffffffffu);
    m.peak = blockDC(m.pcm.get(), m.frames, double(rate));
    const float gain = m.peak > 0 ? float(kLevel / m.peak) : 1.0f;
    const size_t n = size_t(m.frames) * 2;
    s.pcm.resize(n);
    const int64_t nb = int64_t((n + kBlock - 1) / kBlock);
    parallelFor(nb, threadCount(nb), [&](int64_t b, int) {
        const size_t a = size_t(b) * kBlock, e = std::min(n, a + kBlock);
        for (size_t i = a; i < e; i++)
            s.pcm[i] = int16_t(std::lrintf(std::clamp(m.pcm[i] * gain, -1.0f, 1.0f) * 32767.0f));
    });
    return s;
}

double Soundtrack::timeOfTick(int tick) const {
    if (tick < 0 || tickTimes.empty()) return 0.0;
    const int n = int(tickTimes.size());
    if (tick < n) return tickTimes[size_t(tick)];
    const double per = n > 1 ? tickTimes[size_t(n - 1)] - tickTimes[size_t(n - 2)] : 0.0;
    return tickTimes.back() + per * double(tick - (n - 1));  // (the player would go on at the last period)
}

double Soundtrack::refreshOf(double t) const { return startRefresh + t * capture::kRefreshHz; }

double Soundtrack::timeOf(double refresh) const { return (refresh - startRefresh) / capture::kRefreshHz; }

int Soundtrack::positionAt(double refresh) const {
    // ticks whose timer overflow comes before the retrace (run_reve.py records a retrace before an overflow at the
    // same instant)
    const double t = timeOf(refresh);
    const auto it = std::lower_bound(tickTimes.begin(), tickTimes.end(), t);
    const size_t k = size_t(it - tickTimes.begin());
    return k == 0 ? pos0 : positions[k - 1];
}

double Soundtrack::refreshOfPosition(int pos) const {
    if (pos0 >= pos) return -std::numeric_limits<double>::infinity();
    const auto it = std::lower_bound(reached.begin(), reached.end(), pos);
    if (it == reached.end()) return std::numeric_limits<double>::infinity();
    return firstRetraceAfter(startRefresh, tickTimes[size_t(it - reached.begin())]);
}

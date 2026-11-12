// Renders DOPE's soundtrack: the loader's ProTracker player (modplay.cpp) plays DOPE.MOD on the GUS card model, its
// interrupts at the card's events (timer overflows, ramp ends) except where the capture's part starts held them, and
// every register write is recorded at the output frame of its time (the first frame at or after it); the GF1 model
// (gf1.cpp) then renders the voices from these writes. The voices' state is advanced once through the whole song
// without output to take a snapshot every block of frames, and the blocks are mixed in parallel from their snapshots,
// so the result does not depend on the number of threads. See music.h.
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "gf1.h"
#include "modplay.h"

namespace {

constexpr float kLevel = 0.9f;        // output peak
constexpr int64_t kBlock = 1 << 15;   // frames per parallel block
constexpr int kExtraTicks = 500;      // timer interrupts after the capture's stop (music.h)

// Capture time (s): the module load holds the first timer interrupt until main's `sti` (log tick 1 at 0.110398);
// music time 0 is the overflow after it (log tick 2, 345 x 320 us).
constexpr double kLoadEnd = 0.110398;
constexpr double kMusicZero = 345 * 320e-6;

// The capture's timer interrupts delayed at the part starts (capture time, s; notes-music.md 7.3, measured with
// dopeplay.py on full.gus): the overflow and when it was served.
constexpr struct {
    double overflow, served;
} kCaptureDelays[] = {
    {0.192320, 0.212849},      // p1: 20.5 ms, none lost
    {38.675520, 38.708245},    // p2: 32.7 ms, 1 lost
    {143.081280, 143.136638},  // p3: 55.4 ms, 2 lost
    {194.752320, 194.772976},  // p4: 20.7 ms, 1 lost
    {300.900160, 300.927812},  // p5: 27.7 ms, 1 lost
    {365.186880, 365.219975},  // p6: 33.1 ms, 1 lost
    {422.367040, 422.392341},  // p7: 25.3 ms, 1 lost
};

struct Write {
    int64_t frame;
    uint8_t voice, reg;
    uint16_t d;
};

// The card of the render: every write the GF1 receives, with the card's time (frames are set after the run).
class Recorder : public gus::Card {
public:
    std::vector<Write> writes;
    std::vector<double> at;

protected:
    void received(int voice, int reg, uint32_t d) override {
        writes.push_back({0, uint8_t(voice), uint8_t(reg), uint16_t(d)});
        at.push_back(now());
    }
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

double micro(double t) { return std::round(t * 1e6) / 1e6; }

Mix mixSong(std::span<const uint8_t> mod, std::span<const uint8_t> loader, const MixSpec& spec, Soundtrack& times) {
    const double rate = spec.rate;
    if (!(rate > 0)) throw std::runtime_error("DOPE music: bad output rate");
    modplay::Options opt;
    opt.ticks = spec.ticks > 0 ? spec.ticks : modplay::kCaptureTicks + kExtraTicks;
    if (spec.loadHold) opt.holds.push_back({0.0, kLoadEnd});
    for (const IrqHold& h : spec.holds) opt.holds.push_back({h.from + kMusicZero, h.to + kMusicZero});

    // ---- the player, recording the writes ----
    Recorder card;
    card.writes.reserve(size_t(1) << 19);
    card.at.reserve(size_t(1) << 19);
    modplay::Run run;
    modplay::run(mod, loader, card, opt, run);
    if (run.timerServed.size() < size_t(modplay::kStartAfter + 1))
        throw std::runtime_error("DOPE music: the player did not start");
    const double zero = run.timerEvent[modplay::kStartAfter - 1];  // music time 0
    const double first = run.timerServed[modplay::kStartAfter];    // player tick 0
    const double origin = spec.fromTickZero ? first : zero;
    // each write at the first output frame at or after its time (the writes before the origin: frame 0)
    int64_t last = 0;
    for (size_t i = 0; i < card.writes.size(); i++) {
        const int64_t f = std::max(last, int64_t(std::ceil((card.at[i] - origin) * rate)));
        card.writes[i].frame = last = f;
    }

    // ---- times and positions ----
    const size_t k0 = modplay::kStartAfter;
    times.tickTimes.clear();
    times.positions.clear();
    for (size_t k = k0; k < run.timerServed.size(); k++) {
        times.tickTimes.push_back(micro(run.timerServed[k] - origin));
        const modplay::Position& p = run.pos[k];
        times.positions.push_back(p.order << 8 | p.row);
    }
    times.pos0 = run.pos[k0 - 1].order << 8 | run.pos[k0 - 1].row;
    times.reached.resize(times.positions.size());
    int m = -1;  // (the start's 00:3f comes before row 00:00)
    for (size_t k = 0; k < times.positions.size(); k++) times.reached[k] = m = std::max(m, times.positions[k]);
    const size_t endIrq = modplay::kCaptureTicks;  // the overflow after the capture's last timer interrupt
    times.captureEnd = micro((endIrq < run.timerEvent.size() ? run.timerEvent[endIrq] : run.stopTime) - origin);

    const std::vector<Write>& w = card.writes;
    // the render ends where the stop's writes take effect (they are not applied)
    const int64_t total = std::max<int64_t>(0, int64_t(std::ceil((run.stopTime - origin) * rate)));

    // ---- snapshots: the GF1 state at every block start ----
    const int64_t nb = (total + kBlock - 1) / kBlock;
    std::vector<Snapshot> snaps;
    snaps.reserve(size_t(nb));
    gus::Gf1 g(card.dram(), rate);
    g.mixed = spec.voices;
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
    Mix mx;
    mx.frames = total;
    mx.pcm.reset(new float[size_t(total) * 2]);  // (not zeroed: every frame is written once)
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
        float* o = mx.pcm.get() + size_t(b0) * 2;
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
    mx.peak = peaks.empty() ? 0.0 : *std::max_element(peaks.begin(), peaks.end());
    return mx;
}

// The card's line output is AC-coupled: a voice parked on a non-zero byte leaves a constant level, which the output
// capacitors of a real GUS block. A one-pole high-pass at 5 Hz models that.
double blockDC(float* pcm, int64_t frames, double rate) {
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

}  // namespace

std::vector<IrqHold> captureHolds() {
    std::vector<IrqHold> h;
    for (const auto& d : kCaptureDelays) h.push_back({d.overflow - 0.001 - kMusicZero, d.served - kMusicZero});
    return h;
}

std::vector<float> renderMix(std::span<const uint8_t> mod, std::span<const uint8_t> loader, const MixSpec& spec,
                             Soundtrack& times, double* peak) {
    Mix m = mixSong(mod, loader, spec, times);
    if (peak) *peak = m.peak;
    return std::vector<float>(m.pcm.get(), m.pcm.get() + m.frames * 2);
}

Soundtrack renderSoundtrack(std::span<const uint8_t> mod, std::span<const uint8_t> loader, int rate,
                            std::span<const IrqHold> holds) {
    Soundtrack s;
    s.rate = rate;
    MixSpec spec;
    spec.rate = double(rate);
    spec.holds.assign(holds.begin(), holds.end());
    Mix m = mixSong(mod, loader, spec, s);
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

Soundtrack renderSoundtrack(std::span<const uint8_t> mod, std::span<const uint8_t> loader, int rate) {
    const std::vector<IrqHold> h = captureHolds();
    return renderSoundtrack(mod, loader, rate, h);
}

double Soundtrack::timeOfTick(int tick) const {
    if (tick < 0 || tickTimes.empty()) return 0.0;
    const int n = int(tickTimes.size());
    if (tick < n) return tickTimes[size_t(tick)];
    const double per = n > 1 ? tickTimes[size_t(n - 1)] - tickTimes[size_t(n - 2)] : 0.0;
    return tickTimes.back() + per * double(tick - (n - 1));  // (the player would go on at the last period)
}

int Soundtrack::positionAt(double t) const {
    const auto it = std::upper_bound(tickTimes.begin(), tickTimes.end(), t);  // ticks at or before t
    const size_t k = size_t(it - tickTimes.begin());
    return k == 0 ? pos0 : positions[k - 1];
}

double Soundtrack::timeOfPosition(int pos) const {
    if (pos < 0) return 0.0;
    const auto it = std::lower_bound(reached.begin(), reached.end(), pos);
    if (it == reached.end()) return std::numeric_limits<double>::infinity();
    return tickTimes[size_t(it - reached.begin())];
}

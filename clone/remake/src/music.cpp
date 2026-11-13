// Renders clone meets clone's soundtrack: the module of modsynth.cpp is played by MXMPlay (mxmplay.cpp) on the card's
// model (gus::Hardware, at the card's own rate, so the player's volume reads and ramp waits are a real card's), and
// every register write is recorded with its instant (the IRQ's PIT clock plus the card frames of the ramp wait
// before it). The GF1 synthesiser (gus::Synth) then renders the voices from these writes at the output rate: the
// voices' state is advanced once through the whole song without output to take a snapshot every block of frames,
// and the blocks are mixed in parallel from their snapshots, so the result does not depend on the number of
// threads. See music.h.
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "gf1.h"
#include "modsynth.h"
#include "mxmplay.h"

namespace {

constexpr int64_t kPit = 1193182;           // PIT clocks per second: the unit of xmpGetTimer
constexpr uint32_t kSyncTable = 0x420499;   // main's sync table: {dword order x 64 + row, dword event}, event -1 ends
constexpr int kMaxSync = 4096;
constexpr int kLogPlay = 10;                // capture log tick of xmpPlay (IRQ k is logged at 10 + k)
constexpr int kMasterVolume = 0x20;         // main 0x400b0e: xmpSetVolume(0x20) right after xmpPlay
constexpr int kMaxIrqs = 1 << 20;
constexpr double kLevel = 0.89125093813374556;  // output peak: -1 dBFS
constexpr int64_t kBlock = 1 << 15;         // frames per parallel block
constexpr int64_t kUnset = std::numeric_limits<int64_t>::min();

// The position (order x 64 + row) of the sync table's event -1: the intro ends when the music reaches it.
int exitPosition(std::span<const uint8_t> image) {
    auto u32 = [&](uint32_t a) {
        if (a < modsynth::kImageBase || a - modsynth::kImageBase + 4 > image.size())
            throw std::runtime_error("CLONE.EXE image: sync table out of range");
        const uint8_t* p = image.data() + (a - modsynth::kImageBase);
        return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
    };
    for (uint32_t i = 0; i < uint32_t(kMaxSync); i++)
        if (u32(kSyncTable + 8 * i + 4) == 0xffffffffu) return int(u32(kSyncTable + 8 * i));
    throw std::runtime_error("CLONE.EXE image: no end in the sync table");
}

struct Song {
    std::vector<int64_t> irqClock;  // xmpGetTimer at IRQ k (PIT clocks; 0 = music time 0)
    std::vector<int> pos;           // xmpGetPos after IRQ k
    std::vector<int32_t> timerEnd;  // xmpGetTimer at the end of IRQ k's period
    int64_t endClock = 0;           // the IRQ after the last one
    int64_t cardIrq2 = 0, cardEnd = 0;  // with the card's model: its frame count at IRQ 2 and at the end
};

// xmpInit, xmpPlay + xmpSetVolume(0x20), the IRQs up to and including the one at which the player leaves the exit
// row (with `hw`: the card's model runs the frames between the IRQs, as mxmplay.py does), xmpStop if `stop`.
void runSong(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card, gus::Hardware* hw,
             bool stop, Song& s) {
    const int exitPos = exitPosition(image);
    mxm::Player pl(module, image, card);
    pl.init();
    card.tick = kLogPlay;
    if (hw) {
        hw->clock = kUnset;
        hw->waited = 0;
    }
    pl.play(0);
    pl.setVolume(kMasterVolume);
    int64_t clock = int32_t(pl.timer());  // xmpPlay: -(0x10000 + period)
    if (hw && hw->writes)
        for (gus::Write& w : *hw->writes)
            if (w.clock == kUnset) w.clock = clock;
    double facc = 0;
    bool seen = false;
    for (int k = 1;; k++) {
        if (k > kMaxIrqs) throw std::runtime_error("music: the song never reaches the end of the sync table");
        clock += pl.cur();
        card.tick = kLogPlay + k;
        if (hw) {
            hw->clock = clock;
            hw->waited = 0;
            if (k == 2) s.cardIrq2 = hw->frames;
        }
        pl.irq();
        s.irqClock.push_back(clock);
        s.pos.push_back(pl.pos());
        s.timerEnd.push_back(int32_t(pl.timer() + pl.cur()));
        if (hw) {
            // card frames until the next IRQ, less those the ramp wait of this IRQ already ran
            facc += hw->frameRate() * double(pl.cur()) / double(kPit) - double(hw->waited);
            hw->waited = 0;
            const int n = int(facc);
            facc -= n;
            hw->run(n);
        }
        const int p = (pl.pos() >> 8) * 64 + (pl.pos() & 0xff);
        if (p == exitPos) seen = true;
        else if (seen) break;
    }
    s.endClock = clock + pl.cur();
    if (hw) s.cardEnd = hw->frames;
    if (stop) pl.stop();
}

void fillTimes(const Song& s, Soundtrack& t) {
    t.irqTimes.resize(s.irqClock.size());
    for (size_t k = 0; k < s.irqClock.size(); k++) t.irqTimes[k] = double(s.irqClock[k]) / double(kPit);
    t.positions = s.pos;
    t.reached.resize(s.pos.size());
    int m = 0;  // xmpGetPos before IRQ 1: order 0, row 0
    for (size_t k = 0; k < s.pos.size(); k++) t.reached[k] = m = std::max(m, s.pos[k]);
}

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// The first output frame at or after the instant clock / kPit + waited x voices / 617,400 s (exact integers).
int64_t frameAt(int64_t clock, int64_t waited, int64_t voices, int64_t rate) {
    const int64_t a = clock * rate;
    const int64_t q = floorDiv(a, kPit), r = a - q * kPit;
    const int64_t num = r * gus::kGf1ClockInt + waited * voices * rate * kPit;
    const int64_t den = kPit * gus::kGf1ClockInt;
    return q + (num + den - 1) / den;
}

struct Event {
    int64_t frame;
    uint8_t voice, reg;
    uint16_t d;
};

struct Snapshot {
    gus::Synth g;
    size_t next;  // first event at or after the block start
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

Mix mixSong(std::span<const uint8_t> image, int rate, Soundtrack& times, uint32_t voices, bool cardGrid) {
    if (rate < 1000 || rate > 768000) throw std::runtime_error("music: bad output rate");
    // ---- the player on the card's model, recording the writes ----
    const std::vector<uint8_t> module = modsynth::buildModule(image);
    gus::Hardware hw;
    std::vector<gus::Write> writes;
    writes.reserve(size_t(1) << 20);
    hw.writes = &writes;
    Song song;
    runSong(module, image, hw, &hw, false, song);
    fillTimes(song, times);
    std::vector<Event> ev(writes.size());
    for (size_t i = 0; i < writes.size(); i++) {
        const gus::Write& w = writes[i];
        ev[i] = {cardGrid ? w.cardFrame - song.cardIrq2 : frameAt(w.clock, w.waited, w.voices, rate), w.voice, w.reg, w.d};
    }
    // the render ends at the IRQ after the last one (the original's xmpStop comes before it; it is not applied)
    const int64_t total =
        std::max<int64_t>(0, cardGrid ? song.cardEnd - song.cardIrq2 : frameAt(song.endClock, 0, 18, rate));

    // ---- snapshots: the GF1 state at every block start ----
    const int64_t nb = (total + kBlock - 1) / kBlock;
    std::vector<Snapshot> snaps;
    snaps.reserve(size_t(nb));
    gus::Synth g(hw.dram(), double(rate));
    g.mixed = voices;
    size_t e = 0;
    int64_t f = ev.empty() ? 0 : std::min<int64_t>(0, ev[0].frame);
    for (int64_t b = 0; b < nb; b++) {
        const int64_t b0 = b * kBlock;
        for (; e < ev.size() && ev[e].frame < b0; e++) {
            g.advance(ev[e].frame - f);
            f = ev[e].frame;
            g.write(ev[e].voice, ev[e].reg, ev[e].d);
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
        gus::Synth v = snaps[size_t(b)].g;
        size_t i = snaps[size_t(b)].next;
        int64_t at = b0;
        while (at < b1) {
            const int64_t to = i < ev.size() && ev[i].frame < b1 ? std::max(at, ev[i].frame) : b1;
            if (to > at) {
                const int64_t k = at - b0;
                // the constant part of [at, to) goes to dc[k] and comes off at dc[k + (to - at)]
                v.mix(int(to - at), L + k, R + k, dcL + k, dcR + k);
                at = to;
            }
            for (; i < ev.size() && ev[i].frame <= at && at < b1; i++) v.write(ev[i].voice, ev[i].reg, ev[i].d);
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

// The card's line output is AC-coupled: a one-shot sample that ends on a non-zero value leaves its voice at a
// constant level (the synthesised drums stop mid-wave), which the output capacitors of a real GUS block. A one-pole
// high-pass at 5 Hz models that (as STARS). Returns the peak after the filter. The channels run in parallel.
double blockDC(float* pcm, int64_t frames, double rate) {
    const double a = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / rate);
    double peak[2] = {0, 0};
    parallelFor(2, 2, [&](int64_t ch, int) {
        double x1 = 0, y1 = 0, pk = 0;
        for (int64_t i = 0; i < frames; i++) {
            float& v = pcm[i * 2 + ch];
            const double x = v;
            const double y = x - x1 + a * y1;
            x1 = x;
            y1 = y;
            v = float(y);
            pk = std::max(pk, std::fabs(double(v)));
        }
        peak[ch] = pk;
    });
    return std::max(peak[0], peak[1]);
}

}  // namespace

std::vector<float> renderMix(std::span<const uint8_t> image, int rate, Soundtrack& times, double* peak,
                             uint32_t voices, bool cardGrid) {
    times.rate = rate;
    Mix m = mixSong(image, rate, times, voices, cardGrid);
    if (peak) *peak = m.peak;
    return std::vector<float>(m.pcm.get(), m.pcm.get() + m.frames * 2);
}

Soundtrack renderSoundtrack(std::span<const uint8_t> image, int rate) {
    Soundtrack s;
    s.rate = rate;
    Mix m = mixSong(image, rate, s, 0xffffffffu, false);
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

MusicTrace traceMusic(std::span<const uint8_t> image, bool fakeGus) {
    MusicTrace tr;
    tr.module = modsynth::buildModule(image);
    gus::Card fake;
    gus::Hardware model;
    gus::Card& card = fakeGus ? fake : static_cast<gus::Card&>(model);
    tr.gus.reserve(size_t(24) << 20);
    card.log = &tr.gus;
    Song s;
    runSong(tr.module, image, card, fakeGus ? nullptr : &model, true, s);
    tr.dram = card.dram();
    tr.pos = std::move(s.pos);
    tr.timer = std::move(s.timerEnd);
    return tr;
}

double Soundtrack::timeOfTick(int tick) const {
    const int n = int(irqTimes.size());
    if (n == 0) return 0.0;
    const double first = n > 1 ? irqTimes[1] - irqTimes[0] : 0.0, last = n > 1 ? irqTimes[size_t(n - 1)] - irqTimes[size_t(n - 2)] : 0.0;
    if (tick < 0) return irqTimes[0] + first * double(tick);
    if (tick < n) return irqTimes[size_t(tick)];
    return irqTimes.back() + last * double(tick - (n - 1));  // (the player would go on at the last period)
}

int Soundtrack::positionAt(double t) const {
    const auto it = std::upper_bound(irqTimes.begin(), irqTimes.end(), t);  // IRQs at or before t
    const size_t k = size_t(it - irqTimes.begin());
    return k == 0 ? 0 : positions[k - 1];
}

double Soundtrack::timeOfPosition(int pos) const {
    const auto it = std::lower_bound(reached.begin(), reached.end(), pos);
    if (it == reached.end()) return std::numeric_limits<double>::infinity();
    return irqTimes[size_t(it - reached.begin())];
}

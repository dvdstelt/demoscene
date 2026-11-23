// Inside's soundtrack (music.h): EYE.AMF loaded by DSMI's loader and played by AMP 2.00 (amp.cpp) through DSMI's GUS
// driver (gusdrv.cpp) on the card's model (gus::Hardware, at the card's own rate, so the driver's volume reads and
// ramp waits are a real card's), as INSIDE.EXE runs it: 0x4010b4 / 0x400efc (driver init, ampInit, setupChannels(24),
// the module, ampPlayModule(module, 1)) in the main line, then the music part 0x400fcc of the timer callback 0x40070c
// in every timer interrupt (DSMI time += 35 (0x401010, 0x40f620), AMP's poll), and the exit 0x401054
// (ampStopModule, SDIStopOutput) after the callback in which the script (table 0x4222e4, run 0x400b3c) reaches its
// end. Every register write is recorded with its instant (the interrupt's time plus the card frames of the ramp
// waits before it). The GF1 synthesiser (gus::Synth) then renders the voices from these writes at the output rate:
// the voices' state is advanced once through the whole song without output to take a snapshot every block of
// frames, and the blocks are mixed in parallel from their snapshots, so the result does not depend on the number of
// threads (clone meets clone's music.cpp). See music.h; inside/re/notes-music.md sections 4, 8, 9 and 11.
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "amp.h"
#include "demo.h"
#include "exe.h"
#include "gf1.h"
#include "gusdrv.h"

namespace {

constexpr uint32_t kScript = 0x4222e4;    // the script table: {s32 order, s32 row, u32 routine}, routine 0 ends it
constexpr int kScriptMax = 1024;
constexpr int kFirstIrq = 91;             // the timer interrupt of callback 1 (the timer was installed at record 15)
constexpr uint32_t kDsmiStep = 35;        // DSMI time per callback (0x401010)
constexpr int kVoices = 24;               // 0x4010b4: setupChannels(24)
constexpr int kStartRecord = 37;          // pcm frame 0 = the display time of this record
constexpr int kTimerPeriod = 4085;        // PIT clocks between the timer's interrupts within a refresh
constexpr int kRetraceLines = 37;         // the retrace interrupt comes 37 of the 449 lines before line 0
constexpr int kMaxCallbacks = 1 << 20;
constexpr double kLevel = 0.89125093813374556;  // output peak: -1 dBFS
constexpr int64_t kBlock = 1 << 15;       // frames per parallel block
constexpr int64_t kStartup = -1;          // Write::clock of the start-up writes (before pcm frame 0)

// PIT clock of timer interrupt i relative to record 0 of the capture (program time 1 / 70.0863 s): interrupt
// i = 4 m + j (j = 1..4); j = 4 is the retrace interrupt, 37 lines before record 16 + m, the other three follow the
// one before them 4,085 PIT clocks apart (notes-music 9, notes-core 1.2). Evaluated as models/music/render.py does.
double irqClock(int i) {
    const double frame = kPitHz / kRefreshHz, line = frame / 449.0;
    const int m = (i - 1) / 4, j = (i - 1) % 4 + 1;
    if (j == 4) return double(16 + m) * frame - double(kRetraceLines) * line;
    return double(15 + m) * frame - double(kRetraceLines) * line + double(kTimerPeriod * j);
}

struct ScriptEntry {
    int pos;           // order * 64 + row
    uint32_t routine;  // 0: the end
};

std::vector<ScriptEntry> readScript(const InsideExe& exe) {
    std::vector<ScriptEntry> s;
    for (uint32_t a = kScript; s.size() < size_t(kScriptMax); a += 12) {
        s.push_back({exe.s32(a) * 64 + exe.s32(a + 4), exe.u32(a + 8)});
        if (!s.back().routine) return s;
    }
    throw std::runtime_error("INSIDE.EXE: the script table has no end");
}

struct Song {
    std::vector<int> rowStart;     // the callback in which song row R starts
    std::vector<int> pos;          // order * 64 + row after interrupt kFirstIrq + k
    int exitIrq = 0, exitCallback = 0;
    int64_t cardStart = 0, cardEnd = 0;  // with the card's model: its frames after the start-up and at the end
    std::vector<uint32_t> uploads;
    size_t startupLog = 0;
    std::vector<int> waited;       // with the card's model: the card frames the ramp waits ran in each interrupt
};

// The music as INSIDE.EXE runs it on `card` (with `hw`: the card's model runs the frames between the interrupts, as
// models/music/render.py does). lost: interrupts whose callback did not run.
void runSong(const InsideExe& exe, const amp::Module& mod, std::span<const uint8_t> dsd, gus::Card& card,
             gus::Hardware* hw, std::span<const int> lost, Song& s) {
    const std::vector<ScriptEntry> script = readScript(exe);
    gusdrv::Driver drv(card, dsd);
    amp::Player player(mod, exe, drv);
    std::vector<int> rows;
    player.rowLog = &rows;

    // 0x4010b4: SDIInitDevice, ampInit, setupChannels(24), ampPlayModule(module, 1)
    card.tick = kFirstIrq - 1;
    if (hw) {
        hw->clock = kStartup;
        hw->waited = 0;
    }
    drv.initDevice();
    player.init();
    drv.setupChannels(kVoices);
    if (player.play(0, 1)) throw std::runtime_error("music: AMP cannot play EYE.AMF");
    for (const gusdrv::Driver::Upload& u : drv.uploads()) {
        s.uploads.push_back(u.addr);
        s.uploads.push_back(u.size);
    }
    if (card.log) s.startupLog = size_t(std::count(card.log->begin(), card.log->end(), '\n'));

    // the script (0x400b3c) after each callback, as on an infinitely fast machine: every entry whose position the
    // player has reached runs; the demo quits when the next entry is the end
    size_t idx = 0;
    auto scriptEnds = [&] {
        const int p = player.order() * 64 + player.row();
        while (idx < script.size() && script[idx].routine && p >= script[idx].pos) idx++;
        return idx >= script.size() || !script[idx].routine;
    };
    if (scriptEnds()) throw std::runtime_error("INSIDE.EXE: the script ends before the music");

    double rate = 0, acc = 0;
    if (hw) {
        s.cardStart = hw->frames;
        rate = hw->frameRate();
        acc = irqClock(kFirstIrq) * rate / kPitHz;
        const int n = int(acc);
        acc -= n;
        hw->run(n);
    }
    uint32_t time = 0;
    int m = 0;
    for (int i = kFirstIrq;; i++) {
        card.tick = i;
        if (hw) {
            hw->clock = i;
            hw->waited = 0;
        }
        bool quit = false;
        if (!std::count(lost.begin(), lost.end(), i)) {
            if (++m > kMaxCallbacks) throw std::runtime_error("music: the song never reaches the script's end");
            time += kDsmiStep;  // 0x400fcc: tsTick, ampPoll
            player.poll(time);
            for (int r : rows)
                while (r >= 0 && size_t(r) >= s.rowStart.size()) s.rowStart.push_back(m);
            rows.clear();
            quit = scriptEnds();
        }
        s.pos.push_back(player.order() * 64 + player.row());
        if (quit) {  // 0x401054: ampStopModule, SDIStopOutput
            player.stop();
            drv.stopOutput();
            s.exitIrq = i;
            s.exitCallback = m;
        }
        if (hw) {  // card frames until the next interrupt, less those the ramp waits of this one already ran
            s.waited.push_back(hw->waited);
            acc += (irqClock(i + 1) - irqClock(i)) * rate / kPitHz - double(hw->waited);
            const int n = int(acc);
            acc -= n;
            hw->run(n);
        }
        if (quit) break;
    }
    if (hw) s.cardEnd = hw->frames;
}

void fillTimes(const Song& s, Soundtrack& t) {
    t.startRefresh = kStartRecord;
    t.rowStart = s.rowStart;
    t.endRefresh = std::floor((s.exitCallback + 150.0) / 4.0) + 1.0;  // the record after the exit's callback
}

// The first output frame at or after the instant of a write: its interrupt plus `waited` card frames
int64_t frameAt(const gus::Write& w, int rate) {
    if (w.clock == kStartup) return -1;
    const double frame = kPitHz / kRefreshHz;
    const double t = (irqClock(int(w.clock)) - kStartRecord * frame) / kPitHz +
                     double(w.waited) * double(w.voices) / gus::kGf1Clock;
    return int64_t(std::ceil(t * double(rate)));
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

Mix mixSong(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, int rate,
            Soundtrack& times, uint32_t voices, bool cardGrid) {
    if (rate < 1000 || rate > 768000) throw std::runtime_error("music: bad output rate");
    // ---- the player on the card's model, recording the writes ----
    const amp::Module mod = amp::loadAMF(amf);
    gus::Hardware hw;
    std::vector<gus::Write> writes;
    writes.reserve(size_t(1) << 18);
    hw.writes = &writes;
    Song song;
    runSong(exe, mod, dsd, hw, &hw, {}, song);
    times.rate = rate;
    fillTimes(song, times);
    std::vector<Event> ev(writes.size());
    for (size_t i = 0; i < writes.size(); i++) {
        const gus::Write& w = writes[i];
        ev[i] = {cardGrid ? w.cardFrame - song.cardStart : frameAt(w, rate), w.voice, w.reg, w.d};
    }
    const int64_t total = cardGrid ? song.cardEnd - song.cardStart
                                   : int64_t((times.endRefresh - kStartRecord) / kRefreshHz * double(rate));

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

// The card's line output is AC-coupled: a one-shot sample that ends on a non-zero value (or a voice parked on one)
// leaves a constant level, which the output capacitors of a real GUS block. A one-pole high-pass at 5 Hz models
// that (as STARS and clone meets clone). Returns the peak after the filter. The channels run in parallel.
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

int Soundtrack::positionAtCallback(double m) const {
    if (m < 0) return -1;
    auto it = std::upper_bound(rowStart.begin(), rowStart.end(), int(std::floor(m)));
    return std::max(0, int(it - rowStart.begin()) - 1);   // 0:00 from the music start (ampPlayModule) on
}

int Soundtrack::positionAt(double refresh) const { return positionAtCallback(4.0 * std::floor(refresh) - 150.0); }

double Soundtrack::rowCallback(int row) const {
    return row >= 0 && size_t(row) < rowStart.size() ? double(rowStart[size_t(row)])
                                                     : std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return startRefresh + t * kRefreshHz; }
double Soundtrack::timeOf(double refresh) const { return (refresh - startRefresh) / kRefreshHz; }

std::vector<float> renderMix(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, int rate,
                             Soundtrack& times, double* peak, uint32_t voices, bool cardGrid) {
    Mix m = mixSong(exe, amf, dsd, rate, times, voices, cardGrid);
    if (peak) *peak = m.peak;
    return std::vector<float>(m.pcm.get(), m.pcm.get() + m.frames * 2);
}

Soundtrack renderSoundtrack(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd,
                            int rate) {
    Soundtrack s;
    Mix m = mixSong(exe, amf, dsd, rate, s, 0xffffffffu, false);
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

MusicTrace traceMusic(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, bool fakeGus,
                      std::span<const int> lostIrqs) {
    MusicTrace tr;
    const amp::Module mod = amp::loadAMF(amf);
    gus::Card fake;
    gus::Hardware model;
    gus::Card& card = fakeGus ? fake : static_cast<gus::Card&>(model);
    tr.gus.reserve(size_t(4) << 20);
    card.log = &tr.gus;
    Song s;
    runSong(exe, mod, dsd, card, fakeGus ? nullptr : &model, lostIrqs, s);
    tr.firstIrq = kFirstIrq;
    tr.startupLines = s.startupLog;
    tr.dram = card.dram();
    tr.uploads = std::move(s.uploads);
    tr.pos = std::move(s.pos);
    tr.rowStart = std::move(s.rowStart);
    tr.exitIrq = s.exitIrq;
    tr.exitCallback = s.exitCallback;
    tr.waited = std::move(s.waited);
    return tr;
}

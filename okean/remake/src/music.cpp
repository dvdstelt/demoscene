// The Sea Robot of Love's soundtrack (music.h): the intro's ProTracker player and GUS driver (modplay.cpp, code32
// 0x10414..0x14bd3) play the module at code32 0xae0 on a model of the card (gf1.cpp) at the tick times of the
// intro's schedule (schedule.cpp), and the GF1 model renders the voices from the recorded register writes. The voices'
// state is advanced once through the whole song without output to take a snapshot every block of frames, and the
// blocks are mixed in parallel from their snapshots (as dope/remake/src/music.cpp), so the result does not depend on
// the number of threads. okean/re/notes-music.md sections 6-12.
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "demo.h"
#include "exe.h"
#include "gf1.h"
#include "modplay.h"

namespace {

constexpr double kLevel = 0.89125093813374556;  // output peak: -1 dBFS
constexpr int64_t kBlock = 1 << 15;             // frames per parallel block
constexpr uint32_t kMaster = 0x12b8c;           // u32: the master volume in the image (63)

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

// fn(item, thread) for items 0..n-1 on up to `threads` threads (the calling one included)
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
    modplay::Run run;
};

// The master volume [0x12b8c] at program time t: the value of the last entry of sch.master before t (okeanplay.py
// counts the end part's decrements strictly before the write), the image's before the first.
int masterAt(const std::vector<std::pair<double, int>>& m, int master0, double t) {
    auto it = std::lower_bound(m.begin(), m.end(), t,
                               [](const std::pair<double, int>& e, double v) { return e.first < v; });
    return it == m.begin() ? master0 : std::prev(it)->second;
}

Mix mixSong(const OkeanExe& exe, const MusicSchedule& sch, const MixSpec& spec) {
    const double rate = spec.rate;
    if (!(rate > 0)) throw std::runtime_error("music: bad output rate");
    if (!std::is_sorted(sch.master.begin(), sch.master.end()))
        throw std::runtime_error("music: the master volume schedule is not in time order");
    const int master0 = int(exe.u32(kMaster));

    // ---- the player, recording the writes ----
    Recorder card;
    card.writes.reserve(size_t(1) << 17);
    card.at.reserve(size_t(1) << 17);
    modplay::Options opt;
    opt.tickTime = &sch.tickTime;
    opt.stopTime = spec.stopAtOverflow ? -1.0 : sch.stopTime;
    opt.master = [&](double t) { return masterAt(sch.master, master0, t); };
    Mix mx;
    modplay::run(exe, card, opt, mx.run);
    // each write at the first output frame at or after its time (the writes before the origin: frame 0)
    int64_t last = 0;
    for (size_t i = 0; i < card.writes.size(); i++) {
        const int64_t f = std::max(last, int64_t(std::ceil((card.at[i] - spec.origin) * rate)));
        card.writes[i].frame = last = f;
    }
    const std::vector<Write>& w = card.writes;
    // the render ends where the stop's writes take effect (they are not applied)
    const int64_t total = std::max<int64_t>(0, int64_t(std::ceil((mx.run.stopTime - spec.origin) * rate)));

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

// The card's line output is AC-coupled: a voice parked on a non-zero byte (a one-shot ends on its last byte, the
// stopped voices keep theirs) leaves a constant level, which the output capacitors of a real GUS block. A one-pole
// high-pass at 5 Hz models that (as the other GUS remakes). Returns the peak after the filter. The channels run in
// parallel.
double highPass(float* pcm, int64_t frames, double rate) {
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

int Soundtrack::positionAtTime(double tp) const {
    auto it = std::upper_bound(rowTime.begin(), rowTime.end(), tp);
    if (it == rowTime.begin()) return -1;
    return rowPos[size_t(it - rowTime.begin() - 1)];
}

int Soundtrack::positionAt(double refresh) const { return positionAtTime((refresh + 1.0) / kRefreshHz); }

double Soundtrack::timeOfPosition(int pos, double after) const {
    for (size_t i = 0; i < rowTime.size(); i++)
        if (rowPos[i] >= pos && rowTime[i] >= after) return rowTime[i];
    return std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return t * kRefreshHz - 1.0; }
double Soundtrack::timeOf(double refresh) const { return (refresh + 1.0) / kRefreshHz; }

SongTicks songTicks(const OkeanExe& exe, int ticks) {
    std::vector<modplay::Position> pos;
    SongTicks s;
    modplay::songPositions(exe, std::max(ticks, 0), pos, s.newRow);
    s.pos.reserve(pos.size());
    for (const modplay::Position& p : pos) s.pos.push_back(p.order * 64 + p.row);
    return s;
}

MusicMix renderMix(const OkeanExe& exe, const MusicSchedule& sch, const MixSpec& spec) {
    Mix m = mixSong(exe, sch, spec);
    MusicMix r;
    r.pcm.assign(m.pcm.get(), m.pcm.get() + m.frames * 2);
    r.peak = m.peak;
    r.timerServed = std::move(m.run.timerServed);
    r.stopTime = m.run.stopTime;
    return r;
}

Soundtrack renderSoundtrack(const OkeanExe& exe, const SongTicks& song, const MusicSchedule& sch, int rate) {
    Soundtrack s;
    s.rate = rate;
    s.song = song;
    s.schedule = sch;
    s.endTime = sch.stopTime;
    for (size_t j = 0; j < sch.tickTime.size() && j < song.pos.size(); j++)
        if (song.newRow[j]) {
            s.rowTime.push_back(sch.tickTime[j]);
            s.rowPos.push_back(song.pos[j]);
        }

    MixSpec spec;
    spec.rate = double(rate);
    Mix m = mixSong(exe, sch, spec);
    // the positions the render's player reached are the song's (player tick j = timer interrupt j + 4)
    for (size_t j = 0; j + modplay::kStartAfter < m.run.pos.size() && j < song.pos.size(); j++) {
        const modplay::Position& p = m.run.pos[j + modplay::kStartAfter];
        if (p.order * 64 + p.row != song.pos[j]) throw std::runtime_error("music: the render left the song's positions");
    }
    m.peak = highPass(m.pcm.get(), m.frames, double(rate));
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

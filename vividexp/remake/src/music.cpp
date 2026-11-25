// Vivid Experiment's soundtrack (music.h): EXPRMNT1.XM and EXPRMNT2.XM loaded by MIDAS's XM loader and played by its
// Generic Module Player (xmplay.cpp) through its GUS sound device (gusdev.cpp) on the card's model (gus::Hardware, at
// the card's own rate, so the device's volume reads and ramp waits are a real card's), as VIVID.EXE runs them (main
// 0x4001a0): per part Detect / Init (mono), the module loaded (the samples uploaded), tmrSyncScr 0x410d23 at a retrace,
// midasPlayModule 0x40d618 after a number of interrupts, the script, midasStopModule 0x40d728 and midasClose. The
// timer is MIDAS's screen-synchronised one (0x410740..0x410f00, notes-music.md section 6), in PIT clocks as
// vividexp/re/models/music/player.py runs it: an interrupt 95 % of a measured refresh after each retrace, the player's
// interrupts at the module's tempo in between, a refresh accounted as 17,024 clocks; at each player interrupt the
// device's Play writes the registers, then the GMP ticks. Every register write is recorded with its instant (its
// interrupt plus the card frames the ramp waits ran before it); gus::Synth then renders the voices from these writes
// at the output rate, in parallel blocks from snapshots of the voices (so the result does not depend on the number of
// threads), mono. The mix goes through a 5 Hz one-pole high-pass (the card's AC-coupled line output) and is
// normalised so that its peak is at -1 dBFS (as Inside's music.cpp).
//
// The part boundaries come from the scripts: the remake runs every script line in no time (vivid/timeline.h), so part
// I's music stops right after the retrace at which its last wait ends (the capture's main line also stopped it before
// the next interrupt), and part II starts as in the capture: tmrSyncScr at the next retrace, midasPlayModule after 10
// refresh and 7 player interrupts (its loading time in the capture, notes-music 9). The positions the scripts wait for
// come from a first run of each part without the card (the GMP does not depend on the card's answers).
#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "demo.h"
#include "exe.h"
#include "gf1.h"
#include "gusdev.h"
#include "xmplay.h"

namespace {

const double kFrame = kPitHz / kRefreshHz;  // PIT clocks per refresh (17,024.67)
const double kLine = kFrame / 449.0;
constexpr int kSync = 34049;                // [0x825c60]: tmrGetScrSync's measurement (2 x PIT clocks per refresh)
constexpr int kStartRecord = 23;            // the record in which part I's module starts (the capture's)
constexpr int kPart1Sync = 4;               // ... after tmrSyncScr waited for the retrace of refresh 4
constexpr int kPart1Start = 31;             // ... and 31 interrupts (18 refresh, 13 player)
constexpr int kPart2Sync = 1;               // part II: tmrSyncScr at the retrace after part I's end
constexpr int kPart2Start = 17;             // ... midasPlayModule after 17 interrupts (10 refresh, 7 player)
constexpr int kPart2Record = 12;            // ... in the 12th record after part I's end
constexpr int kMaxIrqs = 1 << 20;
constexpr double kLevel = 0.89125093813374556;  // output peak: -1 dBFS
constexpr int64_t kBlock = 1 << 15;         // frames per parallel block
constexpr uint8_t kDramSwitch = 0xff;       // pseudo-register of a write: the DRAM after part d's upload

// The retrace of refresh k starts at k x FRAME + 412 LINE (run_vivid.py's timing; player.py's retrace()).
double retrace(int64_t k) { return double(k) * kFrame + 412.0 * kLine + 1e-4; }

// The first record whose retrace comes after program time t (PIT clocks): when the demo sees what happened at t.
double seenAt(double t) {
    int64_t k = int64_t(std::floor((t - 412.0 * kLine) / kFrame));
    while (retrace(k) <= t) k++;
    while (retrace(k - 1) > t) k--;
    return double(k);
}

// MIDAS's timer in screen-synchronised mode with a tempo-polled sound device (0x410740..0x410f00; player.py's Timer).
class Timer {
public:
    Timer(int sync, int64_t frame0, int irqs0) : irqs(irqs0), S_((0x3b6 * sync / 1000) >> 1), R_((sync >> 1) - S_) {
        scrCount_ = S_;
        t_ = retrace(frame0);  // tmrSyncScr: PIT = scrTmrCnt at the retrace
        count_ = S_;
    }
    int irqs;          // interrupts so far
    double last = 0;   // the time of the last interrupt

    void setUpdRate(int rate) { P_ = 0x71ca5f0 / rate; }  // 0x410e21 tmrSetUpdRate
    double next() const { return t_ + count_; }
    // Runs the next interrupt; true for a player interrupt (then the caller runs the device and the GMP and calls
    // playerDone).
    bool interrupt() {
        const double t = next();
        last = t;
        irqs++;
        if (mode_ == 2) {  // 0x410843: the refresh interrupt; waits for the retrace
            int64_t k = int64_t(std::floor((t - 412.0 * kLine) / kFrame)) + 1;
            while (retrace(k) <= t) k++;
            playCount_ -= scrCount_ + R_;
            scrCount_ = S_;
            setNext(retrace(k));
            return false;
        }
        scrCount_ -= playCount_;  // 0x41092c: the player interrupt
        playCount_ = P_;
        setNext(t);
        p0_ = P_;
        return true;
    }
    void playerDone() {  // the tick changed the tempo: the new period from now
        if (P_ != p0_) {
            playCount_ = P_;
            if (mode_ == 1) setNext(t_);
        }
    }

private:
    void setNext(double t) {  // 0x41074d
        if (playCount_ < 0) {
            playCount_ = 10;
            mode_ = 1;
            count_ = 10;
        } else if (playCount_ < scrCount_) {
            mode_ = 1;
            count_ = playCount_;
        } else {
            if (scrCount_ < 0) scrCount_ = 10;
            mode_ = 2;
            count_ = scrCount_;
        }
        t_ = t;
    }
    const int S_, R_;
    int P_ = 0x5d37, playCount_ = 0x5d37, scrCount_ = 0, p0_ = 0;  // 50 Hz until the player sets its tempo
    int mode_ = 2, count_ = 0;
    double t_ = 0;
};

struct Plan {
    std::span<const uint8_t> xm;
    int syncRefresh = 0, startAfter = 0;
    int64_t stopRefresh = 0;  // after its retrace; untilWrap: when the song wraps round instead
    bool untilWrap = false;
};

struct PartOut {
    std::vector<Soundtrack::Row> rows;
    double wrap = 0;          // untilWrap: the refresh from which the demo would see the song's start again
    int stopIrq = 0;
};

// The music of one run (player.py's Run.go): the parts in order on one card, then the exit.
class Run {
public:
    Run(const VividExe& exe, gus::Card& card, gus::Hardware* hw, MusicTrace* trace)
        : exe_(exe), card_(card), hw_(hw), trace_(trace), sd_(exe, card) {}

    void part(const Plan& p, int index, PartOut& out) {
        const bool first = index == 0;
        if (first) {
            setClock(0);
            sd_.detect();
            sd_.detect();
        } else {
            sd_.close();
            sd_.detect();
        }
        sd_.init(1);  // main 0x4002e0: mode = sdMono
        midas::Module module = midas::loadXM(p.xm, exe_, sd_);
        if (hw_ && hw_->writes)  // the samples are in the card's DRAM now
            hw_->writes->push_back({hw_->clock, 0, uint8_t(hw_->voices()), 0, kDramSwitch, uint16_t(index)});
        if (first) drams_.clear();
        drams_.push_back(card_.dram());

        Timer timer(kSync, p.syncRefresh, irqs_);
        const int startIrq = irqs_ + p.startAfter;
        std::unique_ptr<midas::Player> gmp;
        bool wrapped = false;
        int lastOrder = -1;
        for (;;) {
            if (!gmp && timer.irqs >= startIrq) {  // midasPlayModule
                card_.tick = timer.irqs;
                setClock(timer.last);
                sd_.openChannels(module.numChans);
                gmp = std::make_unique<midas::Player>(module, exe_, sd_, curType_);
                gmp->playSong();
                gmp->tempoChanged();
                timer.setUpdRate(gmp->tempo() * 40);  // gmpSetUpdRateFunct 0x40d87c
            }
            if (p.untilWrap ? wrapped : timer.next() > retrace(p.stopRefresh)) break;
            if (timer.irqs - irqs_ > kMaxIrqs) throw std::runtime_error("music: the song does not end");
            if (!timer.interrupt()) continue;
            // the player interrupt: the device's Play, then the GMP's tick
            const double t = timer.last;
            card_.tick = timer.irqs;
            if (hw_) {
                hw_->advanceTo(t / kPitHz);
                setClock(t);
            }
            if (sd_.play() && gmp) {
                gmp->play();
                if (gmp->tempoChanged()) timer.setUpdRate(gmp->tempo() * 40);
                const midas::Player::Position pos = gmp->position();
                if (gmp->rowRead()) {
                    if (pos.order < lastOrder) {  // the song wrapped round
                        wrapped = true;
                        out.wrap = seenAt(t);
                    } else if (!wrapped) {
                        out.rows.push_back({pos.order, pos.row, seenAt(t), t / kFrame - 1.0});
                    }
                    lastOrder = pos.order;
                }
                if (trace_) trace_->ticks.push_back({timer.irqs, t, index + 1, pos.order, pos.row});
            }
            timer.playerDone();
        }
        // midasStopModule, right after the retrace at which the script ended
        irqs_ = timer.irqs;
        out.stopIrq = irqs_;
        card_.tick = irqs_;
        setClock(p.untilWrap ? timer.last : retrace(p.stopRefresh));
        sd_.closeChannels();
    }

    void exit() { sd_.close(); }  // midasClose

    const std::vector<std::vector<uint8_t>>& drams() const { return drams_; }

private:
    void setClock(double t) {
        if (!hw_) return;
        hw_->clock = t;
        hw_->waited = 0;
    }
    const VividExe& exe_;
    gus::Card& card_;
    gus::Hardware* hw_;
    MusicTrace* trace_;
    midas::GusDevice sd_;
    int curType_ = 0;  // [0x844cb4]
    int irqs_ = 0;
    std::vector<std::vector<uint8_t>> drams_;
};

// A part's rows up to the song's wrap, without the card (the positions only).
PartOut timeSong(const VividExe& exe, std::span<const uint8_t> xm, int sync, int startAfter) {
    gus::Card card;
    Run run(exe, card, nullptr, nullptr);
    PartOut out;
    run.part({xm, sync, startAfter, 0, true}, 0, out);
    return out;
}

void trim(Soundtrack::Song& s, double stop) {
    s.stopRefresh = stop;
    while (!s.rows.empty() && s.rows.back().refresh > stop) s.rows.pop_back();
}

// ---- the mix

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

// The voices from the writes, frames [0, total) at `rate`; returns the peak. pcm frame 0 sounds at the display time of
// record kStartRecord.
double mix(const std::vector<gus::Write>& writes, const std::vector<std::vector<uint8_t>>& drams, int rate,
           int64_t total, std::vector<float>& out) {
    std::vector<std::shared_ptr<const std::vector<float>>> images;
    for (const std::vector<uint8_t>& d : drams) images.push_back(gus::Synth::dramSamples(d));
    std::vector<Event> ev(writes.size());
    const double t0 = double(kStartRecord + 1) * kFrame;
    int64_t last = std::numeric_limits<int64_t>::min();
    for (size_t i = 0; i < writes.size(); i++) {
        const gus::Write& w = writes[i];
        const double t = (w.clock - t0) / kPitHz + double(w.waited) * double(w.voices) / gus::kGf1Clock;
        last = std::max(last, int64_t(std::ceil(t * double(rate))));
        ev[i] = {last, w.voice, w.reg, w.d};
    }

    // snapshots: the GF1 state at every block start
    const int64_t nb = (total + kBlock - 1) / kBlock;
    std::vector<Snapshot> snaps;
    snaps.reserve(size_t(nb));
    gus::Synth g(static_cast<double>(rate));
    auto apply = [&](gus::Synth& s, const Event& e) {
        if (e.reg == kDramSwitch) s.setDram(images.at(e.d));
        else s.write(e.voice, e.reg, e.d);
    };
    size_t e = 0;
    int64_t f = ev.empty() ? 0 : std::min<int64_t>(0, ev[0].frame);
    for (int64_t b = 0; b < nb; b++) {
        const int64_t b0 = b * kBlock;
        for (; e < ev.size() && ev[e].frame < b0; e++) {
            g.advance(ev[e].frame - f);
            f = ev[e].frame;
            apply(g, ev[e]);
        }
        g.advance(b0 - f);
        f = b0;
        snaps.push_back({g, e});
    }

    // the blocks, in parallel
    out.resize(size_t(total));
    std::vector<double> peaks(size_t(nb), 0.0);
    const int nt = threadCount(nb);
    struct Scratch {
        std::vector<double> x, dc;
    };
    std::vector<Scratch> scratch(static_cast<size_t>(nt));
    parallelFor(nb, nt, [&](int64_t b, int t) {
        Scratch& sc = scratch[size_t(t)];
        if (sc.x.empty()) {
            sc.x.resize(kBlock);
            sc.dc.resize(kBlock + 1);
        }
        double *x = sc.x.data(), *dc = sc.dc.data();
        const int64_t b0 = b * kBlock, b1 = std::min(total, b0 + kBlock);
        const int n = int(b1 - b0);
        std::fill(x, x + n, 0.0);
        std::fill(dc, dc + n + 1, 0.0);
        gus::Synth v = snaps[size_t(b)].g;
        size_t i = snaps[size_t(b)].next;
        int64_t at = b0;
        while (at < b1) {
            const int64_t to = i < ev.size() && ev[i].frame < b1 ? std::max(at, ev[i].frame) : b1;
            if (to > at) {
                const int64_t k = at - b0;
                v.mix(int(to - at), x + k, dc + k);  // the constant part of [at, to) in dc[k] .. dc[k + (to - at)]
                at = to;
            }
            for (; i < ev.size() && ev[i].frame <= at && at < b1; i++) apply(v, ev[i]);
        }
        double acc = 0, pk = 0;
        float* o = out.data() + b0;
        for (int k = 0; k < n; k++) {
            acc += dc[k];
            const double y = x[k] + acc;
            pk = std::max(pk, std::fabs(y));
            o[k] = float(y);
        }
        peaks[size_t(b)] = pk;
    });
    return peaks.empty() ? 0.0 : *std::max_element(peaks.begin(), peaks.end());
}

// The card's line output is AC-coupled: a one-shot sample that ends on a non-zero value (or a voice parked on one)
// leaves a constant level, which the output capacitors of a real GUS block. A one-pole high-pass at 5 Hz models that
// (as Inside's and STARS's remakes): y[n] = x[n] - x[n - 1] + a y[n - 1]. Evaluated in parallel blocks: each block's
// response from a zero state, then the state carried into it, decaying by a per frame, is added. Returns the peak.
double blockDC(std::vector<float>& x, double rate) {
    const double a = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / rate);
    const int64_t total = int64_t(x.size()), nb = (total + kBlock - 1) / kBlock;
    const size_t blocks = static_cast<size_t>(nb);
    std::vector<double> lastIn(blocks), endZero(blocks), carry(blocks + 1, 0.0), peaks(blocks);
    for (int64_t b = 0; b < nb; b++) lastIn[size_t(b)] = x[size_t(std::min(total, (b + 1) * kBlock) - 1)];
    const int nt = threadCount(nb);
    parallelFor(nb, nt, [&](int64_t b, int) {
        const int64_t b0 = b * kBlock, b1 = std::min(total, b0 + kBlock);
        double x1 = b > 0 ? lastIn[size_t(b - 1)] : 0.0, y1 = 0;
        for (int64_t i = b0; i < b1; i++) {
            const double y = double(x[size_t(i)]) - x1 + a * y1;
            x1 = x[size_t(i)];
            y1 = y;
            x[size_t(i)] = float(y);
        }
        endZero[size_t(b)] = y1;
    });
    for (int64_t b = 0; b < nb; b++) {  // the state at each block's end: its own response + the decayed carry
        const int64_t n = std::min(total, (b + 1) * kBlock) - b * kBlock;
        carry[size_t(b) + 1] = endZero[size_t(b)] + std::pow(a, double(n)) * carry[size_t(b)];
    }
    parallelFor(nb, nt, [&](int64_t b, int) {
        const int64_t b0 = b * kBlock, b1 = std::min(total, b0 + kBlock);
        double c = carry[size_t(b)] * a, pk = 0;
        for (int64_t i = b0; i < b1; i++) {
            x[size_t(i)] = float(double(x[size_t(i)]) + c);
            c *= a;
            pk = std::max(pk, std::fabs(double(x[size_t(i)])));
        }
        peaks[size_t(b)] = pk;
    });
    return peaks.empty() ? 0.0 : *std::max_element(peaks.begin(), peaks.end());
}

}  // namespace

// ---------------------------------------------------------------------------------------------- Soundtrack

std::pair<int, int> Soundtrack::Song::positionAt(double r) const {
    auto it = std::upper_bound(rows.begin(), rows.end(), r, [](double v, const Row& w) { return v < w.refresh; });
    if (it == rows.begin()) return {0, 0};
    --it;
    return {it->order, it->row};
}

double Soundtrack::Song::reached(int pos, int pat, double from) const {
    // WaitSYNC waits for the next refresh at least once, then polls at every retrace: the first poll whose position
    // satisfies the condition. A row is seen from its refresh to the next row's (the last one to the stop); before
    // the first row the player's position is 0:00.
    const double k = std::floor(from + 1e-9) + 1;
    if (pos <= 0 && pat <= 0 && (rows.empty() || k < rows.front().refresh)) return k;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& w = rows[i];
        const double end = i + 1 < rows.size() ? rows[i + 1].refresh : stopRefresh + 1;
        if (end <= w.refresh || end <= k) continue;  // never seen, or gone before the first poll
        if (w.order >= pos && w.row >= pat) return std::max(k, w.refresh);
    }
    return std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return song[0].startRefresh + t * kRefreshHz; }
double Soundtrack::timeOf(double refresh) const { return (refresh - song[0].startRefresh) / kRefreshHz; }

Soundtrack renderSoundtrack(const VividExe& exe, std::span<const uint8_t> xm1, std::span<const uint8_t> xm2, int rate,
                            const ScriptEnd& scriptEnd) {
    if (rate < 8000 || rate > 768000) throw std::runtime_error("music: bad output rate");
    Soundtrack s;
    s.rate = rate;

    // ---- the positions, and the part boundaries from the scripts ----
    PartOut a = timeSong(exe, xm1, kPart1Sync, kPart1Start);
    s.song[0].startRefresh = kStartRecord;
    s.song[0].rows = std::move(a.rows);
    s.song[0].stopRefresh = a.wrap - 1;
    const double end1 = std::floor(scriptEnd(1, s));
    if (!(end1 >= kStartRecord && end1 <= s.song[0].stopRefresh)) throw std::runtime_error("music: part I's script never ends");
    trim(s.song[0], end1);
    PartOut b = timeSong(exe, xm2, int(end1) + kPart2Sync, kPart2Start);
    s.song[1].startRefresh = end1 + kPart2Record;
    s.song[1].rows = std::move(b.rows);
    s.song[1].stopRefresh = b.wrap - 1;
    const double end2 = std::floor(scriptEnd(2, s));
    if (!(end2 >= s.song[1].startRefresh && end2 <= s.song[1].stopRefresh))
        throw std::runtime_error("music: part II's script never ends");
    trim(s.song[1], end2);
    s.endRefresh = end2;

    // ---- the music on the card's model, every write with its instant ----
    gus::Hardware hw;
    std::vector<gus::Write> writes;
    writes.reserve(size_t(1) << 19);
    hw.writes = &writes;
    Run run(exe, hw, &hw, nullptr);
    PartOut p1, p2;
    run.part({xm1, kPart1Sync, kPart1Start, int64_t(end1), false}, 0, p1);
    run.part({xm2, int(end1) + kPart2Sync, kPart2Start, int64_t(end2), false}, 1, p2);
    run.exit();

    // ---- the mix ----
    const int64_t total = int64_t((s.endRefresh - kStartRecord) / kRefreshHz * double(rate));
    std::vector<float> m;
    mix(writes, run.drams(), rate, total, m);
    const double peak = blockDC(m, double(rate));
    const float gain = peak > 0 ? float(kLevel / peak) : 1.0f;
    s.pcm.resize(size_t(total) * 2);
    const int64_t nb = (total + kBlock - 1) / kBlock;
    parallelFor(nb, threadCount(nb), [&](int64_t blk, int) {
        const int64_t a0 = blk * kBlock, a1 = std::min(total, a0 + kBlock);
        for (int64_t i = a0; i < a1; i++) {
            const int16_t v = int16_t(std::lrintf(std::clamp(m[size_t(i)] * gain, -1.0f, 1.0f) * 32767.0f));
            s.pcm[size_t(2 * i)] = s.pcm[size_t(2 * i + 1)] = v;
        }
    });
    return s;
}

MusicTrace traceMusic(const VividExe& exe, std::span<const MusicPart> parts, bool fakeGus) {
    MusicTrace tr;
    gus::Card fake;
    gus::Hardware model;
    gus::Card& card = fakeGus ? fake : static_cast<gus::Card&>(model);
    tr.gus.reserve(size_t(16) << 20);
    card.log = &tr.gus;
    Run run(exe, card, fakeGus ? nullptr : &model, &tr);
    for (size_t i = 0; i < parts.size(); i++) {
        const MusicPart& p = parts[i];
        PartOut out;
        run.part({p.xm, p.syncRefresh, p.startAfter, p.stopRefresh, false}, int(i), out);
        tr.stopIrq.push_back(out.stopIrq);
    }
    run.exit();
    tr.dram = card.dram();
    tr.writes = int64_t(std::count(tr.gus.begin(), tr.gus.end(), '\n'));
    tr.passes = fakeGus ? 0 : model.passes;
    return tr;
}

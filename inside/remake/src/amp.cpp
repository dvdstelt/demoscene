// DSMI's AMF loader and AMP 2.00 (amp.h), ported routine by routine (inside/re/notes-music.md sections 2 and 5;
// inside/re/models/music/amf.py and dsmi.py class Amp are the reference).
//
// Routines: the loader 0x4103d0 / 0x40fcc8 / 0x40fc60; ampInit 0x40d907, ampPlay 0x40d998 (ampPlayModule 0x40dbb9),
// ampStopModule 0x40dc21, ampGetPattern 0x40de09, ampGetRow 0x40de21, the poll 0x40d878 with the tick period
// 0x40dfab, the tick 0x40d7c0, the order change 0x40d299, the row reader 0x40d1ac with the note 0x40d0c7 and the
// effects of the jump table 0x40cad4 (0x40cb34..0x40cfce), the per-tick effects 0x40d753 (0x40d312..0x40d6e2),
// ampSetPanning 0x40dfd2, the volume scale 0x40cb34. Tables read from INSIDE.EXE: the semitone table 0x40c82e, the
// period table 0x40c936, the sine 0x40ca34 and the arpeggio table 0x40cab4.
//
// EYE.AMF uses instrument, speed, volume slide, volume, tone portamento, sample offset, fine volume slide, note delay
// and tempo; the other handlers are ported from the code and not checked by the capture.
#include "amp.h"

#include <algorithm>
#include <stdexcept>

#include "exe.h"
#include "gusdrv.h"

namespace amp {

namespace {

constexpr uint32_t kSemitone = 0x40c82e;  // 132 words: frequency multiplier << 10 per note (note 60 = 1023)
constexpr uint32_t kPeriod = 0x40c936;    // 127 words: period per note (note 60 = 0x1ac0)
constexpr uint32_t kSine = 0x40ca34;      // 64 words: 4095 sin(pi i / 64)
constexpr uint32_t kArpeggio = 0x40cab4;  // 16 words: 1024 * 2^(n / 12)
constexpr int kSemitones = 132, kPeriods = 127, kSines = 64, kArpeggios = 16;
constexpr uint32_t kMidPeriod = 0x1ac0;   // the period of note 60: freq = rate * 0x1ac0 / period
constexpr int kMinPeriod = 0x64, kMaxPeriod = 0xd5ff;
constexpr int kChannelVolume = 0x40;      // ampInit: ampSetMasterVolume(-1, 0x40)
constexpr uint32_t kHandleBase = 0x1000000;
constexpr uint16_t kTrackEnd = 0xffff;    // read offset of a track that has ended

int s8(uint32_t v) { return int(int8_t(uint8_t(v))); }

struct Reader {
    std::span<const uint8_t> d;
    size_t p = 0;
    void need(size_t n) const {
        if (p + n > d.size()) throw std::runtime_error("EYE.AMF: file too short");
    }
    uint8_t u8() {
        need(1);
        return d[p++];
    }
    uint16_t u16() {
        need(2);
        const uint16_t v = uint16_t(d[p] | d[p + 1] << 8);
        p += 2;
        return v;
    }
    uint32_t u32() {
        need(4);
        const uint32_t v = uint32_t(d[p] | d[p + 1] << 8 | d[p + 2] << 16) | uint32_t(d[p + 3]) << 24;
        p += 4;
        return v;
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------- the loader

Module loadAMF(std::span<const uint8_t> file) {
    Reader r{file};
    if (file.size() < 4 || file[0] != 'A' || file[1] != 'M' || file[2] != 'F')
        throw std::runtime_error("EYE.AMF: not AMF");
    Module m;
    m.version = file[3];
    if (m.version < 0x0c || m.version > 0x0e) throw std::runtime_error("EYE.AMF: unsupported AMF version");
    r.p = 0x24;  // after 'AMF', the version and the 32-byte title
    const int ninst = r.u8(), norders = r.u8(), ntracks = r.u16();
    m.channels = r.u8();
    if (m.channels < 1 || m.channels > 32) throw std::runtime_error("EYE.AMF: bad channel count");
    for (int8_t& p : m.pan) p = int8_t(r.u8());
    if (m.version >= 0x0d) {
        m.tempo = r.u8();
        m.speed = r.u8();
    }
    for (int o = 0; o < norders; o++) {
        Module::Order ord;
        ord.rows = m.version >= 0x0e ? r.u16() : 64;
        for (int& t : ord.track) t = 0;
        for (int ch = 0; ch < m.channels; ch++) ord.track[ch] = r.u16();  // track numbers for now
        m.orders.push_back(ord);
    }
    // instruments, 65 bytes each; the sample sizes (as words) of the sample indices in order of appearance
    std::vector<uint32_t> sizes;
    for (int i = 0; i < ninst; i++) {
        const size_t at = r.p;
        r.need(65);
        Instrument ins;
        r.p = at + 0x2e;
        ins.index = r.u32();
        ins.length = r.u32();
        ins.rate = r.u16();
        ins.volume = r.u8();
        ins.loopStart = r.u32();
        ins.loopEnd = r.u32();
        r.p = at + 65;
        if (sizes.size() < ins.index) sizes.push_back(ins.length & 0xffff);
        m.instruments.push_back(ins);
    }
    std::vector<int> map(static_cast<size_t>(ntracks), 0);
    int maxStored = 0;
    for (int& s : map) maxStored = std::max(maxStored, s = r.u16());
    // the stored tracks: track number t -> map[t - 1] -> stored track (a track without events is a null pointer)
    std::vector<int> stored(size_t(maxStored), -1);
    for (int t = 0; t < maxStored; t++) {
        const int n = r.u16();
        r.u8();  // type (ignored)
        r.need(3 * size_t(n));
        if (!n) {
            continue;
        }
        std::vector<uint8_t> tr(3 + 3 * size_t(n) + 3, 0xff);
        tr[0] = uint8_t(n);
        tr[1] = uint8_t(n >> 8);
        tr[2] = 0;
        std::copy_n(file.begin() + std::ptrdiff_t(r.p), 3 * size_t(n), tr.begin() + 3);
        r.p += 3 * size_t(n);
        stored[size_t(t)] = int(m.tracks.size());
        m.tracks.push_back(std::move(tr));
    }
    for (Module::Order& o : m.orders)
        for (int ch = 0; ch < m.channels; ch++) {
            const int t = o.track[ch], s = t >= 1 && t <= ntracks ? map[size_t(t - 1)] : 0;
            o.track[ch] = s >= 1 ? stored[size_t(s - 1)] : -1;
        }
    for (int ch = m.channels; ch < 32; ch++)
        for (Module::Order& o : m.orders) o.track[ch] = -1;
    // the sample data, by sample index; every instrument with that index gets it
    for (size_t s = 0; s < sizes.size(); s++) {
        if (!sizes[s]) continue;
        r.need(sizes[s]);
        const std::span<const uint8_t> data = file.subspan(r.p, sizes[s]);
        for (Instrument& ins : m.instruments)
            if (ins.index == s + 1) {
                ins.data = data;
                ins.handle = kHandleBase + uint32_t(r.p);
            }
        r.p += sizes[s];
    }
    return m;
}

// ---------------------------------------------------------------------------------------------- the player

namespace {

struct Channel {  // AMP's channel record (0x422a99 + 0x2d * ch) and its sample record (0x423039 + 0x1a * ch)
    int track = -1;        // +0x00 (index into Module::tracks, -1 = none)
    uint16_t pos = 3;      // +0x04 read offset (3 + 3 * event; 0xffff = end)
    int flags = 0;         // +0x06 bit 1: muted
    int note = 0;          // +0x08
    int inst = 0;          // +0x09
    int vol = 0;           // +0x0a
    int count = 0;         // +0x0b ticks since the note (saturating word)
    int period = 0;        // +0x0d
    int cmd = 0;           // +0x0f per-tick command
    int val = 0;           // +0x10
    int target = 0;        // +0x11 tone porta target period
    int tpSpeed = 0;       // +0x13 signed
    int tpLast = 0;        // +0x14
    int portaLast = 0;     // +0x15 signed
    uint32_t arp[3] = {};  // +0x16, +0x1a, +0x1e
    int vibPos = 0;        // +0x23
    int vibParam = 0;      // +0x24
    int tremParam = 0;     // +0x25
    int tremSpeed = 0;     // +0x26
    int tremDepth = 0;     // +0x27
    int tremPos = 0;       // +0x28
    int offLast = 0;       // +0x29
    int pan = 0;           // +0x2c
    gusdrv::Sample s;
};

}  // namespace

struct Player::Impl {
    const Module& m;
    gusdrv::Driver& d;
    uint16_t semitone[kSemitones], period[kPeriods], sine[kSines], arpeggio[kArpeggios];

    int chvol[32];          // [0x423379 + ch]
    int status = 0;         // [0x422a58] bit 0 playing, bit 1 paused
    bool initialised = false;  // [0x423399] bit 0
    int tick = 0;           // [0x422a63]
    int speed = 6, tempo = 125;
    int div = 50;           // [0x4233a8] tempo * 50 / 125
    uint32_t last = 0;      // [0x4233aa] DSMI time of the last tick
    int order = 0;          // [0x422a5d]
    int row = 0;            // [0x422a61] the row read last
    int nextRow = 0;        // [0x422a5f]
    int rows = 64;          // [0x422a97]
    int brk = 0;            // [0x422a70]
    int sync = 0;           // [0x422a6f]
    int first = 0, end = 0, option = 0, nch = 0;
    int oldCmd = 0;         // the row reader's `old` (the instrument handler changes it)
    Channel ch[32];
    std::vector<int>* rowLog = nullptr;

    Impl(const Module& mod, const InsideExe& exe, gusdrv::Driver& drv) : m(mod), d(drv) {
        for (int i = 0; i < kSemitones; i++) semitone[i] = exe.u16(kSemitone + 2 * uint32_t(i));
        for (int i = 0; i < kPeriods; i++) period[i] = exe.u16(kPeriod + 2 * uint32_t(i));
        for (int i = 0; i < kSines; i++) sine[i] = exe.u16(kSine + 2 * uint32_t(i));
        for (int i = 0; i < kArpeggios; i++) arpeggio[i] = exe.u16(kArpeggio + 2 * uint32_t(i));
        if (period[60] != kMidPeriod) throw std::runtime_error("INSIDE.EXE: AMP's period table is not at 0x40c936");
        std::fill(chvol, chvol + 32, kChannelVolume);
    }

    // ---- helpers
    uint32_t scale(uint32_t vol, int c) const { return uint32_t((uint64_t(vol) * uint32_t(chvol[c])) >> 6); }
    int tickPeriod() const { return 10000 / std::max(div, 1); }  // 0x40dfab
    uint32_t freqOf(const Channel& c, int per) const {
        return per > 0 ? uint32_t(uint64_t(c.s.rate) * kMidPeriod / uint32_t(per)) : 0;
    }
    uint32_t noteFreq(const Channel& c) const { return uint32_t((uint64_t(c.s.rate) * semitone[c.note]) >> 10); }
    int periodOf(int note) const { return period[std::clamp(note, 0, kPeriods - 1)]; }

    // ---- API
    void init() {
        status = 0;
        tick = 0;
        order = row = nextRow = brk = sync = 0;
        for (Channel& c : ch) c = Channel{};
        div = 50;
        last = 0;  // GetDSMITime() at the music start
        std::fill(chvol, chvol + 32, kChannelVolume);
        initialised = true;
    }

    int play(int firstOrder, int opt) {
        if (!initialised) return -1;
        status = 0;
        for (Channel& c : ch) c = Channel{};
        brk = 0;
        row = 0;
        sync = 0;
        nextRow = 0;
        speed = m.speed;
        tempo = m.tempo;
        div = tempo * 50 / 125;
        first = order = firstOrder;
        end = int(m.orders.size());
        option = opt;
        nch = m.channels;
        if (first >= end) return -1;
        for (int i = 0; i < nch; i++) setPan(i, m.pan[i]);
        loadOrder();
        for (int i = 0; i < nch; i++) {  // ampUnmuteChannel 0x40ddac
            d.unmute(i);
            ch[i].flags &= ~2;
        }
        for (const Instrument& ins : m.instruments)  // download every instrument that has sample data
            if (!ins.data.empty() && d.download(ins.data, ins.handle, ins.length)) return -2;
        status = 1;
        return 0;
    }

    void stop() {
        status &= ~3;
        for (int i = 0; i < nch; i++) d.stopNote(i);
    }

    void setPan(int c, int pan) {  // 0x40dfd2
        ch[c].pan = pan;
        d.setPanning(c, pan);
    }

    int loadOrder() {  // 0x40d299
        nextRow = 0;
        if (order >= end) {
            if (!(option & 1)) {
                stop();
                return -1;
            }
            order = first;
        }
        rows = m.orders[size_t(order)].rows;
        for (int i = 0; i < nch; i++) {
            ch[i].track = m.orders[size_t(order)].track[i];
            ch[i].pos = 3;
        }
        return 0;
    }

    void poll(uint32_t now) {  // 0x40d878
        int p = tickPeriod();
        int64_t e = int64_t(int32_t(now - last));
        while ((e -= p) > 0) {
            doTick();
            last += uint32_t(p);
            p = tickPeriod();
        }
    }

    void doTick() {  // 0x40d7c0
        if (!(status & 1) || (status & 2)) return;
        if (++tick >= speed) {
            tick = 0;
            if (brk || nextRow >= rows) {
                brk = 0;
                order = (order + 1) & 0xff;
                if (loadOrder()) return;
            }
            for (int i = 0; i < nch; i++) readRow(i);
            row = nextRow;
            nextRow++;
            if (rowLog) rowLog->push_back(order * 64 + row);
        }
        for (int i = 0; i < nch; i++) tickEffect(i);
        for (int i = 0; i < nch; i++) ch[i].count = std::min(ch[i].count + 1, 0xffff);
    }

    // ---- the row reader 0x40d1ac
    void readRow(int i) {
        Channel& c = ch[i];
        oldCmd = c.cmd;
        c.cmd = 0;
        c.val = 0;
        if (c.pos != kTrackEnd && c.track >= 0) {
            const std::vector<uint8_t>& t = m.tracks[size_t(c.track)];
            for (;;) {
                if (size_t(c.pos) + 6 > t.size()) {  // past the stored events (the file lacks the end marker)
                    c.pos = kTrackEnd;
                    break;
                }
                const uint8_t* p = t.data() + c.pos;
                if (p[0] == 0xff) {
                    c.pos = kTrackEnd;
                    break;
                }
                if (p[0] > nextRow) break;
                if (p[1] & 0x80) effect(i, c, p);
                else note(i, c, p);
                c.pos = uint16_t(c.pos + 3);
            }
        }
        if (oldCmd != c.cmd) d.setFrequency(i, c.s.freq);
        d.setVolume(i, scale(uint32_t(c.vol), i));
    }

    void note(int i, Channel& c, const uint8_t* p) {  // 0x40d0c7: {row, note, volume}
        if (p[1] == 0) {
            c.vol = 0;
            d.stopNote(i);
            return;
        }
        if (p[1] != 0x7f) c.note = p[1];
        if (p[2] != 0xff) c.vol = p[2];
        c.count = 0;
        c.vibPos = 0;
        trigger(i, c);
    }

    void trigger(int i, Channel& c) {
        c.period = c.target = periodOf(c.note);
        const uint32_t f = noteFreq(c);
        c.s.freq = f;
        c.s.vol = uint8_t(c.vol);
        const uint32_t v = scale(uint32_t(c.vol), i);
        if (!(c.flags & 2)) d.playNote(i, f, v);
    }

    void setFreq(int i, Channel& c, int per) {
        c.period = per;
        const uint32_t f = freqOf(c, per);
        c.s.freq = f;
        d.setFrequency(i, f);
    }

    static bool noteFollows(const uint8_t* p) { return p[0] == p[3] && !(p[4] & 0x80); }  // the next event, same row

    // ---- the row effects (jump table 0x40cad4, index = command & 0x7f); p[3..5] is the next event
    void effect(int i, Channel& c, const uint8_t* p) {
        const int k = p[1] & 0x7f, v = p[2];
        switch (k) {
        case 0x00: {  // 0x40cfce instrument
            if (size_t(v) >= m.instruments.size()) break;
            c.inst = v;
            oldCmd = noteFollows(p) ? 0 : 1;
            const Instrument& ins = m.instruments[size_t(v)];
            c.s.handle = ins.handle;
            c.s.length = ins.length;
            c.s.rate = ins.rate;
            c.s.loopStart = ins.loopStart;
            c.s.loopEnd = ins.loopEnd;
            c.s.vol = ins.volume;
            c.vol = ins.volume;
            d.setSample(i, c.s);
            d.setVolume(i, scale(c.s.vol, i));
            break;
        }
        case 0x01:  // 0x40cf8e speed
            if (v && v != speed) speed = v;
            break;
        case 0x02:  // 0x40cf1a volume slide (per tick)
            c.cmd = 2;
            c.val = v;
            break;
        case 0x03:  // 0x40cf2e volume
            c.vol = v;
            d.setVolume(i, scale(uint32_t(v), i));
            break;
        case 0x04: {  // 0x40cec5 portamento (per tick; + = period up = pitch down)
            int cl = s8(uint32_t(v));
            if (v == 0x80) {
                cl = c.portaLast;
                if (cl >= 0) cl = -cl;
            } else if (v == 0) {
                cl = c.portaLast;
                if (cl <= 0) cl = -cl;
            }
            c.portaLast = s8(uint32_t(cl));
            c.cmd = 4;
            c.val = cl & 0xff;
            break;
        }
        case 0x05: break;  // 0x40cb51 nothing
        case 0x06: {  // 0x40ce2a tone portamento
            int cl = v ? v : c.tpLast;
            c.tpLast = cl;
            uint32_t vol = p[0];  // EAX still holds the row byte unless a note follows on this row
            if (noteFollows(p)) {
                const int n = p[4] == 0x7f ? c.note : p[4];
                c.note = n;
                c.target = periodOf(n);
                c.pos = uint16_t(c.pos + 3);
                vol = p[5];
                if (p[5] != 0xff) c.vol = p[5];
            }
            d.setVolume(i, scale(vol, i));
            if (c.target < c.period) cl = -s8(uint32_t(cl));
            c.cmd = 6;
            c.tpSpeed = s8(uint32_t(cl));
            break;
        }
        case 0x07: {  // 0x40cd27 tremolo: non-zero nibbles replace the old ones
            c.val = v;
            int dl = c.tremParam;
            if (v & 0x0f) dl = (dl & 0xf0) | (v & 0x0f);
            if (v & 0xf0) dl = (dl & 0x0f) | (v & 0xf0);
            c.tremParam = dl;
            c.cmd = 7;
            c.tremSpeed = dl >> 4;
            c.tremDepth = dl & 0x0f;
            break;
        }
        case 0x08:  // 0x40cdae arpeggio
            if (v) {
                c.cmd = 8;
                c.val = v;
                const uint32_t base = c.s.freq;
                c.arp[0] = base;
                c.arp[1] = uint32_t((uint64_t(arpeggio[v >> 4]) * base) >> 10);
                c.arp[2] = uint32_t((uint64_t(arpeggio[v & 15]) * base) >> 10);
            }
            break;
        case 0x09: {  // 0x40cd75 vibrato
            c.cmd = 9;
            int dl = c.vibParam;
            if (v & 0x0f) dl = (dl & 0xf0) | (v & 0x0f);
            if (v & 0xf0) dl = (dl & 0x0f) | (v & 0xf0);
            c.vibParam = dl;
            c.val = dl;
            break;
        }
        case 0x0a:  // 0x40cd13 tone porta + volume slide
        case 0x0b:  // 0x40ccff vibrato + volume slide
            c.cmd = k;
            c.val = v;
            break;
        case 0x0c: brk = 1; break;  // 0x40cf86 pattern break (always to row 0)
        case 0x0d: {  // 0x40cf52 position jump (plays order v)
            const int o = s8(uint32_t(v - 1));
            if (o < s8(uint32_t(order)) && !(option & 1)) break;
            if (o > s8(uint32_t(end))) break;
            order = o & 0xff;
            brk = 1;
            break;
        }
        case 0x0e: sync = v; break;  // 0x40ccf6
        case 0x0f:  // 0x40ccde retrig
            if (v) {
                c.cmd = 0x0f;
                c.val = v;
            }
            break;
        case 0x10: {  // 0x40cc93 sample offset
            c.cmd = 0x10;
            const int h = v ? v : c.offLast;
            c.offLast = h;
            uint32_t off = uint32_t(h) << 8;
            if (off > c.s.length) off = c.s.loopStart;
            d.setPosition(i, off);
            break;
        }
        case 0x11: {  // 0x40cc53 fine volume slide (once)
            c.cmd = 0x11;
            c.val = v;
            c.vol = std::clamp(s8(uint32_t(v + c.vol)), 0, 0x40);
            d.setVolume(i, scale(uint32_t(c.vol), i));
            break;
        }
        case 0x12: {  // 0x40cbe3 fine porta (once)
            c.cmd = 0x12;
            c.val = v;
            int cx = (c.period + (s8(uint32_t(v)) << 4)) & 0xffff;
            if ((cx & 0x8000) || cx <= kMinPeriod) cx = kMinPeriod;
            setFreq(i, c, cx);
            break;
        }
        case 0x13:  // 0x40cb9c note delay
            c.val = v ? v : 1;
            if (noteFollows(p)) {
                if (p[4] != 0x7f) c.note = p[4];
                if (p[5] != 0xff) c.vol = p[5];
                c.cmd = 0x13;
                c.pos = uint16_t(c.pos + 3);
            }
            break;
        case 0x14:  // 0x40cb88 note cut
            c.cmd = 0x14;
            c.val = v;
            break;
        case 0x15:  // 0x40cfa3 tempo
            if (v && v != tempo) {
                tempo = v;
                div = v * 50 / 125;
            }
            break;
        case 0x16:  // 0x40cf02 fine slide per tick
            if (v) {
                c.cmd = 0x16;
                c.val = v;
            }
            break;
        case 0x17: {  // 0x40cb52 panning
            c.cmd = 0x17;
            c.val = v;
            int pan = s8(uint32_t(v));
            if (v != 0x64) pan = std::clamp(pan, -63, 63);
            setPan(i, pan);
            break;
        }
        default: break;  // 0x18 points into code: never used
        }
    }

    // ---- the per-tick effects 0x40d753 (table 0x40d70b; on the row tick only arpeggio and retrig, 0x40d747)
    void tickEffect(int i) {
        Channel& c = ch[i];
        const int k = c.cmd;
        if (!k || (tick == 0 && k != 8 && k != 0x0f)) return;
        switch (k) {
        case 2: volSlide(i, c); break;
        case 4: porta(i, c); break;
        case 6: tonePorta(i, c); break;
        case 7: tremolo(i, c); break;
        case 9: vibrato(i, c); break;
        case 0x0a:
            tonePorta(i, c);
            volSlide(i, c);
            break;
        case 0x0b:
            vibrato(i, c);
            volSlide(i, c);
            break;
        case 0x13:  // 0x40d630 note delay
            if (c.val == tick) {
                c.count = 0;
                trigger(i, c);
            }
            break;
        case 0x14:  // 0x40d6e2 note cut
            if (c.val == 0 || c.val == tick) {
                c.vol = 0;
                d.setVolume(i, 0);
            }
            break;
        case 0x16: {  // 0x40d345 fine slide
            int cx = c.period + s8(uint32_t(c.val));
            if ((cx & 0x8000) || cx < 0) cx = kMinPeriod;
            else if (s8(uint32_t(c.val)) == 0) return;
            else if (cx <= kMinPeriod) cx = kMinPeriod;
            setFreq(i, c, cx & 0xffff);
            break;
        }
        case 8: d.setFrequency(i, c.arp[tick % 3]); break;  // 0x40d4b2 arpeggio
        case 0x0f:  // 0x40d60d retrig
            if (c.val && tick % c.val == 0) d.setPosition(i, 0);
            break;
        default: break;
        }
    }

    void volSlide(int i, Channel& c) {  // 0x40d312
        c.vol = std::clamp(s8(uint32_t(c.val + c.vol)), 0, 0x40);
        d.setVolume(i, scale(uint32_t(c.vol), i));
    }

    void porta(int i, Channel& c) {  // 0x40d3a9
        int cx = c.period, ax = s8(uint32_t(c.val)) * 16;
        if (ax == 0) return;
        if (ax > 0) {
            cx = (cx + ax) & 0xffff;
        } else {
            ax = -ax;
            if (ax > cx) {
                cx = kMinPeriod;
            } else {
                cx -= ax;
                if (cx <= kMinPeriod) cx = kMinPeriod;
            }
        }
        if (cx >= kMaxPeriod) cx = kMaxPeriod;
        setFreq(i, c, cx);
    }

    void tonePorta(int i, Channel& c) {  // 0x40d429 (16-bit arithmetic, unsigned compares)
        int cx = c.period;
        int ax = (c.tpSpeed * 16) & 0xffff;
        if (ax == 0) return;
        int dx = (c.target - cx) & 0xffff;
        bool toTarget = false;
        if (dx & 0x8000) {
            ax = -ax & 0xffff;
            dx = -dx & 0xffff;
            if (ax > dx) toTarget = true;
            else ax = -ax & 0xffff;
        } else if (ax > dx) {
            toTarget = true;
        }
        if (toTarget) {
            cx = c.target;
            if (cx == 0) return;
        } else {
            cx = (cx + ax) & 0xffff;
        }
        setFreq(i, c, cx);
    }

    void vibrato(int i, Channel& c) {  // 0x40d4dd: a quarter-sine sawtooth (entries 0..31, sign from bit 7)
        const int sv = sine[(c.vibPos >> 2) & 0x1f];
        const int ax = ((c.vibParam & 0x0f) * sv >> 7) & 0xffff;
        int cx = (c.vibPos & 0x80) ? (c.period - ax) & 0xffff : (c.period + ax) & 0xffff;
        if (cx <= kMinPeriod) cx = kMinPeriod;
        d.setFrequency(i, freqOf(c, cx));
        c.vibPos = (c.vibPos + ((c.vibParam >> 2) & 0x3c)) & 0xff;
    }

    void tremolo(int i, Channel& c) {  // 0x40d56d
        const int sv = sine[(c.tremPos >> 2) & 0x1f];
        const int dx = (c.tremDepth * sv >> 10) & 0xffff;
        int cx = c.vol;
        if (c.tremPos & 0x80) cx += dx;
        else cx = std::max(cx - dx, 0);
        cx = std::clamp(cx, 0, 0x40);
        d.setVolume(i, scale(uint32_t(cx), i));
        c.tremPos = (c.tremPos + ((c.tremSpeed << 2) & 0x3c)) & 0xff;
    }
};

Player::Player(const Module& m, const InsideExe& exe, gusdrv::Driver& drv)
    : p_(std::make_unique<Impl>(m, exe, drv)) {}
Player::~Player() = default;

void Player::init() { p_->init(); }
int Player::play(int first, int option) {
    p_->rowLog = rowLog;
    return p_->play(first, option);
}
void Player::stop() { p_->stop(); }
void Player::poll(uint32_t dsmiTime) {
    p_->rowLog = rowLog;
    p_->poll(dsmiTime);
}
int Player::order() const { return p_->initialised ? p_->order : -1; }
int Player::row() const { return p_->initialised ? p_->row : -1; }

}  // namespace amp

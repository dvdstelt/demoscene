// MIDAS's XM loader and Generic Module Player (XM branch), routine by routine after vividexp/re/models/music/midas.py
// (which reproduces every register write of vividexp/re/full.gus and part2.gus). See xmplay.h; notes-music.md
// sections 3 and 4.
#include "xmplay.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "exe.h"
#include "gusdev.h"

namespace midas {

namespace {
constexpr uint32_t kXmFx = 0x8238e4;     // XM effect 0x00..0x23 -> GMP command
constexpr uint32_t kXmEfx = 0x823908;    // XM Exy: x -> GMP command
constexpr uint32_t kAmiga = 0x822280;    // 96 dwords: 12 notes x 8 finetunes, FT2's Amiga periods
constexpr uint32_t kLinear = 0x822430;   // 768 dwords: 8363 x 64 x 2^(-k / 768)
constexpr uint32_t kVibSine = 0x824c30;  // 32 bytes: half a sine, 0..255
constexpr uint32_t kMaxPeriod = 0xd600, kMinPeriod = 0x39;  // play handle +0x34 / +0x38 for XM
constexpr int kShift = 2;                // +0x3c: period shift
constexpr int kMaxVolume = 0x40;         // +0x40

int s8(int x) { return int8_t(uint8_t(x)); }
int s16(int x) { return int16_t(uint16_t(x)); }
int32_t s32(int64_t x) { return int32_t(uint32_t(uint64_t(x))); }

struct Reader {  // little-endian reads of the file, bounds-checked
    std::span<const uint8_t> d;
    void need(size_t at, size_t n) const {
        if (at + n > d.size() || at + n < at) throw std::runtime_error("XM module: truncated");
    }
    uint8_t u8(size_t at) const { need(at, 1); return d[at]; }
    uint16_t u16(size_t at) const { need(at, 2); return uint16_t(d[at] | d[at + 1] << 8); }
    uint32_t u32(size_t at) const { need(at, 4); return uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | uint32_t(d[at + 3]) << 24); }
};

// 0x418918: one FT2 packed pattern -> GMP pattern data. used / maxInst: the "instrument used" flags and the highest
// instrument number the patterns name ([0x846d08], [0x846d20]).
std::vector<uint8_t> convertPattern(const VividExe& exe, const Reader& f, size_t at, size_t packed, int rows,
                                    int chans, std::array<uint8_t, 256>& used, int& maxInst) {
    std::vector<uint8_t> out;
    if (!packed) return std::vector<uint8_t>(size_t(rows), 0);  // 0x418939: every row empty
    size_t i = at;
    const size_t end = at + packed;
    auto next = [&] {
        if (i >= end) throw std::runtime_error("XM module: pattern data overrun");
        return int(f.u8(i++));
    };
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < chans; c++) {
            int flag = 0, note = 0xff, inst = 0xff, vol = 0, fx = 0, fp = 0;
            int b = next(), m;
            if (b & 0x80) {
                m = b & 0x7f;
            } else {
                m = 0x7f;
                i--;
            }
            if (m & 1) {
                const int n = next();
                if (n > 0x60) {
                    note = 0xfe;
                } else {  // C division: note 0 gives 0xff, "no note"
                    const int k = n - 1, o = k / 12;
                    note = ((o << 4) | (k - o * 12)) & 0xff;
                }
                flag |= 0x20;
            }
            if (m & 2) {
                inst = (next() - 1) & 0xff;
                if (note == 0xfe) inst = 0xff;  // dropped with a key off
                if (inst != 0xff) {
                    maxInst = std::max(maxInst, inst);
                    used[size_t(inst)] = 1;
                    flag |= 0x20;
                }
            }
            if (m & 4) {
                vol = next();
                if (vol) {
                    if (vol >= 0x10 && vol <= 0x50) vol -= 0x10;
                    flag |= 0x40;
                }
            }
            if (m & 8) fx = next();
            if (m & 0x10) fp = next();
            int cmd = 0;
            if (fx || fp) {
                cmd = exe.u8(kXmFx + uint32_t(fx));
                const int lo = fp & 0xf;
                auto slide = [&] { fp = (fp & 0xf0) ? fp >> 4 : (-lo) & 0xff; };  // x != 0: +x, else -y
                if (fx < 0xf) {
                    if (fx == 5 || fx == 6 || fx == 0xa) {
                        slide();
                    } else if (fx == 0xe) {
                        const int sub = fp >> 4;
                        cmd = exe.u8(kXmEfx + uint32_t(sub));
                        fp = (sub == 1 || sub == 0xb) ? (-lo) & 0xff : lo;  // E1x, EBx: -x
                    }
                } else if (fx == 0xf) {
                    cmd = fp >= 0x20 ? 0x10 : 0xf;
                } else if (fx < 0x14) {
                    if (fx == 0x11) slide();
                } else if (fx == 0x14) {  // Kxx: key off now (the tick is ignored), keeps the instrument byte
                    note = 0xfe;
                    flag |= 0x20;
                    cmd = 0;
                } else if (fx == 0x19) {
                    slide();
                } else if (fx == 0x21) {
                    const int h = fp & 0xf0;
                    if (h == 0x10) fp = (-lo) & 0xff;
                    else if (h == 0x20) fp = lo;
                    else cmd = 0;
                }
                if (cmd) flag |= 0x80;
            }
            if (flag) {
                out.push_back(uint8_t(flag | c));
                if (flag & 0x20) {
                    out.push_back(uint8_t(note));
                    out.push_back(uint8_t(inst));
                }
                if (flag & 0x40) out.push_back(uint8_t(vol));
                if (flag & 0x80) {
                    out.push_back(uint8_t(cmd));
                    out.push_back(uint8_t(fp));
                }
            }
        }
        out.push_back(0);
    }
    return out;
}

Envelope makeEnvelope(const Reader& f, size_t points, int n, int sus, int ls, int le) {
    Envelope e;
    e.n = n;
    e.sus = s8(sus);
    e.ls = s8(ls);
    e.le = s8(le);
    e.raw[0] = uint8_t(n);
    e.raw[1] = uint8_t(sus);
    e.raw[2] = uint8_t(ls);
    e.raw[3] = uint8_t(le);
    for (size_t k = 0; k < 48; k++) e.raw[4 + k] = f.u8(points + k);
    return e;
}
}  // namespace

int Envelope::x(int k) const {
    const int o = 4 + 4 * k;
    return k >= -1 && o + 1 < int(raw.size()) ? s16(raw[size_t(o)] | raw[size_t(o) + 1] << 8) : 0;
}

int Envelope::y(int k) const {
    const int o = 6 + 4 * k;
    return k >= -1 && o + 1 < int(raw.size()) ? s16(raw[size_t(o)] | raw[size_t(o) + 1] << 8) : 0;
}

// ---------------------------------------------------------------------------------------------- the loader

Module loadXM(std::span<const uint8_t> file, const VividExe& exe, GusDevice& sd) {
    const Reader f{file};
    if (file.size() < 80 || std::string(reinterpret_cast<const char*>(file.data()), 17) != "Extended Module: " ||
        f.u16(58) < 0x104)
        throw std::runtime_error("not an XM module MIDAS loads");
    Module m;
    m.name.assign(reinterpret_cast<const char*>(file.data()) + 17, 20);
    m.name = m.name.substr(0, m.name.find('\0'));
    const uint32_t headerSize = f.u32(60);
    m.length = f.u16(64);
    m.restart = f.u16(66);
    m.numChans = f.u16(68);
    const int filePatterns = f.u16(70), fileInstruments = f.u16(72);
    m.linear = f.u16(74) & 1;
    m.speed = f.u16(76);
    m.tempo = f.u16(78);
    if (m.length < 1 || m.length > 256 || m.numChans < 1 || m.numChans > 32)
        throw std::runtime_error("XM module: bad header");
    int patterns = filePatterns;
    for (int i = 0; i < m.length; i++) {
        m.orders.push_back(f.u8(80 + size_t(i)));
        patterns = std::max(patterns, m.orders.back() + 1);  // 0x419008: patterns the orders use but the file lacks
    }

    // the patterns, converted; the missing ones get 64 empty rows (0x4191d1)
    std::array<uint8_t, 256> used{};
    int maxInst = 0;
    size_t p = 60 + size_t(headerSize);
    for (int i = 0; i < patterns; i++) {
        Pattern pt;
        if (i < filePatterns) {
            const uint32_t hl = f.u32(p);
            pt.rows = f.u16(p + 5);
            const size_t packed = f.u16(p + 7);
            f.need(p + hl, packed);
            pt.data = convertPattern(exe, f, p + hl, packed, pt.rows, m.numChans, used, maxInst);
            p += hl + packed;
        } else {
            pt.data.assign(64, 0);
        }
        m.patterns.push_back(std::move(pt));
    }

    // the instruments: a used one gets max(keymap) + 1 samples (at least 1), an unused one none; one the file does
    // not have but a pattern names gets one empty sample
    const int instruments = std::max(fileInstruments, maxInst + 1);
    struct FileSample { uint32_t length, loopStart, loopLength; int volume, finetune, type, panning, relNote; size_t data; };
    for (int i = 0; i < instruments; i++) {
        Instrument in;
        std::vector<FileSample> fs;
        int nfile = 0;
        if (i < fileInstruments) {
            const uint32_t isize = f.u32(p);
            nfile = f.u16(p + 27);
            int nkey = 0;
            if (nfile) {
                const size_t q = p + 29;
                const uint32_t shsize = f.u32(q);
                std::vector<uint8_t> keymap(96);
                for (size_t k = 0; k < 96; k++) {
                    keymap[k] = f.u8(q + 4 + k);
                    nkey = std::max(nkey, keymap[k] + 1);
                }
                if (nfile > 1) in.keymap = keymap;
                const size_t t = q + 196;  // nvol, npan, volsus, volls, volle, pansus, panls, panle, voltype, pantype
                const int volType = f.u8(t + 8), panType = f.u8(t + 9);
                if (volType & 1) {
                    in.hasVolEnv = true;
                    in.volEnv = makeEnvelope(f, q + 100, f.u8(t), (volType & 2) ? f.u8(t + 2) : 0xff,
                                             (volType & 4) ? f.u8(t + 3) : 0xff, (volType & 4) ? f.u8(t + 4) : 0xff);
                }
                if (panType & 1) {
                    in.hasPanEnv = true;
                    in.panEnv = makeEnvelope(f, q + 148, f.u8(t + 1), (panType & 2) ? f.u8(t + 5) : 0xff,
                                             (panType & 4) ? f.u8(t + 6) : 0xff, (panType & 4) ? f.u8(t + 7) : 0xff);
                }
                in.fadeout = f.u16(q + 210);
                p += isize;
                for (int s = 0; s < nfile; s++) {
                    fs.push_back({f.u32(p), f.u32(p + 4), f.u32(p + 8), f.u8(p + 12), s8(f.u8(p + 13)), f.u8(p + 14),
                                  f.u8(p + 15), s8(f.u8(p + 16)), 0});
                    p += shsize;
                }
                for (FileSample& h : fs) {
                    h.data = p;
                    p += h.length;
                }
            } else {
                p += isize;
            }
            const int n = used[size_t(i)] ? std::max(nkey, 1) : 0;
            in.samples.resize(size_t(n));
        } else if (used[size_t(i)]) {
            in.samples.resize(1);
        }
        for (size_t s = 0; s < in.samples.size(); s++) {
            Sample& sm = in.samples[s];
            if (int(s) >= nfile) continue;  // an empty sample (type 0)
            const FileSample& h = fs[s];
            sm.length = h.length;
            sm.loopStart = h.loopStart;
            sm.loopEnd = h.loopStart + h.loopLength;
            sm.volume = h.volume;
            sm.panning = h.panning;
            sm.relNote = h.relNote;
            sm.finetune = h.finetune;
            sm.type = (h.type & 0x10) ? 2 : 1;
            const int lt = h.type & 3;
            sm.loop = lt == 1 || lt == 2;
            sm.loopType = sm.loop ? lt : 0;
            if (sm.loopEnd == sm.loopStart) sm.loop = sm.loopType = 0;
        }
        m.instruments.push_back(std::move(in));
        if (i < fileInstruments) {
            // the sample data (deltas), decoded to unsigned 8 bits as MIDAS keeps them, then AddSample (0x419b75..)
            Instrument& ins = m.instruments.back();
            for (size_t s = 0; s < ins.samples.size(); s++) {
                Sample& sm = ins.samples[s];
                if (sm.length == 0) sm.type = 0;
                std::vector<uint8_t> data;
                if (sm.type == 2) {  // 16-bit: summed, the high byte + 0x80
                    f.need(fs[s].data, sm.length);
                    int acc = 0;
                    for (uint32_t k = 0; k < sm.length / 2; k++) {
                        acc = int16_t(uint16_t(acc + s16(f.u16(fs[s].data + 2 * k))));
                        data.push_back(uint8_t((acc >> 8) + 0x80));
                    }
                    sm.type = 1;
                    sm.length >>= 1;
                    sm.loopStart >>= 1;
                    sm.loopEnd >>= 1;
                } else if (sm.type == 1) {
                    f.need(fs[s].data, sm.length);
                    uint8_t acc = 0;
                    for (uint32_t k = 0; k < sm.length; k++) {
                        acc = uint8_t(acc + file[fs[s].data + k]);
                        data.push_back(uint8_t(acc + 0x80));
                    }
                }
                sm.handle = sd.addSample(data, sm.type, sm.length, sm.loop, sm.loopStart, sm.loopEnd, sm.loopType);
            }
        } else {
            for (Sample& sm : m.instruments.back().samples) sm.handle = sd.addSample({}, 0, 0, 0, 0, 0, 0);
        }
    }
    return m;
}

// ---------------------------------------------------------------------------------------------- the player

Player::Player(Module& m, const VividExe& exe, GusDevice& sd, int& curType)
    : m_(m), exe_(exe), sd_(sd), curType_(curType) {
    setTempo(125);  // gmpInit 0x40d820
}

const Instrument& Player::ins(int i) const {
    // instrument -1 (no instrument yet) is only read where its sample is 0xff, so never used (midas.py reads the last)
    return i < 0 ? m_.instruments.back() : m_.instruments[size_t(i)];
}

const Sample& Player::sampleOf(const Instrument& in, int s) const {
    static const Sample none;  // out of range: memory MIDAS would read past the samples (never in these songs)
    return s >= 0 && size_t(s) < in.samples.size() ? in.samples[size_t(s)] : none;
}

void Player::setTempo(int bpm) {  // 0x40ebc8
    tempo_ = bpm;
    sd_.setUpdRate(bpm * 40);
    tempoChanged_ = true;
}

void Player::playSong() {  // 0x40d8b0 gmpPlaySong(module, -1, -1, -1, -1, channels from 0, &handle)
    pos_ = 0;
    pattern_ = m_.orders[0];
    row_ = 0;
    loopEnd_ = m_.length - 1;
    restart_ = m_.restart;
    speed_ = m_.speed;
    tick_ = 0;
    patDelay_ = 0;
    hasData_ = false;
    sync_ = -1;
    info_ = {0, pattern_, 0};
    setTempo(m_.tempo);
    ch_.assign(size_t(m_.numChans), Channel{});
    for (int i = 0; i < m_.numChans; i++) {
        ch_[size_t(i)].sdch = i;
        cur_ = &ch_[size_t(i)];
        setPanning(0x80);  // the module's panning table: all 0x80
    }
}

// ---- periods and frequencies (0x40ec10 .. 0x40f0a4)

uint32_t Player::notePeriod(int note) const {
    const Channel& c = *cur_;
    const Sample& s = sampleOf(ins(c.inst), c.sample);
    const int n = std::clamp((note & 0xf) + (note >> 4) * 12 + s.relNote, 0, 0x76);
    if (m_.linear) return uint32_t(0x1e00 - n * 64 - s.finetune / 2);
    int ft = s.finetune;
    const int oct = n / 12, ni = n % 12, q = ft / 16;  // the index can leave the table: read what the program reads
    const int64_t p1 = exe_.u32(uint32_t(int64_t(kAmiga) + 4 * (ni * 8 + q)));
    int i2;
    if (ft < 0) {
        ft = -ft;
        i2 = ni * 8 + q - 1;
    } else {
        i2 = ni * 8 + q + 1;
    }
    const int64_t p2 = exe_.u32(uint32_t(int64_t(kAmiga) + 4 * i2));
    const int fr = ft & 0xf;
    return uint32_t(s32((p1 * (16 - fr) + p2 * fr) * 2) >> oct);
}

uint32_t Player::periodRate(uint32_t per) const {
    if (per == 0) throw std::runtime_error("GMP: period 0 (error 0x11)");
    if (m_.linear) return exe_.u32(kLinear + 4 * (per % 0x300)) >> ((per / 0x300) & 31);
    return 0xda7790u / per;
}

void Player::setPeriod(int64_t per) {
    if (per == 0) return;
    const uint32_t p = std::clamp(uint32_t(uint64_t(per)), kMinPeriod, kMaxPeriod);  // unsigned: negative -> max
    cur_->period = p;
    sd_.setRate(cur_->sdch, periodRate(p));
}

void Player::changePeriod(int64_t per) {
    const uint32_t p = std::clamp(uint32_t(uint64_t(per)), kMinPeriod, kMaxPeriod);
    sd_.setRate(cur_->sdch, periodRate(p));
}

void Player::setVolume(int v) {
    v = std::clamp(v, 0, kMaxVolume);
    cur_->vol = cur_->vol2 = v;
}

void Player::setPanning(int p) {
    if (curType_ == 3) {
        p = std::clamp(p, 0, 0xff);
    } else {  // the MOD / S3M branch: -64..64 (0xa4: surround)
        p = p == 0xa4 ? 0x80 : std::clamp(p - 0x40, -0x40, 0x40);
        sd_.setPanning(cur_->sdch, p);
    }
    cur_->pan = p & 0xff;
}

void Player::newNote() {
    Channel& c = *cur_;
    c.vibPos = 0;
    if (c.inst != -1 && c.sample != 0xff) {
        if (c.note == 0xfe) {  // key off: the release starts; without a volume envelope the volume goes to 0
            c.release = 1;
            c.vdone = c.pdone = 0;
            if (!ins(c.inst).hasVolEnv) c.vol = c.vol2 = 0;
        } else {
            c.release = 0;
            c.lastNote = c.note;
            c.period = notePeriod(c.note);
            sd_.playSound(c.sdch, periodRate(c.period));
            if (c.offset) sd_.setPosition(c.sdch, c.offset);
        }
    }
    c.flags &= ~1;
}

// ---- the tick (0x40de40), the row (0x40e3dc)

void Player::play() {
    curType_ = 3;
    rowRead_ = false;
    if (++tick_ >= speed_) {
        tick_ = 0;
        playRow();
    } else {
        tickCommands();
    }
    update();
}

void Player::advanceOrder() {
    for (;;) {
        if (++pos_ > loopEnd_ || m_.orders[size_t(pos_)] == 0xffff) pos_ = restart_;
        if (m_.orders[size_t(pos_)] != 0xfffe) break;
    }
    pattern_ = m_.orders[size_t(pos_)];
}

void Player::playRow() {
    rowRead_ = true;
    info_ = {pos_, pattern_, row_};
    if (patDelay_) {
        for (Channel& c : ch_) {
            cur_ = &c;
            if (c.flags & 8) rowCommand(c.cmd, c.rawInfo);  // the row's own byte, not the shared memory
        }
        tickCommands();
        patDelay_--;
        return;
    }
    const std::vector<uint8_t>& pd = m_.patterns[size_t(pattern_)].data;
    auto at = [&](size_t i) -> int {
        if (i >= pd.size()) throw std::runtime_error("GMP: pattern data overrun");
        return pd[i];
    };
    if (!hasData_) {  // the pattern from its start, skipping `row` rows
        size_t i = 0;
        for (int r = 0; r < row_; r++)
            for (;;) {
                const int b = at(i++);
                i += (b & 0x20 ? 2 : 0) + (b & 0x40 ? 1 : 0) + (b & 0x80 ? 2 : 0);
                if (b == 0) break;
            }
        hasData_ = true;
        dataPos_ = i;
    }
    for (Channel& c : ch_) c.flags &= ~0xf;
    size_t i = dataPos_;
    for (;;) {
        const int b = at(i++);
        const int chn = b & 0x1f;
        if (chn >= m_.numChans) {
            i += (b & 0x20 ? 2 : 0) + (b & 0x40 ? 1 : 0) + (b & 0x80 ? 2 : 0);
        } else {
            Channel& c = ch_[size_t(chn)];
            if (b & 0x20) {
                const int nt = at(i), inst = at(i + 1);
                i += 2;
                if (nt != 0xff) {
                    c.note = nt;
                    c.flags |= 1;
                }
                if (inst != 0xff) {
                    c.flags |= 4;
                    c.newInst = inst;
                }
            }
            if (b & 0x40) {
                c.volByte = at(i++);
                c.flags |= 2;
            }
            if (b & 0x80) {
                c.cmd = at(i);
                c.rawInfo = at(i + 1);
                c.flags |= 8;
                i += 2;
            }
        }
        if (b == 0) break;
    }
    dataPos_ = i;
    for (Channel& c : ch_) {
        cur_ = &c;
        if (c.flags & 4) {  // an instrument byte
            c.vpos = c.vdone = c.ppos = c.pdone = 0;
            c.fade = 0x10000;
            c.release = 0;
            const Instrument* in;
            if (((c.flags & 8) && c.cmd == 4) || ((c.flags & 1) && c.note == 0xfe)) {
                in = &ins(c.inst);  // tone portamento (not 5xy) and key off keep the current instrument
            } else {
                c.inst = c.newInst;
                in = &ins(c.inst);
                if ((c.flags & 1) && c.note < 0xfe) {
                    const int k = std::clamp((c.note >> 4) * 12 + (c.note & 0xf), 0, 0x5f);
                    c.sample = in->keymap.empty() ? 0 : in->keymap[size_t(k)];
                }
            }
            if (c.sample != 0xff) {
                const Sample& s = sampleOf(*in, c.sample);
                const int v = std::min(std::max(s.volume, 0), kMaxVolume);
                c.vol = c.vol2 = v;
                setPanning(s.panning);
                c.offset = 0;
                if ((c.flags & 1) && s.handle) sd_.setSample(c.sdch, s.handle);
            }
        }
        if (c.flags & 2) {
            if (c.volByte <= 0x40) c.vol = c.vol2 = std::min(c.volByte, kMaxVolume);
            else volumeRow(c);
        }
        if (c.flags & 8) {
            if (c.rawInfo) c.info = c.rawInfo;  // one memory for all commands
            if (c.cmd) {
                if (c.cmd >= 0x1f) throw std::runtime_error("GMP: bad command");
                rowCommand(c.cmd, c.info);
            }
        }
        if (c.flags & 1) {  // a note (tone portamento, sample offset and note delay clear it)
            if (c.inst != -1) {
                const Instrument& in = ins(c.inst);
                if (c.note < 0xfe) {
                    const int k = std::clamp((c.note >> 4) * 12 + (c.note & 0xf), 0, 0x5f);
                    c.sample = in.keymap.empty() ? 0 : in.keymap[size_t(k)];
                }
                if (c.sample != 0xff && c.note != 0xfe) {
                    c.inst = c.newInst;
                    c.offset = 0;
                    const Sample& s = sampleOf(ins(c.newInst), c.sample);
                    if (s.handle) sd_.setSample(c.sdch, s.handle);
                }
            }
            newNote();
        } else if (!((c.flags & 8) && c.cmd == 5) && (c.volByte & 0xf0) != 0xb0) {
            changePeriod(c.period);  // undoes vibrato / arpeggio offsets (not on a vibrato row, stale Bx too)
        }
    }
    if (hasData_) {  // no jump or break this row
        if (++row_ >= m_.patterns[size_t(pattern_)].rows) {
            row_ = 0;
            advanceOrder();
            hasData_ = false;
        }
    }
}

void Player::tickCommands() {  // 0x40eaf8
    for (Channel& c : ch_) {
        cur_ = &c;
        if ((c.flags & 2) && c.volByte > 0x40) volumeTick(c);
        if (c.flags & 8) tickCommand(c.cmd, c.info);
    }
}

// One envelope step of 0x40dec4: the value (y << 8, interpolated, the division first) at pos, then the advance.
void Player::envelope(const Envelope& e, int& pos, int& done, int release, bool pan, int& value) {
    int idx = -2;
    for (int k = 0; k < e.n; k++)
        if (e.x(k) >= pos) {
            idx = k;
            break;
        }
    if (idx == -2) idx = pan ? e.n : e.n - 1;  // volume: the last point; panning: one past it
    if (e.x(idx) == pos) {
        value = e.y(idx) * 256;
    } else {
        const int y0 = e.y(idx - 1), y1 = e.y(idx), x0 = e.x(idx - 1), x1 = e.x(idx);
        value = y0 * 256 + (x1 != x0 ? (y1 - y0) * 256 / (x1 - x0) : 0) * (pos - x0);
    }
    if (e.x(e.sus) == pos && !release && e.sus != -1) done = 1;
    if (!done) {
        if (e.x(e.n - 1) > pos) pos++;
        if (e.x(e.le) <= pos && e.le != -1) pos = e.x(e.ls);
        if (e.x(e.sus) == pos && !release && e.sus != -1) done = 1;
    }
}

void Player::update() {  // 0x40dec4 (XM branch)
    const int gv = m_.globalVolume;
    for (Channel& c : ch_) {
        cur_ = &c;
        int vol = c.vol2;
        const Instrument* in = c.inst != -1 ? &ins(c.inst) : nullptr;
        if (in && in->hasVolEnv) {
            vol = int((int64_t(vol) * c.fade) >> 16);
            if (c.release) c.fade = std::max(0, c.fade - in->fadeout);  // the fade after its use
            int value = 0;
            envelope(in->volEnv, c.vpos, c.vdone, c.release, false, value);
            vol = std::clamp(int((int64_t(value) * vol) >> 14), 0, 0x40);
        }
        vol = (vol * gv) >> 6;
        c.finalVol = vol & 0xff;
        sd_.setVolume(c.sdch, uint32_t(vol));
        int pan = c.pan;
        if (in && in->hasPanEnv) {
            int value = 0;
            envelope(in->panEnv, c.ppos, c.pdone, c.release, true, value);
            const int e = (value >> 8) - 0x20;
            pan += (0x80 - std::abs(pan - 0x80)) * e / 32;
            if (pan < 0) pan = 0xff;
            if (pan > 0xff) pan = 0xff;
        }
        sd_.setPanning(c.sdch, (pan - 0x80) / 2);  // -64..63; stored only (mono)
    }
}

// ---- commands (row handlers table 0x824d68, tick handlers 0x824de4)

void Player::volSlide(int info) { setVolume(cur_->vol + s8(info)); }  // 0x41e9f0

void Player::tonePortaRow() {  // 0x41ecf0 (also volume column Fx)
    Channel& c = *cur_;
    if ((c.flags & 1) && c.inst != -1 && c.sample != 0xff && c.note != 0xfe) {
        c.target = notePeriod(c.note);
        c.flags &= ~1;
    }
}

void Player::tonePortaTick(int speed) {  // 0x41ecd4
    Channel& c = *cur_;
    if (c.target == 0) return;
    if (speed) c.portaSpeed = speed & 0xff;
    if (c.period == c.target) return;
    const int64_t step = int64_t(c.portaSpeed) << kShift, per = c.period, tgt = c.target;
    if (per < tgt) setPeriod(per + step >= tgt ? tgt : per + step);
    else setPeriod(per - tgt <= step ? tgt : per - step);
}

void Player::vibratoTick(int x) {  // 0x41ed6c
    Channel& c = *cur_;
    if (x & 0xf) c.vibDepth = x & 0xf;
    if (x & 0xf0) c.vibSpeed = (x >> 4) & 0xff;
    const int d = (exe_.u8(kVibSine + uint32_t(c.vibPos & 0x1f)) * c.vibDepth) >> (7 - kShift);
    changePeriod(s32(int64_t(c.period) + ((c.vibPos & 0x20) ? -d : d)));
    c.vibPos = (c.vibPos + c.vibSpeed) & 0xff;
}

void Player::rowCommand(int cmd, int info) {
    Channel& c = *cur_;
    switch (cmd) {
    case 4:
    case 6: tonePortaRow(); break;                                  // 3xx, 5xy
    case 9: setPanning(c.rawInfo); break;                           // 8xx
    case 0xa:                                                       // 9xx sample offset: adds up
        if (c.rawInfo) c.offMem = c.rawInfo;
        if (c.flags & 1) {
            c.offset += uint32_t(c.offMem) << 8;
            newNote();
        }
        break;
    case 0xc:                                                       // Bxx
        pos_ = c.rawInfo;
        if (pos_ > loopEnd_) pos_ = restart_;
        pattern_ = m_.orders[size_t(pos_)];
        hasData_ = false;
        row_ = 0;
        break;
    case 0xd: setVolume(c.rawInfo); break;                          // Cxx
    case 0xe: {                                                     // Dxx
        if (hasData_) {
            advanceOrder();
            hasData_ = false;
        }
        const int r = c.rawInfo;
        row_ = r <= 0x63 ? (r >> 4) * 10 + (r & 0xf) : 0x3f;
        break;
    }
    case 0xf:                                                       // Fxx speed (F00: restart, not stop)
        if (c.rawInfo) {
            speed_ = c.rawInfo;
        } else {
            pos_ = restart_;
            pattern_ = m_.orders[size_t(pos_)];
            hasData_ = false;
            row_ = 0;
        }
        break;
    case 0x10: setTempo(c.rawInfo); break;                          // Fxx tempo
    case 0x11: setPeriod(s32(int64_t(c.period) + s8(info) * 4)); break;  // E1y / E2y
    case 0x12: {                                                    // E6y pattern loop
        const int x = c.rawInfo;
        int to = -1;
        if (x == 0) {
            c.loopRow = row_ & 0xff;
        } else if (c.loopCount == 0) {
            c.loopCount = x;
            to = c.loopRow;
        } else if (--c.loopCount > 0) {
            to = c.loopRow;
        }
        if (to != -1) {
            hasData_ = false;
            row_ = to;
        }
        break;
    }
    case 0x13: {                                                    // E8y (S3M style)
        const int x = c.rawInfo;
        setPanning(x < 7 ? x << 3 : x > 8 ? 0x80 - (15 - x) * 8 : 0x40);
        break;
    }
    case 0x14:
    case 0x1b: c.retrig = 1; break;                                 // E9y, Rxy
    case 0x15: volSlide(info); break;                             // EAy / EBy
    case 0x16:                                                      // ECy
        if (c.rawInfo == tick_) setVolume(0);
        break;
    case 0x17:                                                      // EDy note delay
        if (c.rawInfo && (c.flags & 1)) c.flags = (c.flags | 0x10) & ~1;
        else c.flags &= ~0x10;
        break;
    case 0x18:                                                      // EEy
        if (patDelay_ == 0) patDelay_ = c.rawInfo;
        break;
    case 0x19: m_.globalVolume = std::min(c.rawInfo, 0x40); break;  // Gxx
    case 0x1c: sync_ = c.rawInfo; break;                            // Wxx (no callback)
    case 0x1d: setPeriod(s32(int64_t(c.period) + s8(info))); break;  // X1y / X2y
    default: break;
    }
}

void Player::tickCommand(int cmd, int info) {
    Channel& c = *cur_;
    switch (cmd) {
    case 1: {                                                       // 0xy arpeggio
        int ni = c.note & 0xf, oc = (c.note >> 4) & 0xf;
        if (c.inst == -1 || c.sample == 0xff || c.note == 0xfe) return;
        const int t = tick_ % 3;
        if (t == 1) ni += (info >> 4) & 0xf;
        else if (t == 2) ni += info & 0xf;
        if (ni > 11) {
            oc++;
            ni -= 12;
        }
        changePeriod(notePeriod((oc << 4) | ni));
        break;
    }
    case 2: setPeriod(s32(int64_t(c.period) - (int64_t(info) << kShift))); break;  // 1xx
    case 3: setPeriod(s32(int64_t(c.period) + (int64_t(info) << kShift))); break;  // 2xx
    case 4: tonePortaTick(c.rawInfo); break;                        // 3xx
    case 5: vibratoTick(c.rawInfo); break;                          // 4xy
    case 6:                                                         // 5xy
        volSlide(info);
        tonePortaTick(0);
        break;
    case 7:                                                         // 6xy
        volSlide(info);
        vibratoTick(0);
        break;
    case 0xb: volSlide(info); break;                              // Axy
    case 0x14:                                                      // E9y retrig
        if (c.rawInfo && c.retrig >= (c.rawInfo & 0xf)) {
            sd_.setPosition(c.sdch, c.offset);
            c.retrig = 0;
        }
        c.retrig = (c.retrig + 1) & 0xff;
        break;
    case 0x16:                                                      // ECy
        if (c.rawInfo == tick_) setVolume(0);
        break;
    case 0x17:                                                      // EDy
        if (c.rawInfo == tick_ && (c.flags & 0x10)) newNote();
        break;
    case 0x1a: {                                                    // Hxy
        int v = (m_.globalVolume + info) & 0xff;
        if (v > 0x7f) v = 0;
        m_.globalVolume = std::min(v, 0x40);
        break;
    }
    case 0x1b: break;  // Rxy multi retrig (0x41eeb8): not ported, no song uses it
    case 0x1e: setPanning(c.pan + s8(info)); break;               // Pxy
    default: break;
    }
}

void Player::volumeRow(Channel& c) {  // 0x41f328
    const int x = c.volByte & 0xf;
    switch (c.volByte & 0xf0) {
    case 0x80: setVolume(c.vol - x); break;
    case 0x90: setVolume(c.vol + x); break;
    case 0xa0: c.vibSpeed = x; break;
    case 0xc0: setPanning((x << 4) | x); break;
    case 0xf0: tonePortaRow(); break;
    default: break;
    }
}

void Player::volumeTick(Channel& c) {  // 0x41f3a4
    const int x = c.volByte & 0xf;
    switch (c.volByte & 0xf0) {
    case 0x60: volSlide((-x) & 0xff); break;
    case 0x70: volSlide(x); break;
    case 0xb0: vibratoTick(x); break;
    case 0xd0: setPanning(c.pan - x); break;
    case 0xe0: setPanning(c.pan + x); break;
    case 0xf0: tonePortaTick(x << 4); break;
    default: break;
    }
}

}  // namespace midas

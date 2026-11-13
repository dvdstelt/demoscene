// clone meets clone's MXMPlay, ported routine by routine (clone/re/notes-music.md section 4, mirroring
// clone/re/models/music/mxmplay.py; the shared parts as jizz/remake/src/mxmplay.cpp). It plays the module of
// modsynth.cpp on a gus::Card.
//
// Routines: 0x40196a xmpInit + 0x4025de sample upload, 0x402129 xmpPlay, 0x40219a xmpStop, 0x402399 GF1 reset,
// 0x402245 / 0x4021ac timer install and IRQ0, 0x401bf4 xmpGetPos, 0x401bde xmpSetVolume, 0x4024ab GUS update,
// 0x402425 set volume, 0x40248b voice off, 0x401e9e player tick, 0x401a47 new note, 0x4019ee pitch -> frequency,
// 0x401c02 pitch clamp, 0x4018e3 vibrato and volume tables, the volume / envelope code 0x40203c..0x4020e2 and the
// effect handlers 0x401c21..0x401e87 (dispatch 0x401e88 through the jump table 0x40274d).
//
// Differences from JIZZ's build (notes 4.8, 4.9): the row reader skips channels without an entry in the row (no new
// note, no effect; the volume-column command keeps the previous row's), new note has 9xx sample offset and clears
// the vibrato / arpeggio / retrigger counters but has no K00 and no "key off cuts"; arpeggio, porta up / down, tone
// porta, vibrato, multi retrig and volume-column fine slides are implemented; no pattern break / delay, no E1x / E2x.
#include "mxmplay.h"

#include <algorithm>
#include <stdexcept>

#include "gf1.h"

namespace mxm {

namespace {

constexpr uint32_t kImageBase = 0x400000;
constexpr uint32_t kTempoK = 0x2d86bf;    // PIT clocks per player tick x BPM
constexpr uint32_t kFreqDiv = 0x1ee;      // GF1 FC = freq * voices / 494, bit 0 cleared
constexpr uint32_t kFreqTab[4] = {0x4026ad, 0x4026cd, 0x4026ed, 0x40270d};  // 16 x u16, u16, u16, u32
constexpr uint32_t kVibSrc = 0x40266d;    // 64 bytes: quarter sine 0..63 (64 is implied)
constexpr uint32_t kJumpTab = 0x40274d;   // 0x88 dwords: handler - 0x401c21
constexpr uint32_t kFxBase = 0x401c21;
constexpr int kJumpEntries = 0x88;
constexpr uint32_t kNone = 0xffffffffu;   // "no next order"

int32_t s8(uint32_t v) { return int32_t(int8_t(uint8_t(v))); }
int32_t s16(uint32_t v) { return int32_t(int16_t(uint16_t(v))); }

// the effect handlers of this build (the jump table's targets)
enum Fx : uint8_t {
    kRet, kArpRow, kArpTick, kPupRow, kPupTick, kPdnRow, kPdnTick, kTpRow, kTpTick, kVibRow, kVibTick,
    kVslideRow, kVslideTick, kPan, kVol, kSpeed, kRetrigRow, kRetrigTick,
    kVcVol1, kVcVol2, kVcVol3, kVcVol4, kVcVol5, kVcFineDn, kVcFineUp, kVcPan
};

Fx handlerAt(uint32_t a) {
    switch (a) {
    case 0x401c21: return kRet;
    case 0x401c6b: return kArpRow;
    case 0x401c88: return kArpTick;
    case 0x401cec: return kPupRow;
    case 0x401d0a: return kPupTick;
    case 0x401cfb: return kPdnRow;
    case 0x401d22: return kPdnTick;
    case 0x401d3a: return kTpRow;
    case 0x401d49: return kTpTick;
    case 0x401d78: return kVibRow;
    case 0x401d93: return kVibTick;
    case 0x401dbd: return kVslideRow;
    case 0x401dc8: return kVslideTick;
    case 0x401c51: return kPan;
    case 0x401c41: return kVol;
    case 0x401c22: return kSpeed;
    case 0x401df3: return kRetrigRow;
    case 0x401e0c: return kRetrigTick;
    case 0x401cbe: return kVcVol1;
    case 0x401cba: return kVcVol2;
    case 0x401cb6: return kVcVol3;
    case 0x401cb2: return kVcVol4;
    case 0x401cae: return kVcVol5;
    case 0x401cdb: return kVcFineDn;
    case 0x401cc8: return kVcFineUp;
    case 0x401c5b: return kVcPan;
    default: throw std::runtime_error("CLONE.EXE image: unexpected MXMPlay effect handler");
    }
}

// ---- channel record (data block + 0x9aa + 0x100 * ch) ----
struct Chan {
    int active = 0;                                 // +0x00
    uint32_t start = 0, end = 0, loop = 0;          // +0x01, +0x05, +0x09 DRAM addresses (16 bit: word addresses)
    int mode = 0;                                   // +0x0d GF1 voice control
    int stop = 0, newsmp = 0;                       // +0x0e, +0x0f
    int64_t offset = 0;                             // +0x10 restart at start + offset (-1 = keep playing)
    int gfreq = 0, gvol = 0, gpan = 0;              // +0x14, +0x16, +0x18
    uint32_t vol = 0, fvol = 0, pan = 0, fpan = 0;  // +0x19..+0x1c
    int32_t pitch = 0, fpitch = 0;                  // +0x1d, +0x21 (1/256 semitone, lower = higher)
    uint32_t ins = 0, insp = 0, rel = 0;            // +0x25, +0x26 (module offset, 0 = none), +0x2a
    int keyon = 0;                                  // +0x2c
    int32_t fade = 0;                               // +0x2d
    uint32_t envpt = 0, envpos = 0;                 // +0x31, +0x35
    uint32_t svol = 0, span = 0;                    // +0x3d, +0x3e
    uint32_t eff = 0, volcmd = 0;                   // +0x3f (xmpPlay clears it to 0), +0x40
    int32_t target = 0, tpspeed = 0;                // +0x41, +0x45
    uint32_t vslide = 0;                            // +0x49
    int32_t pupspeed = 0, pdnspeed = 0;             // +0x4f, +0x53
    uint32_t vibspeed = 0, vibpos = 0, vibdepth = 0;  // +0x5b, +0x5c, +0x5e
    uint32_t arpidx = 0, arp[3] = {0, 0, 0};        // +0x65, +0x66..+0x68
    uint32_t rtcount = 0, rtint = 0, rtvol = 0;     // +0x6a, +0x6b, +0x6c
    uint32_t note = 0, ofsmem = 0;                  // +0x6d, +0x6e
};

}  // namespace

struct Player::Impl {
    std::span<const uint8_t> d;  // the module
    gus::Card& g;
    uint32_t ordnum = 0, restart = 0, channels = 0, insnum = 0, opt = 0, smpstart = 0, smp8 = 0, smp16 = 0;
    uint32_t mspeed = 0, mbpm = 0;
    int32_t lowpitch = 0, highpitch = 0;

    uint32_t ft[4][16] = {};
    uint32_t voltab[257] = {};
    int32_t vibtab[256] = {};
    Fx fx[kJumpEntries] = {};

    Chan ch[32];
    uint32_t globalvol = 0, mastervol = 0x40;           // +0x00, +0x01
    uint32_t tick = 0, speed = 0, newrow = 0;           // +0x03, +0x04, +0x05
    uint32_t row = 0, patpos = 0, patrows = 0;          // +0x06, +0x0a, +0x0e
    uint32_t order = 0, jumporder = 0, jumprow = 0, patdelay = 0;  // +0x12, +0x16, +0x1a, +0x1e
    uint32_t nNote = 0, nIns = 0, nVol = 0, nEff = 0, nPar = 0, nFlag24 = 0;  // +0x1f..+0x24: the event being read
    uint32_t cur = 0, timer = 0, period = 0, remain = 0;  // +0x35, +0x39, +0x41, +0x45
    uint32_t voices = 14;                               // +0x53

    Impl(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card) : d(module), g(card) {
        auto img8 = [&](uint32_t a) -> uint32_t {
            if (a < kImageBase || a - kImageBase >= image.size())
                throw std::runtime_error("CLONE.EXE image: address out of range (MXMPlay tables)");
            return image[a - kImageBase];
        };
        auto img16 = [&](uint32_t a) { return img8(a) | img8(a + 1) << 8; };
        auto img32 = [&](uint32_t a) { return img16(a) | img16(a + 2) << 16; };
        if (d.size() < 0x750 || u32(0) != 0x004d584du) throw std::runtime_error("music: not an MXM module");
        ordnum = u32(4);
        restart = u32(8);
        channels = u32(0xc);
        insnum = u32(0x14);
        mspeed = u8(0x18);
        mbpm = u8(0x19);
        opt = u16(0x1a);
        smpstart = u32(0x1c);
        smp8 = u32(0x20);
        smp16 = u32(0x24);
        lowpitch = int32_t(u32(0x28));
        highpitch = int32_t(u32(0x2c));
        if (channels < 1 || channels > 32 || mbpm == 0 || ordnum == 0) throw std::runtime_error("music: unexpected MXM header");
        if (uint64_t(smpstart) + smp8 + 2 * uint64_t(smp16) > d.size()) throw std::runtime_error("music: MXM sample data missing");
        for (uint32_t i = 0; i < 16; i++) {
            ft[0][i] = img16(kFreqTab[0] + 2 * i);
            ft[1][i] = img16(kFreqTab[1] + 2 * i);
            ft[2][i] = img16(kFreqTab[2] + 2 * i);
            ft[3][i] = img32(kFreqTab[3] + 4 * i);
        }
        // 0x4018e3: vibrato sine at +0x2daa (v[i] = round(64 sin(2 pi i / 256))) and the volume table at +0x7a8
        int32_t half[128];
        for (int i = 0; i < 64; i++) half[i] = s8(img8(kVibSrc + uint32_t(i)));
        half[64] = 0x40;
        for (int i = 1; i < 64; i++) half[64 + i] = half[64 - i];
        for (int i = 0; i < 128; i++) {
            vibtab[i] = s8(uint32_t(half[i]));
            vibtab[128 + i] = s8(uint32_t(-half[i]));
        }
        voltab[0] = 0;
        voltab[256] = 0xefff;
        for (uint32_t i = 1; i < 256; i++) {
            uint32_t e = 7;
            for (uint32_t v = i; v; v >>= 1) e++;
            voltab[i] = ((((i << (20 - e)) & 0xfff) | (e << 12)) - 0x1000) & 0xffff;
        }
        for (int i = 0; i < kJumpEntries; i++) fx[i] = handlerAt(img32(kJumpTab + 4 * uint32_t(i)) + kFxBase);
    }

    uint32_t u8(uint32_t a) const {
        if (a >= d.size()) throw std::runtime_error("music: MXM read out of range");
        return d[a];
    }
    uint32_t u16(uint32_t a) const { return u8(a) | u8(a + 1) << 8; }
    uint32_t u24(uint32_t a) const { return u16(a) | u8(a + 2) << 16; }
    uint32_t u32(uint32_t a) const { return u16(a) | u16(a + 2) << 16; }
    uint32_t insPtr(uint32_t i) const { return u32(0x14c + 4 * i); }  // instrument i (1..): [D + 0x1a8 + 4 (i - 1)]

    // ---- 0x40196a xmpInit: the upload 0x4025de, 16-byte blocks from DRAM 0 (the rest of the length is dropped)
    void init() {
        const uint32_t n = smp16 * 2 + smp8;
        for (uint32_t a = 0; a < (n >> 4) << 4; a++) g.poke(a, uint8_t(u8(smpstart + a)));
    }

    // ---- 0x402399 GF1 reset (voices = max(channels, 14)); every write is followed by delays on the real card
    void gf1Reset(uint32_t nch) {
        voices = (nch & 0xff) >= 14 ? nch : (nch & ~0xffu) | 14;
        g.w8(0x4c, 0);
        g.w8(0x4c, 1);
        g.w8(0x0e, int(((voices - 1) | 0xc0) & 0xff));
        for (int v = 0; v < 32; v++) {
            g.select(v);
            g.w16(0x09, 0);
            g.w8(0x00, 3);  // 0x4022f2: write, delay, write again
            g.w8(0x00, 3);
            g.w8(0x0d, 3);
            g.w8(0x0d, 3);
            g.w8(0x06, 0x3f);
        }
        g.w8(0x4c, 7);
        g.select(0);
        g.port(0, 8);
    }

    // ---- 0x402129 xmpPlay (with the GUS start 0x4025ae and the timer install 0x402245)
    void play(uint32_t ord) {
        jumporder = ord;
        order = ord;
        row = 0;
        gf1Reset(channels);
        g.select(0);
        g.port(0, 9);
        for (int i = 0; i < 32; i++) {
            ch[i] = Chan{};
            ch[i].pan = u8(0x30 + uint32_t(i));
        }
        globalvol = 0x40;
        jumprow = 0;
        speed = mspeed;
        tick = (mspeed - 1) & 0xff;
        period = kTempoK / mbpm;
        remain = 0;
        cur = 0x10000;
        timer = 0xffff0000u - period;
    }

    // ---- 0x4021ac IRQ0
    bool irq() {
        timer += cur;
        const uint32_t maxper = 0x10000;
        uint32_t edx = remain - maxper;
        if (!(remain > maxper)) edx = period;
        remain = edx;
        cur = std::min(remain, maxper);
        if (remain != period) return false;
        gusUpdate();
        playerTick();
        return true;
    }

    // ---- 0x4024ab GUS update: stop the voices that get a new note, wait for the ramps, then every channel
    void gusUpdate() {
        const uint32_t n = channels;
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch[i];
            if (c.active && c.stop) {
                g.select(int(i));
                g.w8(0x00, c.mode | 3);
                g.w8(0x00, c.mode | 3);
                voiceOff();
                c.stop = 0;
            }
        }
        g.waitRamps(int(n));  // 0x4024ee: until bit 0 of reg 0x8d, every channel
        for (uint32_t i = 0; i < n; i++) {
            g.select(int(i));
            Chan& c = ch[i];
            if (!c.active) {
                voiceOff();
            } else {
                if (c.newsmp) {
                    waddr(0x02, c.loop);
                    waddr(0x04, c.end);
                }
                if (c.offset != -1) {
                    uint32_t a = uint32_t(c.offset) + c.start;
                    if (a >= c.end) a = c.end - 1;
                    waddr(0x0a, a);
                    const int ctl = g.r8(0x80);
                    g.w8(0x00, (ctl & 0x40) | c.mode);
                }
                g.r8(0x80);  // result ignored: the code tests bl (= 0x80), not al
                g.w8(0x0c, c.gpan);
                g.w16(0x01, c.gfreq);
                setVolume(c.gvol);
            }
            c.newsmp = 0;
            c.offset = -1;
        }
    }

    void waddr(int reg, uint32_t a) {  // 0x40237a
        g.w16(reg, int((a >> 7) & 0x1fff));
        g.w16(reg + 1, int((a << 9) & 0xffff));
    }

    void voiceOff() {  // 0x40248b: ramp down to 0x04xx
        g.w8(0x07, 0x04);
        g.w8(0x08, 0xfc);
        g.w8(0x0d, 0x40);
        g.w8(0x0d, 0x40);
    }

    void setVolume(int target) {  // 0x402425: from the current volume (reg 0x89)
        const int cur16 = g.r16(0x89);
        int lo = cur16, hi = target, mode = 0;
        if (cur16 > target) {
            lo = target;
            hi = cur16;
            mode = 0x40;
        }
        const int diff = hi - lo;
        if (diff == 0) return;
        if (diff < 0x1000) {  // less than one exponent step: set directly
            g.w16(0x09, target);
            return;
        }
        lo = std::max(lo, 0x1000);
        hi = std::min(hi, 0xfc00);
        g.w8(0x07, lo >> 8);
        g.w8(0x08, hi >> 8);
        g.w8(0x0d, mode);
        g.w8(0x0d, mode);
    }

    // ---- 0x4019ee: ~6848 * 2^(-pitch / 3072) from four nibble tables (64-bit products, >> 15, low 32 bits)
    uint32_t freq(int32_t pitch) const {
        const uint32_t x = uint32_t(pitch) + 0x8000;
        uint64_t f = ft[3][(x >> 12) & 15];
        f = ((f * ft[2][(x >> 8) & 15]) >> 15) & 0xffffffffu;
        f = ((f * ft[1][(x >> 4) & 15]) >> 15) & 0xffffffffu;
        f = ((f * ft[0][x & 15]) >> 15) & 0xffffffffu;
        return uint32_t(f);
    }

    int32_t clampPitch(int64_t p) const {  // 0x401c02
        if (!(p > lowpitch)) p = lowpitch;
        if (!(p < highpitch)) p = highpitch;
        return int32_t(p);
    }

    // ---- 0x401e9e player tick
    void playerTick() {
        const uint32_t n = channels;
        newrow = 0;
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch[i];
            c.fvol = c.vol;
            c.fpan = c.pan;
            c.fpitch = c.pitch;
        }
        tick = (tick + 1) & 0xff;
        if (tick == speed) {
            tick = 0;
            if (patdelay) {
                patdelay = (patdelay - 1) & 0xff;
            } else {
                newrow = 1;
                row++;
                bool newpat = true;
                if (jumporder == kNone) {
                    if (row < patrows) {
                        newpat = false;
                    } else {
                        jumporder = order + 1;
                        jumprow = 0;
                    }
                }
                if (newpat) {
                    // (a change of order clears bytes +0x63 / +0x64 of every channel: never read)
                    order = jumporder < ordnum ? jumporder : restart;
                    row = jumprow;
                    jumporder = kNone;
                    uint32_t p = u32(0x350 + 4 * u8(0x50 + (order & 0xff)));
                    patrows = u32(p);
                    p += 4;
                    for (; jumprow; jumprow--) {  // skip rows
                        for (;;) {
                            const uint32_t f = u8(p++);
                            if (!f) break;
                            if (f & 0x20) p += 2;
                            if (f & 0x40) p += 1;
                            if (f & 0x80) p += 2;
                        }
                    }
                    patpos = p;
                }
                readRow();
            }
        }
        // 0x402018: the effects of every tick, then the GF1 values
        for (uint32_t i = 0; i < n; i++) {
            Chan& c = ch[i];
            effect(0x78 + c.volcmd, c);
            if (c.eff < 0x34) effect(0x34 + c.eff, c);
            if (c.insp) volumeEnvelope(c);
            if (c.fvol > 128) throw std::runtime_error("music: volume out of range");
            c.gvol = int(voltab[2 * c.fvol]);
            c.gpan = int(c.fpan >> 4);
            c.gfreq = int((uint64_t(freq(c.fpitch)) * voices / kFreqDiv) & 0xfffe);
        }
    }

    void readRow() {
        uint32_t p = patpos;
        for (uint32_t i = 0; i < channels; i++) {
            Chan& c = ch[i];
            nNote = nIns = nVol = nEff = nPar = 0;
            c.eff = 0xff;
            const uint32_t f = u8(p);
            if (f == 0 || (f & 0x1f) != i) continue;  // no entry for this channel: nothing else runs
            p++;
            if (f & 0x20) {
                nNote = u8(p);
                nIns = u8(p + 1);
                p += 2;
            }
            if (f & 0x40) nVol = u8(p++);
            if (f & 0x80) {
                nEff = u8(p);
                nPar = u8(p + 1);
                p += 2;
            }
            nFlag24 = 0;  // +0x24: never set to 1 in this build
            newNote(c);
            const uint32_t v = nVol;
            nVol &= 0x0f;
            c.volcmd = v >> 4;
            effect(0x68 + (v >> 4), c);  // volume column, row tick
            if (nEff < 0x34) {
                c.eff = nEff;
                effect(nEff, c);  // effect, row tick
            }
        }
        patpos = p + 1;
    }

    // ---- 0x401a47 new note (row tick, per channel with an entry)
    void newNote(Chan& c) {
        const bool porta = nEff == 3 || nEff == 5 || nVol >= 0xf0;  // +0x57
        if (nNote == 0x61) {  // key off (no K00 and no "no envelope: cut" in this build)
            c.keyon = 0;
            nNote = 0;
        }
        if (nIns != 0 && nIns <= insnum) c.ins = nIns;
        if (c.ins == 0) return;
        if (nNote != 0) {
            if (nIns != 0) c.keyon = 1;
            c.note = nNote;
            if (nEff == 0x31 && nPar != 0) return;  // EDx (note delay): the note is dropped
            const uint32_t note = nNote - 1;
            if (porta) {
                c.target = s16(0x3000 - ((note << 8) + c.rel));  // 0x401b84: only the target
            } else {
                c.stop = 1;
                const uint32_t insp = insPtr(c.ins);
                const uint32_t smp = u8(insp + 4 + note);
                if (smp >= u32(insp)) return;
                const uint32_t h = insp + 0x100 + 16 * smp;
                c.active = 1;
                c.newsmp = 1;
                c.start = u24(h);
                c.loop = u24(h + 3);
                c.end = u24(h + 6);
                c.mode = int(u8(h + 9));
                if (nIns != 0) {
                    c.insp = insp;
                    c.svol = u8(h + 0xa);
                    c.span = u8(h + 0xb);
                }
                c.rel = u16(h + 0xc);
                c.pitch = c.fpitch = c.target = s16(0x3000 - ((note << 8) + c.rel));
                uint32_t ofs = 0;
                if (nEff == 9) {  // 9xx: sample offset xx * 256 address units (with memory)
                    if (nPar) c.ofsmem = nPar;
                    ofs = c.ofsmem << 8;
                }
                c.offset = ofs;
                c.vibpos = 0;  // +0x5c; also +0x60, +0x65, +0x6a, +0x70
                c.arpidx = 0;
                c.rtcount = 0;
            }
        }
        // 0x401b97: an instrument resets volume, panning, fade and envelope
        if (c.keyon && nIns != 0) {
            if (nFlag24 != 1) {
                c.vol = c.fvol = c.svol;
                if (!(opt & 2)) c.pan = c.fpan = c.span;
            }
            c.fade = 0x8000;
            c.envpt = 0;
            c.envpos = 0;
        }
    }

    // ---- 0x40203c..0x4020e2: master and global volume, fadeout, volume envelope (stepped per tick)
    void volumeEnvelope(Chan& c) {
        const uint32_t ip = c.insp;
        uint32_t v = ((c.fvol * mastervol) >> 6) & 0xff;
        v = (v * globalvol) & 0xffff;
        v = ((uint32_t((uint64_t(v) * uint32_t(c.fade)) >> 16)) >> 4) & 0xff;
        c.fvol = v;
        if (!c.keyon) c.fade = std::max<int32_t>(c.fade - int32_t(u16(ip + 0x64)), 0);
        uint32_t pt = c.envpt;
        if (c.envpos != 0 && (pt & 0xff) == u8(ip + 0x6d)) {
            pt = (pt & ~0xffu) | u8(ip + 0x6c);
            c.envpt = pt;
        }
        const uint32_t e = ip + 0x6e + 4 * pt;
        if ((pt & 0xff) == u8(ip + 0x6a)) {
            c.fvol = ((u8(e + 2) * c.fvol) >> 6) & 0xff;
            return;
        }
        const uint32_t ln = u16(e);
        const int32_t v0 = s16(u16(e + 2)), v1 = s16(u16(e + 6));
        const int32_t num = s16(uint32_t(v1 - v0)) * s16(c.envpos);
        const int32_t den = s16(ln);
        if (den == 0) throw std::runtime_error("music: envelope segment of length 0");
        const int32_t q = num / den;  // idiv: truncates toward zero
        const uint32_t val = uint32_t(q + int32_t(u8(e + 2))) & 0xff;
        c.fvol = ((val * c.fvol) >> 6) & 0xff;
        uint32_t pos = c.envpos;
        if (!(pos == 0 && c.keyon && (c.envpt & 0xff) == u8(ip + 0x6b))) {
            pos++;
            if (pos >= ln) {
                pos = 0;
                c.envpt++;
            }
        }
        c.envpos = pos & 0xffff;
    }

    // ---- 0x401e88: effect dispatch (0x00..0x33 row, 0x34..0x67 every tick, 0x68..0x77 / 0x78..0x87 volume column
    // on the row / every tick)
    void effect(uint32_t idx, Chan& c) {
        if (idx >= uint32_t(kJumpEntries)) throw std::runtime_error("music: effect index out of range");
        switch (fx[idx]) {
        case kRet: break;
        case kArpRow:  // 0x401c6b: 0xy (0x00 also for "no effect")
            if (nPar == 0) c.eff = 0xff;
            c.arp[0] = 0;
            c.arp[1] = nPar >> 4;
            c.arp[2] = nPar & 15;
            break;
        case kArpTick:  // 0x401c88: every tick, the row tick too; a step counter of its own
            c.fpitch = clampPitch(int64_t(c.fpitch) - (int64_t(c.arp[c.arpidx]) << 8));
            c.arpidx = (c.arpidx + 1) & 0xff;
            if (c.arpidx == 3) c.arpidx = 0;
            break;
        case kPupRow:  // 0x401cec: 1xx memory
            if (nPar) c.pupspeed = int32_t(nPar << 4);
            break;
        case kPdnRow:  // 0x401cfb: 2xx memory
            if (nPar) c.pdnspeed = int32_t(nPar << 4);
            break;
        case kPupTick:  // 0x401d0a: not on the row tick
            if (!newrow) c.pitch = c.fpitch = clampPitch(int64_t(c.pitch) - c.pupspeed);
            break;
        case kPdnTick:  // 0x401d22
            if (!newrow) c.pitch = c.fpitch = clampPitch(int64_t(c.pitch) + c.pdnspeed);
            break;
        case kTpRow:  // 0x401d3a: 3xx memory
            if (nPar) c.tpspeed = int32_t(nPar << 4);
            break;
        case kTpTick: {  // 0x401d49: slide the pitch to the target, not on the row tick; pitch = fpitch always
            int64_t p = c.pitch;
            if (!newrow && p != c.target) {
                if (p > c.target) {
                    p -= c.tpspeed;
                    if (p < c.target) p = c.target;
                } else {
                    p += c.tpspeed;
                    if (p > c.target) p = c.target;
                }
            }
            c.pitch = c.fpitch = int32_t(uint32_t(p));
            break;
        }
        case kVibRow:  // 0x401d78: 4xy, x / y = 0 keep the memory
            if (nPar & 0x0f) c.vibdepth = ((nPar & 0x0f) << 2) & 0xff;
            if (nPar & 0xf0) c.vibspeed = (nPar & 0xf0) >> 2;
            break;
        case kVibTick: {  // 0x401d93: every tick; the position moves after the row tick
            const int32_t t = vibtab[c.vibpos & 0xff];        // movsx eax, byte [table + pos]
            const uint32_t prod = uint32_t(t * s8(c.vibdepth)) & 0xffff;  // imul byte: AX only
            const uint32_t eax = t >= 0 ? prod : (0xffff0000u | prod);  // the upper half: the table byte's sign
            const int32_t delta = int32_t(eax) >> 3;          // sar eax, 3
            c.fpitch = clampPitch(int64_t(int32_t(uint32_t(delta) + uint32_t(c.fpitch))));
            if (!newrow) c.vibpos = (c.vibpos + c.vibspeed) & 0xff;
            break;
        }
        case kVslideRow:  // 0x401dbd: 5xy / 6xy / Axy memory
            if (nPar) c.vslide = nPar;
            break;
        case kVslideTick: {  // 0x401dc8: Axy, not on the row tick
            uint32_t al = c.vol;
            const uint32_t bl = c.vslide;
            if (!newrow) {
                if (bl & 0xf0) al = std::min(al + (bl >> 4), 0x40u);
                else al = al >= bl ? al - bl : 0;
            }
            c.vol = c.fvol = al;
            break;
        }
        case kPan: c.pan = c.fpan = nPar; break;                       // 0x401c51: 8xx
        case kVol: c.vol = c.fvol = std::min(nPar, 0x40u); break;      // 0x401c41: Cxx
        case kSpeed:                                                   // 0x401c22: Fxx
            if (nPar >= 0x20) period = kTempoK / nPar;
            else if (nPar) speed = nPar;
            break;
        case kRetrigRow:  // 0x401df3: Rxy (x = volume change, y = interval)
            if (nPar) {
                c.rtint = nPar & 15;
                c.rtvol = nPar >> 4;
                c.rtcount = 0;
            }
            break;
        case kRetrigTick: {  // 0x401e0c: every tick, the row tick too
            const uint32_t al0 = c.rtcount;
            c.rtcount = (c.rtcount + 1) & 0xff;
            if (s8(al0) < s8(c.rtint)) break;
            c.rtcount = 0;
            c.offset = 0;  // restart the sample (no stop / ramp first)
            const uint32_t bl = c.rtvol;
            if (!(bl & 7)) break;
            const uint32_t ah = ((0x80u << (bl & 7)) | (0x80u >> (8 - (bl & 7)))) & 0xff;  // rol ah, cl
            uint32_t al = c.vol;
            if (!(bl & 8)) {
                if (bl <= 5) al = (al - ah) & 0xff;
                if (bl == 6) al = ((al + (al >> 2)) & 0xff) >> 1;
                if (bl == 7) al >>= 1;
            } else {
                if (bl <= 13) al = (al + ah) & 0xff;
                if (bl == 14) al = (al + (al >> 1)) & 0xff;
                if (bl == 15) {
                    al = (al << 1) & 0xff;
                    if (al & 0x80) al -= 1;
                }
            }
            if (s8(al) < 0) al = 0;
            if (al > 0x40) al = 0x40;
            c.vol = c.fvol = al;
            break;
        }
        // volume column 0x10..0x50 (0x401cae..0x401cbe: x + 0x10 per step down the chain; 0x5x is 0x40)
        case kVcVol5: nVol = 0x10; [[fallthrough]];
        case kVcVol4: nVol = (nVol + 0x10) & 0xff; [[fallthrough]];
        case kVcVol3: nVol = (nVol + 0x10) & 0xff; [[fallthrough]];
        case kVcVol2: nVol = (nVol + 0x10) & 0xff; [[fallthrough]];
        case kVcVol1: c.vol = c.fvol = nVol; break;
        case kVcFineDn: c.vol = c.fvol = c.vol >= nVol ? c.vol - nVol : 0; break;     // 0x401cdb: 0x8x
        case kVcFineUp: c.vol = c.fvol = std::min((c.vol + nVol) & 0xff, 0x40u); break;  // 0x401cc8: 0x9x
        case kVcPan: c.pan = c.fpan = ((nVol << 4) | nVol) & 0xff; break;            // 0x401c5b: 0xCx
        }
    }
};

Player::Player(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card)
    : p_(std::make_unique<Impl>(module, image, card)) {}
Player::~Player() = default;

void Player::init() { p_->init(); }
void Player::play(int order) { p_->play(uint32_t(order)); }
void Player::setVolume(int v) { p_->mastervol = uint32_t(v) & 0xff; }
void Player::stop() { p_->gf1Reset(14); }
bool Player::irq() { return p_->irq(); }
int Player::pos() const { return int((p_->order & 0xff) << 8 | (p_->row & 0xff)); }
uint32_t Player::timer() const { return p_->timer; }
uint32_t Player::cur() const { return p_->cur; }

}  // namespace mxm

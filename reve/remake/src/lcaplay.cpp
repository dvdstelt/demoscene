// LCA's MOD-Player V3.03 (GUS path) as REVE.LFL links it: card detection, module conversion and sample upload,
// start / stop, and the timer IRQ that plays the song (reve/re/notes-music.md sections 2-6; a routine-by-routine
// port that mirrors reve/re/models/music/lcaplay.py; started from STARS's port of V3.02, the V3.03 differences are
// marked). The player writes the GUS through gus::Card; the card's timers decide when it ticks. See lcaplay.h for
// the routine map.
#include "lcaplay.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace lca {
namespace {

constexpr int kDma = 3, kIrq = 7;        // ULTRASND=240,3,3,7,7 (the capture's environment)
constexpr uint32_t kPtab = 0x26db4c;     // 16 finetunes x 36 periods (u16), finetune 0 first; 8..15 = -8..-1
constexpr uint32_t kSine = 0x26dfcc;     // 32 bytes: vibrato / tremolo sine (half wave)
constexpr uint32_t kVolTab = 0x26e060;   // 65 u16: volume 0..64 -> GF1 volume (the 8-bit ramp regs take the high byte)
constexpr uint32_t kPanTab = 0x26e03f;   // 32 bytes: pan of voice 0..31
constexpr uint32_t kDivTab = 0x26e0e2;   // u16 per voice count 14..32: 617400 / voices (truncated)
constexpr uint32_t kIrqLatch = 0x26e027, kDmaLatch = 0x26e037;  // IRQ / DMA number -> latch code
constexpr uint32_t kVoices0 = 0x26e012;  // [0x26e012]: GF1 voices before the conversion (14)
constexpr uint32_t kFcNum = 0x26e8f3;    // immediate of 0x26e8f2 `mov eax, 0xda7a69e2` (3,579,545.5 Hz x 1024)
constexpr int kFcFirst = 55, kFcLast = 4000;  // 0x26e8e6 / 0x26e8eb: periods with an FC (0x26e8ba's loop)
constexpr int kOrderTab = 128;           // the standard MOD's order table (signature at 0x438)
constexpr int kExtraChannels = 0;        // BL of main's call of 0x25f698 -> [0x26040e]: no sound-effect channels

uint32_t s32cmp(uint32_t v) { return v ^ 0x80000000u; }  // signed comparison of u32 values via unsigned

class Image {
public:
    explicit Image(std::span<const uint8_t> b) : b_(b) {}
    uint8_t u8(uint32_t a) const {
        if (a < kImageBase || a - kImageBase >= b_.size()) throw std::runtime_error("REVE.LFL: read outside the image");
        return b_[a - kImageBase];
    }
    uint32_t u16(uint32_t a) const { return uint32_t(u8(a) | u8(a + 1) << 8); }
    uint32_t u32(uint32_t a) const { return u16(a) | u16(a + 2) << 16; }

private:
    std::span<const uint8_t> b_;
};

// sample record, 0x1c bytes at 0x26c3cc + 0x1c * (n - 1) (V3.02: 0x18; V3.03 adds +0x18, the rate of a
// sound-effect sample, unused here)
struct Sample {
    uint32_t len = 0;    // +0x00 length in bytes; after the upload: DRAM address of the end
    int ft = 0;          // +0x04 finetune 0..15
    int vol = 0;         // +0x05 volume (not clamped here)
    int flag = 0;        // +0x06 GF1 voice control: 8 = loop, 0 = one-shot
    uint32_t ls = 0;     // +0x08 loop start; after the upload: DRAM address
    uint32_t le = 0;     // +0x0c loop end = min(ls + replen, len); after the upload: DRAM address
    uint32_t dram = 0;   // +0x10 DRAM start
    uint32_t lerel = 0;  // +0x14 loop end relative to the start (the 9xx limit)
};

// channel record, 0x34 bytes at 0x26d4cc + 0x34 * ch
struct Chan {
    uint32_t start = 0;   // +0x04 DRAM address the next note starts at
    int smp = 0;          // +0x08 sample record (0: none yet)
    uint32_t period = 0;  // +0x10
    uint32_t target = 0;  // +0x14 tone portamento target (0 = reached)
    uint32_t fc = 0;      // +0x18 FC to write (0 = nothing pending)
    uint32_t eff = 0;     // +0x20 effect << 8 | parameter, for the ticks
    int ins = 0;          // +0x22
    int vol = 0;          // +0x23 volume 0..64
    int ft = 0;           // +0x24 finetune
    int arp = 0;          // +0x25 arpeggio counter
    int ctl = 0;          // +0x26 voice control of the sample (8 loop / 0)
    int gvol = 0;         // +0x27 volume last sent to the GF1
    int pspeed = 0;       // +0x28 tone portamento speed
    int voice = 0;        // +0x29 GF1 voice: 2 ch or 2 ch + 1 (toggled at every note start)
    int vib = 0;          // +0x2a vibrato speed << 4 | depth
    int trem = 0;         // +0x2b tremolo speed << 4 | depth
    int vibpos = 0;       // +0x2c
    int trempos = 0;      // +0x2d
    int wave = 0;         // +0x2e vibrato waveform (bits 0-1), tremolo waveform (bits 4-5)
    int tbase = 0;        // +0x2f tremolo base volume
    int retrig = 0;       // +0x30 E9x counter
    int gliss = 0;        // +0x31 E3x
    int newsmp = 0;       // +0x32 notes left that write the loop registers (2 after an instrument number)
};

struct Event {
    int ins;        // instrument (0: none)
    uint32_t per;   // Amiga period (0: none)
    uint32_t eff;   // effect << 8 | parameter
};

class Player {
public:
    Player(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card)
        : m_(module), img_(image), c_(card), voices_(img_.u8(kVoices0)) {
        if (voices_ < 14 || voices_ > 32) throw std::runtime_error("REVE.LFL: unexpected voice count");
    }

    // ---- start-up ----

    // 0x25f698 (AL = 3 options, BL = 0 extra channels; [0x26e10c] = 45000, the MIX: rate, so the cards are looked
    // for): GUS detection 0x25f75b (ULTRASND= read by 0x25f793, no hardware probe), GF1 init 0x25f8d7 with the IRQ /
    // DMA latches, no ULTRA16= (0x25f819). V3.03: the SB probe 0x25fbc6 runs only when there is no GUS.
    void detect() {
        c_.w8(0x4c, 0);
        c_.w8(0x4c, 1);
        c_.port(gus::kBase + 0x100, 3);  // MIDI control: reset
        c_.port(gus::kBase + 0x100, 0);
        c_.w8(0x41, 0);
        c_.w8(0x45, 0);
        c_.w8(0x49, 0);
        c_.w8(0x0e, (voices_ - 1) | 0xc0);  // 14 voices still
        for (int v = 31; v >= 0; v--) {
            c_.select(v);
            c_.w8(0x00, 3);
            c_.w8(0x0d, 3);
        }
        c_.w8(0x4c, 7);
        for (int v = 31; v >= 0; v--) {
            c_.select(v);
            for (int r = 2; r <= 5; r++) c_.w16(r, 0);
            c_.w8(0x06, 0x3f);
            c_.w16(0x09, 0);
            c_.w16(0x0a, 0);
            c_.w16(0x0b, 0);
            c_.w8(0x0c, 7);
        }
        // IRQ 7 and DMA 3 for both channels -> code | 0x40
        const int irq = img_.u8(kIrqLatch + kIrq) | 0x40;
        const int dma = img_.u8(kDmaLatch + kDma) | 0x40;
        const int b = gus::kBase;
        c_.port(b + 0xf, 5);
        c_.port(b, 0xb);
        c_.port(b + 0xb, 0);
        c_.port(b + 0xf, 0);
        c_.port(b, 0xb);
        c_.port(b + 0xb, dma | 0x80);
        c_.port(b, 0x4b);
        c_.port(b + 0xb, irq);
        c_.port(b, 0xb);
        c_.port(b + 0xb, dma);
        c_.port(b, 0x4b);
        c_.port(b + 0xb, irq);
        c_.select(0);
        c_.port(b, 0xb);
        c_.select(0);
        c_.port(b, 8);
    }

    // 0x26e16c (called by the load entry 0x2601dc; V3.02 had this standard-MOD converter, 0x19dd0, without a caller):
    // channels, samples, orders, patterns -> 6-byte events, upload, FC table, player state
    void convert() {
        if (m_.size() < 0x43c) throw std::runtime_error("Reve music: module too short");
        int al = m_[0x438], ah = m_[0x439];
        if (al == 0x4d) al = 0x30, ah = 0x34;  // 'M.K.'
        if (ah >= 0x30 && ah <= 0x39) std::swap(al, ah);
        if (al < 0x30 || al > 0x39) throw std::runtime_error("Reve music: unexpected module signature");
        if (!(ah >= 0x30 && ah < 0x39)) ah = 0x30;
        nch_ = ((ah - 0x30) * 10 + (al - 0x30)) & 0xff;  // '8CHN' -> 8
        if (nch_ < 1 || nch_ > 16) throw std::runtime_error("Reve music: unexpected channel count");
        if (nch_ > 7) voices_ = 2 * nch_;
        uint32_t o = 0x14;
        smp_.assign(32, Sample{});
        for (int n = 1; n <= 31; n++, o += 0x1e) {  // 0x26e26a
            Sample& s = smp_[size_t(n)];
            const uint32_t ln = be16(o + 0x16), ls = be16(o + 0x1a), lr = be16(o + 0x1c);
            s.len = 2 * ln;
            s.ft = m_[o + 0x18];
            s.vol = m_[o + 0x19];
            s.ls = 2 * ls;
            s.flag = 2 * lr > 2 ? 8 : 0;
            s.le = std::min(2 * ls + 2 * lr, s.len);
        }
        songlen_ = m_[o];
        restart_ = m_[o + 1] <= m_[o] ? m_[o + 1] : 0;
        ordtab_.assign(m_.begin() + o + 2, m_.begin() + o + 2 + kOrderTab);
        const int npat = *std::max_element(ordtab_.begin(), ordtab_.end()) + 1;
        o += 2 + kOrderTab + 4;  // (+4: the signature)
        stride_ = 6 * nch_;      // [0x260414]
        const size_t cells = size_t(npat) * 64 * size_t(nch_);
        if (m_.size() < o + 4 * cells) throw std::runtime_error("Reve music: module too short for its patterns");
        events_.resize(cells);
        for (size_t i = 0; i < cells; i++, o += 4) {  // 0x26e31b
            const int b0 = m_[o], b1 = m_[o + 1], b2 = m_[o + 2], b3 = m_[o + 3];
            events_[i] = {(b0 & 0xf0) | (b2 >> 4), uint32_t((b0 & 0xf) << 8 | b1), uint32_t((b2 & 0xf) << 8 | b3)};
        }
        // 0x26e365..0x26e3fc: mix control 0xb (output off), then per sample the DMA blocks and the loop tail by PIO
        c_.port(gus::kBase, 0xb);
        uint32_t edi = 0;
        for (int n = 1; n <= 31; n++) {
            Sample& s = smp_[size_t(n)];
            s.dram = edi;
            if (s.len) {
                if (edi >> 18 != (edi + s.len) >> 18) {  // a sample must not cross a 256 KB bank
                    edi = (edi + s.len) >> 18 << 18;
                    s.dram = edi;
                }
                edi = upload(o, edi, s.len);
                const uint32_t tail = s.le > 0 && s.le <= 16 ? s.le : 16;
                edi = upload(o + s.ls, edi, tail);  // the bytes from the loop start, after the sample
                o += s.len;
                if (s.flag == 0) s.le = s.len = s.len - 1;  // 0x26e3cc: a one-shot ends on its last byte
            }
            s.lerel = s.le;
            s.len += s.dram;
            s.le += s.dram;
            s.ls += s.dram;
            edi = (edi + 0x1f) & ~0x1fu;
        }
        // V3.03 0x26e402: channels = module + extra ones; voices = 2 x channels if more than 7, else 14
        allch_ = (kExtraChannels + nch_) & 0xff;
        voices_ = allch_ > 7 ? 2 * allch_ : 14;
        if (voices_ > 32) throw std::runtime_error("Reve music: too many channels");
        // 0x26e8ba: period -> FC table
        const uint32_t div = img_.u16(kDivTab + 2 * uint32_t(voices_ - 14));
        const uint32_t num = img_.u32(kFcNum);
        if (!div) throw std::runtime_error("REVE.LFL: no FC divisor");
        freq_.assign(kFcLast + 1, 0);
        for (uint32_t p = kFcFirst; p <= uint32_t(kFcLast); p++) {
            const uint32_t q = (num / p + div) / div;  // `add edx, edx; adc eax, 0` never carries
            freq_[p] = q & ~1u;
        }
        // 0x26e427: channel records; 0x26e464: pattern pointers; player state
        for (int i = 0; i < 32; i++) {
            ch_[i] = Chan{};
            ch_[i].voice = 2 * i;
        }
        row_ = 0;
        order_ = 0;
        counter_ = 1;
        speed_ = 6;
        loopcnt_ = 0;
        looprow_ = looporder_ = 0;
        loopptr_ = 0;
        tempo(125);  // 0x26e4aa: timer 1, count 6 (only stored)
        ptr_ = patptr(ordtab_[0]);
    }

    // 0x26ec3e (= V3.02 0x1a91c)
    void start() {
        gf1Reset();
        c_.port(gus::kBase + 8, 4);
        c_.port(gus::kBase + 9, 0xe0);  // AdLib timer control: reset the flags, mask
        c_.w8(0x4c, 7);
        c_.port(gus::kBase + 8, 4);
        c_.port(gus::kBase + 9, 3);     // start timer 1 and timer 2 (t_s)
        c_.w8(0x45, 0);
        c_.w8(0x46, 6);                 // timer 1: (256 - 6) x 80 us = 20 ms (125 BPM)
        c_.w8(0x45, 4);                 // timer 1 IRQ only
        c_.port(gus::kBase, 9);         // mix control: line out on, latches on
    }

    // 0x26ecc4 (= V3.02 0x1a9a2)
    void stop() {
        c_.port(gus::kBase, 0xb);
        c_.port(gus::kBase, 0xb);
        c_.port(gus::kBase + 8, 4);
        c_.port(gus::kBase + 9, 0xe0);
        c_.w8(0x45, 0);
        gf1Reset();
    }

    // 0x26eecc: GUS timer IRQ. V3.03: counter 0 -> row, 0xff -> stuck (after F00), anything else -> tick effects
    // (V3.02: `jle` -> row unless negative -> stuck)
    void tick() {
        c_.w8(0x45, 0);
        c_.w8(0x45, tctl_);  // acknowledge, re-enable
        counter_ = (counter_ - 1) & 0xff;
        if (counter_ == 0) {
            rowTick();
        } else if (counter_ == 0xff) {
            counter_ = 0;  // 0x26fc89
        } else {
            for (int i = 0; i < allch_; i++) tickEffect(ch_[i]);
        }
    }

    // 0x2602eb returns AL = order, AH = row; the demo's timer interrupt swaps them
    int position() const { return order_ << 8 | row_; }

private:
    uint32_t be16(uint32_t a) const { return uint32_t(m_[a] << 8 | m_[a + 1]); }
    uint32_t patptr(int p) const { return uint32_t(p) * 64 * uint32_t(stride_); }  // 0x26d0cc[128]
    uint32_t pt(uint32_t i) const { return img_.u16(kPtab + 2 * i); }

    // 0x26e9bb (ESI source, EDI DRAM address, ECX bytes): PIO 0x26ea91 for up to 20 bytes, else DMA 0x26eac8 straight
    // from the module's memory (only sources at or above [0x260424] = 16 MB would go through the bounce buffer), in
    // blocks that do not cross a 64 KB page of the source's physical address: at a page end the block is split, the
    // DRAM address is brought to a multiple of 32 by PIO-ing up to 31 bytes, and the rest goes by DMA again. Every
    // DMA block starts at the DRAM address rounded up to 32 bytes. Returns the DRAM address after the data.
    uint32_t upload(uint32_t src, uint32_t edi, uint32_t n) {
        if (src > m_.size() || n > m_.size() - src) throw std::runtime_error("Reve music: sample outside the module");
        if (n <= 0x14) {
            c_.upload(edi, m_.data() + src, n);
            return edi + n;
        }
        while (n) {
            const uint32_t phys = kModuleAddr + src;
            const uint32_t k = std::min(n, 0x10000u - (phys & 0xffff));  // to the end of the source's 64 KB page
            const uint32_t a = (edi + 0x1f) >> 5 << 1;  // DRAM address >> 4, rounded up to 32 bytes
            c_.w16(0x42, int(a));
            c_.w8(0x41, 0x21);  // start, 8-bit channel, IRQ at the end
            const uint32_t d = a << 4;
            c_.upload(d, m_.data() + src, k);
            edi = d + k;
            src += k;
            n -= k;
            if (n && (edi & 0x1f)) {  // split: realign the DRAM address by PIO
                const uint32_t j = std::min(32 - (edi & 0x1f), n);
                c_.upload(edi, m_.data() + src, j);
                edi += j;
                src += j;
                n -= j;
            }
        }
        return edi;
    }

    // 0x26ed0e (= V3.02 0x1a9ec)
    void gf1Reset() {
        c_.w8(0x4c, 0);
        c_.w8(0x4c, 0);
        c_.w8(0x4c, 1);
        c_.w8(0x41, 0);
        c_.w8(0x45, 0);
        c_.w8(0x49, 0);
        c_.w8(0x0e, (voices_ - 1) | 0xc0);  // 16 voices: 617400 / 16 = 38,587.5 Hz
        for (int v = 0; v < voices_; v++) {
            c_.select(v);
            c_.w8(0x00, 3);
            c_.w8(0x00, 3);
            c_.w8(0x06, 0x3f);
            c_.w8(0x06, 0x3f);
            c_.w16(0x09, int(vol(0)));
            c_.w8(0x0d, 3);
            c_.w8(0x0d, 3);
            c_.w8(0x0c, img_.u8(kPanTab + uint32_t(v)));
            c_.w8(0x0c, img_.u8(kPanTab + uint32_t(v)));
        }
        c_.w8(0x4c, 7);
    }

    // 0x26e4aa / 0x26f9c8: tick period 2.5 s / bpm in timer steps, truncated (twice for timer 2)
    void tempo(int bpm) {
        uint32_t n = 1000000u / uint32_t(bpm * 32);  // 80 us steps
        if (n >> 8) {                                // too long for timer 1: timer 2, 320 us steps
            n >>= 2;
            treg_ = 0x47;
            tctl_ = 8;
        } else {
            treg_ = 0x46;
            tctl_ = 4;
        }
        tcount_ = int((0u - n) & 0xff);  // [0x26e00f] reg, [0x26e010] reg 0x45 value, [0x26e011] count
    }

    uint32_t vol(int v) const { return img_.u16(kVolTab + 2 * uint32_t(v)); }
    const Sample& smp(const Chan& ch) const { return smp_[size_t(ch.smp)]; }

    // ---- helpers ----

    // period -> the finetune's table: the first i with period >= the finetune-0 row's entry (0x26f622 and copies)
    uint32_t snap(uint32_t ax, int ft) const {
        ax &= 0xffff;
        uint32_t i = 0;
        while (i < 36 && ax < pt(i)) i++;
        return pt(uint32_t(ft) * 36 + i);
    }

    uint32_t fcOf(uint32_t p) const {
        // the table at 0x268544 covers 0..4000; the song stays inside it (lcaplay.py checks it), the original reads
        // whatever follows for other periods
        return p <= uint32_t(kFcLast) ? freq_[p] : 0;
    }

    // 0x26fb7c / 0x26f4ae: ramp from the last sent volume to the channel volume
    void volUpdate(Chan& ch) {
        if (ch.gvol == ch.vol) return;
        const uint32_t a = vol(ch.gvol), b = vol(ch.vol);
        if (a < b) {
            c_.w16(7, int(a));
            c_.w16(8, int(b));
            c_.w8(0x0d, 0);
        } else {
            c_.w16(7, int(b));
            c_.w16(8, int(a));
            c_.w8(0x0d, 0x40);
        }
        ch.gvol = ch.vol;
    }

    // 0x26fbd8 / 0x26f502: frequency, then the voice control (starts the voice)
    void fcUpdate(Chan& ch) {
        if (!ch.fc) return;
        const uint32_t fc = ch.fc;
        ch.fc = 0;
        c_.w16(1, int(fc));
        c_.w8(0, ch.ctl);
    }

    // 0x26fa68 / 0x26f2e3: ramp the old voice down, switch to the other voice of the pair, set its position
    void noteStart(Chan& ch) {
        c_.w16(7, int(vol(0)));
        c_.w16(8, int(vol(ch.vol)));
        c_.w8(0x0d, 0x40);
        ch.gvol = 0;
        ch.voice ^= 1;
        c_.select(ch.voice);
        if (ch.newsmp) {
            ch.newsmp--;
            const Sample& s = smp(ch);
            if (ch.ctl) {
                c_.w16(2, int(s.ls >> 7));
                c_.w16(3, int(s.ls << 9));
            }
            c_.w16(4, int(s.le >> 7));
            c_.w16(5, int(s.le << 9));
        }
        c_.w16(0x0b, int(ch.start << 9));
        c_.w16(0x0b, int(ch.start << 9));
        c_.w16(0x0a, int(ch.start >> 7));
        c_.w16(0x0a, int(ch.start >> 7));
    }

    // 0x26f1dc
    static void volslide(Chan& ch, int p, int maxvol) {
        if (!(p & 0xf)) {
            ch.vol = (ch.vol + (p >> 4)) & 0xff;
            if (ch.vol > maxvol) ch.vol = maxvol;
        } else {
            const int v = (ch.vol - (p & 0xf)) & 0xff;
            ch.vol = v & 0x80 ? 0 : v;
        }
    }

    // 0x26f004
    void toneporta(Chan& ch) const {
        const uint32_t spd = uint32_t(ch.pspeed), t = ch.target;
        uint32_t a = ch.period;
        if (a > t) {
            a -= spd;
            if (a & 0x80000000u) {
                ch.period = t;
                ch.target = 0;
                return;
            }
            ch.period = a;
            if (s32cmp(a) > s32cmp(t)) return gliss(ch);
        } else if (a < t) {
            a += spd;
            ch.period = a;
            if (s32cmp(a) < s32cmp(t)) return gliss(ch);
        }
        ch.period = t;
        ch.target = 0;
    }

    // 0x26f044 (E3x on): the period is rounded to the semitone table in place
    void gliss(Chan& ch) const {
        if (ch.gliss) ch.period = snap(ch.period, ch.ft);
    }

    // waveform value at phase pos (0x26f0a0 / 0x26f130): sine, ramp, square
    int wave(int pos, int w) const {
        const int idx = (pos >> 2) & 0x1f;
        if (w == 0) return img_.u8(kSine + uint32_t(idx));
        if (w == 1) {
            const int m = (idx << 3) & 0xff;
            return pos & 0x80 ? -m & 0xff : m;
        }
        return 0xff;
    }

    // 0x26f0a0: returns period + vibrato (the period itself is not changed)
    uint32_t vibrato(Chan& ch) const {
        const int pos = ch.vibpos;
        int d = ((ch.vib & 0xf) * wave(pos, ch.wave & 3)) >> 7;
        if (pos & 0x80) d = -d;
        ch.vibpos = (pos + ((ch.vib >> 2) & 0x3c)) & 0xff;
        return ch.period + uint32_t(d);
    }

    static int nibbles(int old, int p) {
        if (p & 0xf) old = (old & 0xf0) | (p & 0xf);
        if (p & 0xf0) old = (old & 0x0f) | (p & 0xf0);
        return old;
    }

    // ---- the tick ----

    // 0x26ef18: one channel on a tick between rows (table 0x26ef34)
    void tickEffect(Chan& ch) {
        const uint32_t e = ch.eff;
        if (!e) return;
        const int p = int(e & 0xff), x = int(e >> 8);
        switch (x) {
        case 0: {  // 0x26ef74 arpeggio: counter 0 -> +y, 1 -> +x, 2 -> base
            const int a = ch.arp;
            ch.arp = (a + 1) & 0xff;
            if (a >= 2) {
                ch.arp = 0;
                return outFreq(ch, ch.period);
            }
            const uint32_t n = uint32_t(a == 0 ? p & 0xf : p >> 4);
            const uint32_t b = uint32_t(ch.ft) * 36, dx = ch.period & 0xffff;
            for (uint32_t i = 0; i < 36; i++)
                if (dx >= pt(b + i)) return outFreq(ch, pt(b + i + n));
            return outFreq(ch, pt(b + 35 + n));
        }
        case 1:  // 0x26efd8 portamento up (no limits)
            ch.period -= uint32_t(p);
            return outFreq(ch, ch.period);
        case 2:  // 0x26efe4 portamento down
            ch.period += uint32_t(p);
            return outFreq(ch, ch.period);
        case 3:  // 0x26efec tone portamento
            if (!ch.target) return;
            toneporta(ch);
            return outFreq(ch, ch.period);
        case 4:  // 0x26f078 vibrato
            if (p) ch.vib = nibbles(ch.vib, p);
            return outFreq(ch, vibrato(ch));
        case 5:  // 0x26f0f0 tone portamento + volume slide
            if (!ch.target) {
                volslide(ch, p, kMaxVol);
                return outVol(ch);
            }
            toneporta(ch);
            volslide(ch, p, kMaxVol);
            return outFreq(ch, ch.period);
        case 6: {  // 0x26f114 vibrato + volume slide
            const uint32_t a = vibrato(ch);
            volslide(ch, p, kMaxVol);
            return outFreq(ch, a);
        }
        case 7: {  // 0x26f130 tremolo
            if (p) ch.trem = nibbles(ch.trem, p);
            const int pos = ch.trempos;
            int d = ((ch.trem & 0xf) * wave(pos, (ch.wave >> 4) & 3)) >> 6;
            if (pos & 0x80) d = -d;
            const int v = (d + ch.tbase) & 0xff;
            ch.vol = v & 0x80 ? 0 : std::min(v, kMaxVol);
            ch.trempos = (pos + ((ch.trem >> 2) & 0x3c)) & 0xff;
            return outVol(ch);
        }
        case 9:  // 0x26f1c0: later notes start at the sample start again
            ch.start = smp(ch).dram;
            ch.eff = 0;
            return;
        case 0xa:  // 0x26f1d4 volume slide
            volslide(ch, p, kMaxVol);
            return outVol(ch);
        case 0xe: {  // 0x26f214 (table 0x26f224)
            const int y = p >> 4, v = p & 0xf;
            if (y == 9) {  // 0x26f264 retrigger
                ch.retrig = (ch.retrig - 1) & 0xff;
                if (ch.retrig) return;
                ch.retrig = v;
                return retrigger(ch);
            }
            if (y == 0xc) {  // 0x26f278 note cut, when the row's tick DOWN-counter equals x
                if (counter_ != v) return;
                ch.vol = 0;
                c_.select(ch.voice);
                c_.w16(7, int(vol(0)));
                c_.w16(8, int(vol(ch.vol)));
                c_.w8(0x0d, 0x40);
                ch.gvol = 0;
                return;
            }
            if (y == 0xd) {  // 0x26f2d4 note delay, when the tick counter equals x
                if (counter_ != v) return;
                return retrigger(ch);
            }
            if (y == 0xf) {  // V3.03 0x26f484: clear the effect and restart the note (the sound-effect call 0x26034e
                ch.eff = 0;  // puts EF0 on its channel; the song does not use EFx)
                return retrigger(ch);
            }
            return;
        }
        default: return;  // 8, B, C, D, F: nothing
        }
    }

    // 0x26f496: FC of a period, then 0x26f4a0
    void outFreq(Chan& ch, uint32_t per) {
        ch.fc = fcOf(per);
        outVol(ch);
    }

    // 0x26f4a0: select the channel's voice, volume ramp, pending frequency
    void outVol(Chan& ch) {
        c_.select(ch.voice);
        volUpdate(ch);
        fcUpdate(ch);
    }

    // 0x26f2e3
    void retrigger(Chan& ch) {
        c_.select(ch.voice);
        noteStart(ch);
        uint32_t a = ch.period;
        if (ch.ft) a = ch.period = snap(a, ch.ft);
        ch.fc = fcOf(a);
        volUpdate(ch);
        fcUpdate(ch);
    }

    Event eventAt(uint32_t ptr) const {
        // (outside the patterns the original reads whatever is there; not reached by this song)
        const size_t k = ptr / 6;
        if (ptr % 6 || k >= events_.size()) return {0, 0, 0};
        return events_[k];
    }

    // 0x26f544: read a row (the module's channels, [0x26040b])
    void rowTick() {
        counter_ = speed_;
        uint32_t edi = ptr_;
        for (int i = 0; i < nch_; i++, edi += 6) {
            Chan& ch = ch_[i];
            c_.select(ch.voice);
            const Event ev = eventAt(edi);
            if (ev.ins) {
                const Sample& s = smp_[size_t(ev.ins & 31)];
                ch.ins = ev.ins;
                ch.smp = ev.ins & 31;  // (ins <= 31: the high nibble of b0 only carries bit 4 in a MOD)
                ch.vol = std::min(s.vol, kMaxVol);
                ch.ft = s.ft;
                ch.ctl = s.flag;
                ch.start = s.dram;
                ch.newsmp = 2;
            }
            rowEffect(ch, ev.per, ev.eff);
        }
        row_ = (row_ + 1) & 0xff;
        if (row_ >= 0x40) {
            row_ = 0;
            order_ = (order_ + 1) & 0xff;
            if (order_ >= songlen_) order_ = restart_;  // V3.03: the song goes on (V3.02 stopped: counter = 0)
            ptr_ = patptr(ordtab_[size_t(order_)]);
        } else {
            ptr_ += uint32_t(stride_);
        }
    }

    // effect table 0x26f5cc, then 0x26fa5c (note start), 0x26fb44 (period), 0x26fb7c (volume, frequency)
    void rowEffect(Chan& ch, uint32_t a, uint32_t e) {
        const int x = int(e >> 8), p = int(e & 0xff);
        switch (x) {
        case 0: ch.arp = 0; break;  // 0x26f60c
        case 3:
        case 5:  // 0x26f618 / 0x26f65c tone portamento: the note is the target, no note starts
            if (a) {
                if (ch.ft) a = snap(a, ch.ft);
                ch.target = a;
                a = 0;
            }
            if (x == 3 && p) ch.pspeed = p;
            break;
        case 7: ch.tbase = ch.vol; break;  // 0x26f69c
        case 9: {  // 0x26f6a8 sample offset (bytes = param << 8)
            const uint32_t off = uint32_t(p) << 8;
            if (smp(ch).lerel >= off) ch.start = smp(ch).dram + off;
            else e = 0;
            break;
        }
        case 0xb:  // 0x26f6d0 position jump
            row_ = 0x3f;
            order_ = (p - 1) & 0xff;
            e = 0;
            break;
        case 0xc:  // 0x26f6e8
            ch.vol = std::min(p, kMaxVol);
            e = 0;
            break;
        case 0xd: {  // 0x26f700 pattern break (BCD row)
            const int v = ((p >> 4) * 10 + (p & 0xf)) & 0xff;
            if (p == 0 || v > 0x3f) {
                row_ = 0x3f;
            } else {
                row_ = (v - 1) & 0xff;
                order_ = (order_ + 1) & 0xff;
                if (order_ >= songlen_) order_ = restart_;
                ptr_ = patptr(ordtab_[size_t(order_)]) + uint32_t(stride_ * row_);
            }
            e = 0;
            break;
        }
        case 0xe: {  // 0x26f784 (table 0x26f790)
            const int y = p >> 4, v = p & 0xf;
            switch (y) {
            case 1:
            case 2:  // 0x26f7d0 / 0x26f81c fine portamento
                if (!a) {
                    ch.eff = 0;
                    return rowPeriod(ch, y == 1 ? ch.period - uint32_t(v) : ch.period + uint32_t(v));
                }
                if (ch.ft) a = snap(a, ch.ft);
                a = y == 1 ? a - uint32_t(v) : a + uint32_t(v);
                e = 0;
                break;
            case 3: ch.gliss = v & 1; e = 0; break;
            case 4: ch.wave = (ch.wave & 0xf0) | v; e = 0; break;
            case 5: ch.ft = v; e = 0; break;
            case 6:  // 0x26f8a0 pattern loop
                if (v == 0) {
                    looporder_ = order_;
                    int r = (row_ - 1) & 0xff;
                    if (r & 0x80) {
                        r = 0x3f;
                        looporder_ = (looporder_ - 1) & 0xff;
                    }
                    looprow_ = r;
                    loopptr_ = ptr_ - uint32_t(stride_);
                } else if (loopcnt_ >= v) {
                    loopcnt_ = 0;
                } else {
                    row_ = looprow_;
                    order_ = looporder_;
                    ptr_ = loopptr_;
                    loopcnt_++;
                }
                e = 0;
                break;
            case 7: ch.wave = (ch.wave & 0xf) | ((p << 4) & 0xff); e = 0; break;
            case 9: ch.retrig = v; break;
            case 0xa: ch.vol = std::min((ch.vol + v) & 0xff, kMaxVol); e = 0; break;
            case 0xb: {
                const int vv = (ch.vol - v) & 0xff;
                ch.vol = vv & 0x80 ? 0 : vv;
                e = 0;
                break;
            }
            case 0xd:  // 0x26f994 note delay: nothing now
                ch.period = a;
                ch.eff = e;
                return;
            case 0xe:  // 0x26f9a0 pattern delay: counter = x * speed
                counter_ = (v * speed_) & 0xff;
                e = 0;
                break;
            default: break;  // E0, E8, ECx, EF: kept for the ticks / nothing
            }
            break;
        }
        case 0xf:  // 0x26f9bc
            if (p >= 0x20) {
                tempo(p);
                c_.w8(0x45, 0);
                c_.w8(treg_, tcount_);
                c_.w8(0x45, tctl_);
            } else {
                speed_ = counter_ = p;
            }
            e = 0;
            break;
        default: break;  // 1, 2, 4, 6, 8, A: the effect word is kept for the ticks
        }
        // 0x26fa5c
        ch.eff = e & 0xffff;
        if (!a) return rowVolume(ch);
        noteStart(ch);
        rowPeriod(ch, a);
    }

    // 0x26fb44
    void rowPeriod(Chan& ch, uint32_t a) {
        if (ch.ft) a = snap(a, ch.ft);
        ch.period = a;
        ch.fc = fcOf(a);
        rowVolume(ch);
    }

    // 0x26fb7c
    void rowVolume(Chan& ch) {
        volUpdate(ch);
        fcUpdate(ch);
    }

    static constexpr int kMaxVol = 0x40;  // [0x26040c]

    std::span<const uint8_t> m_;
    Image img_;
    gus::Card& c_;
    int voices_ = 14;  // [0x26e012] (set from the image in the constructor)
    int nch_ = 4;      // [0x26040b] the module's channels
    int allch_ = 4;    // [0x26040f] V3.03: module and extra channels
    std::vector<Sample> smp_;
    std::vector<uint8_t> ordtab_;
    std::vector<Event> events_;
    std::vector<uint32_t> freq_;
    int songlen_ = 0, restart_ = 0, stride_ = 0;
    Chan ch_[32];
    int row_ = 0, order_ = 0, counter_ = 1, speed_ = 6;  // [0x26042c], [0x26042d], [0x260431], [0x260432]
    int loopcnt_ = 0, looprow_ = 0, looporder_ = 0;      // [0x260435], [0x260436], [0x260437]
    uint32_t loopptr_ = 0, ptr_ = 0;                     // [0x260438], [0x260418]
    int treg_ = 0x46, tctl_ = 4, tcount_ = 0;            // [0x26e00f], [0x26e010], [0x26e011]
};

}  // namespace

void run(std::span<const uint8_t> module, std::span<const uint8_t> image, gus::Card& card, const StopRule& stop,
         Run& out, const std::function<void(int, int64_t)>& beforeTick) {
    Player pl(module, image, card);
    card.tick = 0;
    pl.detect();
    pl.convert();
    pl.start();
    out.tickUs.clear();
    out.pos.clear();
    out.pos0 = pl.position();
    out.stopUs = 0;
    for (int k = 0;; k++) {
        const int64_t us = card.timers().nextIrq();
        if (us < 0) throw std::runtime_error("Reve music: the GUS timers stopped");
        if (stop(out, us)) break;
        if (k >= (1 << 22)) throw std::runtime_error("Reve music: the stop rule never stops");
        out.tickUs.push_back(us);
        card.tick = k + 1;
        if (beforeTick) beforeTick(k, us);
        pl.tick();
        out.pos.push_back(pl.position());
    }
    const int ticks = int(out.tickUs.size());
    if (beforeTick) beforeTick(ticks, out.stopUs);
    card.tick = ticks;
    pl.stop();
}

}  // namespace lca

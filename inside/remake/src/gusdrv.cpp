// DSMI's GUS driver SDIGUS.DSD as Inside runs it (gusdrv.h): reset 0x11546aa, voice clear 0x1154114, setupChannels
// 0x115419b, the volume ramp 0x1152e5e and its wait 0x1152f50, the CDI functions 0x1153576..0x1153b7b, the DRAM
// allocator 0x11532ab and the DMA upload 0x1153c6c / 0x1153df0. inside/re/notes-music.md sections 7.2-7.6; the
// Python port inside/re/models/music/dsmi.py (class Driver) is the reference.
#include "gusdrv.h"

#include <algorithm>
#include <stdexcept>

#include "gf1.h"

namespace gusdrv {

namespace {
constexpr size_t kVolTab = 0x700c;      // file offset of the volume table (the loaded driver's 0x11557ac)
constexpr int kVolSteps = 65;           // volumes 0..64
constexpr uint32_t kDramSize = 1u << 20;  // the capture's card (the driver probes 256 KB / 512 KB / 1 MB)
constexpr uint32_t kFirstBlock = 0x2000, kFirstBlockSize = 0x20;  // dramInit 0x11530c9
constexpr uint32_t kDmaBlock = 0x2000;  // the DOS buffer of the upload
constexpr int kRampRate = 0x3f;         // reg 0x06: 63 twelve-bit steps per frame
}  // namespace

Driver::Driver(gus::Card& card, std::span<const uint8_t> dsd) : c_(card) {
    if (dsd.size() < kVolTab + 4 * kVolSteps || dsd[0] != 'D' || dsd[1] != 'S')
        throw std::runtime_error("SDIGUS.DSD: not DSMI's GUS driver");
    for (int i = 0; i < kVolSteps; i++) {
        const uint8_t* p = dsd.data() + kVolTab + 4 * size_t(i);
        volTab_[i] = uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
        if (i > 0 && volTab_[i] < volTab_[i - 1]) throw std::runtime_error("SDIGUS.DSD: no volume table at 0x700c");
    }
}

// ---------------------------------------------------------------------------------------------- helpers

void Driver::w8x2(int reg, int val) {  // write, delay(1) (6 reads of 3X7), write
    c_.w8(reg, val);
    c_.w8(reg, val);
}

void Driver::w16x2(int reg, int val) {
    c_.w16(reg, val);
    c_.w16(reg, val);
}

// The selected voice's volume ramps from where it is now (reg 0x89, top 8 bits) to the table's 8-bit target.
void Driver::ramp(uint32_t vol) {
    vol = std::min<uint32_t>(vol, 0x40);
    vol = (master_ * vol) >> 8;
    const uint32_t target = volTab_[vol] >> 8;
    w8x2(0x0d, 3);  // stop the ramp
    const uint32_t cur = uint32_t(c_.r16(0x89) & 0xffff) >> 8;
    c_.w16(0x09, int(cur << 8));
    const bool down = cur > target;
    c_.w8(0x07, int(down ? target : cur));
    c_.w8(0x08, int(down ? cur : target));
    c_.w8(0x06, kRampRate);
    w8x2(0x0d, down ? 0x40 : 0);  // go (bit 6: down)
}

void Driver::waitRamp() { c_.waitVoice(); }

void Driver::setPos(int hi, int lo, uint32_t a) {
    c_.w16(hi, int((a >> 7) & 0x1fff));
    c_.w16(lo, int((a & 0x7f) << 9));
}

// FC = 2 * round(freq * 512 / rate), rate = the rate table's 617,400 / voices (0x1155878)
int Driver::fc(uint32_t freq) const {
    const uint64_t r = uint64_t(gus::kGf1ClockInt / voices_);
    const uint64_t f = (r / 2 + uint64_t(uint32_t(freq << 9))) / r * 2;
    return int(std::min<uint64_t>(f, 0xfffe));
}

uint32_t Driver::sampleAddr(uint32_t handle) const {
    for (const Upload& u : samples_)
        if (u.handle == handle) return u.addr;
    return 0;
}

// ---------------------------------------------------------------------------------------------- init

void Driver::reset() {
    c_.w8(0x4c, 0);       // GF1 reset
    c_.w8(0x4c, 1);       // run
    c_.port(0x100, 3);    // MIDI reset
    c_.port(0x100, 0);
    c_.w8(0x41, 0);       // DMA, timers, sampling off
    c_.w8(0x45, 0);
    c_.w8(0x49, 0);
    c_.w8(0x0e, 0xdf);    // 32 voices while clearing
    for (int i = 0; i < 32; i++) {
        c_.select(i);
        clearVoice(i);
        c_.w8(0x06, 0);
        c_.w8(0x0d, 3);
        c_.w16(0x09, 0);
        c_.w8(0x00, 3);
        c_.w8(0x0c, 8);
    }
    c_.w8(0x0e, (voices_ - 1) | 0xc0);
    c_.w8(0x4c, 3);       // run + DAC on
    c_.port(0, 9);        // mix control: line in off, output on, latches on
}

void Driver::clearVoice(int v) {
    stopNote(v);
    for (int r : {0x02, 0x03, 0x04, 0x05, 0x0a, 0x0b}) c_.w16(r, 0);
    v_[v].addr = 0;
    v_[v].freq = 0;
}

void Driver::initDevice() {
    voices_ = 14;
    master_ = 0x100;
    for (Voice& v : v_) v = Voice{};
    samples_.clear();
    reset();
    blocks_ = {{kFirstBlock, kFirstBlockSize}, {kDramSize, 0}};
}

int Driver::setupChannels(int n) {
    for (int i = 0; i < voices_; i++) clearVoice(i);
    voices_ = std::clamp(n, 14, 32);
    c_.w8(0x0e, (voices_ - 1) | 0xc0);  // on the voice selected last
    for (int i = 0; i < voices_; i++) clearVoice(i);
    return 0;
}

int Driver::stopOutput() {
    for (int i = 0; i < voices_; i++) clearVoice(i);
    return 0;
}

// ---------------------------------------------------------------------------------------------- CDI

int Driver::setSample(int ch, const Sample& s) {
    if (ch < 0 || ch >= voices_) return -1;
    const uint32_t addr = sampleAddr(s.handle);
    c_.select(ch);
    Voice& v = v_[ch];
    v.mode = s.loopEnd ? 8 : 0;
    if (v.addr == addr) {  // the same sample: only the fade (AMP's setVolume follows)
        ramp(0);
        return 0;
    }
    v.addr = addr;
    ramp(0);
    waitRamp();
    w8x2(0x00, 3);  // stop the voice
    setPos(0x0a, 0x0b, addr);
    uint32_t ls = addr;
    if (s.loopEnd) {
        ls += s.loopStart;
        if (s.loopStart > 1) ls--;
    }
    setPos(0x02, 0x03, ls);
    setPos(0x04, 0x05, addr + (s.loopEnd ? s.loopEnd - 1 : s.length - 2));
    return 0;
}

int Driver::playNote(int ch, uint32_t freq, uint32_t vol) {
    Voice& v = v_[ch & 31];
    v.vol = vol;
    v.freq = freq;
    c_.select(ch);
    ramp(0);  // the old note: its ramp down starts, the voice is stopped about one frame later
    waitRamp();
    w8x2(0x00, 3);
    setPos(0x0a, 0x0b, v.addr);
    c_.w16(0x01, fc(freq));
    w16x2(0x09, int(volTab_[0] & 0xffff));  // the start volume, -87 dB
    w8x2(0x00, v.mode);                      // start
    if (!v.muted) ramp(vol);
    return 0;
}

int Driver::setVolume(int ch, uint32_t vol) {
    Voice& v = v_[ch & 31];
    v.vol = std::min<uint32_t>(vol, 0x40);
    if (v.muted) return 0;
    c_.select(ch);
    ramp(v.vol);
    return 0;
}

int Driver::setFrequency(int ch, uint32_t freq) {
    const int f = fc(freq);
    v_[ch & 31].freq = freq;
    c_.select(ch);
    c_.w16(0x01, f);
    return 0;
}

int Driver::setPosition(int ch, uint32_t pos) {
    Voice& v = v_[ch & 31];
    c_.select(ch);
    ramp(0);
    waitRamp();
    w8x2(0x00, 3);
    w8x2(0x0d, 3);
    setPos(0x0a, 0x0b, v.addr + pos);
    w8x2(0x00, v.mode);
    if (!v.muted) {
        ramp(v.vol);
        waitRamp();
    }
    return 0;
}

int Driver::setPanning(int ch, int pan) {  // -64..63 -> 0..15 (100 = surround: centre)
    if (pan == 100) pan = 0;
    if (pan > 63) pan = 63;
    else if (pan <= -63) pan = -64;
    c_.select(ch);
    c_.w8(0x0c, pan / 8 + 8);  // division toward zero: -63 -> 0, 63 -> 15
    return 0;
}

int Driver::setMasterVolume(int vol) {
    master_ = uint32_t(std::min(vol * 4, 0x100));
    for (int i = 0; i < voices_; i++) setVolume(i, v_[i].vol);
    return 0;
}

int Driver::mute(int ch) {
    c_.select(ch);
    ramp(0);
    v_[ch & 31].muted = 1;
    return 0;
}

int Driver::unmute(int ch) {
    Voice& v = v_[ch & 31];
    if (v.muted) {
        c_.select(ch);
        ramp(v.vol);
        v.muted = 0;
    }
    return 0;
}

int Driver::stopNote(int ch) {
    c_.select(ch);
    ramp(0);
    waitRamp();
    v_[ch & 31].vol = 0;
    w8x2(0x0d, 3);
    w8x2(0x00, 3);
    c_.w16(0x09, int(volTab_[0] & 0xffff));
    return 0;
}

// ---------------------------------------------------------------------------------------------- DRAM

// Best fit among the gaps of the block list; a sample never crosses a 256 KB bank (it moves to the next one).
uint32_t Driver::alloc(uint32_t size) {
    const int64_t n = int64_t((size + 0x40) & ~0x1fu);
    int64_t best = 0x2000000;
    int bi = -1;
    bool cross = false;
    for (size_t k = 0; k + 1 < blocks_.size(); k++) {
        const int64_t start = blocks_[k].addr + blocks_[k].size, next = blocks_[k + 1].addr;
        if ((start + n) >> 18 != start >> 18) {
            const int64_t gap = next - ((start + n) & ~int64_t(0x3ffff));
            if (n < gap && gap < best) {
                best = gap;
                bi = int(k);
                cross = true;
            }
        } else if (const int64_t gap = next - start; n < gap && gap < best) {
            best = gap;
            bi = int(k);
            cross = false;
        }
    }
    if (bi < 0) return 0;
    const int64_t end = blocks_[size_t(bi)].addr + blocks_[size_t(bi)].size;
    const int64_t addr = cross ? (end + n) & ~int64_t(0x3ffff) : end;
    blocks_.insert(blocks_.begin() + bi + 1, Block{addr, n});
    return uint32_t(addr);
}

// The DMA upload in blocks of 0x2000 bytes with reg 0x41 = 0x81 (start, memory -> card, 8 bit, invert MSB): the
// GF1 stores every byte XOR 0x80, so the unsigned AMF samples become signed in DRAM.
int Driver::download(std::span<const uint8_t> data, uint32_t handle, uint32_t size) {
    const uint32_t addr = alloc(size);
    if (!addr) return -1;
    c_.w8(0x41, 0);
    uint32_t left = size, a = addr, p = 0, chunk = kDmaBlock;
    while (left > 0) {
        c_.w16(0x42, int((a >> 4) & 0xffff));
        if (left < chunk) chunk = left;
        for (uint32_t i = 0; i < chunk && p + i < data.size(); i++) c_.poke(a + i, uint8_t(data[p + i] ^ 0x80));
        c_.w8(0x41, 0x81);
        c_.w8(0x41, 0);
        left -= chunk;
        a += chunk;
        p += chunk;
    }
    samples_.push_back({handle, addr, size});
    return 0;
}

}  // namespace gusdrv

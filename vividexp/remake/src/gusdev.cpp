// MIDAS's GUS sound device (0x41b1a0..0x41cd10), routine by routine after vividexp/re/models/music/midas.py, which
// reproduces every register write of vividexp/re/full.gus and part2.gus. See gusdev.h; notes-music.md section 5.
#include "gusdev.h"

#include <algorithm>
#include <stdexcept>

#include "exe.h"

namespace midas {

namespace {
constexpr uint32_t kRates = 0x824452;   // 19 words: GF1 output rate for 14..32 active voices
constexpr uint32_t kVolTab = 0x824478;  // 65 words: GF1 volume (12.4 log) for volume 0..64
constexpr int kMaxPasses = 1 << 20;     // the start loop has no bound; a model that never ends a ramp is a bug
}  // namespace

GusDevice::GusDevice(const VividExe& exe, gus::Card& card) : exe_(exe), p_(card) { resetState(); }

void GusDevice::resetState() {
    for (Voice& r : v_) r = Voice{};
    samples_.assign(1, Sample{});
    channels_ = outRate_ = updRate_ = 0;
    master_ = 0x40;
    everyVolume_ = open_ = paused_ = mono_ = 0;
    nextHandle_ = 1;
    maxHandle_ = 0;
    heap_ = 0;
}

// ---- 0x41b436 reset, 0x41b1b5 Detect, 0x41b267 Init, 0x41b45d Close

void GusDevice::reset() {
    p_.w8(0x4c, 0);
    delay();
    delay();
    p_.w8(0x4c, 1);
    delay();
    delay();
}

void GusDevice::detect() {
    reset();
    p_.poke(0, 0x55);  // the memory test: 0x55 / 0xaa at the start of bank 0, read back
    p_.poke(1, 0xaa);
}

void GusDevice::init(int mode) {
    resetState();
    uploads_.clear();
    mono_ = mode & 1;
    reset();
    for (uint32_t bank = 0; bank < kDram >> 18; bank++) {  // the memory test of every 256 KB bank
        p_.poke(bank << 18, 0x55);
        p_.poke((bank << 18) + 1, 0xaa);
    }
    p_.port(0, 3);
    p_.w8(0x0e, 0xdf);
    for (int v = 32; v > 0; v--) {  // voice 32 selects voice 0
        p_.select(v);
        p_.w8(0x00, 3);
        p_.w16(0x09, 0x500);
        p_.w8(0x0c, 8);
        p_.w8(0x0d, 3);
        p_.w8(0x06, 0x3f);  // ramp rate: 63 steps per frame
    }
    p_.w8(0x4c, 3);
    p_.port(0, 0);
}

void GusDevice::close() {
    reset();
    p_.port(0, 0);
}

// ---- 0x41b4c3 OpenChannels, 0x41b5ce CloseChannels (0x41b5ea ClearChannels)

void GusDevice::openChannels(int n) {
    channels_ = n;
    paused_ = 0;
    const int voices = std::max(n, 14);
    outRate_ = exe_.u16(kRates + 2 * uint32_t(voices - 14));
    p_.w8(0x0e, (voices - 1) | 0xc0);
    for (Voice& r : v_) r = Voice{};
    for (int v = 0; v < n; v++) {
        p_.select(v);
        p_.w8(0x0c, 8);
        v_[v].pan = 0;
        v_[v].flags = 1;
    }
    open_ = 1;
    p_.port(0, 0);
}

void GusDevice::closeChannels() {
    for (int v = 0; v < channels_; v++) {
        p_.select(v);
        p_.w8(0x00, 3);
        delay();
        p_.w8(0x00, 3);
        p_.w8(0x0c, 8);
        Voice& r = v_[v];
        r.pan = 0;
        r.sample = 0;
        r.playing = 0;
        r.flags = 1;
    }
    open_ = 0;
    channels_ = 0;
}

// ---- the per-channel calls

uint32_t GusDevice::fcOf(uint32_t rate) const {  // ((rate << 10) / output rate) & ~1, signed division
    const int32_t x = int32_t(rate << 10);
    return uint32_t(x / outRate_) & ~1u;
}

void GusDevice::setRate(int ch, uint32_t rate) {
    Voice& r = v_[ch];
    if (r.rate != rate) {
        r.rate = rate;
        r.fc = fcOf(rate);
        r.flags |= 8;
    }
}

int GusDevice::playSound(int ch, uint32_t rate) {
    setRate(ch, rate);
    Voice& r = v_[ch];
    const int h = r.sample;
    if (h == 0 || h > maxHandle_) return 0x13;
    const Sample& s = samples_[size_t(h)];
    if (s.length == 0) {
        stopSound(ch);
        return 0;
    }
    r.start = s.addr;
    r.flags = (r.flags & ~0x101) | 2;
    r.active = 1;
    return 0;
}

void GusDevice::stopSound(int ch) {
    Voice& r = v_[ch];
    r.flags = (r.flags & ~2) | 1;
}

void GusDevice::setVolume(int ch, uint32_t vol) {
    vol = std::min<uint32_t>(vol, 0x40);
    Voice& r = v_[ch];
    if (r.volume != vol) {
        r.volume = vol;
        r.flags |= 4;
    }
}

int GusDevice::setSample(int ch, int h) {
    if (h == 0 || h > maxHandle_) return 0x13;
    Voice& r = v_[ch];
    const Sample& s = samples_[size_t(h)];
    if (r.sample == h) return 0;
    r.sample = h;
    if (s.length == 0) {
        stopSound(ch);
        return 0;
    }
    r.type = s.type;
    r.start = s.addr;
    r.loop1 = s.loop1;
    r.loop2 = s.loop2;
    r.loopMode = s.mode;
    if (s.mode != 0 && s.mode != 4) {
        r.loop2End = s.addr + s.l2end;
        r.loop2Start = s.addr + s.l2start;
        r.loopEnd = s.addr + s.l1end;
        r.loopStart = s.addr + s.l1start;
    } else {
        r.loopEnd = s.addr + s.length;
        r.loopStart = s.addr;
    }
    r.flags |= 0x10;
    return 0;
}

int GusDevice::setPosition(int ch, uint32_t pos) {
    Voice& r = v_[ch];
    const int h = r.sample;
    if (h == 0) return 0;
    const Sample& s = samples_[size_t(h)];
    if (s.length < pos) {
        if (s.mode != 5) {
            stopSound(ch);
            return 0;
        }
        pos = s.l1start;
    }
    r.start = pos + s.addr;
    r.flags = (r.flags & ~1) | 2;
    r.active = 1;
    return 0;
}

void GusDevice::setPanning(int ch, int pan) {
    p_.select(ch);
    Voice& r = v_[ch];
    if (r.pan == pan) return;
    r.pan = pan;
    if (mono_) return;
    // Stereo (never here: main 0x4002e0 sets mono): GF1 pan = (pan >> 3, + 1 if negative) + 7, flags |= 0x40, every
    // voice's volume recomputed. Not ported.
    throw std::logic_error("GUS device: stereo panning is not ported (VIVID.EXE runs MIDAS in mono)");
}

// ---- 0x41c001 AddSample, 0x41cb45 DRAM allocation, 0x41c11b upload

int GusDevice::addSample(std::span<const uint8_t> data, int type, uint32_t length, int loop, uint32_t loopStart,
                         uint32_t loopEnd, int loopType) {
    if (type == 0) length = 0;
    const int h = nextHandle_;
    if (size_t(h) >= 256) throw std::runtime_error("GUS device: too many samples");
    Sample s;
    s.type = type ? type : 1;
    s.length = length;
    s.mode = loop;
    s.loop1 = loopType;
    s.l1start = loopStart;
    s.l1end = loopEnd;
    if (length) {
        if (data.size() < length) throw std::runtime_error("GUS device: sample data missing");
        const uint32_t size = (length & ~0x1fu) + 0x40;
        if (heap_ + size > kDram) throw std::runtime_error("GUS device: out of DRAM");
        const uint32_t addr = heap_;
        heap_ += size;
        s.addr = addr;
        // unsigned -> signed (every byte ^ 0x80), poked byte by byte; then 0x40 - (end & 0x1f) bytes of padding: the
        // loop's first bytes again (a looping sample) or the last byte repeated (a one-shot), to the block's end
        for (uint32_t i = 0; i < length; i++) p_.poke(addr + i, uint8_t(data[i] ^ 0x80));
        const uint32_t a = addr + length, pad = 0x40 - (a & 0x1f);
        for (uint32_t i = 0; i < pad; i++) {
            uint8_t b;
            if (loop == 0 || loop == 2 || loop == 4) b = uint8_t(data[length - 1] ^ 0x80);
            else b = loopStart + i < length ? uint8_t(data[loopStart + i] ^ 0x80) : 0;
            p_.poke(a + i, b);
        }
        uploads_.push_back(addr);
        uploads_.push_back(length + pad);
    }
    if (samples_.size() <= size_t(h)) samples_.resize(size_t(h) + 1);
    samples_[size_t(h)] = s;
    int nh = h;
    while (size_t(nh) < samples_.size() && samples_[size_t(nh)].type) nh++;
    nextHandle_ = nh;
    maxHandle_ = std::max(maxHandle_, h);
    return h;
}

// ---- 0x41c43e Play: the register writes, once per player interrupt before the player runs

int GusDevice::volume(const Voice& r) const {
    const uint32_t vol = (r.flags & 0x80) ? 0 : r.volume;
    return exe_.u16(kVolTab + 2 * (((vol * uint32_t(master_ & 0xff)) & 0xffff) >> 6));
}

bool GusDevice::play() {
    if (paused_ || !open_) return false;
    for (int v = 0; v < channels_; v++) {
        Voice& r = v_[v];
        if (!r.active) continue;
        p_.select(v);
        if (r.flags & 3) {  // stop or start: ramp the old note down, stop the wave
            r.flags &= ~1;
            p_.w8(0x07, 0x15);
            p_.w8(0x0d, 0x40);
            p_.w8(0x00, 3);
            delay();
            p_.w8(0x00, 3);
            p_.w8(0x0d, 0x40);
        } else if (everyVolume_ || (r.flags & 4)) {
            r.flags &= ~4;
            const int cur = p_.r16(0x89) & 0xff00;
            const int tgt = volume(r) & 0xff00;
            if (cur != tgt) {
                const int lo = std::min(cur, tgt) >> 8, hi = std::max(cur, tgt) >> 8, dir = cur < tgt ? 0 : 0x40;
                p_.w8(0x0d, 3);
                delay();
                p_.w8(0x0d, 3);
                p_.w8(0x07, lo);
                p_.w8(0x08, hi);
                p_.w8(0x0d, dir);
                delay();
                p_.w8(0x0d, dir);
            }
        }
        if (r.flags & 8) {
            r.flags &= ~8;
            p_.w16(0x01, int(r.fc));
        }
        if ((r.flags & 0x10) && (r.loopMode == 4 || r.loopMode == 5) && !(r.flags & 2))
            throw std::logic_error("GUS device: Amiga loop emulation (MOD only) is not ported");
    }
    // start the voices: wait for their ramps first (each pass polls every voice still to start)
    for (int passes = 0;; passes++) {
        if (passes > kMaxPasses) throw std::runtime_error("GUS device: a volume ramp never ends");
        bool retry = false;
        for (int v = 0; v < channels_; v++) {
            Voice& r = v_[v];
            if (!r.active || !(r.flags & 2)) continue;
            p_.select(v);
            if (!(p_.r8(0x8d) & 1)) {  // ramp still running: next voice, then again
                retry = true;
                continue;
            }
            if (r.flags & 0x10) {
                p_.w16(0x04, int(r.loopEnd >> 7));
                p_.w16(0x05, int(r.loopEnd << 9));
                p_.w16(0x02, int(r.loopStart >> 7));
                p_.w16(0x03, int(r.loopStart << 9));
                r.flags &= ~0x10;
            }
            p_.w8(0x00, 3);
            p_.w16(0x0a, int(r.start >> 7));
            p_.w16(0x0b, int(r.start << 9));
            delay();
            p_.w16(0x0b, int(r.start << 9));
            p_.w16(0x0a, int(r.start >> 7));
            p_.w8(0x00, 3);
            r.playing = r.sample;
            const int ctl = r.loop1 == 0 ? 0 : r.loop1 == 1 ? 8 : 0x18;  // one-shot, forward, bidirectional
            p_.w8(0x00, ctl);
            p_.w8(0x08, volume(r) >> 8);
            p_.w8(0x0d, 0);
            delay();
            p_.w8(0x0d, 0);
            p_.w8(0x00, ctl);
            r.flags &= ~6;
        }
        if (!retry) break;
        p_.pass();  // the loop polls again: time passes on the card
    }
    everyVolume_ = 0;
    return true;
}

}  // namespace midas

// DSMI's GUS driver SDIGUS.DSD ("Gravis UltraSound GF1 sound driver 1.0", Virtual Visions / Otto Chrons) as Inside
// loads it: the device init and reset, the CDI channel functions AMP calls (voice = channel), the volume ramps, the
// DRAM allocator and the DMA upload, ported routine by routine from inside/re/models/music/dsmi.py (class Driver),
// which reproduces every GUS register write of the capture (inside/re/notes-music.md section 7). Addresses are the
// driver's as the capture loaded it (code 0x1152bb5..0x11556c0).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gus {
class Card;
}

namespace gusdrv {

// AMP's sample record (0x423039 + 0x1a * ch), the structure the driver's setSample reads.
struct Sample {
    uint32_t handle = 0;     // +0x00 the sample's handle (AMP: the address of its data in memory)
    uint32_t length = 0;     // +0x04
    uint32_t loopStart = 0;  // +0x08
    uint32_t loopEnd = 0;    // +0x0c (0: no loop)
    uint16_t rate = 0;       // +0x13 Hz at note 60
    uint32_t freq = 0;       // +0x15 Hz
    uint8_t vol = 0;         // +0x19
};

// The driver on one card. dsd: SDIGUS.DSD, whose 65-entry volume table (file offset 0x700c, the loaded driver's
// 0x11557ac) is read at construction; throws std::runtime_error if the file is not that driver.
class Driver {
public:
    Driver(gus::Card& card, std::span<const uint8_t> dsd);

    void initDevice();  // SDIInitDevice 0x1154881: GF1 reset with 14 voices, the DRAM allocator (1 MB)
    int stopOutput();   // SDIStopOutput 0x1154a44: clears every voice

    // the CDI functions (the table the driver registers, 0x1155c1a: + offset)
    int setSample(int ch, const Sample& s);               // +0x00 0x1153576
    int playNote(int ch, uint32_t freq, uint32_t vol);    // +0x04 0x1153716
    int setVolume(int ch, uint32_t vol);                  // +0x08 0x1153831
    int setFrequency(int ch, uint32_t freq);              // +0x0c 0x115388e
    int setPosition(int ch, uint32_t pos);                // +0x14 0x1153927
    int setPanning(int ch, int pan);                      // +0x18 0x11539ef
    int setMasterVolume(int vol);                         // +0x1c 0x1153a63
    int mute(int ch);                                     // +0x20 0x1153ad2
    int unmute(int ch);                                   // +0x24 0x1153b26
    int stopNote(int ch);                                 // +0x28 0x1153b7b
    int download(std::span<const uint8_t> data, uint32_t handle, uint32_t size);  // +0x34 0x1153df0
    int setupChannels(int n);                             // +0x54 0x115419b

    struct Upload {
        uint32_t handle, addr, size;
    };
    const std::vector<Upload>& uploads() const { return samples_; }  // the sample table 0x1156248
    int voices() const { return voices_; }

private:
    struct Voice {  // 0x1155fe8 + 14 * v
        uint32_t addr = 0, freq = 0, vol = 0;  // +0 DRAM address of the sample, +4, +8
        int mode = 0, muted = 0;               // +0xc (8: looped), +0xd
    };
    void reset();                                  // 0x11546aa
    void clearVoice(int v);                        // 0x1154114
    void ramp(uint32_t vol);                       // 0x1152e5e
    void waitRamp();                               // 0x1152f50
    void w8x2(int reg, int val);                   // 0x1152c63
    void w16x2(int reg, int val);                  // 0x1152cea
    void setPos(int hi, int lo, uint32_t a);       // address -> GF1 (bits 19..7 high, 6..0 << 9 low)
    int fc(uint32_t freq) const;                   // Hz -> the GF1's frequency control
    uint32_t sampleAddr(uint32_t handle) const;    // 0x1153509
    uint32_t alloc(uint32_t size);                 // 0x11532ab

    gus::Card& c_;
    uint32_t volTab_[65];
    int voices_ = 14;        // [0x11561dc]
    uint32_t master_ = 0x100;  // [0x11561c0]
    Voice v_[32];
    std::vector<Upload> samples_;
    struct Block {
        int64_t addr, size;
    };
    std::vector<Block> blocks_;  // the DRAM block list 0x11557a0
};

}  // namespace gusdrv

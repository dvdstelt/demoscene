// MIDAS Sound System 0.5a's XM loader (0x418d44, pattern conversion 0x418918) and Generic Module Player for XM
// modules (gmpPlaySong 0x40d8b0, gmpPlay 0x40de40, the row 0x40e3dc, tick 0x40eaf8 and update 0x40dec4 routines,
// the commands 0x41e930..0x41f437), as VIVID.EXE plays EXPRMNT1.XM and EXPRMNT2.XM (vividexp/re/notes-music.md
// sections 3 and 4; vividexp/re/models/music/midas.py, which reproduces the captures' register streams).
//
// Everything is the original's integer arithmetic, quirks included: the shared info memory of the commands, the
// sample offset that adds up, tone portamento with an instrument keeping the current one, the vibrato row that keeps
// the vibrato's period, the truncated envelope interpolation, Kxx as an immediate key off. Tables are read from
// VIVID.EXE: the XM effect table 0x8238e4 and the Exy table 0x823908 (loader), the Amiga period table 0x822280, the
// linear frequency table 0x822430 and the vibrato sine 0x824c30 (player).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class VividExe;

namespace midas {

class GusDevice;

struct Envelope {     // 0x34 bytes: numPoints, sustain, loop start, loop end (signed bytes, -1 none), 12 points
    int n = 0, sus = -1, ls = -1, le = -1;
    std::array<uint8_t, 4 + 48 + 8> raw{};   // as in memory, + 8 zero bytes for reads one past the points
    int x(int k) const;                      // point k's x as the code reads it (k = -1: the header bytes)
    int y(int k) const;
};

struct Sample {
    int handle = 0;                          // the sound device's
    uint32_t length = 0, loopStart = 0, loopEnd = 0;
    int volume = 0, panning = 0, relNote = 0, finetune = 0;
    int type = 0;                            // 0 none, 1 8-bit (16-bit is converted)
    int loop = 0, loopType = 0;              // loop 0 / 1; type 0 none, 1 forward, 2 bidirectional
};

struct Instrument {
    std::vector<uint8_t> keymap;             // 96 entries, or empty (sample 0 for every note)
    bool hasVolEnv = false, hasPanEnv = false;
    Envelope volEnv, panEnv;
    int fadeout = 0;
    std::vector<Sample> samples;             // none if no pattern uses the instrument
};

struct Pattern {
    int rows = 64;
    std::vector<uint8_t> data;               // converted (notes-music 3.2): per row entries, ended by a 0 byte
};

struct Module {
    std::string name;
    int length = 0, restart = 0, numChans = 0;
    int globalVolume = 0x40;                 // Gxx writes it here
    int speed = 6, tempo = 125;
    bool linear = false;                     // linear frequencies (else Amiga periods)
    std::vector<int> orders;
    std::vector<Pattern> patterns;
    std::vector<Instrument> instruments;
};

// 0x418d44: loads an XM module and gives every sample to the sound device (AddSample, in loader order). Throws
// std::runtime_error if the file is not an XM module MIDAS loads.
Module loadXM(std::span<const uint8_t> file, const VividExe& exe, GusDevice& sd);

// The Generic Module Player playing one module on channels 0..numChans-1 of the sound device.
class Player {
public:
    struct Position { int order = 0, pattern = 0, row = 0; };

    // curType: [0x844cb4], the module type the GMP last worked on (0 before the first tick of the program: part I's
    // gmpPlaySong sets the initial panning through the MOD/S3M branch), kept across modules.
    Player(Module& m, const VividExe& exe, GusDevice& sd, int& curType);

    void playSong();                         // 0x40d8b0
    void play();                             // 0x40de40: one tick
    int tempo() const { return tempo_; }     // BPM; the timer's period is 0x71ca5f0 / (40 x BPM)
    bool tempoChanged() { bool c = tempoChanged_; tempoChanged_ = false; return c; }
    // What 0x400104 copies for the demo: the position of the row read last (gmpPlay's info, 0x40de94).
    Position position() const { return info_; }
    int tick() const { return tick_; }
    bool rowRead() const { return rowRead_; }  // the last play() read a row (playRow ran)

private:
    struct Channel {                         // 0x40 bytes
        uint32_t period = 0;                 // +0x00
        int inst = -1;                       // +0x04 current instrument
        int newInst = -1;                    // +0x08 instrument of the last instrument byte
        uint32_t target = 0;                 // +0x0c tone portamento target
        uint32_t offset = 0;                 // +0x10 sample offset
        int sample = 0xff;                   // +0x14
        int sdch = 0;                        // +0x15
        int volByte = 0;                     // +0x16 volume column
        int note = 0xff;                     // +0x17
        int info = 0;                        // +0x18 last non-zero info of any command
        int vol = 0x40;                      // +0x1c
        int cmd = 0, rawInfo = 0;            // +0x1d, +0x1e
        int portaSpeed = 0;                  // +0x1f
        int vol2 = 0x40;                     // +0x20 volume used
        int pan = 0x80;                      // +0x21
        int vpos = 0, ppos = 0;              // +0x22, +0x26 envelope positions
        int fade = 0x10000;                  // +0x2a
        int vdone = 0, pdone = 0;            // +0x2e, +0x2f envelope held at the sustain point
        int release = 0;                     // +0x30 key off
        int retrig = 0;                      // +0x31
        int finalVol = 0;                    // +0x32
        int lastNote = 0xff;                 // +0x33
        int vibSpeed = 0, vibDepth = 0, vibPos = 0;  // +0x34..+0x36
        int offMem = 0;                      // +0x37
        int loopRow = 0, loopCount = 0;      // +0x38, +0x39
        int flags = 0;                       // +0x3c: 1 note, 2 volume, 4 instrument, 8 command, 0x10 delayed note
    };

    const Instrument& ins(int i) const;
    const Sample& sampleOf(const Instrument& in, int s) const;
    uint32_t notePeriod(int note) const;     // 0x40ec10
    uint32_t periodRate(uint32_t per) const; // 0x40ee6c
    void setPeriod(int64_t per);             // 0x40eef4
    void changePeriod(int64_t per);          // 0x40ef58
    void setVolume(int v);                   // 0x40efb0
    void setPanning(int p);                  // 0x40f000
    void setTempo(int bpm);                  // 0x40ebc8
    void newNote();                          // 0x40f0a4
    void playRow();                          // 0x40e3dc
    void advanceOrder();
    void tickCommands();                     // 0x40eaf8
    void update();                           // 0x40dec4
    static void envelope(const Envelope& e, int& pos, int& done, int release, bool pan, int& value);
    void rowCommand(int cmd, int info);   // info: the shared memory (+0x18), or the row's byte in a pattern delay
    void tickCommand(int cmd, int info);
    void volumeRow(Channel& c);              // 0x41f328
    void volumeTick(Channel& c);             // 0x41f3a4
    void volSlide(int info);                 // 0x41e9f0
    void tonePortaRow();                     // 0x41ecf0
    void tonePortaTick(int speed);           // 0x41ecd4
    void vibratoTick(int x);                 // 0x41ed6c

    Module& m_;
    const VividExe& exe_;
    GusDevice& sd_;
    int& curType_;
    std::vector<Channel> ch_;
    Channel* cur_ = nullptr;
    Position info_;
    int pos_ = 0, pattern_ = 0, row_ = 0, loopEnd_ = 0, restart_ = 0;
    int tempo_ = 125, speed_ = 6, tick_ = 0, patDelay_ = 0, sync_ = -1;
    bool hasData_ = false;                   // +0x08 (0: read the pattern from row +0x14)
    size_t dataPos_ = 0;
    bool tempoChanged_ = false, rowRead_ = false;
};

}  // namespace midas

// Renders JIZZ's soundtrack from JIZZ.EXE: the module expander (0x1632b, mp4synth.cpp), MXMPlay (0x296dc..0x2a316,
// mxmplay.cpp) and a model of the GUS GF1 chip (gf1.cpp). See jizz/re/notes-music.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class JizzExe;

struct Soundtrack {
    std::vector<float> pcm;  // interleaved stereo, sample 0 = music time 0 (the first IRQ after xmpPlay)
    double rate = 48000;     // Hz (the GF1's 44,100 Hz with 14 voices)
    struct Row { int pos; double time; };
    std::vector<Row> rows;   // every row the player reaches (order << 8 | row) and its music time, in playing order
    double timer0 = 0;       // xmpGetTimer (PIT clocks) at music time 0
    double length = 0;       // music time at which the intro exits (order 0x15 row 0x1c: sync event -1)
};

bool renderSoundtrack(const JizzExe& exe, Soundtrack& out, std::string* err);

// For checks (tools/test_music.cpp): MXMPlay from xmpInit to xmpStop, recording what the capture recorded.
// fakeGus = true: the capture's fake GUS (volume reads answer the last write to reg 9), as in jizz/re/full.gus;
// false: the GF1 model answers them (real hardware; mxmplay.py --real-volume-read).
struct MusicTrace {
    std::vector<uint8_t> module;  // the expanded module (dump/smokeout.mxm without its 11 trailing zero bytes)
    std::string gus;              // GUS register writes in run_jizz.py's text format ("tick vN rXX value")
    std::vector<uint8_t> dram;    // GUS DRAM after the sample upload (1 MB)
    std::vector<int> pos;         // xmpGetPos after IRQ k at index k - 1 (capture log tick 433 + k)
};

bool traceSoundtrack(const JizzExe& exe, bool fakeGus, MusicTrace& out, std::string* err);

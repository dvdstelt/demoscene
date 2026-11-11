// Renders JIZZ's soundtrack from JIZZ.EXE: the module decruncher (0x1632b), MXMPlay (0x296dc..0x2a316) and a
// model of the GUS GF1 chip. See jizz/re/notes-music.md.
#pragma once
#include <string>
#include <vector>

class JizzExe;

struct Soundtrack {
    std::vector<float> pcm;  // interleaved stereo, sample 0 = music time 0 (xmpPlay)
    double rate = 48000;     // Hz
    struct Row { int pos; double time; };
    std::vector<Row> rows;   // every row the player reaches (order << 8 | row) and its music time, in playing order
    double timer0 = 0;       // xmpGetTimer (PIT clocks) at music time 0
    double length = 0;       // music time at which the intro exits (order 0x15 row 0x1c: sync event -1)
};

bool renderSoundtrack(const JizzExe& exe, Soundtrack& out, std::string* err);

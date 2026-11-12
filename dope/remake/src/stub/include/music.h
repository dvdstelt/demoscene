// A silent stand-in for the soundtrack (src/music.h), used by CMakeLists.txt only while src/music.h does not exist:
// the music agent ports the player there. Same interface, no sound; the tick times and positions follow the closed
// form of dope/re/notes-music.md section 7.2 (speed 9 at tempo 111 for orders 0..2, then speed 3 at tempo 122; the
// song has no pattern breaks or jumps), so the clock, the placeholders and --info work.
//
// Music time here: seconds since the GUS timer overflow at which the handler reloads the player's first count
// (capture program time 0.110400); the first player tick (row 00:00) follows 19.84 ms later. The framework does not
// depend on this choice: it anchors the part schedule on timeOfTick(0) (demo.h).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct Soundtrack {
    int rate = 44100;              // stereo int16 interleaved
    std::vector<int16_t> pcm;      // pcm frame 0 = music time 0 (empty: silence)
    double timeOfTick(int tick) const;     // music time of player tick n (0 = the first, row 00:00)
    int positionAt(double t) const;        // order << 8 | row at music time t
    double timeOfPosition(int pos) const;  // first music time at which positionAt(t) >= pos
};

Soundtrack renderSoundtrack(std::span<const uint8_t> mod, std::span<const uint8_t> loader, int rate);

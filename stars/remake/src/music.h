// STARS's soundtrack: the module (overlay file 1) played by a port of LCA's MOD-Player V3.02 (lcaplay.cpp) on a
// model of the GUS GF1 (gf1.cpp), rendered whole at load time (stars/re/notes-music.md).
//
// Music time: seconds since the player's timer start t_s, the moment 0x1a91c starts the GUS timers (main calls it
// after its precalc; frame 279 of the capture). Everything in the music is clocked by the GUS timers from there:
// player tick 0 at 20.48 ms, tick 1 at 81.92 ms, then every 24.96 ms (tempo 99 on GUS timer 2) up to tick 2938 and
// every 21.44 ms after it (tempo 115), the truncated periods that make the song 1.17 % / 1.39 % faster than its
// nominal tempo. timeOfTick(0) = 0.02048.
//
// pcm frame i plays at music time i / rate (frame 0 = t_s; nothing sounds before tick 0). It ends at the timer IRQ
// after tick 16870 (music time 372.11296 s), where main stops the music in the capture (0x1a9a2 turns the output
// off): the original cuts there. The voices are evaluated at the output rate with the card's pitch, ramps and
// timing (gf1.h), and the mix is normalised to 90 % of its peak (LCA's player sets no master volume).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct Soundtrack {
    int rate = 44100;              // stereo int16 interleaved
    std::vector<int16_t> pcm;
    double startTime = 0;          // music time (s) of the first player tick (0.02048)
    double timeOfTick(int tick) const;     // music time of player tick n (timer 1, then GUS timer 2 periods)
    int positionAt(double t) const;        // the player's position word (order<<8 | row; names the next row) at time t
    double timeOfPosition(int pos) const;  // first time positionAt(t) >= pos (+infinity if never)

    // filled by renderSoundtrack (use the functions above)
    std::vector<double> tickTimes;  // music time of tick k (the capture's 16,871 ticks)
    std::vector<int> positions;     // position after tick k; before tick 0 it is pos0
    std::vector<int> reached;       // running maximum of positions (for timeOfPosition)
    int pos0 = 0;
};

// module: overlay file 1, unpacked (780,560 bytes); image: the unpacked program image. Throws std::runtime_error
// if they are not what the player expects. Takes well under a second (the mix runs on worker threads).
Soundtrack renderSoundtrack(std::span<const uint8_t> module, std::span<const uint8_t> image, int rate);

// For checks (tools/test_music.cpp): the mix before normalisation at any frame rate, interleaved stereo float, frame
// 0 = music time 0 (1.0 = one voice at full scale and gain 1, hard panned). At rate 617400 / 32 the frames are the
// GF1's own, as lcaplay.py --wav renders them. Also fills `times` like renderSoundtrack. `voices`: the GF1 voices
// mixed (bit v = voice v; channel c plays on voices 2c and 2c + 1).
std::vector<float> renderMix(std::span<const uint8_t> module, std::span<const uint8_t> image, double rate,
                             Soundtrack& times, double* peak, uint32_t voices = 0xffffffffu);

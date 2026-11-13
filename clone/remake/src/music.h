// clone meets clone's soundtrack (music by Simon): the module the intro synthesises at start-up (modsynth.cpp)
// played by a port of its MXMPlay build (mxmplay.cpp) on a model of the GUS (gf1.cpp), rendered whole at load time.
// See clone/re/notes-music.md.
//
// Music time: t = xmpGetTimer / 1,193,182 s (PIT clocks since the music start, as the original's clock reads it;
// t70 = t x 1,193,182 / 17,043). t = 0 at the second IRQ0 after xmpPlay, the instant at which the first row
// (00:00) reaches the GUS; xmpPlay itself is at t = -87,636 / 1,193,182 = -0.07345 s (the timer is installed with a
// first period of 0x10000 PIT clocks, then 22,100 per player tick). Player tick n runs in IRQ n + 1, at music time
// (n - 1) x 22,100 / 1,193,182 s (timeOfTick); it reads row R = order x 64 + row at tick 3R, and that row is heard
// from tick 3R + 1, at music time R x 66,300 / 1,193,182 s.
//
// pcm frame i plays at music time i / rate (frame 0 = t 0; nothing sounds before it). It ends at the IRQ after the
// one at which the player leaves the last row of the intro's sync table (3c:3f, event -1: IRQ 11,713 wraps to
// 00:00), music time 11,712 x 22,100 / 1,193,182 = 216.929 s: the last row is heard for its whole three ticks, as in
// mxmplay.py --wav. The original stops the music (xmpStop) at its first frame after that IRQ, which depends on the
// frame rate (in the capture before the next IRQ).
//
// The voices are evaluated at the output rate with the card's pitch, ramps and timing (gf1.h): the register writes
// are those a real card gets (the player reads the voices' current volume from the card's model running at its own
// 34,300 Hz), each applied at its exact instant. The mix goes through a 5 Hz one-pole high-pass (the card's AC-
// coupled line output, as STARS) and is normalised so that its peak is at -1 dBFS.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct Soundtrack {
    int rate = 48000;               // stereo int16 interleaved
    std::vector<int16_t> pcm;
    double timeOfTick(int tick) const;     // music time of player tick n (IRQ n + 1)
    int positionAt(double t) const;        // xmpGetPos at music time t: order << 8 | row (names the row last read)
    double timeOfPosition(int pos) const;  // first music time at which positionAt(t) >= pos (+infinity if never)
    double endTime() const { return rate > 0 ? double(pcm.size() / 2) / rate : 0.0; }

    // filled by renderSoundtrack (use the functions above)
    std::vector<double> irqTimes;   // music time of IRQ k at index k - 1 (the capture's 11,713)
    std::vector<int> positions;     // xmpGetPos after IRQ k
    std::vector<int> reached;       // running maximum of positions (for timeOfPosition)
};

// image: the unpacked program image (address 0x400000 at offset 0). Throws std::runtime_error if it is not what the
// player expects. Takes well under a second (the module's samples and the mix are computed on worker threads).
Soundtrack renderSoundtrack(std::span<const uint8_t> image, int rate);

// ---- for checks (tools/test_music.cpp) ----

// MXMPlay from xmpInit to xmpStop, recording what the capture recorded (clone/re/full.*). fakeGus = true: the
// capture's fake GUS (a volume read answers the last write to reg 9); false: the card's model answers the reads, as
// on hardware (mxmplay.py --real-volume-read). Log ticks as run_clone.py: xmpPlay at 10, IRQ k at 10 + k, xmpStop
// after the last IRQ (at 10 + K).
struct MusicTrace {
    std::vector<uint8_t> module;  // the module buffer of 0x414aee (1,200,000 bytes)
    std::string gus;              // GUS register writes in run_clone.py's text format ("tick vN rXX value")
    std::vector<uint8_t> dram;    // GUS DRAM after the upload (1 MB)
    std::vector<int> pos;         // xmpGetPos after IRQ k at index k - 1
    std::vector<int32_t> timer;   // xmpGetTimer at the end of IRQ k's period (full.ticks column 6)
};
MusicTrace traceMusic(std::span<const uint8_t> image, bool fakeGus);

// The mix before the high-pass and normalisation at any frame rate, interleaved stereo float, frame 0 = music
// time 0 (1.0 = one voice at full scale and gain 1, hard panned). Also fills `times` like renderSoundtrack.
// `voices`: the GF1 voices mixed (bit v = voice v = channel v). cardGrid (only at the card's 34,300 Hz): every write
// takes effect at the card frame at which the card's model received it instead of its exact instant, as in
// mxmplay.py --wav (whose frame 635 is frame 0 here).
std::vector<float> renderMix(std::span<const uint8_t> image, int rate, Soundtrack& times, double* peak,
                             uint32_t voices = 0xffffffffu, bool cardGrid = false);

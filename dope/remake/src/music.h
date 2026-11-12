// DOPE's soundtrack: DOPE.MOD played by a port of the loader's ProTracker player for the GUS (modplay.cpp) on a model
// of the GUS GF1 (gf1.cpp), rendered whole at load time (dope/re/notes-music.md).
//
// Music time: seconds on the GUS clock from the timer-2 overflow that the capture serves as its log tick 2 (full.gus;
// capture time 0.110400 s = 345 x 320 us after the GUS init started the timer; "the first music tick" of
// notes-core.md section 6, whose column "after the first music tick" is music time). Main calls the player start
// 0x2c17 right after that interrupt; player tick 0 (row 00:00) comes one timer period later: timeOfTick(0) = 0.01984
// (62 x 320 us, the image's count 0xc2), ticks 1 and 2 at the same period, then 22.40 ms per tick (tempo 111) up to
// tick 1730 and 20.48 ms per tick (tempo 122) after it (notes section 7.2). Music time = capture time - 0.1104 s.
//
// Lost ticks (notes 7.3): each part's PIT calibration runs with interrupts off for 20-55 ms and holds the GUS
// interrupt; when that exceeds a tick period the overflow is lost and the music falls a tick behind the GUS clock. On
// real hardware the count depends on where the calibration falls between two overflows; the soundtrack uses the
// capture's (captureHolds(): one tick delayed by 20.5 ms at p1, one lost at p2, two at p3, one at p4..p7; the music is
// 145.28 ms behind the closed form at the end). The holds are tied to the capture's part starts; renderSoundtrack
// with other holds places them elsewhere (an empty list: no lost ticks, every tick on the GUS clock).
//
// pcm frame i plays at music time i / rate; nothing sounds before tick 0. A note is heard 0.16..1.84 ms after its tick
// (the old note ramps down first and the update waits for the last ramp of the tick, notes 9.4). The capture stops
// the music after player tick 22202 (log tick 22205, position 6d:28), before the overflow at captureEnd; the remake's p7
// exit fade runs at its hardware speed, 3.8 s longer than in the capture (notes-core 5.2), so the soundtrack goes on
// for 500 more ticks (10.24 s; the song continues to order 7b) and the caller stops it where its TERM comes. The
// voices are evaluated at the output rate with the card's pitch, ramps and timing (gf1.h); the output passes the
// GUS's AC coupling (a 5 Hz high-pass) and is normalised to 90 % of its peak.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct Soundtrack {
    int rate = 44100;              // stereo int16 interleaved
    std::vector<int16_t> pcm;      // pcm frame 0 = music time 0
    double timeOfTick(int tick) const;     // music time of player tick n (served; n = 0 reads row 00:00)
    int positionAt(double t) const;        // order<<8 | row (the row read last) at music time t; 0x003f before tick 0
    double timeOfPosition(int pos) const;  // first tick time with positionAt(t) >= pos (the 0x003f before tick 0
                                           // does not count; +infinity if never)

    // filled by renderSoundtrack (use the functions above)
    std::vector<double> tickTimes;  // music time of player tick k (to the microsecond)
    std::vector<int> positions;     // position after tick k
    std::vector<int> reached;       // running maximum of positions from tick 0 on (for timeOfPosition)
    int pos0 = 0x003f;              // position after the start, before tick 0 ([0x1aa0], [0x1a9c])
    double captureEnd = 0;          // music time of the timer overflow after the capture's last tick (458.19904)
};

// Interrupts held off (music time, s): a GUS interrupt raised in [from, to) is served at `to`, and a second timer
// overflow in the window is lost.
struct IrqHold {
    double from, to;
};
// The capture's holds at the part starts p1..p7: `to` is when the capture served the delayed timer interrupt, `from`
// 1 ms before the overflow it delayed (the calibration's start is not in the capture; any time after the previous
// overflow gives the same ticks).
std::vector<IrqHold> captureHolds();

// mod: DOPE.MOD; loader: the unpacked loader image (dope/re/unpacked/loader.bin, code32 at file offset 0x17e0).
// Throws std::runtime_error if they are not what the player expects. Takes about 0.2 s on 20 threads, 0.5 s on 4,
// 0.9 s on one (the mix runs on worker threads; the result does not depend on their number).
Soundtrack renderSoundtrack(std::span<const uint8_t> mod, std::span<const uint8_t> loader, int rate);
Soundtrack renderSoundtrack(std::span<const uint8_t> mod, std::span<const uint8_t> loader, int rate,
                            std::span<const IrqHold> holds);

// For checks (tools/test_music.cpp): the mix before the high-pass and the normalisation, interleaved stereo float
// (1.0 = one voice at full scale and gain 1, hard panned), and the times as renderSoundtrack fills them.
struct MixSpec {
    double rate = 44100;
    std::vector<IrqHold> holds = captureHolds();
    bool loadHold = true;        // the capture's module load holds the first interrupt until 0.110398 s (capture time)
    int ticks = 0;               // timer interrupts (full.gus numbering) before the stop; 0: the soundtrack's
    bool fromTickZero = false;   // frame 0 = player tick 0 (dopeplay.py --wav) instead of music time 0
    uint32_t voices = 0xffffffffu;  // the GF1 voices mixed (bit v = voice v = channel v)
};
std::vector<float> renderMix(std::span<const uint8_t> mod, std::span<const uint8_t> loader, const MixSpec& spec,
                             Soundtrack& times, double* peak);

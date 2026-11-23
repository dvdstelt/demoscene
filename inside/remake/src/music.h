// Inside's soundtrack ("eyegabo0om" by Yolk and Legend, EYE.AMF): DSMI's AMP 2.00 player (amp.*) as the demo drives
// it, on DSMI's GUS driver (gusdrv.*) and a model of the GUS (gf1.*), rendered whole at load time
// (inside/re/notes-music.md).
//
// Clock: the player runs in the demo's timer callback, 4 per screen refresh (demo.h), on DSMI time that advances 35
// units per callback (inside/re/notes-core.md section 1). The callback count M (1 at the music start) is the master
// clock: record k of the capture follows the retrace callback M = 4k - 150. Song row R starts in callback
// rowStart[R], as the player reads it. pcm frame 0 sounds at the display time of record startRefresh (= 37,
// program time 38 / 70.0863 s); music time t (seconds, the playback clock) is refresh startRefresh + t * kRefreshHz.
// Every GUS write sounds at the program time of its callback (notes-core 1.2: callback 4k - 150 + s, s = 0..3, at
// (k + 412 / 449) / 70.0863 s + s x 4,085 / 1,193,182 s) plus the card frames the driver's ramp waits let pass
// before it. The pcm ends at the display time of endRefresh, the record after the one in which the script's last
// entry runs (the demo exits; the exit's stopNote / SDIStopOutput writes are applied in that callback).
//
// The voices are evaluated at the output rate with the card's pitch, ramps and timing (gf1.h): the register writes
// are those a real card gets (the driver reads the voices' current volume from the card's model running at its own
// 25,725 Hz, and its ramp waits take card time), each applied at its exact instant. The mix goes through a 5 Hz
// one-pole high-pass (the card's AC-coupled line output) and is normalised so that its peak is at -1 dBFS.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class InsideExe;

struct Soundtrack {
    int rate = 48000;                 // stereo int16 interleaved
    std::vector<int16_t> pcm;
    double startRefresh = 0;          // refresh at which pcm frame 0 plays
    double endRefresh = 0;            // refresh at which the demo ends (after the last script entry)

    // The callback in which song row R (= order * 64 + row: the song plays its orders in sequence) starts.
    std::vector<int> rowStart;
    int positionAtCallback(double m) const;      // the row the script sees after callback m (-1 before the first)
    int positionAt(double refresh) const;        // ... at record `refresh` (after its retrace callback)
    double rowCallback(int row) const;           // rowStart[row] (+inf past the end)
    double refreshOf(double t) const;            // music time t (s) -> refresh
    double timeOf(double refresh) const;         // refresh -> music time t (s)
};

// amf: EYE.AMF; dsd: SDIGUS.DSD, DSMI's GUS driver (the remake reads its volume table, notes-music.md). Throws
// std::runtime_error if they are not the files the player expects. Takes well under a second (the mix is computed on
// worker threads).
Soundtrack renderSoundtrack(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, int rate);

// ---- for checks (tools/test_music.cpp) ----

// The music from the driver's init to the exit as the capture recorded it (inside/re/full.gus, full.ticks).
// fakeGus = true: the capture's fake GUS (a volume read answers the last write to reg 9, the ramp waits end at once);
// false: the card's model answers the reads, as on hardware (models/music/render.py's card). lostIrqs: timer
// interrupts whose callback did not run (the capture lost 12117 in the credits; the remake loses none). Log stamps:
// the start-up writes at firstIrq - 1 (the capture's 86..90 depend on the machine), then the timer interrupt; the
// exit with the interrupt of the callback in which the script ends.
struct MusicTrace {
    int firstIrq = 0;             // the timer interrupt of callback 1 (91)
    std::string gus;              // GUS register writes in full.gus's text format
    size_t startupLines = 0;      // its first lines: SDIInitDevice, setupChannels, the pans and the uploads
    std::vector<uint8_t> dram;    // GUS DRAM after the upload (the samples XOR 0x80, as a GF1 stores them)
    std::vector<uint32_t> uploads;  // DRAM address and length of every uploaded sample (pairs)
    std::vector<int> pos;         // order * 64 + row after interrupt firstIrq + k
    std::vector<int> rowStart;    // the callback in which song row R starts
    int exitIrq = 0, exitCallback = 0;  // the interrupt and callback after which the demo exits
    std::vector<int> waited;      // with the card's model: card frames the ramp waits ran in interrupt firstIrq + k
};
MusicTrace traceMusic(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, bool fakeGus,
                      std::span<const int> lostIrqs);

// The mix before the high-pass and normalisation at any frame rate, interleaved stereo float (1.0 = one voice at
// full scale and gain 1, hard panned), frame 0 = pcm frame 0 of renderSoundtrack. Also fills `times` like
// renderSoundtrack. `voices`: the GF1 voices mixed (bit v = voice v = channel v). cardGrid (only at the card's
// 25,725 Hz): every write takes effect at the card frame at which the card's model received it instead of its exact
// instant, and frame 0 is record 0 of the capture, as in models/music/render.py --skip ''; the mix then runs to the
// end of the interrupt period in which the demo exits.
std::vector<float> renderMix(const InsideExe& exe, std::span<const uint8_t> amf, std::span<const uint8_t> dsd, int rate,
                             Soundtrack& times, double* peak, uint32_t voices = 0xffffffffu, bool cardGrid = false);

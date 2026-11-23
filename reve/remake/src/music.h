// Reve's soundtrack: ZIC\PULSE.MOD ("PULSE" by Shad, 8 channels) played by a port of LCA's MOD-Player V3.03
// (lcaplay.cpp) on a model of the GUS GF1 (gf1.cpp), rendered whole at load time (reve/re/notes-music.md).
//
// Clock: screen refreshes of the capture (refresh k = record k of reve/re/full.frames, taken at the start of the
// vertical retrace, program time (k + 412 / 449) / 70.0863 s; the demo's timer interrupt runs at that retrace and
// reads the music position there). The player is clocked by the GUS timers alone, from the moment 0x26ec3e starts
// them: startRefresh (29.0000000059 in the capture: the retrace at which main installed its timer). Music time t
// (seconds since then) and refresh r are related by r = startRefresh + t * 70.0863 (both are the same crystal in the
// capture, run_reve.py's PIT clock). Tick k of the player is at music time tickTimes[k]: the GUS timers'
// periods, (256 - count) x 80 us on timer 1 or x 320 us on timer 2 (notes-music.md section 5): 20.48 ms for tick 0,
// then 31.04 ms (tempo 80, timer 2) and 16.64 ms (tempo 150, timer 1) with the switches the song makes.
//
// pcm frame i plays at music time i / rate (frame 0 = startRefresh). It ends at endRefresh, where main stops the
// music (0x2602c6 turns the output off): after the last script entry (end position 40:3f, where the song's F00 has
// stopped the player and the voices hold) main shows the title picture and waits 210 retraces (endRefresh = 17728 =
// 17518 + 210 in the capture; the song has faded out by then: the last 3.7 s are silent). The voices are
// evaluated at the output rate with the card's pitch, ramps and timing (gf1.h); the mix goes through a 5 Hz one-pole
// high-pass (the card's AC-coupled output) and is normalised so that its peak is at -1 dBFS (LCA's player sets no
// master volume).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

class ReveExe;

struct Soundtrack {
    int rate = 48000;               // stereo int16 interleaved
    std::vector<int16_t> pcm;
    double startRefresh = 0;        // the refresh (fractional) at which pcm frame 0 sounds: the GUS timers start
    double endRefresh = 0;          // the refresh at which main stops the music and the demo ends; the pcm ends there

    // The position word order << 8 | row (the next row the player reads) as the timer interrupt's call of 0x2602eb
    // sees it at the retrace of `refresh`: after every player tick whose timer overflow comes before that retrace.
    int positionAt(double refresh) const;
    double refreshOfPosition(int pos) const;  // the first refresh whose retrace sees a position >= pos (+inf if never)
    double refreshOf(double t) const;         // music time t (s, t = 0 at startRefresh) -> refresh
    double timeOf(double refresh) const;      // refresh -> music time (s)
    double timeOfTick(int tick) const;        // music time of player tick n

    // filled by renderSoundtrack (use the functions above)
    std::vector<double> tickTimes;  // music time of tick k
    std::vector<int> positions;     // position after tick k; before tick 0 it is pos0
    std::vector<int> reached;       // running maximum of positions (for refreshOfPosition)
    int pos0 = 0;
};

// The whole soundtrack from REVE.EXE: the module ZIC\PULSE.MOD, the player's tables and the script table's last end
// position from REVE.LFL. Throws std::runtime_error if they are not what the player expects. Takes well under a
// second (the mix runs on worker threads).
Soundtrack renderSoundtrack(const ReveExe& exe, int rate);
// The same from the unpacked files: module = ZIC\PULSE.MOD, image = REVE.LFL's image (at 0x20427c).
Soundtrack renderSoundtrack(std::span<const uint8_t> module, std::span<const uint8_t> image, int rate);

// ---- for checks (tools/test_music.cpp) ----

// The capture's clock (reve/re/run_reve.py) and what was measured in it (reve/re/models/music/run_reve.py, see
// notes-music.md section 5).
namespace capture {
constexpr double kPitHz = 1193182.0;                         // run_reve.py's clock
constexpr double kRefreshHz = 25175000.0 / (800.0 * 449.0);  // 70.0863 Hz: 449 lines of 800 pixel clocks
constexpr double kRetraceLine = 412.0 / 449.0;               // record k is taken at line 412 of refresh k
// The program time (PIT ticks) at which 0x26ec3e starts the GUS timers: the retrace of refresh 29 (main installs its
// timer right after a retrace and starts the music at once; the emulator runs that code at the retrace's time).
constexpr double kTimerStartPit = 509331.124025;
// main's stop (0x2602c6) comes this many refreshes after the retrace at which the timer interrupt sees the last
// script entry's end position: main draws the title picture (0x20538d, 0.44 refresh in the capture) and waits for 210
// retraces (0x2045d0 x 210); the stop runs right after the last of them (refresh 17518 -> 17728 in the capture).
constexpr double kStopAfterLastEntry = 210.0;
}  // namespace capture

// The last script entry's end position (script table 0x205ed0 in the image).
int lastEndPosition(std::span<const uint8_t> image);

// The mix before the high-pass and normalisation at any frame rate, interleaved stereo float (1.0 = one voice at full
// scale and gain 1, hard panned), frame 0 = music time 0. Also fills `times` like renderSoundtrack. At rate 617400 / 16
// the frames are the GF1's own, as lcaplay.py --wav renders them. `voices`: the GF1 voices mixed (bit v = voice v;
// channel c plays on voices 2c and 2c + 1).
std::vector<float> renderMix(std::span<const uint8_t> module, std::span<const uint8_t> image, double rate,
                             Soundtrack& times, double* peak, uint32_t voices = 0xffffffffu);

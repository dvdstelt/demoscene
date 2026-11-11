// JIZZ's soundtrack: Probe's module, expanded from JIZZ.EXE by a port of the intro's decruncher, played by a
// port of MXMPlay (Niklas Beisert's XM player for the Gravis UltraSound) through a model of the GUS GF1 chip
// (see jizz/re/notes-music.md). The whole soundtrack is rendered at load time; playback goes through SDL3, and
// the audible position is the demo's clock, in seconds of music time.
//
// Music time 0 is the moment xmpPlay starts the music (the end of the precalc; tick 434 of the reference capture).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class JizzExe;
struct SDL_AudioStream;

class Music {
public:
    static constexpr double kPitHz = 1193182.0;  // PIT input clock: the unit of xmpGetTimer

    Music();
    ~Music();
    bool load(const JizzExe& exe, std::string* err);  // expand the module and render the soundtrack

    bool startPlayback();  // opens the default audio device; without one the clock runs on SDL's timer
    void stopPlayback();
    double time();         // audible position in seconds of music time (smoothed, monotonic between seeks)
    void seek(double t);
    void setPaused(bool p);
    bool paused() const { return paused_; }
    bool ended() const { return ended_; }  // reached length()
    double length() const { return length_; }  // music time at which the intro exits (sync event -1)

    // Player state at music time t, as the original sees it:
    double timer(double t) const;      // xmpGetTimer (0x29f44): PIT clocks, interpolated within the player tick
    int position(double t) const;      // xmpGetPos (0x299c4): order << 8 | row
    double positionTime(int pos) const;  // music time at which `position` first reaches pos (order << 8 | row)

    // The whole soundtrack, interleaved stereo float at rate() Hz; sample 0 = music time 0.
    const std::vector<float>& pcm() const { return pcm_; }
    double rate() const { return rate_; }

private:
    double clockLocked(uint64_t ns);  // smoothed, monotonic audible position (lock_ held)
    static void audioCallback(void* user, SDL_AudioStream* stream, int additional, int total);

    struct RowStart { int pos; double time; };
    std::vector<RowStart> rows_;  // every row the player reaches, in playing order
    double timer0_ = 0;           // xmpGetTimer at music time 0
    double length_ = 0;

    std::vector<float> buf_;     // callback scratch
    bool ownAudioInit_ = false;  // startPlayback initialised SDL's audio subsystem
    std::vector<float> pcm_;
    double rate_ = 48000;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> ended_{false};
    size_t playPos_ = 0;          // next sample frame handed to SDL (guarded by lock_)
    double posAtCallback_ = 0;    // audible position (s) when the last block was handed over
    uint64_t nsAtCallback_ = 0;   // SDL_GetTicksNS() at that moment
    double lastTime_ = 0;         // last value returned by time()
    uint64_t lastNs_ = 0;         // SDL_GetTicksNS() of that call (0 = re-anchor at the next call)
    bool awaitCallback_ = false;  // device open, position anchored, first callback not yet seen
    bool haveDevice_ = false;
};

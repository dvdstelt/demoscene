// Paper's soundtrack: the intro's own tracker player (code32 0x1ed6) driving a model of the Gravis UltraSound
// GF1, ported from paper/re/player_ref.py (see paper/re/notes-music.md). The tune and the 8 samples are read from
// PAPER.EXE. The whole soundtrack is rendered at load time; playback goes through SDL3, and the audible position
// is the demo's clock, in IRQ0 ticks (70.0018 Hz) since program start.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class PaperExe;
struct SDL_AudioStream;

class Music {
public:
    static constexpr double kTickHz = 1193182.0 / 0x4295;  // IRQ0 rate, 70.0018 Hz
    static constexpr int kMusicStart = 104;                 // first tick whose IRQ runs the player (counter 0)
    static constexpr int kEndTick = 12235;                  // the intro exits in this tick (sound off)

    Music();
    ~Music();
    bool load(const PaperExe& exe, std::string* err);  // parse tune + samples and render the soundtrack

    bool startPlayback();  // opens the default audio device; without one the clock runs on SDL's timer
    void stopPlayback();
    double tick();         // audible position in ticks since program start (smoothed, monotonic between seeks)
    void seekTick(double t);
    void setPaused(bool p);
    bool paused() const { return paused_; }
    bool ended() const { return ended_; }  // reached kEndTick

    // Player state as the IRQ of tick t leaves it: music counter [0x1ea8b] (-1 before kMusicStart) and the
    // sub-row countdown [0x1e4e6] (6..1 within a row at speed 6).
    int counter(int t) const;
    int rowCountdown(int t) const;

    // The whole soundtrack, interleaved stereo float at rate() Hz; sample 0 = start of tick 0.
    const std::vector<float>& pcm() const { return pcm_; }
    double rate() const { return rate_; }

private:
    friend struct MusicTest;  // tools/test_music.cpp
    // load() with an optional register-write log in player_ref.py's .gus text format (test hook).
    bool render(const PaperExe& exe, std::string* err, std::string* gusLog);
    double clockLocked(uint64_t ns);  // smoothed, monotonic audible position (lock_ held); updates lastTick_
    static void audioCallback(void* user, SDL_AudioStream* stream, int additional, int total);
    std::vector<float> buf_;       // callback scratch
    bool ownAudioInit_ = false;    // startPlayback initialised SDL's audio subsystem
    std::vector<float> pcm_;
    double rate_ = 0;
    std::vector<int16_t> counter_, countdown_;  // per tick
    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> ended_{false};
    size_t playPos_ = 0;            // next sample frame handed to SDL (guarded by lock_)
    double posAtCallback_ = 0;      // audible position in ticks when the last block was handed over
    uint64_t nsAtCallback_ = 0;     // SDL_GetTicksNS() at that moment
    double lastTick_ = 0;           // last value returned by tick()
    uint64_t lastNs_ = 0;           // SDL_GetTicksNS() of that call (0 = re-anchor at the next call)
    bool awaitCallback_ = false;    // device open, position anchored, first callback not yet seen
    bool haveDevice_ = false;
};

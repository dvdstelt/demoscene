// Playback and the demo clock (the pattern of the Paper and JIZZ remakes' music.cpp): the rendered soundtrack
// (Soundtrack::pcm, music.h) is handed to SDL3, and its audible position, in seconds of music time, drives the
// picture. Without an audio device the clock runs on SDL's timer. After the end of the PCM (the music stops at 9a:34,
// four seconds before the demo ends) the clock runs on in silence up to `length`.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

struct SDL_AudioStream;

class Player {
public:
    Player();
    ~Player();
    // The sound to play (interleaved stereo int16 at `rate`, sample 0 = music time 0; kept by reference) and the
    // music time at which the demo ends.
    void setSound(const std::vector<int16_t>* pcm, int rate, double length);

    bool start();  // opens the default audio device; without one the clock runs on SDL's timer
    void stop();
    double time();  // audible position in seconds of music time (smoothed, monotonic between seeks)
    void seek(double t);
    void setPaused(bool p);
    bool paused() const { return paused_; }
    bool ended() const { return ended_; }  // reached the length
    double length() const { return length_; }

private:
    double clockLocked(uint64_t ns);  // smoothed, monotonic audible position (lock_ held)
    static void audioCallback(void* user, SDL_AudioStream* stream, int additional, int total);

    const std::vector<int16_t>* pcm_ = nullptr;
    int rate_ = 44100;
    double length_ = 0;
    std::vector<int16_t> buf_;   // callback scratch
    bool ownAudioInit_ = false;  // start() initialised SDL's audio subsystem
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

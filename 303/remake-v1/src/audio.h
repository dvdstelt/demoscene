// Music: GARLIC.PAK (PolyTracker module) + KETCHUP.PAK (MPEG-1 layer II vocals).
// 303's MIDAS hack pulled samples with no DOS filename out of the decoded vocal stream;
// we rebuild the module in memory with those samples filled in and play it with libopenmpt.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioStream;
namespace openmpt { class module; }

class Music {
public:
    Music();
    ~Music();
    bool load(const std::string& ptmPath, const std::string& mp2Path, std::string* err = nullptr);
    bool startPlayback();         // opens the default audio device
    void stopPlayback();
    double time();                // current audible music time in seconds (smoothed)
    void seek(double seconds);
    void setPaused(bool p);
    bool paused() const { return paused_; }
    double duration() const;
    bool ended() const { return ended_; }
    // offline rendering (for WAV export / analysis)
    std::vector<float> renderAll(int rate);
    // module position helpers
    int currentOrder();
    int currentRow();
    const std::vector<uint8_t>& rebuiltModule() const { return ptm_; }

    static const int kRate = 48000;

private:
    static void audioCallback(void* user, SDL_AudioStream* stream, int additional, int total);
    std::vector<uint8_t> ptm_;
    std::unique_ptr<openmpt::module> mod_;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> ended_{false};
    // timing (guarded by lock_)
    double posAtCallback_ = 0.0;   // module seconds at the end of the last rendered block
    uint64_t ticksAtCallback_ = 0; // SDL_GetTicksNS() when that block was handed over
    double lastTime_ = 0.0;
    std::vector<float> buf_;
};

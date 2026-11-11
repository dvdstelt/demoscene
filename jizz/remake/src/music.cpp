// Soundtrack playback and the demo clock (the pattern of the Paper remake's music.cpp): the rendered soundtrack
// is handed to SDL3, and the audible position, in seconds of music time, drives the picture. The soundtrack itself
// (module expansion, MXMPlay and the GF1 model) is rendered by renderSoundtrack() in mxmplay.cpp.
#include "music.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "mxmplay.h"

namespace {
constexpr int kFrameBytes = int(sizeof(float) * 2);
}

Music::Music() { nsAtCallback_ = SDL_GetTicksNS(); }
Music::~Music() { stopPlayback(); }

bool Music::load(const JizzExe& exe, std::string* err) {
    Soundtrack s;
    try {
        if (!renderSoundtrack(exe, s, err)) return false;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
    std::lock_guard<std::mutex> g(lock_);
    pcm_ = std::move(s.pcm);
    rate_ = s.rate;
    rows_.clear();
    for (const auto& r : s.rows) rows_.push_back({r.pos, r.time});
    timer0_ = s.timer0;
    length_ = s.length;
    return true;
}

double Music::timer(double t) const { return timer0_ + t * kPitHz; }

int Music::position(double t) const {
    if (rows_.empty()) return 0;
    auto it = std::upper_bound(rows_.begin(), rows_.end(), t, [](double v, const RowStart& r) { return v < r.time; });
    return it == rows_.begin() ? rows_.front().pos : std::prev(it)->pos;
}

double Music::positionTime(int pos) const {
    for (const RowStart& r : rows_)
        if (r.pos >= pos) return r.time;
    return length_;
}

// ---- playback ----

void Music::audioCallback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    // SDL holds the stream's lock while calling this; lock_ is taken second (main thread: same order).
    Music* m = static_cast<Music*>(user);
    int frames = additional / kFrameBytes;
    if (frames <= 0) return;
    std::lock_guard<std::mutex> g(m->lock_);
    m->buf_.assign(size_t(frames) * 2, 0.0f);
    if (!m->paused_) {
        size_t total = m->pcm_.size() / 2;
        if (m->playPos_ < total) {
            size_t k = std::min(size_t(frames), total - m->playPos_);
            std::memcpy(m->buf_.data(), m->pcm_.data() + m->playPos_ * 2, k * 2 * sizeof(float));
        }
        m->playPos_ += size_t(frames);  // silence after the end
    }
    SDL_PutAudioStreamData(stream, m->buf_.data(), frames * kFrameBytes);
    if (!m->paused_ && m->rate_ > 0) {
        // audible "now" = everything handed over minus what is still queued in the stream
        int queued = SDL_GetAudioStreamQueued(stream) / kFrameBytes;
        m->posAtCallback_ = (double(m->playPos_) - double(queued)) / m->rate_;
        m->nsAtCallback_ = SDL_GetTicksNS();
        m->awaitCallback_ = false;
        if (m->posAtCallback_ >= m->length_) m->ended_ = true;
    }
}

bool Music::startPlayback() {
    stopPlayback();
    {
        std::lock_guard<std::mutex> g(lock_);
        // the clock (re)starts now from the current position, with or without a device
        posAtCallback_ = lastTime_;
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
    }
    if (pcm_.empty() || rate_ <= 0) return false;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("audio: %s", SDL_GetError());
            return false;
        }
        ownAudioInit_ = true;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = int(std::lround(rate_));
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Music::audioCallback, this);
    if (!s) {
        SDL_Log("audio: %s", SDL_GetError());
        if (ownAudioInit_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            ownAudioInit_ = false;
        }
        return false;
    }
    SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        stream_ = s;
        haveDevice_ = true;
        posAtCallback_ = lastTime_;
        playPos_ = size_t(std::llround(lastTime_ * rate_));
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
        awaitCallback_ = true;
    }
    SDL_UnlockAudioStream(s);
    SDL_ResumeAudioStreamDevice(s);
    return true;
}

void Music::stopPlayback() {
    if (!stream_) return;
    // SDL_Quit destroys all streams; only touch ours while the audio subsystem is still up. After
    // SDL_DestroyAudioStream no callback runs any more.
    const bool alive = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    if (alive) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    if (ownAudioInit_ && alive) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownAudioInit_ = false;
    std::lock_guard<std::mutex> g(lock_);
    double t = clockLocked(SDL_GetTicksNS());  // continue on the timer from the last audible position
    haveDevice_ = false;
    awaitCallback_ = false;
    posAtCallback_ = t;
    nsAtCallback_ = lastNs_;
    playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_)) : 0;
}

double Music::clockLocked(uint64_t now) {
    constexpr double kStartWait = 0.5;  // s: longest wait for the first callback before running anyway
    constexpr double kSnap = 0.1;       // s: larger errors are not slewed
    constexpr double kSlewTau = 0.5;    // s: time constant of the slew towards the callback estimate
    double raw = posAtCallback_;        // position estimate: last callback + elapsed time
    if (!paused_ && now > nsAtCallback_) {
        double el = double(now - nsAtCallback_) * 1e-9;
        if (awaitCallback_) el = std::max(0.0, el - kStartWait);  // hold until the device starts pulling
        raw += el;
    }
    double t = raw;
    if (haveDevice_ && !paused_ && lastNs_ != 0 && now > lastNs_) {
        // The callback estimate jitters by a few ms (device period, scheduling). Run at the nominal rate from the
        // last returned value and slew towards the estimate (rate within +-25%); snap when it is far off.
        double dt = double(now - lastNs_) * 1e-9, pred = lastTime_ + dt, err = raw - pred;
        if (std::fabs(err) < kSnap) {
            double lim = 0.25 * dt;
            t = pred + std::clamp(err * std::min(1.0, dt / kSlewTau), -lim, lim);
        }
    }
    t = std::max(t, lastTime_);  // never backwards between seeks
    lastTime_ = t;
    lastNs_ = now;
    if (t >= length_) ended_ = true;
    return t;
}

double Music::time() {
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    double t;
    {
        std::lock_guard<std::mutex> g(lock_);
        t = clockLocked(SDL_GetTicksNS());
    }
    if (s) SDL_UnlockAudioStream(s);
    return t;
}

void Music::seek(double t) {
    t = std::max(0.0, t);
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_)) : 0;
        if (s) SDL_ClearAudioStream(s);
        posAtCallback_ = t;
        lastTime_ = t;
        nsAtCallback_ = SDL_GetTicksNS();
        lastNs_ = 0;
        awaitCallback_ = haveDevice_;
        ended_ = t >= length_;
    }
    if (s) SDL_UnlockAudioStream(s);
}

void Music::setPaused(bool p) {
    SDL_AudioStream* s = stream_;
    if (s) SDL_LockAudioStream(s);
    {
        std::lock_guard<std::mutex> g(lock_);
        if (p != paused_) {
            uint64_t now = SDL_GetTicksNS();
            if (p) {
                // freeze at the current position; playback resumes from exactly there
                double t = clockLocked(now);
                posAtCallback_ = t;
                playPos_ = rate_ > 0 ? size_t(std::llround(t * rate_)) : 0;
            }
            if (s) SDL_ClearAudioStream(s);  // drop queued music on pause, queued silence on resume
            nsAtCallback_ = now;
            lastNs_ = 0;
            awaitCallback_ = haveDevice_ && !p;
            paused_ = p;
        }
    }
    if (s) SDL_UnlockAudioStream(s);
}

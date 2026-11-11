#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <libopenmpt/libopenmpt.hpp>
#include <sstream>

#include "assets.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD_DETECT_AVX
#include "minimp3.h"

namespace {
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

std::vector<int16_t> decodeMp2(const Bytes& d) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    static short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    std::vector<int16_t> out;
    out.reserve(2500000);
    size_t pos = 0;
    while (pos < d.size()) {
        int n = mp3dec_decode_frame(&dec, d.data() + pos, int(std::min<size_t>(d.size() - pos, 1 << 20)), pcm, &info);
        if (info.frame_bytes == 0) break;
        pos += size_t(info.frame_bytes);
        for (int i = 0; i < n; i++) out.push_back(pcm[i * info.channels]);
    }
    return out;
}

// Rebuild the PTM so the vocal samples (no DOS filename, data in KETCHUP.PAK) carry real 16-bit data.
std::vector<uint8_t> rebuildPtm(const Bytes& ptm, const std::vector<int16_t>& vox) {
    std::vector<uint8_t> out(ptm.begin(), ptm.end());
    const int nsmp = rd16(&ptm[34]);
    for (int i = 0; i < nsmp; i++) {
        size_t h = 0x260 + size_t(i) * 80;
        uint8_t type = out[h];
        uint32_t len = rd32(&out[h + 22]);
        bool hasDosName = out[h + 1] != 0;
        if ((type & 3) != 1 || hasDosName || len == 0) continue;
        uint32_t off = rd32(&out[h + 18]);
        uint32_t ls = rd32(&out[h + 26]), le = rd32(&out[h + 30]);
        if (off >= vox.size()) continue;
        len = std::min<uint32_t>(len, uint32_t(vox.size() - off));
        uint32_t dataOff = uint32_t(out.size());
        // 16-bit PTM samples: little-endian byte stream with a running 8-bit delta
        uint8_t prev = 0;
        out.reserve(out.size() + len * 2);
        for (uint32_t k = 0; k < len; k++) {
            uint16_t s = uint16_t(vox[off + k]);
            uint8_t lo = uint8_t(s & 0xff), hi = uint8_t(s >> 8);
            out.push_back(uint8_t(lo - prev)); prev = lo;
            out.push_back(uint8_t(hi - prev)); prev = hi;
        }
        out[h] = uint8_t(type | 0x10);
        wr32(&out[h + 18], dataOff);
        wr32(&out[h + 22], len * 2);
        wr32(&out[h + 26], ls * 2);
        wr32(&out[h + 30], le * 2);
    }
    return out;
}
}  // namespace

Music::Music() = default;
Music::~Music() { stopPlayback(); }

bool Music::load(const std::string& ptmPath, const std::string& mp2Path, std::string* err) {
    try {
        Bytes ptm = readFile(ptmPath);
        Bytes mp2 = readFile(mp2Path);
        std::vector<int16_t> vox = decodeMp2(mp2);
        ptm_ = rebuildPtm(ptm, vox);
        mod_ = std::make_unique<openmpt::module>(ptm_);
        // 303 sits with the vocal samples at full volume; keep the mix close to MIDAS (no extra gain)
        mod_->set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, 8);
        mod_->set_repeat_count(0);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

double Music::duration() const { return mod_ ? mod_->get_duration_seconds() : 0.0; }

void Music::audioCallback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    Music* m = static_cast<Music*>(user);
    int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    std::lock_guard<std::mutex> g(m->lock_);
    m->buf_.assign(size_t(frames) * 2, 0.0f);
    if (!m->paused_ && !m->ended_) {
        size_t got = m->mod_->read_interleaved_stereo(kRate, size_t(frames), m->buf_.data());
        if (got < size_t(frames)) m->ended_ = true;
    }
    SDL_PutAudioStreamData(stream, m->buf_.data(), frames * int(sizeof(float) * 2));
    // what is audible "now" is everything rendered minus what is still queued
    int queued = SDL_GetAudioStreamQueued(stream) / int(sizeof(float) * 2);
    m->posAtCallback_ = m->mod_->get_position_seconds() - double(queued) / kRate;
    m->ticksAtCallback_ = SDL_GetTicksNS();
}

bool Music::startPlayback() {
    if (!mod_) return false;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Music::audioCallback, this);
    if (!stream_) {
        SDL_Log("audio: %s", SDL_GetError());
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        posAtCallback_ = mod_->get_position_seconds();
        ticksAtCallback_ = SDL_GetTicksNS();
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Music::stopPlayback() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

double Music::time() {
    std::lock_guard<std::mutex> g(lock_);
    double t = posAtCallback_;
    if (!paused_ && !ended_) t += double(SDL_GetTicksNS() - ticksAtCallback_) * 1e-9;
    // never run backwards between callbacks (small jitter)
    if (t < lastTime_ && lastTime_ - t < 0.05) t = lastTime_;
    lastTime_ = t;
    return t;
}

void Music::seek(double seconds) {
    std::lock_guard<std::mutex> g(lock_);
    double p = mod_->set_position_seconds(std::max(0.0, seconds));
    if (stream_) SDL_ClearAudioStream(stream_);
    ended_ = false;
    posAtCallback_ = p;
    lastTime_ = p;
    ticksAtCallback_ = SDL_GetTicksNS();
}

void Music::setPaused(bool p) {
    std::lock_guard<std::mutex> g(lock_);
    if (p == paused_) return;
    if (p) {
        // freeze time at the current audible position
        posAtCallback_ += double(SDL_GetTicksNS() - ticksAtCallback_) * 1e-9;
        mod_->set_position_seconds(posAtCallback_);
        if (stream_) SDL_ClearAudioStream(stream_);
    }
    ticksAtCallback_ = SDL_GetTicksNS();
    paused_ = p;
}

int Music::currentOrder() { std::lock_guard<std::mutex> g(lock_); return mod_->get_current_order(); }
int Music::currentRow() { std::lock_guard<std::mutex> g(lock_); return mod_->get_current_row(); }

std::vector<float> Music::renderAll(int rate) {
    std::vector<float> all;
    openmpt::module m(ptm_);
    m.set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, 8);
    std::vector<float> b(4096 * 2);
    for (;;) {
        size_t n = m.read_interleaved_stereo(rate, 4096, b.data());
        if (n == 0) break;
        all.insert(all.end(), b.begin(), b.begin() + n * 2);
    }
    return all;
}

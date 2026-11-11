// The textures of "decrunching gfx" (0x12c50) as GL textures for the parts: runs the generator (texgen.h: exact
// 256x256 bytes and the checked high-resolution versions) and uploads one mipmapped, wrapping texture per buffer.
// Also the lookups by name, pointer variable [0x654e..0x658e] and engine slot (table 0x654a).
#include "jizz/textures.h"

#include <algorithm>
#include <future>
#include <mutex>
#include <stdexcept>

#include "exe.h"

#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_RGB16F
#define GL_RGB16F 0x881B
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif

namespace jizz {

namespace {
bool valid(Tex t) { return int(t) >= 1 && int(t) < kTexCount; }

texgen::Options options() {
    texgen::Options opt;
    opt.hiresMask = ~(1u << texgen::kMap9);  // map9 is never shown
    return opt;
}

std::mutex g_prefetchMu;
const JizzExe* g_prefetchExe = nullptr;
std::future<texgen::Result> g_prefetch;

void anisotropy() {
    gl::TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    gl::GetError();  // optional extension
}
}  // namespace

void Textures::prefetch(const JizzExe& exe) {
    std::lock_guard<std::mutex> lk(g_prefetchMu);
    if (g_prefetchExe) return;
    g_prefetchExe = &exe;
    g_prefetch = std::async(std::launch::async, [&exe] { return texgen::build(exe, options()); });
}

Textures::Textures(Ctx& c) : res_(std::make_unique<texgen::Result>()) {
    {
        std::lock_guard<std::mutex> lk(g_prefetchMu);
        if (g_prefetchExe == c.exe && g_prefetch.valid()) *res_ = g_prefetch.get();
    }
    if (res_->msTotal == 0) *res_ = texgen::build(*c.exe, options());
    for (int i = 1; i < kTexCount; i++) {
        texgen::Texture& t = res_->tex[i];
        Texture& g = gl_[i];
        if (i == texgen::kBlobHeights) {  // float: height and signed gradients
            const int n = t.size;
            std::vector<float> rgb(size_t(n) * n * 3);
            for (size_t j = 0; j < size_t(n) * n; j++)
                for (int p = 0; p < 3; p++)
                    rgb[3 * j + p] = t.hiresUsed ? t.hires[p][j] / 255.0f
                                     : p == 0     ? t.planar[j] / 255.0f
                                                  : float(int8_t(t.planar[p * texgen::kPlane + j])) / 255.0f;
            g.createRaw(n, n, GL_RGB16F, GL_RGB, GL_FLOAT, rgb.data(), true, true, true);
            for (auto& p : t.hires) std::vector<float>().swap(p);
        } else if (t.hiresUsed) {
            g.createRaw(t.size, t.size, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, t.rgb10.data(), true,
                        true, true);
            std::vector<uint32_t>().swap(t.rgb10);
        } else {
            std::vector<uint8_t> rgb(size_t(texgen::kPlane) * 3);
            for (int j = 0; j < texgen::kPlane; j++)
                for (int p = 0; p < 3; p++) rgb[3 * j + p] = t.planar[p * texgen::kPlane + j];
            g.createRaw(256, 256, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, rgb.data(), true, true, true);
        }
        anisotropy();
    }
}

Textures::~Textures() {
    for (Texture& g : gl_) g.destroy();
}

GLuint Textures::id(Tex t) const { return valid(t) ? gl_[int(t)].id : 0; }

void Textures::bind(Tex t, int unit) const {
    gl::ActiveTexture(GL_TEXTURE0 + unit);
    gl::BindTexture(GL_TEXTURE_2D, id(t));
}

int Textures::size(Tex t) const { return valid(t) ? gl_[int(t)].w : 0; }

bool Textures::hires(Tex t) const { return valid(t) && res_->tex[int(t)].hiresUsed; }

std::array<GLuint, 18> Textures::slotIds(bool map5InSlot1) const {
    std::array<GLuint, 18> r{};
    for (int s = 1; s < 18; s++) r[s] = id(bySlot(s, map5InSlot1));
    return r;
}

const uint8_t* Textures::planar(Tex t) const {
    if (!valid(t)) throw std::out_of_range("jizz::Textures: no such texture");
    return res_->tex[int(t)].planar.data();
}

const std::vector<uint8_t>& Textures::original(Tex t) const {
    if (!valid(t)) throw std::out_of_range("jizz::Textures: no such texture");
    return res_->tex[int(t)].original;
}

uint16_t Textures::rgb565(Tex t, int x, int y) const {
    const std::vector<uint8_t>& o = original(t);
    const int w = t == Tex::Weirdtxt ? 256 : 320;
    if ((t != Tex::Weirdtxt && t != Tex::Logo1 && t != Tex::Logo2) || x < 0 || y < 0 || x >= w || y >= 256 ||
        (w == 320 && y >= 200))
        return 0;
    const size_t i = 2 * (size_t(y) * w + x);
    return uint16_t(o[i] | o[i + 1] << 8);
}

Tex Textures::byPointer(uint32_t var) {
    const int id = texgen::byPointerVar(var);
    return id > 0 ? Tex(id) : Tex::None;
}

Tex Textures::bySlot(int slot, bool map5InSlot1) {
    if (slot < 1 || slot > 17) return Tex::None;
    if (slot == 1 && map5InSlot1) return Tex::Map5;
    return Tex(slot);
}

Tex Textures::byName(const std::string& n) {
    const int id = texgen::byName(n);
    return id > 0 ? Tex(id) : Tex::None;
}

const char* Textures::name(Tex t) { return texgen::name(int(t)); }

uint32_t Textures::pointerVar(Tex t) { return texgen::pointerVar(int(t)); }

}  // namespace jizz

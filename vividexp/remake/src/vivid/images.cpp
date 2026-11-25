// Picture and palette decoding and indexed uploads (images.h).
#include "vivid/images.h"

#include <algorithm>
#include <stdexcept>

#include "exe.h"

namespace vivid {

Indexed decodePcx(std::span<const uint8_t> f) {
    if (f.size() < 128 + 769) throw std::runtime_error("PCX too short");
    Indexed p;
    p.w = (f[8] | f[9] << 8) + 1;
    p.h = (f[10] | f[11] << 8) + 1;
    p.px.assign(size_t(p.w) * size_t(p.h), 0);
    size_t i = 128, o = 0;
    while (o < p.px.size() && i < f.size()) {
        uint8_t b = f[i++];
        if ((b & 0xc0) == 0xc0) {
            int n = b & 0x3f;
            uint8_t v = i < f.size() ? f[i++] : 0;
            while (n-- > 0 && o < p.px.size()) p.px[o++] = v;
        } else {
            p.px[o++] = b;
        }
    }
    // the palette: 769 bytes from the end, after the 0x0c marker
    size_t pp = f.size() - 769;
    if (f[pp] == 0x0c)
        for (int k = 0; k < 768; ++k) p.pal[size_t(k)] = uint8_t(f[pp + 1 + size_t(k)] >> 2);
    return p;
}

Indexed decodeRaw(std::span<const uint8_t> f) {
    if (f.size() < 800 || f[0] != 'm' || f[1] != 'h') throw std::runtime_error("not an mhwanh raw picture");
    Indexed p;
    p.w = f[8] << 8 | f[9];
    p.h = f[10] << 8 | f[11];
    if (f.size() < 800 + size_t(p.w) * size_t(p.h)) throw std::runtime_error("raw picture too short");
    for (int k = 0; k < 768; ++k) p.pal[size_t(k)] = uint8_t(f[32 + size_t(k)] >> 2);
    p.px.assign(f.begin() + 800, f.begin() + 800 + size_t(p.w) * size_t(p.h));
    return p;
}

Indexed loadPicture(const VividExe& exe, const std::string& name) {
    std::span<const uint8_t> f = exe.file(name);
    std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".raw" ? decodeRaw(f) : decodePcx(f);
}

std::array<uint8_t, 768> loadPal(const VividExe& exe, const std::string& name) {
    std::span<const uint8_t> f = exe.file(name);
    std::array<uint8_t, 768> p{};
    std::copy_n(f.begin(), std::min<size_t>(768, f.size()), p.begin());
    return p;
}

Palette toColours(const std::array<uint8_t, 768>& p6) {
    Palette c;
    for (size_t i = 0; i < 256; ++i)
        c[i] = Vec3(std::min(p6[i * 3], uint8_t(63)) / 63.0f, std::min(p6[i * 3 + 1], uint8_t(63)) / 63.0f,
                    std::min(p6[i * 3 + 2], uint8_t(63)) / 63.0f);
    return c;
}

Texture uploadIndices(std::span<const uint8_t> px, int w, int h) {
    Texture t;
    t.createRaw(w, h, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, px.data(), false, true, false);
    return t;
}

Texture uploadIndices(const Indexed& p) { return uploadIndices(p.px, p.w, p.h); }

namespace {
std::vector<float> paletteData(const Palette& pal) {
    std::vector<float> d(256 * 4);
    for (size_t i = 0; i < 256; ++i) {
        d[i * 4 + 0] = pal[i].x;
        d[i * 4 + 1] = pal[i].y;
        d[i * 4 + 2] = pal[i].z;
        d[i * 4 + 3] = 1.0f;
    }
    return d;
}
}  // namespace

Texture uploadPalette(const Palette& pal) {
    std::vector<float> d = paletteData(pal);
    Texture t;
    t.createRaw(256, 1, GL_RGBA32F, GL_RGBA, GL_FLOAT, d.data(), false, false, false);
    return t;
}

void updatePalette(Texture& t, const Palette& pal) {
    std::vector<float> d = paletteData(pal);
    gl::BindTexture(GL_TEXTURE_2D, t.id);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, d.data());
}

const char* kIndexedGLSL = R"(
vec3 palColor(sampler2D pal, uint i) { return texelFetch(pal, ivec2(int(i), 0), 0).rgb; }
uint idxAt(usampler2D tex, ivec2 t) {
    ivec2 s = textureSize(tex, 0);
    return texelFetch(tex, ivec2(((t.x % s.x) + s.x) % s.x, ((t.y % s.y) + s.y) % s.y), 0).r;
}
uint idxClamp(usampler2D tex, ivec2 t) {
    ivec2 s = textureSize(tex, 0);
    return texelFetch(tex, clamp(t, ivec2(0), s - 1), 0).r;
}
)";

}  // namespace vivid

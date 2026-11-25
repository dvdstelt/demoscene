// Vivid Experiment's VGA memory as the script's drawing commands leave it, drawn at any resolution in true colour.
// Shared by the pictures scene (pictures.cpp: what the script draws between the effects) and the zoom credits
// (zoom.cpp). Header-only, so that either scene builds on its own (VIVID_PARTS lists .cpp basenames).
//
// Ports (vividexp/re/notes-core.md 3-8): the slots of InitPCX 0x401884, DeinitPCX 0x401c20, InitZOOM 0x402460 and
// DeinitZOOM 0x402630; the rectangle blitter of PutPCXSYNC 0x401c2c (mode 0 copy, 1 copy without index 0, 2 and
// 10..16 add the source, shifted, to the indices under it; clipped right and bottom); ClearSCR 0x402784; the zoom
// window of RunZOOMSYNC 0x402644 (zoomFrame 0x402494, zoomBlit 0x40ccb8); and what the fades (0x4020b8, 0x401ed0)
// do to the pictures' colours.
//
// Replay. The script lines up to refresh r (Timeline::lines(), each one drawn at its start, in no time) give the
// commands whose pixels are still in VGA memory, oldest first; everything before the last full-screen overwrite
// (ClearSCR, an opaque full-screen copy, an effect) is dropped. Each is drawn as a quad over its rectangle:
//   - copies (modes 0, 1) draw the slot's picture in their rectangle, mode 1 with index 0 transparent;
//   - adding modes (the opening's texts: mode 14, so a pixel becomes sky + 32 * level) are drawn as what they do in
//     colour: each source index turns the colour under it into lerp(under, G, a), with (a, G) fitted per frame from
//     the palette over the indices it was added to. That is exact for every palette the demo shows them in
//     (taivas.pal: a = 0; taivas2.pal: a = 1, G = the level's grey; the fades between them: a = the progress;
//     taivas4.pal: white either way), and it works over a redrawn sky and with a redrawn text;
//   - the zoom window: the sky (slot 0) in rows b .. 199 - b and the name (slot 1) over it, index 0 transparent,
//     scaled continuously: z(n) = max(z0 + dz n, 1), s = a / (2 z) screen pixels per picture pixel, the picture's
//     centre on the screen's (160, 100) (0x402494: x0 = 160 - a * 320 / z for the 1280 columns, y0 = 100 - a * 50 / z
//     for the 200 rows); n is fractional, refreshes since the line started; after the line, its last frame stays;
//   - an effect's last frame (the water's, during the white flash before the first bump) is its scene's frame.
//
// Colours. The original pixels are drawn through the palette the DAC holds, each tap looked up and then blended, as
// sharp-edged texels (each a flat square whose edges are anti-aliased over an output pixel, as the other remakes
// show original pictures; minified, they are averaged). A redraw (art/<name>.png) replaces them:
//   - text pictures (index 0 black and up to three grey levels: alkutext.pcx and the six names) are grey coverage
//     masks: the grey is mapped to the original's levels through the greys of its palette (129, 130, 131 = grey 20,
//     41, 63: grey 20 / 63 is level 1, a grey between two levels' greys interpolates them), and the level's colour is
//     the palette's at that level's index, so the white flash whitens the text and the adding mode blends the level
//     over the sky. Where the original's index 0 is transparent (the label of mode 1, the zoom's names), coverage =
//     min(level, 1): the original draws its grey edge pixels opaquely, so the redraw's faint edge is a thin dark
//     fringe of grey 20 as well. Magnified (the zoom ends at 100 screen pixels per picture pixel), the names' edge
//     comes from a signed distance field built at start-up from the mask (halved on the GPU, its grey read back), at
//     the contour where coverage is 1/2, and stays sharp at any scale. A text redraw counts only within two pixels
//     of the original's letters (index != 0), so that it cannot add letters where the original has none;
//   - other pictures are colour redraws: under a fade from palette A to B at progress s, the colour is
//     lerp(A', B', s), where A' is the redraw's colour where A equals the picture's own palette at the original's
//     index there, and A's colour otherwise (fades from and to black or white become colour lerps; taivas4.pal
//     whitens the sky but keeps the black bands).
//
// Evaluated exactly instead of the original's shortcuts: the zoom's integer rectangle and 16.16 stepping (which drops
// the start fraction) become the continuous transform above; the fades are continuous (the framework's palette);
// the index arithmetic of mode 14 becomes the blend above; the rectangles' edges are anti-aliased.
// Dropped artefacts: when a fade starts in the refresh in which its picture is drawn, the DAC shows the previous
// palette for that one refresh (the title picture in the colours of the sky's cut fade to white, record 5513; the
// water's last frame in watback.pal's colours at the start of the flash): the scene shows the fade's first palette
// instead, and an effect's frame in its own colours.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"

namespace vga {

using vivid::Palette;

// A drawing command whose pixels are (partly) in VGA memory.
struct Op {
    enum Kind { Clear, Rect, Zoom, Effect } kind = Clear;
    int line = -1;                  // its index in Timeline::lines()
    std::string file;               // Rect: the slot's picture; Zoom: the name (slot 1)
    std::string sky;                // Zoom: the sky (slot 0, from InitZOOM)
    int dx = 0, dy = 0, sx = 0, sy = 0, w = 0, h = 0, mode = 0;   // Rect, clipped as 0x401c2c clips
    std::vector<uint8_t> under;     // Rect in an adding mode: the indices it was added to
    double start = 0, end = 0;      // Zoom: the RunZOOMSYNC line's start and end (refreshes)
    float z0 = 0, a = 0, dz = 0;    // Zoom: the line's numbers (float, as 0x402644 reads them with %f)
    int band = 0;                   // Zoom: rows of each letterbox band (the window is rows band .. 199 - band)
    const vivid::Segment* seg = nullptr;   // Effect: the Run command's segment
};

// A picture the script draws: the original's indices and palette, and the redraw if there is one.
struct Picture {
    const vivid::Indexed* ix = nullptr;
    const Texture* idx = nullptr;   // the original's indices (GL_R8UI)
    const Texture* art = nullptr;   // the redraw, or nullptr
    Palette own{};                  // the picture's own palette (equal to its .pal file on the indices it uses)
    std::array<bool, 256> used{};   // the indices it uses
    int levels = 0;                 // a text picture: the number of grey levels after index 0 (else 0)
    std::array<int, 4> levelIndex{};
    std::array<float, 4> levelGrey{};
    Texture footprint;              // a text redraw: where the original has letters, widened by two pixels
    Texture sdf;                    // a magnified text redraw: signed distance to the letters' edge (see sdfFromMask)
    float sdfScale[2] = {1, 1};     // redraw uv -> sdf uv
};

// The fade the DAC is in: palette a -> b at progress s (a == b: a palette that holds), and the palette drawn with.
struct Fade {
    Palette a{}, b{}, eff{};
    float s = 0;
};

// The number of script lines that have run at refresh r (each line runs at its start).
inline size_t linesUpTo(const vivid::Timeline& t, double r) {
    const auto& L = t.lines();
    return size_t(std::upper_bound(L.begin(), L.end(), r,
                                   [](double v, const vivid::TimedLine& l) { return v < l.start; }) -
                  L.begin());
}

inline bool sameColour(const Vec3& p, const Vec3& q) {
    return std::fabs(p.x - q.x) < 1e-4f && std::fabs(p.y - q.y) < 1e-4f && std::fabs(p.z - q.z) < 1e-4f;
}

// One colour for every entry (white.pal, black.pal)?
inline bool uniformPalette(const Palette& p) {
    for (const Vec3& c : p)
        if (!sameColour(c, p[0])) return false;
    return true;
}

// Signed distance, in texels (+ inside), to the contour where the grey mask (w x h, 0..255) equals `level` (0..1):
// seeds next to the contour get the sub-texel offset from the local gradient, then two sweeps pass the vector to the
// nearest contour point on to the neighbours (8SSEDT). Clamped to +-8.
inline std::vector<float> sdfFromMask(const std::vector<uint8_t>& grey, int w, int h, float level) {
    std::vector<float> m(grey.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = grey[i] * (1.0f / 255.0f);
    auto at = [&](int x, int y) {
        return m[size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(std::clamp(x, 0, w - 1))];
    };
    const float kFar = 1e9f;
    std::vector<float> vx(m.size(), kFar), vy(m.size(), kFar);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            bool in = at(x, y) >= level;
            if ((at(x - 1, y) >= level) == in && (at(x + 1, y) >= level) == in && (at(x, y - 1) >= level) == in &&
                (at(x, y + 1) >= level) == in)
                continue;
            float gx = 0.5f * (at(x + 1, y) - at(x - 1, y)), gy = 0.5f * (at(x, y + 1) - at(x, y - 1));
            float g2 = gx * gx + gy * gy;
            size_t i = size_t(y) * size_t(w) + size_t(x);
            if (g2 < 1e-8f) { vx[i] = vy[i] = 0; continue; }
            float t = (level - at(x, y)) / g2;   // the contour point ~ the texel + t * gradient
            float ox = t * gx, oy = t * gy, len = std::sqrt(ox * ox + oy * oy);
            if (len > 1.0f) { ox /= len; oy /= len; }
            vx[i] = ox;
            vy[i] = oy;
        }
    auto relax = [&](int x, int y, int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) return;
        size_t n = size_t(ny) * size_t(w) + size_t(nx), i = size_t(y) * size_t(w) + size_t(x);
        if (vx[n] >= kFar) return;
        float cx = vx[n] + float(nx - x), cy = vy[n] + float(ny - y);
        if (cx * cx + cy * cy < vx[i] * vx[i] + vy[i] * vy[i]) { vx[i] = cx; vy[i] = cy; }
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            relax(x, y, x - 1, y); relax(x, y, x - 1, y - 1); relax(x, y, x, y - 1); relax(x, y, x + 1, y - 1);
        }
        for (int x = w - 1; x >= 0; --x) relax(x, y, x + 1, y);
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            relax(x, y, x + 1, y); relax(x, y, x + 1, y + 1); relax(x, y, x, y + 1); relax(x, y, x - 1, y + 1);
        }
        for (int x = 0; x < w; ++x) relax(x, y, x - 1, y);
    }
    std::vector<float> d(m.size());
    for (size_t i = 0; i < m.size(); ++i) {
        float len = vx[i] >= kFar ? 8.0f : std::min(8.0f, std::sqrt(vx[i] * vx[i] + vy[i] * vy[i]));
        d[i] = m[i] >= level ? len : -len;
    }
    return d;
}

inline constexpr const char* kVS = R"(
uniform vec4 quad;   // x0, y0, x1, y1 in VGA coordinates
out vec2 vga;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vga = mix(quad.xy, quad.zw, c);
    gl_Position = vec4(vga.x / 160.0 - 1.0, 1.0 - vga.y / 100.0, 0.0, 1.0);
}
)";

// One command's pixels, premultiplied (blended with ONE, ONE_MINUS_SRC_ALPHA).
inline constexpr const char* kFS = R"(
in vec2 vga;
out vec4 o;
uniform usampler2D idx;   // unit 0: the picture's indices
uniform sampler2D pal;    // unit 1: the palette
uniform sampler2D tab;    // unit 2, per index: rows 0 / 1 the fade's palettes A / B (alpha 1 where equal to the
                          // picture's own colour: take the redraw's), row 2 the adding mode's blend (a * G, a)
uniform sampler2D art;    // unit 3: the redraw
uniform sampler2D sdf;    // unit 4: signed distance to the redraw's letters' edge (texels, + inside)
uniform sampler2D foot;   // unit 5: 1 within two pixels of the original's letters (text redraws)
uniform int source;       // 0 the original pixels, 1 a colour redraw, 2 a text redraw (grey: coverage and level)
uniform int mode;         // 0 opaque, 1 index 0 transparent, 2 added to the colours under it
uniform int useSdf;
uniform vec4 clipRect;    // x0, y0, x1, y1 (VGA): where the command drew
uniform vec4 xform;       // picture texel = vga * xform.xy + xform.zw
uniform vec4 srcRect;     // x0, y0, x1, y1: the texels the command read
uniform vec2 picSize;
uniform vec2 sdfScale;
uniform float fadeS;
uniform int lvCount;
uniform float lvGrey[4];
uniform int lvIndex[4];

float cover(float x, float a, float b, float fw) {
    return clamp((x - a) / fw + 0.5, 0.0, 1.0) * clamp((b - x) / fw + 0.5, 0.0, 1.0);
}

// the original texel t (clamped into the rectangle read), premultiplied; artc: the redraw's colour (source 1)
vec4 tap(ivec2 t, vec3 artc) {
    uint i = texelFetch(idx, clamp(t, ivec2(srcRect.xy), ivec2(srcRect.zw) - 1), 0).r;
    if (mode == 2) return texelFetch(tab, ivec2(int(i), 2), 0);
    if (mode == 1 && i == 0u) return vec4(0.0);
    if (source == 1) {
        vec4 a = texelFetch(tab, ivec2(int(i), 0), 0), b = texelFetch(tab, ivec2(int(i), 1), 0);
        return vec4(mix(mix(a.rgb, artc, a.a), mix(b.rgb, artc, b.a), fadeS), 1.0);
    }
    return vec4(palColor(pal, i), 1.0);
}

// the original pixels at texel coordinate p: flat squares whose edges are blended over e texels (e = 1: bilinear)
vec4 pixels(vec2 p, vec2 e, vec3 artc) {
    vec2 q = p - 0.5, b = floor(q);
    vec2 f = clamp((q - b - 0.5) / e + 0.5, 0.0, 1.0);
    ivec2 t = ivec2(b);
    return mix(mix(tap(t, artc), tap(t + ivec2(1, 0), artc), f.x),
               mix(tap(t + ivec2(0, 1), artc), tap(t + ivec2(1, 1), artc), f.x), f.y);
}

// a text redraw's grey -> the original's level (0 .. lvCount), through the levels' greys
float levelOf(float g) {
    float prev = 0.0;
    for (int L = 1; L <= lvCount; ++L) {
        float k = lvGrey[L];
        if (g <= k) return float(L - 1) + clamp((g - prev) / max(k - prev, 1e-4), 0.0, 1.0);
        prev = k;
    }
    return float(lvCount);
}
vec4 levelEntry(int L) {
    if (mode == 2) return texelFetch(tab, ivec2(lvIndex[L], 2), 0);
    return vec4(palColor(pal, uint(lvIndex[L])), 1.0);
}
vec4 levelColour(float l) {
    int L = int(floor(l));
    return mix(levelEntry(L), levelEntry(min(L + 1, lvCount)), l - float(L));
}

void main() {
    vec2 fw = max(fwidth(vga), vec2(1e-6));
    float cov = cover(vga.x, clipRect.x, clipRect.z, fw.x) * cover(vga.y, clipRect.y, clipRect.w, fw.y);
    vec2 pr = vga * xform.xy + xform.zw;
    vec2 dx = dFdx(pr), dy = dFdy(pr);
    vec2 p = clamp(pr, srcRect.xy, srcRect.zw);
    vec4 c;
    if (source == 2) {
        vec2 uv = p / picSize;
        vec4 a = textureGrad(art, uv, dx / picSize, dy / picSize);
        float fp = texture(foot, uv).r;
        float l = levelOf(luma(a.rgb) * a.a * fp);
        if (mode == 1) {
            float k = clamp(l, 0.0, 1.0);
            if (useSdf != 0) {
                // magnified: the edge where coverage is 1/2, from the distance field, over one output pixel
                float d = texture(sdf, uv * sdfScale).r;
                float ks = clamp(d / max(fwidth(d), 1e-4) + 0.5, 0.0, 1.0) * fp;
                vec2 artScale = vec2(textureSize(art, 0)) / picSize;
                float texels = max(length(dx * artScale), length(dy * artScale));   // redraw texels per pixel
                k = mix(ks, k, smoothstep(0.5, 1.0, texels));
            }
            c = vec4(levelColour(clamp(l, 1.0, float(lvCount))).rgb * k, k);
        } else {
            c = levelColour(l);
        }
    } else {
        vec3 artc = source == 1 ? textureGrad(art, p / picSize, dx / picSize, dy / picSize).rgb : vec3(0.0);
        vec2 e = max(abs(dx) + abs(dy), vec2(1e-4));   // texels per output pixel
        int n = int(clamp(ceil(max(e.x, e.y)), 1.0, 4.0));
        if (n == 1) {
            c = pixels(p, min(e, vec2(1.0)), artc);
        } else {   // minified: n x n bilinear samples over the pixel
            c = vec4(0.0);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    vec2 off = (vec2(float(i), float(j)) + 0.5) / float(n) - 0.5;
                    c += pixels(clamp(pr + off.x * dx + off.y * dy, srcRect.xy, srcRect.zw), vec2(1.0), artc);
                }
            c /= float(n * n);
        }
    }
    o = c * cov;
}
)";

// An effect's frame under a fade: from / to are the fade's palettes when they are one colour (alpha 1), else the
// frame's own colours.
inline constexpr const char* kEffectFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D frame;
uniform vec4 from, to;
uniform float fadeS;
void main() {
    vec3 c = texture(frame, uv).rgb;
    o = vec4(mix(mix(c, from.rgb, from.a), mix(c, to.rgb, to.a), fadeS), 1.0);
}
)";

class Memory {
public:
    explicit Memory(Ctx& c) {
        if (!shader_.build(kVS, std::string(vivid::kIndexedGLSL) + kFS, "vga") ||
            !effectShader_.build(kFullscreenVS, kEffectFS, "vga effect"))
            SDL_Log("pictures: shader build failed");
        pal_ = vivid::uploadPalette(c.palette(0));
        std::vector<float> zero(256 * 3 * 4, 0.0f);
        tab_.createRaw(256, 3, GL_RGBA32F, GL_RGBA, GL_FLOAT, zero.data(), false, false, false);
        // every picture the scripts draw, decoded and uploaded now (the original's InitPCX decodes at run time)
        uint64_t t0 = SDL_GetTicksNS();
        std::vector<Picture*> names;
        for (const vivid::TimedLine& tl : c.timeline->lines()) {
            const vivid::ScriptLine& l = tl.line;
            const std::string& cmd = l.command();
            try {
                if (cmd == "InitPCX") picture(c, l.arg(1));
                else if (cmd == "InitZOOM") picture(c, l.arg(0));
                else if (cmd == "RunZOOMSYNC") names.push_back(&picture(c, l.arg(0)));
            } catch (const std::exception& e) {
                SDL_Log("pictures: script line %d: %s", l.line, e.what());
            }
        }
        uint64_t t1 = SDL_GetTicksNS();
        buildSdfs(c, names);
        SDL_Log("pictures: %d pictures in %.0f ms, distance fields %.0f ms", int(pics_.size()), (t1 - t0) * 1e-6,
                (SDL_GetTicksNS() - t1) * 1e-6);
    }
    ~Memory() {
        pal_.destroy();
        tab_.destroy();
        for (auto& [k, p] : pics_) {
            p->sdf.destroy();
            p->footprint.destroy();
        }
    }
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    // VGA memory at refresh r into c.out.
    void draw(Ctx& c, double r) {
        Fade f = fadeAt(c, r);
        vivid::updatePalette(pal_, f.eff);
        size_t n = linesUpTo(*c.timeline, r);
        if (n != count_) {
            ops_ = replay(c, n);
            count_ = n;
        }
        setBlend(Blend::None);
        c.clear(f.eff[0]);   // VGA memory before any command: index 0
        for (const Op& op : ops_) {
            switch (op.kind) {
                case Op::Clear: setBlend(Blend::None); c.clear(f.eff[0]); break;
                case Op::Rect: drawRect(c, op, f); break;
                case Op::Zoom: drawZoom(c, op, f, r); break;
                case Op::Effect: drawEffect(c, op, f, r); break;
            }
        }
        setBlend(Blend::None);
    }

private:
    Picture& picture(Ctx& c, const std::string& file) {
        std::unique_ptr<Picture>& p = pics_[file];
        if (p) return *p;
        auto q = std::make_unique<Picture>();
        q->ix = &c.pictures->indexed(file);
        q->idx = &c.pictures->indices(file);
        q->art = c.pictures->redraw(file);
        q->own = vivid::toColours(q->ix->pal);
        for (uint8_t i : q->ix->px) q->used[i] = true;
        // a text picture: index 0 black, then up to three greys getting lighter (alkutext.pcx, the names)
        std::vector<int> u;
        for (int i = 0; i < 256; ++i)
            if (q->used[size_t(i)]) u.push_back(i);
        bool text = !u.empty() && u[0] == 0 && u.size() <= 4 && sameColour(q->own[0], Vec3(0, 0, 0));
        for (size_t k = 1; text && k < u.size(); ++k) {
            const Vec3& g = q->own[size_t(u[k])];
            text = std::fabs(g.x - g.y) < 1e-4f && std::fabs(g.x - g.z) < 1e-4f && g.x > q->own[size_t(u[k - 1])].x;
        }
        if (text) {
            q->levels = int(u.size()) - 1;
            for (size_t k = 0; k < u.size(); ++k) {
                q->levelIndex[k] = u[k];
                q->levelGrey[k] = q->own[size_t(u[k])].x;
            }
            if (q->art) buildFootprint(*q);
        }
        p = std::move(q);
        return *p;
    }

    // A text redraw is used within two pixels of the original's letters (index != 0), so that it cannot add letters
    // the original does not have there.
    static void buildFootprint(Picture& p) {
        const int w = p.ix->w, h = p.ix->h, r = 2;
        std::vector<uint8_t> a(size_t(w) * size_t(h)), b(a.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t m = 0;
                for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r) && !m; ++k) m = p.ix->at(k, y) ? 255 : 0;
                a[size_t(y) * size_t(w) + size_t(x)] = m;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t m = 0;
                for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r) && !m; ++k) m = a[size_t(k) * size_t(w) + size_t(x)];
                b[size_t(y) * size_t(w) + size_t(x)] = m;
            }
        p.footprint.createRaw(w, h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, b.data(), true, false, false);
    }

    // The text redraws that the zoom magnifies: their distance fields, at half the redraw's size (the GPU halves the
    // mask, its grey read back), computed in parallel.
    void buildSdfs(Ctx& c, const std::vector<Picture*>& pics) {
        struct Job { Picture* p; std::vector<uint8_t> grey; std::vector<float> d; int w = 0, h = 0; };
        std::vector<Job> jobs;
        for (Picture* p : pics) {
            if (!p->art || p->levels == 0 || p->sdf.id) continue;
            if (std::any_of(jobs.begin(), jobs.end(), [&](const Job& j) { return j.p == p; })) continue;
            Job j{p, {}, {}, std::max(1, p->art->w / 2), std::max(1, p->art->h / 2)};
            Target t;
            t.create(j.w, j.h, false, GL_RGBA8);
            t.bind();
            setBlend(Blend::None);
            c.drawTexture(p->art->id, 0, 0, 320, 200, 1.0f, 0, 1, 1, 0);   // flipped: read back top row first
            j.grey.resize(size_t(j.w) * size_t(j.h));
            gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, j.w, j.h, GL_RED, GL_UNSIGNED_BYTE, j.grey.data());
            t.destroy();
            jobs.push_back(std::move(j));
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        std::vector<std::thread> threads;
        for (Job& j : jobs)   // the contour where coverage = min(level, 1) is 1/2: half the first level's grey
            threads.emplace_back([&j] { j.d = sdfFromMask(j.grey, j.w, j.h, 0.5f * j.p->levelGrey[1]); });
        for (std::thread& t : threads) t.join();
        for (Job& j : jobs) {
            j.p->sdf.createRaw(j.w, j.h, GL_R16F, GL_RED, GL_FLOAT, j.d.data(), true, false, false);
            j.p->sdfScale[0] = float(j.p->art->w) / float(2 * j.w);
            j.p->sdfScale[1] = float(j.p->art->h) / float(2 * j.h);
        }
    }

    const Palette& palFile(Ctx& c, const std::string& name) {
        auto it = palFiles_.find(name);
        if (it == palFiles_.end()) it = palFiles_.emplace(name, vivid::toColours(vivid::loadPal(*c.exe, name))).first;
        return it->second;
    }

    // The fade the DAC is in at refresh r: the last line that set or faded the palette, checked against the
    // timeline's palette (its progress is the projection of the DAC's palette onto the fade).
    Fade fadeAt(Ctx& c, double r) {
        Fade f;
        f.a = f.b = f.eff = c.palette(r);
        const auto& lines = c.timeline->lines();
        for (size_t i = linesUpTo(*c.timeline, r); i-- > 0;) {
            const vivid::TimedLine& tl = lines[i];
            const vivid::ScriptLine& l = tl.line;
            const std::string& cmd = l.command();
            if (cmd == "LoadPAL" || cmd == "WaterEffect" || cmd == "GenericEffect" ||
                ((cmd == "InitPCX" || cmd == "InitBUMP") && l.argInt(cmd == "InitPCX" ? 2 : 1) == 0))
                return f;   // a palette set at once: it holds
            if (cmd != "InitFADE") continue;
            const Palette &A = palFile(c, l.arg(0)), &B = palFile(c, l.arg(1));
            int first = std::clamp(l.argInt(3), 0, 255), last = std::clamp(l.argInt(4), 0, 255);
            double num = 0, den = 0;
            for (int k = first; k <= last; ++k) {
                Vec3 d = B[size_t(k)] - A[size_t(k)], p = f.eff[size_t(k)] - A[size_t(k)];
                num += d.dot(p);
                den += d.dot(d);
            }
            float s = den > 0 ? float(std::clamp(num / den, 0.0, 1.0)) : 0.0f;
            bool fits = true;
            for (int k = 0; k < 256 && fits; ++k) {
                Vec3 e = k >= first && k <= last ? A[size_t(k)] * (1 - s) + B[size_t(k)] * s : A[size_t(k)];
                Vec3 d = e - f.eff[size_t(k)];
                fits = std::fabs(d.x) < 1e-3f && std::fabs(d.y) < 1e-3f && std::fabs(d.z) < 1e-3f;
            }
            if (fits) {
                f.a = A;
                f.b = B;
                f.s = s;
            } else if (r < tl.start + 1.0) {
                // the fade's first refresh, which still shows the palette before it: show the fade's first palette
                f.a = f.eff = A;
                f.b = B;
                f.s = 0;
            }
            return f;
        }
        return f;
    }

    // The commands in VGA memory once `count` lines have run (see the top of the file). A CPU copy of the indices
    // gives each adding command the indices it was added to.
    std::vector<Op> replay(Ctx& c, size_t count) {
        const auto& lines = c.timeline->lines();
        std::vector<Op> ops;
        std::map<int, std::string> slot;
        std::vector<int> mem(320 * 200, 0);   // -1: drawn by the zoom or an effect (not tracked)
        for (size_t i = 0; i < count; ++i) {
            const vivid::ScriptLine& l = lines[i].line;
            const std::string& cmd = l.command();
            Op op;
            op.line = int(i);
            if (cmd == "InitPCX") {
                slot[l.argInt(0)] = l.arg(1);
            } else if (cmd == "DeinitPCX") {
                slot.erase(l.argInt(0));
            } else if (cmd == "InitZOOM") {
                slot[0] = l.arg(0);
            } else if (cmd == "DeinitZOOM") {
                slot.erase(0);
            } else if (cmd == "ClearSCR") {
                ops.clear();
                std::fill(mem.begin(), mem.end(), 0);
                op.kind = Op::Clear;
                ops.push_back(std::move(op));
            } else if (cmd == "PutPCXSYNC") {
                auto s = slot.find(l.argInt(0));
                op.kind = Op::Rect;
                op.dx = l.argInt(1); op.dy = l.argInt(2); op.sx = l.argInt(3); op.sy = l.argInt(4);
                op.w = l.argInt(5); op.h = l.argInt(6); op.mode = l.argInt(7);
                if (op.dx + op.w > 320) op.w = 320 - op.dx;   // clipped right and bottom only
                if (op.dy + op.h > 200) op.h = 200 - op.dy;
                bool drawn = op.mode == 0 || op.mode == 1 || op.mode == 2 || (op.mode >= 10 && op.mode <= 16);
                if (s == slot.end() || op.w <= 0 || op.h <= 0 || !drawn) continue;
                op.file = s->second;
                apply(c, op, mem);
                if (op.mode == 0 && op.dx <= 0 && op.dy <= 0 && op.w >= 320 && op.h >= 200) ops.clear();
                ops.push_back(std::move(op));
            } else if (cmd == "RunZOOMSYNC") {
                auto s = slot.find(0);
                if (s == slot.end()) continue;
                op.kind = Op::Zoom;
                op.sky = s->second;
                op.file = l.arg(0);
                op.z0 = float(l.argFloat(1));
                op.a = float(l.argFloat(2));
                op.dz = float(l.argFloat(3));
                op.band = l.argInt(4);
                op.start = lines[i].start;
                op.end = lines[i].end;
                std::erase_if(ops, [](const Op& o) { return o.kind == Op::Zoom; });   // the window is redrawn whole
                for (int y = op.band; y < 200 - op.band; ++y)
                    std::fill_n(mem.begin() + y * 320, 320, -1);
                ops.push_back(std::move(op));
            } else {
                for (const vivid::Segment& sg : c.timeline->segments())
                    if (sg.run == int(i) && sg.scene != "zoom") {   // an effect: it draws the whole screen
                        ops.clear();
                        std::fill(mem.begin(), mem.end(), -1);
                        op.kind = Op::Effect;
                        op.seg = &sg;
                        ops.push_back(std::move(op));
                        break;
                    }
            }
        }
        return ops;
    }

    // PutPCXSYNC 0x401c2c on the CPU copy of the indices; an adding command notes the indices under its pixels.
    void apply(Ctx& c, Op& op, std::vector<int>& mem) {
        const vivid::Indexed& p = c.pictures->indexed(op.file);
        std::array<bool, 256> under{};
        for (int y = 0; y < op.h; ++y)
            for (int x = 0; x < op.w; ++x) {
                int px = op.sx + x, py = op.sy + y, qx = op.dx + x, qy = op.dy + y;
                if (px < 0 || py < 0 || px >= p.w || py >= p.h || qx < 0 || qy < 0) continue;
                int s = p.at(px, py);
                int& d = mem[size_t(qy) * 320 + size_t(qx)];
                if (op.mode == 0) d = s;
                else if (op.mode == 1) { if (s) d = s; }
                else if (d >= 0) {
                    if (s) under[size_t(d)] = true;
                    d = (d + (op.mode == 2 ? s : (s << (op.mode - 9)) & 255)) & 255;
                }
            }
        for (int i = 0; i < 256; ++i)
            if (under[size_t(i)]) op.under.push_back(uint8_t(i));
    }

    // Row 2 of the table: what each source index of an adding command does in colour, E = (1 - a) X + a G, fitted
    // over the indices X it was added to (E = the palette at X + the shifted index).
    void fitAdd(const Op& op, const Picture& src, const Palette& P, std::vector<float>& tab) {
        int shift = op.mode == 2 ? 0 : op.mode - 9;
        for (int i = 1; i < 256; ++i) {
            if (!src.used[size_t(i)]) continue;
            int add = op.mode == 2 ? i : (i << shift) & 255;
            Vec3 mx, me;
            size_t n = op.under.size();
            for (uint8_t s : op.under) {
                mx = mx + P[s];
                me = me + P[size_t((s + add) & 255)];
            }
            float u = 0;
            Vec3 v = P[size_t(add)];   // nothing known under it: the indexed colour, opaque
            if (n > 0) {
                mx = mx * (1.0f / float(n));
                me = me * (1.0f / float(n));
                double sxx = 0, sxe = 0;
                for (uint8_t s : op.under) {
                    Vec3 dx = P[s] - mx, de = P[size_t((s + add) & 255)] - me;
                    sxx += dx.dot(dx);
                    sxe += dx.dot(de);
                }
                u = sxx > 1e-9 ? float(std::clamp(sxe / sxx, 0.0, 1.0)) : 0.0f;
                v = me - mx * u;
            }
            float* t = &tab[(size_t(2) * 256 + size_t(i)) * 4];
            t[0] = v.x; t[1] = v.y; t[2] = v.z; t[3] = 1 - u;
        }
    }

    struct Quad {
        float clip[4];    // x0, y0, x1, y1 (VGA)
        float xform[4];   // picture texel = vga * xy + zw
        float src[4];     // texels read: x0, y0, x1, y1
    };

    // A picture through a quad: mode 0 opaque, 1 index 0 transparent, 2 added (`op` gives the indices under it).
    void drawPicture(Ctx& c, Picture& p, int mode, const Quad& q, const Fade& f, const Op* op) {
        int source = !p.art ? 0 : p.levels > 0 ? 2 : mode == 2 ? 0 : 1;
        if (source == 1 || mode == 2) {
            std::vector<float> tab(256 * 3 * 4, 0.0f);
            if (source == 1)
                for (size_t i = 0; i < 256; ++i) {
                    float* a = &tab[i * 4];
                    float* b = &tab[(256 + i) * 4];
                    a[0] = f.a[i].x; a[1] = f.a[i].y; a[2] = f.a[i].z; a[3] = sameColour(f.a[i], p.own[i]) ? 1.0f : 0.0f;
                    b[0] = f.b[i].x; b[1] = f.b[i].y; b[2] = f.b[i].z; b[3] = sameColour(f.b[i], p.own[i]) ? 1.0f : 0.0f;
                }
            if (mode == 2 && op) fitAdd(*op, p, f.eff, tab);
            gl::BindTexture(GL_TEXTURE_2D, tab_.id);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 3, GL_RGBA, GL_FLOAT, tab.data());
        }
        setBlend(Blend::Premul);
        shader_.use();
        p.idx->bind(0);
        pal_.bind(1);
        tab_.bind(2);
        gl::ActiveTexture(GL_TEXTURE3);
        gl::BindTexture(GL_TEXTURE_2D, p.art ? p.art->id : 0);
        gl::ActiveTexture(GL_TEXTURE0 + 4);
        gl::BindTexture(GL_TEXTURE_2D, p.sdf.id);
        gl::ActiveTexture(GL_TEXTURE0 + 5);
        gl::BindTexture(GL_TEXTURE_2D, p.footprint.id);
        gl::ActiveTexture(GL_TEXTURE0);
        shader_.set("idx", 0);
        shader_.set("pal", 1);
        shader_.set("tab", 2);
        shader_.set("art", 3);
        shader_.set("sdf", 4);
        shader_.set("foot", 5);
        shader_.set("source", source);
        shader_.set("mode", mode);
        shader_.set("useSdf", p.sdf.id != 0 && mode == 1 ? 1 : 0);
        shader_.set("clipRect", q.clip[0], q.clip[1], q.clip[2], q.clip[3]);
        shader_.set("xform", q.xform[0], q.xform[1], q.xform[2], q.xform[3]);
        shader_.set("srcRect", q.src[0], q.src[1], q.src[2], q.src[3]);
        shader_.set("picSize", float(p.ix->w), float(p.ix->h));
        shader_.set("sdfScale", p.sdfScale[0], p.sdfScale[1]);
        shader_.set("fadeS", f.s);
        shader_.set("lvCount", p.levels);
        gl::Uniform1fv(shader_.loc("lvGrey"), 4, p.levelGrey.data());
        gl::Uniform1iv(shader_.loc("lvIndex"), 4, p.levelIndex.data());
        // the quad: the clip rectangle and a margin of two output pixels for its anti-aliased edges
        float mx = 2.0f * 320.0f / float(std::max(1, c.out->w)), my = 2.0f * 200.0f / float(std::max(1, c.out->h));
        shader_.set("quad", q.clip[0] - mx, q.clip[1] - my, q.clip[2] + mx, q.clip[3] + my);
        drawQuad();
    }

    void drawRect(Ctx& c, const Op& op, const Fade& f) {
        Picture& p = picture(c, op.file);
        float sx0 = float(std::clamp(op.sx, 0, p.ix->w - 1)), sy0 = float(std::clamp(op.sy, 0, p.ix->h - 1));
        float sx1 = float(std::clamp(op.sx + op.w, 1, p.ix->w)), sy1 = float(std::clamp(op.sy + op.h, 1, p.ix->h));
        Quad q{{float(op.dx), float(op.dy), float(op.dx + op.w), float(op.dy + op.h)},
               {1, 1, float(op.sx - op.dx), float(op.sy - op.dy)},
               {sx0, sy0, std::max(sx1, sx0 + 1), std::max(sy1, sy0 + 1)}};
        drawPicture(c, p, op.mode == 0 ? 0 : op.mode == 1 ? 1 : 2, q, f, &op);
    }

    // The zoom window (0x402494): the sky, then the name centred on the screen at scale a / (2 z).
    void drawZoom(Ctx& c, const Op& op, const Fade& f, double r) {
        // refreshes since the line started; after it, its last frame (drawn the refresh before the wait ended)
        double n = std::max(0.0, r < op.end ? r - op.start : op.end - 1.0 - op.start);
        double z = std::max(double(op.z0) + double(op.dz) * n, 1.0);
        double s = double(op.a) / (2.0 * z);
        float y0 = float(op.band), y1 = 200.0f - float(op.band);
        Picture& sky = picture(c, op.sky);
        Quad qs{{0, y0, 320, y1}, {1, 1, 0, 0}, {0, 0, float(sky.ix->w), float(sky.ix->h)}};
        drawPicture(c, sky, 0, qs, f, nullptr);
        Picture& name = picture(c, op.file);
        double cx = name.ix->w * 0.5, cy = name.ix->h * 0.5;
        Quad qn{{float(std::max(0.0, 160 - cx * s)), float(std::max(double(y0), 100 - cy * s)),
                 float(std::min(320.0, 160 + cx * s)), float(std::min(double(y1), 100 + cy * s))},
                {float(1 / s), float(1 / s), float(cx - 160 / s), float(cy - 100 / s)},
                {0, 0, float(name.ix->w), float(name.ix->h)}};
        if (qn.clip[2] > qn.clip[0] && qn.clip[3] > qn.clip[1]) drawPicture(c, name, 1, qn, f, nullptr);
    }

    // An effect's last frame: its scene renders it (in the palette it had then), and the fade since is applied.
    void drawEffect(Ctx& c, const Op& op, const Fade& f, double r) {
        double re = std::min(r, op.seg->end - 1e-3);
        Target* out = c.out;
        TargetPool::Lease t = c.tempSized(out->w, out->h);
        t->bind();
        setBlend(Blend::None);
        c.clear();
        vivid::updatePalette(c.paletteTex_, c.palette(re));
        c.renderScene(op.seg->scene, op.seg, re, *t);
        vivid::updatePalette(c.paletteTex_, c.palette(r));
        out->bind();
        setBlend(Blend::None);
        effectShader_.use();
        t->bindTex(0);
        effectShader_.set("frame", 0);
        bool ua = uniformPalette(f.a), ub = uniformPalette(f.b);
        effectShader_.set("from", f.a[0].x, f.a[0].y, f.a[0].z, ua ? 1.0f : 0.0f);
        effectShader_.set("to", f.b[0].x, f.b[0].y, f.b[0].z, ub ? 1.0f : 0.0f);
        effectShader_.set("fadeS", f.s);
        drawFullscreen();
    }

    Shader shader_, effectShader_;
    Texture pal_, tab_;
    std::map<std::string, std::unique_ptr<Picture>> pics_;
    std::map<std::string, Palette> palFiles_;
    size_t count_ = size_t(-1);
    std::vector<Op> ops_;
};

}  // namespace vga

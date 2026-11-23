// Script entries 12 and 13 (scene "dots": init 0x20562c / 0x20569c, retrace routine 0x2056f3, frame routine
// 0x20578c; reve/re/notes-parts2d-a.md section 5): a dim rotozoomed texture smeared outwards by a zooming feedback,
// and 30 pink dots on a turning spiral that leave fading echoes drifting outwards; through GFX\TEXTURE.RAW's palette.
//
// The original, per frame g (the state after g retrace calls): layer 1 (0x25a69c, scenes/roto.h) writes the 16 x 16
// block of TEXTURE.RAW at 0x5010 into a quarter of the pixels (angle a); 0x25eaec blends every pixel in place with
// GFX\SPIRBLUR.PAL, new = table[zoomed][current] = the palette index nearest to 3/4 current + 1/4 zoomed (what
// 0x25a275 computes), the zoomed source being the buffer enlarged by 1024 / Z about the centre; 0x25f038 draws the
// top-left 6 x 6 dot of GFX\SPIRALE.RAW at 30 points (160 + cos(b) R / 2, 100 + sin(b) R / 2), b = q + 300 k
// (4096 a turn), R = 50 + 5 k. The retrace routine steps a (p += 10, a += SIN[p] >> 8, continuing from where entry 7
// left them), q += spd >> 16 (the dots use q before the step), and every 210 refreshes takes the next pair of
// per-refresh increments of spd and Z from the key list 0x217518 (entry 13's init restarts it at 0x217540).
//
// The remake keeps the feedback in colour and steps it once per original frame. The quarter-pixel texture writes
// cycle through four pixel classes, so every pixel gets a fresh texel every fourth frame and a dot's echo is gone from
// a pixel after at most four frames (but for what the zoom carries outwards); the remake keeps one colour field per
// class (four render targets) and shows their mean, the average of the original's dither
// (reve/re/models/parts2d-a/layers.py):
//   step g, class c:  M_c = T (c = g mod 4: the class layer 1 writes, T = the layer per pixel, read filtered)
//                           or B_c (the others);  M = mean of the M_c
//                     B_c' = 3/4 M_c + 1/4 M(zoomed) - bias, then the dots
// The blend is the table in colour, minus the mean loss of its truncation (the table blends the 6-bit values as
// (a + 3 b) >> 2: on average 3/8 of a level lower; without it the background and echoes are brighter than the
// capture's), double-buffered (the original's in-place order is not visible). The zoom maps a pixel at VGA x to
// imm + frac + (x - 1/2) s (s = Z / 1024; imm and the start fraction as 0x25eaec computes them; the same for y), the
// mean position the original's carry steps read. A frame depends on the frames before it, but forgets them fast:
// after 14 steps less than 4e-4 of the old buffer is left, so the remake replays the last kWindow steps from black
// (from the entry start when that is nearer: the buffer is black there) and a frame depends only on its refresh. The
// dots are discs with SPIRALE's colours by radius, anti-aliased. The feedback fields have half the output's
// resolution (640 .. 960 wide: the feedback is a blur); the last step's dots are drawn at the output's.
//
// The frames are shown as they are, one per refresh, not blended between refreshes: the spiral turns up to 28 degrees
// per refresh while its dots are 26 degrees apart, and what it looks like (standing, creeping, arms moving in or out)
// is the stroboscopic effect of the original's 70 frames per second. Not reproduced: the palette quantisation of the
// blend (colours that stick instead of fading), the dither itself and the pixel-quantised dots.
#include <SDL3/SDL.h>

#include <cmath>
#include <vector>

#include "roto.h"

namespace {
constexpr uint32_t kTexturePal = 0x205ceb;   // "GFX\TEXTURE.RAW": its palette is the DAC (init 0x20562c)
constexpr uint32_t kSpirale = 0x205e17;      // "GFX\SPIRALE.RAW"
constexpr int kWindow = 14;                  // feedback steps replayed per frame
constexpr float kBias = 3.0f / 8 / 63;       // the mean loss of the blend table's truncation, per step

const char* kFeedFS = R"(
in vec2 uv;
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
uniform sampler2D b0, b1, b2, b3, tex;
uniform int phase;    // the class layer 1 writes this frame
uniform float ang, scale, size, bias;
uniform vec3 zoom;    // source = zoom.xy + p * zoom.z (VGA)
vec3 T(vec2 p) { return texture(tex, rotoUV(p, ang, scale, size)).rgb; }   // bilinear: it is blended away
void main() {
    vec2 p = uv * vec2(320.0, 200.0);   // the fields: row 0 = VGA y 0
    vec2 q = zoom.xy + p * zoom.z, tq = q / vec2(320.0, 200.0);
    vec3 t = T(p), tz = T(q);
    vec3 m0 = phase == 0 ? t : texture(b0, uv).rgb, z0 = phase == 0 ? tz : texture(b0, tq).rgb;
    vec3 m1 = phase == 1 ? t : texture(b1, uv).rgb, z1 = phase == 1 ? tz : texture(b1, tq).rgb;
    vec3 m2 = phase == 2 ? t : texture(b2, uv).rgb, z2 = phase == 2 ? tz : texture(b2, tq).rgb;
    vec3 m3 = phase == 3 ? t : texture(b3, uv).rgb, z3 = phase == 3 ? tz : texture(b3, tq).rgb;
    vec3 z = 0.0625 * (z0 + z1 + z2 + z3) - bias;   // 1/4 of the zoomed mean
    o0 = vec4(max(0.75 * m0 + z, 0.0), 1.0);
    o1 = vec4(max(0.75 * m1 + z, 0.0), 1.0);
    o2 = vec4(max(0.75 * m2 + z, 0.0), 1.0);
    o3 = vec4(max(0.75 * m3 + z, 0.0), 1.0);
}
)";

const char* kShowFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D b0, b1, b2, b3;
void main() {
    vec2 t = vec2(uv.x, 1.0 - uv.y);
    o = vec4(0.25 * (texture(b0, t).rgb + texture(b1, t).rgb + texture(b2, t).rgb + texture(b3, t).rgb), 1.0);
}
)";

const char* kDotVS = R"(
uniform float q;        // spiral angle (4096 a turn)
uniform vec4 geo;       // first radius, radius step, angle step, sine amplitude / 65536
uniform vec2 centre;    // the spiral's centre + half the dot (VGA)
uniform float half_;    // half the quad (VGA)
uniform float flip;     // +1: the output (VGA y down = clip y down), -1: a feedback buffer (row 0 = VGA y 0)
out vec2 local;
void main() {
    float k = float(gl_InstanceID);
    float b = (q + geo.z * k) * (6.283185307179586 / 4096.0);
    float R = geo.x + geo.y * k;
    vec2 c = centre + vec2(cos(b), sin(b)) * R * geo.w;
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    local = corner * half_;
    vec2 p = c + local;
    gl_Position = vec4(p.x / 160.0 - 1.0, flip * (1.0 - p.y / 100.0), 0.0, 1.0);
}
)";

const char* kDotFS = R"(
in vec2 local;
layout(location = 0) out vec4 o;    // the dot goes into all four class fields (or the output)
layout(location = 1) out vec4 o1;
layout(location = 2) out vec4 o2;
layout(location = 3) out vec4 o3;
uniform vec4 prof[8];   // rgb at radius w (VGA pixels), by increasing radius
uniform int nprof;
uniform float edge;     // radius of the disc
void main() {
    float d = length(local);
    vec3 c = prof[0].rgb;
    for (int i = 1; i < nprof; i++) {
        float t = clamp((d - prof[i - 1].w) / (prof[i].w - prof[i - 1].w), 0.0, 1.0);
        if (d > prof[i - 1].w) c = mix(prof[i - 1].rgb, prof[i].rgb, t);
    }
    float a = clamp((edge - d) / max(fwidth(d), 1e-4) + 0.5, 0.0, 1.0);
    o = vec4(c * a, a);
    o1 = o;
    o2 = o;
    o3 = o;
}
)";

// Four RGBA16F colour fields (one per pixel class) in one framebuffer.
struct Fields {
    GLuint fbo = 0;
    Texture tex[4];
    int w = 0, h = 0;
    void create(int w_, int h_) {
        destroy();
        w = w_;
        h = h_;
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        GLenum att[4];
        for (int i = 0; i < 4; i++) {
            tex[i].createEmpty(w, h, GL_RGBA16F, false);
            att[i] = GLenum(GL_COLOR_ATTACHMENT0 + i);
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, att[i], GL_TEXTURE_2D, tex[i].id, 0);
        }
        gl::DrawBuffers(4, att);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) SDL_Log("dots: fields incomplete");
    }
    void destroy() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        fbo = 0;
        for (Texture& t : tex) t.destroy();
    }
    void bind() const {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, w, h);
    }
    void bindTex(int unit) const {
        for (int i = 0; i < 4; i++) tex[i].bind(unit + i);
    }
};

// The frame state after g retrace calls.
struct Frame {
    float angle;          // layer 1's angle (radians)
    float zx, zy, zs;     // the zoom map
    double q;             // the dots' spiral angle (q before the step)
};

struct DotsScene : Scene {
    Shader feedSh, showSh, dotSh;
    Fields fields[2];   // ping-pong
    Texture tex;
    GLuint vao = 0;
    int size = 0;
    float scale = 0;
    std::vector<Frame> frames;   // g = 0 .. both entries' calls + 1
    int calls12 = 0;
    // the dot
    float prof[8][4] = {};
    int nprof = 0;
    float edge = 0, half_ = 0;
    float geo[4] = {};
    float centre[2] = {};
    int count = 0;

    ~DotsScene() override {
        for (Fields& f : fields) f.destroy();
        if (vao) gl::DeleteVertexArrays(1, &vao);
    }

    void init(Ctx& c) override {
        const ReveExe& e = *c.exe;
        // init 0x20562c: 16 x 16 from TEXTURE + 0x5010 to [0x205e90] (`add esi, imm32`, `mov ecx, imm32` dwords a row)
        size = 4 * int(e.u32(0x20565c));
        roto::loadBlock(c, tex, e.u32(0x205652), size, kTexturePal);
        const double amp = e.s16(roto::kSin + 0x800);   // the sine table's peak, 32767
        scale = float(amp * std::ldexp(1.0, 16 - e.u8(0x25a6be)) / std::ldexp(1.0, e.u8(0x25a6f8)) / 2);
        buildFrames(c);
        buildDot(c);
        feedSh.build(kFullscreenVS, std::string(roto::kRotoGLSL) + kFeedFS, "dots feedback");
        showSh.build(kFullscreenVS, kShowFS, "dots show");
        dotSh.build(kDotVS, kDotFS, "dots dot");
        gl::GenVertexArrays(1, &vao);
    }

    // The retrace routine 0x2056f3 replayed from entry 12's start (entry 7's angle walk first: a and p continue).
    void buildFrames(Ctx& c) {
        const ReveExe& e = *c.exe;
        roto::AngleWalk w;
        w.read(e, 0x204c5e, 0x204c79);   // entry 7's 0x204c5d
        w.a = e.s32(0x2174c8);
        w.p = e.u32(0x2174cc);
        for (int k = roto::callsOf(c, 7); k > 0; k--) w.step(e);
        w.read(e, 0x2056f4, 0x20570f);   // 0x2056f3: p += 10, a += SIN[p] >> 8
        int32_t q = e.s32(0x2174ec), qprev = e.s32(0x2174f0), zoom = e.s32(0x2174f4), spd = e.s32(0x2174f8);
        uint32_t ptr = e.u32(0x217508), cnt = e.u32(0x21750c);
        int32_t inc1 = e.s32(0x217510), inc2 = e.s32(0x217514);
        const uint32_t listEnd = e.u32(0x205730 + 6), reload = e.u32(0x205744 + 6);
        calls12 = roto::callsOf(c, 12);
        const int total = calls12 + roto::callsOf(c, 13) + 1;
        constexpr double kRad = 6.283185307179586 / 4096 / 2;   // radians per unit of a (2 per sine table step)
        for (int g = 0; g <= total; g++) {
            if (g == calls12 + 1) {   // entry 13's init 0x20569c: mov dword ptr [cnt], 1 / [ptr], 0x217540
                cnt = e.u32(0x20569d + 6);
                ptr = e.u32(0x2056a7 + 6);
            }
            if (g > 0) {
                w.step(e);
                qprev = q;
                q += spd >> 16;
                if (ptr <= listEnd) {
                    bool skip = false;
                    if (--cnt == 0) {
                        cnt = reload;
                        if (ptr == listEnd) skip = true;
                        else { inc1 = e.s32(ptr); inc2 = e.s32(ptr + 4); ptr += 8; }
                    }
                    if (!skip) { spd += inc1; zoom += inc2; }
                }
            }
            frames.push_back({float(w.a * kRad), 0, 0, 0, double(qprev)});
            zoomMap(e, zoom, frames.back());
        }
    }

    // 0x25eaec: step = Z << 22 (a 0.32 fraction), h = step >> 16; imm_x = (320 * 65536 - 320 h) >> 17 and the start
    // fraction x0 = that >> 1 (the same for y with 200): the source of pixel i is imm_x + floor((x0 + i step) / 2^32).
    static void zoomMap(const ReveExe& e, int32_t z, Frame& f) {
        const uint32_t step = uint32_t(z) << 22, h = step >> 16;
        const double s = step / 4294967296.0;
        auto axis = [&](uint32_t pxAddr, uint32_t fullAddr) {   // mov eax, 320 / mov edx, 320 * 65536 (imm32 at +1)
            const int32_t ex = int32_t(e.u32(fullAddr + 1) - e.u32(pxAddr + 1) * h);
            const double imm = double(ex >> 17), frac = double(uint32_t(ex) >> 1) / 4294967296.0;
            // the mean source (its pixel centre) of the pixel centred at x = i + 1/2: imm + frac + (x - 1/2) s
            return float(imm + frac - 0.5 * s);
        };
        f.zx = axis(0x25eafe, 0x25eb06);
        f.zy = axis(0x25eb1f, 0x25eb27);
        f.zs = float(s);
    }

    // The dot: the top-left n x n of GFX\SPIRALE.RAW (n = mov ch, imm8 in 0x25f094) through TEXTURE's palette, as a
    // disc: each colour at the mean distance of its pixels from the centre, the edge where the disc has the sprite's
    // area. The spiral: 0x25f038's immediates.
    void buildDot(Ctx& c) {
        const ReveExe& e = *c.exe;
        RawImage spr = rawImage(e, e.cstr(kSpirale));
        RawImage pal = rawImage(e, e.cstr(kTexturePal));
        const int n = e.u8(0x25f09e);
        std::vector<std::pair<int, std::pair<double, int>>> by;   // index -> (sum of distances, pixels)
        int opaque = 0;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                int i = spr.px[size_t(y) * spr.w + x];
                if (!i) continue;
                opaque++;
                double d = std::hypot(x + 0.5 - n / 2.0, y + 0.5 - n / 2.0);
                auto it = std::find_if(by.begin(), by.end(), [&](auto& b) { return b.first == i; });
                if (it == by.end()) by.push_back({i, {d, 1}});
                else { it->second.first += d; it->second.second++; }
            }
        std::vector<std::pair<double, int>> rings;
        for (auto& b : by) rings.push_back({b.second.first / b.second.second, b.first});
        std::sort(rings.begin(), rings.end());
        nprof = int(std::min<size_t>(rings.size(), 8));
        for (int k = 0; k < nprof; k++) {
            Vec3 col = pal.color(rings[size_t(k)].second);
            prof[k][0] = col.x;
            prof[k][1] = col.y;
            prof[k][2] = col.z;
            prof[k][3] = float(rings[size_t(k)].first);
        }
        edge = float(std::sqrt(opaque / 3.141592653589793));
        half_ = edge + 1.0f;
        const float dotCentre = n / 2.0f;
        const uint32_t base = e.u32(0x25f040);   // add edi, 100 * 320 + 160
        centre[0] = float(base % 320) + dotCentre;
        centre[1] = float(base / 320) + dotCentre;
        count = int(e.u32(0x25f045));             // mov ebp, 30
        geo[0] = float(e.u32(0x25f04a));          // mov edx, 50: the first R
        geo[1] = float(e.s8(0x25f08d));           // add edx, 5
        geo[2] = float(e.u32(0x25f087));          // add ebx, 300
        geo[3] = float(e.s16(roto::kSin + 0x800) / std::ldexp(1.0, e.u8(0x25f070)));   // * R >> 16
    }

    void drawDots(double q, float flip) {
        setBlend(Blend::Premul);
        dotSh.use();
        dotSh.set("q", float(q));
        dotSh.set("geo", geo[0], geo[1], geo[2], geo[3]);
        dotSh.set("centre", centre[0], centre[1]);
        dotSh.set("half_", half_);
        dotSh.set("flip", flip);
        gl::Uniform4fv(dotSh.loc("prof"), nprof, &prof[0][0]);
        dotSh.set("nprof", nprof);
        dotSh.set("edge", edge);
        gl::BindVertexArray(vao);
        gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    }

    void render(Ctx& c, double r) override {
        const double x = c.refreshes(r) + (c.cue->entry == 13 ? calls12 : 0);
        const int g = std::clamp(int(std::floor(x)), 0, int(frames.size()) - 1);
        const int w = std::clamp(c.w / 2, 640, 960), h = w * 3 / 4;
        for (Fields& f : fields)
            if (f.w != w || f.h != h) f.create(w, h);
        Target* out = c.out;
        int cur = 0;
        fields[0].bind();
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        for (int k = std::max(0, g - kWindow + 1); k <= g; k++) {
            const Frame& f = frames[size_t(k)];
            fields[1 - cur].bind();
            setBlend(Blend::None);
            feedSh.use();
            fields[cur].bindTex(0);
            tex.bind(4);
            feedSh.set("b0", 0);
            feedSh.set("b1", 1);
            feedSh.set("b2", 2);
            feedSh.set("b3", 3);
            feedSh.set("tex", 4);
            feedSh.set("phase", k & 3);
            feedSh.set("ang", f.angle);
            feedSh.set("scale", scale);
            feedSh.set("size", float(size));
            feedSh.set("bias", kBias);
            feedSh.set("zoom", f.zx, f.zy, f.zs);
            drawFullscreen();
            cur = 1 - cur;
            if (k < g) drawDots(f.q, -1.0f);   // the last step's dots go to the output at its resolution
        }
        out->bind();
        setBlend(Blend::None);
        showSh.use();
        fields[cur].bindTex(0);
        showSh.set("b0", 0);
        showSh.set("b1", 1);
        showSh.set("b2", 2);
        showSh.set("b3", 3);
        drawFullscreen();
        drawDots(frames[size_t(g)].q, 1.0f);
        gl::ActiveTexture(GL_TEXTURE0);
    }
};
}  // namespace

REGISTER_SCENE(DotsScene, "dots");

#include "exefx.h"

#include <cmath>

const std::array<int, 16>& exeJitter(const Ctx& c) {
    static std::array<int, 16> j;
    static bool done = false;
    if (!done) {
        for (int i = 0; i < 16; i++) j[size_t(i)] = c.exe.s8(0x52f10 + uint32_t(i));
        done = true;
    }
    return j;
}

const std::vector<std::string>& lyricCycle(const Ctx& c) {
    static std::vector<std::string> w;
    if (w.empty())
        for (uint32_t i = 0; i < 10; i++) w.push_back(c.exe.str(0x57dc0 + 4 * i));
    return w;
}

const std::vector<SongEntry>& songTable(const Ctx& c) {
    static std::vector<SongEntry> t;
    if (t.empty())
        for (uint32_t i = 0; i < 41; i++)
            t.push_back({c.exe.s32(0x57e8c + 4 * i), c.exe.s32(0x57de8 + 4 * i), c.exe.str(0x57f30 + 4 * i)});
    return t;
}

int songIndex(const Ctx& c, int pos) {
    const auto& t = songTable(c);
    int ix = (pos + 2) / 2, order = ix / 64, row = ix % 64;
    if (order == 21 || order == 25 || order == 27) order = 17;
    if (order == 22 || order == 26 || order == 28) order = 18;
    for (int i = 0; i < 40; i++)
        if (t[size_t(i)].row == row && t[size_t(i)].order == order) return i;
    return 40;
}

const char* kExePaletteGLSL = R"(
uniform float palLevel, palMult;
uniform int palInvert;
vec3 exePalette(vec3 p) {
    vec3 q;
    if (palLevel < 64.0) q = p * palLevel / 64.0;
    else if (palLevel < 65.0) q = p;
    else {
        float c = min(palLevel - 65.0, 63.0);
        q = palInvert != 0 ? ((1.0 - p) * c + p * (63.0 - c)) / 64.0 : c / 63.0 + p * (63.0 - c) / 64.0;
    }
    return min(q * palMult, vec3(1.0));
}
)";

const char* kSphereRayGLSL = R"(
vec2 sphereRayTexel(vec2 px, vec3 R0, vec3 R1, vec3 R2, float focal, float scale, float off) {
    vec2 g = px / 8.0;
    vec3 d = normalize((g.x - 20.5) * R0 + (g.y - 13.0) * R1 + focal * R2);
    return vec2(128.0 + 128.0 * scale * d.x + off, 128.0 + 128.0 * scale * d.y);
}
)";

void ExePalette::apply(const Shader& s) const {
    float lv = float(level), m = mult;
    if (frac > 0.0f) {
        ExePalette n = *this;
        n.tick();
        lv += float(n.level - level) * frac;
        m += (n.mult - mult) * frac;
    }
    s.set("palLevel", lv / 256.0f);
    s.set("palMult", m);
    s.set("palInvert", invert ? 1 : 0);
}

void exeMatrix(float a, float b, Vec3& R0, Vec3& R1, Vec3& R2) {
    const float k = 6.28318530718f / 1024.0f;
    float sa = std::sin(a * k), ca = std::cos(a * k), sb = std::sin(b * k), cb = std::cos(b * k);
    R0 = Vec3(cb, 0.0f, -sb);
    R1 = Vec3(-sb * sa, ca, -cb * sa);
    R2 = Vec3(sb * ca, sa, cb * ca);
}

int StepClock::steps(const Ctx& c, float hz) {
    bool reset = std::fabs(c.T - lastT - c.dt) > 0.25f || lastT < -1e8f;
    double prev = lastT;
    lastT = c.T;
    if (reset) return -1;
    // Steps fall on the absolute grid k / hz, so where they land does not depend on where rendering started:
    // after a seek and a short warm-up the feedback is the same as in uninterrupted playback.
    long long n = (long long)std::floor(double(c.T) * hz) - (long long)std::floor(prev * hz);
    return int(std::clamp(n, 0LL, 8LL));
}

// ---------------------------------------------------------------- lines
namespace {
const char* kLineVS = R"(
layout(location = 0) in vec2 pos;
layout(location = 1) in float across;
layout(location = 2) in float val;
out float a, v;
void main() {
    a = across;
    v = val;
    gl_Position = vec4(pos.x / 160.0 - 1.0, 1.0 - pos.y / 100.0, 0.0, 1.0);
}
)";
const char* kLineFS = R"(
in float a, v;
out vec4 frag;
void main() { frag = vec4(v * max(0.0, 1.0 - abs(a)), 0.0, 0.0, 1.0); }
)";
Shader& lineShader() {
    static Shader s;
    if (!s.id()) s.build(kLineVS, kLineFS, "lines");
    return s;
}
}  // namespace

void LineBatch::add(float x0, float y0, float x1, float y1, float value, float hw) {
    // offsets in square units (an original pixel is 1.2 times taller than wide on a 4:3 screen)
    float dx = x1 - x0, dy = (y1 - y0) * 1.2f;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { dx = 1; dy = 0; len = 1; }
    dx /= len;
    dy /= len;
    float nx = -dy * hw, ny = dx * hw / 1.2f;
    float ex = dx * hw * 0.5f, ey = dy * hw * 0.5f / 1.2f;  // small end overlap to soften joints
    float ax = x0 - ex, ay = y0 - ey, bx = x1 + ex, by = y1 + ey;
    const float q[6][3] = {{ax + nx, ay + ny, 1}, {ax - nx, ay - ny, -1}, {bx + nx, by + ny, 1},
                           {bx + nx, by + ny, 1}, {ax - nx, ay - ny, -1}, {bx - nx, by - ny, -1}};
    for (auto& p : q) {
        v_.push_back(p[0]);
        v_.push_back(p[1]);
        v_.push_back(p[2]);
        v_.push_back(value);
    }
}

void LineBatch::circle(float cx, float cy, float r, float value, float hw) {
    if (r <= 0.0f) return;
    int n = std::clamp(int(r * 0.6f), 16, 256);
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float a = 6.28318530718f * float(i) / float(n);
        float qx = cx + r * std::cos(a), qy = cy + r * std::sin(a);
        add(px, py, qx, qy, value, hw);
        px = qx;
        py = qy;
    }
}

void LineBatch::draw() {
    if (v_.empty()) return;
    if (!vao_) {
        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
        gl::VertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (void*)8);
        gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 16, (void*)12);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::EnableVertexAttribArray(2);
    }
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = v_.size() * sizeof(float);
    if (bytes > cap_) {
        cap_ = bytes * 2;
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(cap_), nullptr, GL_DYNAMIC_DRAW);
    }
    gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), v_.data());
    lineShader().use();
    gl::Enable(GL_BLEND);
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE);
    gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(v_.size() / 4));
    gl::BindVertexArray(0);
    setBlend(Blend::None);
    v_.clear();
}

// ---------------------------------------------------------------- lyrics
namespace {
const char* kLyricFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D prev, word;
uniform vec4 quad;      // x0, y0, x1, y1
uniform float width, smear, finv, j4, reset;
void main() {
    vec2 p = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    float col = clamp((p.x - quad.x) * width / max(quad.z - quad.x, 1.0) + 8.0, 0.0, width + 16.0);
    float row;
    if (p.y < quad.y) row = p.y < 8.0 ? mix(25.0, 25.0 - smear, p.y / 8.0) : 25.0 - smear;
    else if (p.y >= quad.w) row = p.y > 192.0 ? mix(61.0 + smear, 61.0, (p.y - 192.0) / 8.0) : 61.0 + smear;
    else row = (p.y - quad.y) * 56.0 / max(quad.w - quad.y, 1.0) + 16.0;
    float S = texture(word, vec2((col + 0.5) / 256.0, (row + 0.5) / 96.0)).r * 255.0;
    float B = reset > 0.5 ? 0.0 : texture(prev, uv).r;
    float i = min(255.0, 0.7 * B + 0.5 * S) + 5.0 * j4;
    frag = vec4(clamp(i * (1.0 - finv) + (255.0 - i) * finv, 0.0, 255.0), 0.0, 0.0, 1.0);
}
)";

// Glyph runs of fontblur.raw as the exe finds them (0x29599): a column belongs to a glyph when any of
// rows 29..59 is above 8; runs narrower than 4 are skipped; the 26 runs are 'a'..'z'. The words are built from the
// file's coverage bytes, so an art/ redraw does not replace fontblur.raw.
struct GlyphRun { int x0, w; };
const std::vector<GlyphRun>& glyphRuns(Ctx& c) {
    static std::vector<GlyphRun> runs;
    if (!runs.empty()) return runs;
    const Bytes& raw = c.pak.get("fontblur.raw");
    auto empty = [&](int x) {
        for (int y = 29; y < 60; y++) if (raw[size_t(y) * 560 + x] > 8) return false;
        return true;
    };
    int x = 0;
    while (runs.size() < 26 && x < 560) {
        while (x < 560 && empty(x)) x++;
        int s = x;
        while (x < 560 && !empty(x)) x++;
        if (x - s >= 4) runs.push_back({s, x - s});
    }
    return runs;
}
}  // namespace

int LyricLayer::wordWidth(Ctx& c, const std::string& w) {
    const auto& runs = glyphRuns(c);
    int n = 0;
    for (char ch : w) {
        int g = ch - 'a';
        n += (g >= 0 && g < int(runs.size()) ? runs[size_t(g)].w : 8) + 1;
    }
    return n;
}

void LyricLayer::buildWord(Ctx& c, const std::string& w) {
    const auto& runs = glyphRuns(c);
    const Bytes& raw = c.pak.get("fontblur.raw");
    std::vector<uint8_t> tb(256 * 96 * 4, 0);
    int x = 8;
    for (char ch : w) {
        int g = ch - 'a';
        if (g < 0 || g >= int(runs.size())) { x += 9; continue; }
        const GlyphRun& r = runs[size_t(g)];
        for (int yy = 0; yy < 96; yy++)
            for (int i = 0; i < r.w && x + i < 256; i++) {
                uint8_t v = raw[size_t(yy) * 560 + r.x0 + i];
                uint8_t* d = &tb[(size_t(yy) * 256 + x + i) * 4];
                d[0] = d[1] = d[2] = d[3] = v;
            }
        x += r.w + 1;
    }
    if (!word_.id) word_.createEmpty(256, 96, GL_RGBA8, false);
    word_.update(tb.data());
    built_ = w;
}

void LyricLayer::init(Ctx&) { sh_.build(kFullscreenVS, kLyricFS, "lyrics"); }

void LyricLayer::update(Ctx& c, const LyricState& s, int steps) {
    if (acc_[0].w != c.w || acc_[0].h != c.h)
        for (Target& t : acc_) t.create(c.w, c.h, false, GL_R16F);
    if (s.word != built_) buildWord(c, s.word);
    bool reset = steps < 0;
    int n = reset ? 4 : steps;  // after a seek, run a few steps to build up the trails
    float x0, y0, x1, y1;
    if (!s.zoom) {
        x0 = float(s.tx + 2 * s.jit[0]); y0 = float(s.ty + 2 * s.jit[1]);
        x1 = float(s.tx + s.width + 4 * s.jit[2]); y1 = float(s.ty + 56 + s.jit[3]);
    } else {
        x0 = float(s.tx - s.width); y0 = float(s.ty - 56);
        x1 = float(s.tx + 2 * s.width + 4 * s.jit[2]); y1 = float(s.ty + 112 + 4 * s.jit[3]);
    }
    setBlend(Blend::None);
    for (int k = 0; k < n; k++) {
        Target& dst = acc_[cur_ ^ 1];
        dst.bind();
        sh_.use();
        acc_[cur_].bindTex(0);
        word_.bind(1);
        sh_.set("prev", 0);
        sh_.set("word", 1);
        sh_.set("quad", x0, y0, x1, y1);
        sh_.set("width", float(s.width));
        sh_.set("smear", float(s.smear));
        sh_.set("finv", s.finv);
        sh_.set("j4", float(s.jit[4]));
        sh_.set("reset", reset && k == 0 ? 1.0f : 0.0f);
        drawFullscreen();
        cur_ ^= 1;
    }
}

// ---------------------------------------------------------------- composite
namespace {
const char* kCompositeFS = R"(
in vec2 uv;
out vec4 frag;
uniform sampler2D src, lyr;
uniform vec3 win;          // winx, winy, open
uniform int useLyrics;
uniform vec3 tint[32];
void main() {
    vec2 px = vec2(uv.x * 320.0, (1.0 - uv.y) * 200.0);
    vec3 col = texture(src, uv).rgb;
    if (win.x > -500.0) {
        float f = win.z;
        float x0 = floor(53.0 * (1.0 - f)), y0 = floor(33.0 * (1.0 - f)), w = floor(106.0 * f), h = floor(66.0 * f);
        vec2 q = px - win.xy;
        if (w > 0.0 && h > 0.0 && q.x >= x0 - 1.0 && q.x < x0 + w + 1.0 && q.y >= y0 - 1.0 && q.y < y0 + h + 1.0) {
            if (q.x < x0 || q.x >= x0 + w || q.y < y0 || q.y >= y0 + h) col = vec3(1.0);
            else col = 1.0 - texture(src, vec2(q.x * 3.0 / 320.0, 1.0 - q.y * 3.0 / 200.0)).rgb;
        }
    }
    if (useLyrics != 0) {
        float rows = clamp(texture(lyr, uv).r / 8.0 - 0.5, 0.0, 31.0);
        int i = int(min(rows, 30.0));
        vec3 dev = mix(tint[i], tint[i + 1], rows - float(i));
        float dark = 1.0 - dot(col, vec3(0.3, 0.59, 0.11));
        col = clamp(mix(col, vec3(1.0), rows / 31.0) + dev * dark, 0.0, 1.0);
    }
    frag = vec4(exePalette(col), 1.0);
}
)";
}  // namespace

void exeComposite(Ctx& c, const Target& src, const ExeComposite& k, const ExePalette& pal) {
    static Shader s;
    if (!s.id()) s.build(kFullscreenVS, std::string(kExePaletteGLSL) + kCompositeFS, "exe composite");
    (void)c;
    setBlend(Blend::None);
    s.use();
    src.bindTex(0);
    s.set("src", 0);
    if (k.lyrics) {
        k.lyrics->bindTex(1);
        s.set("lyr", 1);
    }
    s.set("useLyrics", k.lyrics ? 1 : 0);
    static const std::vector<float> zero(96, 0.0f);
    gl::Uniform3fv(s.loc("tint"), 32, (k.tint ? *k.tint : zero).data());
    s.set("win", k.winx, k.winy, k.open);
    pal.apply(s);
    drawFullscreen();
}

std::vector<float> fadDarkTint(Ctx& c, const std::string& fad, const std::string& paletteRaw) {
    const Bytes& f = c.pak.get(fad);
    const Bytes& raw = c.pak.get(paletteRaw);
    int black = 0, best = 1 << 30;
    for (int i = 0; i < 256; i++) {
        int l = 3 * raw[size_t(i) * 3] + 6 * raw[size_t(i) * 3 + 1] + raw[size_t(i) * 3 + 2];
        if (l < best) { best = l; black = i; }
    }
    std::vector<float> d(96);
    for (int r = 0; r < 32; r++) {
        int idx = f[size_t(32 + r) * 256 + size_t(black)];
        for (int k = 0; k < 3; k++)
            d[size_t(r) * 3 + size_t(k)] = std::min(63, int(raw[size_t(idx) * 3 + size_t(k)])) / 63.0f - float(r) / 31.0f;
    }
    return d;
}

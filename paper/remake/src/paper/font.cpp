// Font 0xe2e (RLE at code32 0x18ed7), text placement 0x11b0 / 0xe8a / 0xedc, and the high-resolution glyph atlas
// (signed distance fields) with an anti-aliased, saturating glyph renderer. See font.h.
#include "paper/font.h"

#include <algorithm>
#include <cmath>

#include "demo.h"
#include "exe.h"

namespace paper {

static constexpr uint32_t kFontRle = 0x18ed7;

// ---------------------------------------------------------------- 0xe2e decode
FontData::FontData(const PaperExe& exe) {
    uint32_t src = kFontRle;
    for (FontGlyph& g : glyphs_) {
        g.width = exe.u8(src++);
        for (int row = 0; row < 32; row++) {
            int x = 0;
            for (;;) {
                int b = exe.u8(src++);
                if (b == 0) break;
                int skip, run;
                if (b == 0xff) { skip = exe.u8(src); run = exe.u8(src + 1); src += 2; }
                else { skip = b & 15; run = b >> 4; }
                if (run == 0) run = 256;  // dec ah / jne: a zero count would mean 256 (never in the data)
                x += skip;
                for (; run > 0; run--, x++)
                    if (x < 60) g.bits[row * 60 + x] = 1;
            }
        }
    }
}

int FontData::glyphIndex(char c) {
    int g = int(uint8_t(c)) - 'a';
    return g >= 0 && g < kGlyphs ? g : -1;
}

int FontData::drawBytes(uint8_t* buf, int bufW, int bufH, int stride, int x, int y, char c) const {
    int gi = glyphIndex(c);
    if (gi < 0) return x + 10;
    const FontGlyph& g = glyphs_[gi];
    for (int r = 0; r < 32; r++)
        for (int k = 0; k < g.width; k++) {
            int px = x + k, py = y + r;
            if (px < 0 || py < 0 || px >= bufW || py >= bufH || !g.bits[r * 60 + k]) continue;
            buf[py * stride + px] = 255;  // saturating add of 0xff
        }
    return x + g.width + 1;
}

int FontData::drawBytesBig(uint8_t* buf, int bufW, int bufH, int stride, int x, int y, char c) const {
    int gi = glyphIndex(c);
    if (gi < 0) return x;
    const FontGlyph& g = glyphs_[gi];
    for (int r = 0; r < 25; r++)
        for (int k = 0; k <= g.width; k++)       // stores 4 bytes (v, v, v, 0) per glyph pixel, 3 apart
            for (int dy = 0; dy < 7; dy++)
                for (int dx = 0; dx < (k < g.width ? 3 : 1); dx++) {
                    int px = x + 3 * k + dx, py = y + 7 * r + dy;
                    if (px < 0 || py < 0 || px >= bufW || py >= bufH) continue;
                    buf[py * stride + px] = (k < g.width && g.bits[r * 60 + k]) ? 255 : 0;
                }
    return x + 3 * g.width + 2;
}

// ---------------------------------------------------------------- layout
std::vector<GlyphPos> layoutText(const FontData& f, const std::string& s, int x, int y, int stride) {
    std::vector<GlyphPos> out;
    int64_t addr = int64_t(y) * stride + x;  // the original's edi, relative to the buffer start
    for (char ch : s) {
        if (ch == 0) break;
        int gi = FontData::glyphIndex(ch);
        if (gi < 0) {
            addr += 10;
            if (ch == 1) addr += 0x303e;
            continue;
        }
        GlyphPos p;
        p.glyph = gi;
        int64_t row = addr >= 0 ? addr / stride : -((-addr + stride - 1) / stride);
        p.x = float(addr - row * stride);
        p.y = float(row);
        out.push_back(p);
        addr += f.glyph(gi).width + 1;
    }
    return out;
}

std::vector<GlyphPos> layoutBig(const FontData& f, const std::string& s, int x, int y) {
    std::vector<GlyphPos> out;
    for (char ch : s) {
        int gi = FontData::glyphIndex(ch);
        if (gi < 0) continue;
        GlyphPos p;
        p.glyph = gi;
        p.x = float(x);
        p.y = float(y);
        p.sx = 3;
        p.sy = 7;
        p.rows = 25;
        out.push_back(p);
        x += 3 * f.glyph(gi).width + 2;
    }
    return out;
}

int textAdvance(const FontData& f, const std::string& s) {
    int a = 0;
    for (char ch : s) a += FontData::glyphIndex(ch) < 0 ? 10 : f.width(ch) + 1;
    return a;
}

// ---------------------------------------------------------------- signed distance fields
namespace {

constexpr int kPad = 3;     // font pixels of padding around each glyph box
constexpr int kScale = 10;  // atlas texels per font pixel

// 1D squared distance transform (Felzenszwalb & Huttenlocher).
void edt1d(const float* f, int n, float* d, int* v, float* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
        while (s <= z[k]) {
            k--;
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = float((q - v[k]) * (q - v[k])) + f[v[k]];
    }
}

// Distance from every texel to the nearest texel whose mask equals `target`.
std::vector<float> edt2d(const std::vector<uint8_t>& mask, int w, int h, uint8_t target) {
    std::vector<float> g(size_t(w) * h);
    for (size_t i = 0; i < g.size(); i++) g[i] = mask[i] == target ? 0.0f : 1e20f;
    int n = std::max(w, h);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) f[y] = g[size_t(y) * w + x];
        edt1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; y++) g[size_t(y) * w + x] = d[y];
    }
    for (int y = 0; y < h; y++) {
        edt1d(&g[size_t(y) * w], w, d.data(), v.data(), z.data());
        for (int x = 0; x < w; x++) g[size_t(y) * w + x] = std::sqrt(d[x]);
    }
    return g;
}

struct Pt { double x, y; };

// Boundary loops of the glyph bitmap along pixel edges (pixel (x, y) = [x, x+1] x [y, y+1] in font pixels, y down),
// walked with the ink on the right (outer loops clockwise on screen, holes anticlockwise). At a vertex where two
// pixels touch diagonally the walk turns right, so they stay separate.
std::vector<std::vector<Pt>> traceGlyph(const FontGlyph& g) {
    auto ink = [&](int x, int y) { return x >= 0 && x < g.width && y >= 0 && y < 32 && g.bits[y * 60 + x]; };
    const int W = g.width + 3;
    auto key = [&](int x, int y) { return (y + 1) * W + (x + 1); };
    std::vector<std::vector<int>> outs(size_t(W) * 35);
    auto edge = [&](int x0, int y0, int x1, int y1) { outs[key(x0, y0)].push_back(key(x1, y1)); };
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < g.width; x++) {
            if (!ink(x, y)) continue;
            if (!ink(x, y - 1)) edge(x, y, x + 1, y);
            if (!ink(x + 1, y)) edge(x + 1, y, x + 1, y + 1);
            if (!ink(x, y + 1)) edge(x + 1, y + 1, x, y + 1);
            if (!ink(x - 1, y)) edge(x, y + 1, x, y);
        }
    auto pt = [&](int k) { return Pt{double(k % W - 1), double(k / W - 1)}; };
    std::vector<std::vector<Pt>> loops;
    for (size_t s0 = 0; s0 < outs.size(); s0++) {
        while (!outs[s0].empty()) {
            int start = int(s0), a = start, b = outs[s0].back();
            outs[s0].pop_back();
            std::vector<Pt> loop{pt(a)};
            while (b != start) {
                loop.push_back(pt(b));
                std::vector<int>& o = outs[b];
                Pt pa = pt(a), pb = pt(b);
                size_t pick = 0;
                double bestTurn = -2;
                for (size_t i = 0; i < o.size(); i++) {  // prefer the right turn
                    Pt pc = pt(o[i]);
                    double turn = (pb.x - pa.x) * (pc.y - pb.y) - (pb.y - pa.y) * (pc.x - pb.x);
                    if (turn > bestTurn) { bestTurn = turn; pick = i; }
                }
                int c = o[pick];
                o.erase(o.begin() + long(pick));
                a = b;
                b = c;
            }
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

// Turns a pixel-edge loop into a smooth outline: every vertex moves towards the mean of its neighbours but stays
// within half a pixel (per axis) of where it was, which straightens staircases into the lines and curves they
// sample (the shortest path through the half-pixel band); vertices between two straight runs of 3+ pixels are true
// corners and stay put. A few unconstrained passes then round the remaining polygon kinks.
void smoothLoop(std::vector<Pt>& P) {
    const size_t n = P.size();
    auto dir = [&](size_t i) {  // direction of the unit edge leaving vertex i
        const Pt& a = P[i];
        const Pt& b = P[(i + 1) % n];
        return int(b.x - a.x) * 3 + int(b.y - a.y);
    };
    std::vector<int> d(n);
    for (size_t i = 0; i < n; i++) d[i] = dir(i);
    std::vector<char> pinned(n, 0);
    for (size_t i = 0; i < n; i++) {
        size_t prev = (i + n - 1) % n;
        if (d[i] == d[prev]) continue;
        size_t out = 0, in = 0;
        while (out < n && d[(i + out) % n] == d[i]) out++;
        while (in < n && d[(prev + n - in) % n] == d[prev]) in++;
        pinned[i] = out >= 3 && in >= 3;
    }
    std::vector<Pt> Q = P, T(n);
    for (int it = 0; it < 300; it++) {
        for (size_t i = 0; i < n; i++) {
            if (pinned[i]) { T[i] = P[i]; continue; }
            const Pt& a = Q[(i + n - 1) % n];
            const Pt& b = Q[(i + 1) % n];
            T[i] = {std::clamp(0.5 * (a.x + b.x), P[i].x - 0.5, P[i].x + 0.5),
                    std::clamp(0.5 * (a.y + b.y), P[i].y - 0.5, P[i].y + 0.5)};
        }
        std::swap(Q, T);
    }
    for (int it = 0; it < 4; it++) {
        for (size_t i = 0; i < n; i++) {
            const Pt& a = Q[(i + n - 1) % n];
            const Pt& b = Q[(i + 1) % n];
            T[i] = pinned[i] ? P[i] : Pt{0.25 * a.x + 0.5 * Q[i].x + 0.25 * b.x, 0.25 * a.y + 0.5 * Q[i].y + 0.25 * b.y};
        }
        std::swap(Q, T);
    }
    P = Q;
}

double segDist(Pt p, Pt a, Pt b) {
    double vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
    double l2 = vx * vx + vy * vy;
    double t = l2 > 0 ? std::clamp((wx * vx + wy * vy) / l2, 0.0, 1.0) : 0.0;
    double dx = wx - t * vx, dy = wy - t * vy;
    return std::sqrt(dx * dx + dy * dy);
}

// Signed distance (font pixels, positive inside) of one glyph's padded box at kScale texels per pixel, from the
// smoothed outline: inside by scanline parity, distance by an EDT, exact segment distance near the outline.
std::vector<float> glyphSdf(const FontGlyph& g, int& W, int& H) {
    std::vector<std::vector<Pt>> loops = traceGlyph(g);
    for (auto& l : loops) smoothLoop(l);
    W = (g.width + 2 * kPad) * kScale;
    H = (32 + 2 * kPad) * kScale;
    auto fx = [&](int i) { return -kPad + (i + 0.5) / kScale; };  // texel centre in font pixels
    // inside mask: crossings of each texel row with the outline, even-odd
    std::vector<uint8_t> inside(size_t(W) * H, 0);
    std::vector<double> xs;
    for (int j = 0; j < H; j++) {
        double y = fx(j);
        xs.clear();
        for (const auto& l : loops)
            for (size_t i = 0; i < l.size(); i++) {
                Pt a = l[i], b = l[(i + 1) % l.size()];
                if ((a.y <= y) != (b.y <= y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
            }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int i0 = std::max(0, int(std::ceil((xs[k] + kPad) * kScale - 0.5)));
            int i1 = std::min(W - 1, int(std::floor((xs[k + 1] + kPad) * kScale - 0.5)));
            for (int i = i0; i <= i1; i++) inside[size_t(j) * W + i] = 1;
        }
    }
    std::vector<float> dOut = edt2d(inside, W, H, 1), dIn = edt2d(inside, W, H, 0);
    // segments bucketed per font pixel for the exact distance near the outline
    const int cw = g.width + 2 * kPad, ch = 32 + 2 * kPad;
    std::vector<std::vector<std::pair<Pt, Pt>>> cells(size_t(cw) * ch);
    for (const auto& l : loops)
        for (size_t i = 0; i < l.size(); i++) {
            Pt a = l[i], b = l[(i + 1) % l.size()];
            int x0 = int(std::floor(std::min(a.x, b.x) + kPad)) - 1, x1 = int(std::floor(std::max(a.x, b.x) + kPad)) + 1;
            int y0 = int(std::floor(std::min(a.y, b.y) + kPad)) - 1, y1 = int(std::floor(std::max(a.y, b.y) + kPad)) + 1;
            for (int y = std::max(0, y0); y <= std::min(ch - 1, y1); y++)
                for (int x = std::max(0, x0); x <= std::min(cw - 1, x1); x++) cells[size_t(y) * cw + x].push_back({a, b});
        }
    std::vector<float> sd(size_t(W) * H);
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            size_t k = size_t(j) * W + i;
            double d = inside[k] ? dIn[k] - 0.5 : -(dOut[k] - 0.5);  // texels
            if (std::fabs(d) < 3.0) {
                Pt p{fx(i), fx(j)};
                const auto& c = cells[size_t(std::min(ch - 1, j / kScale)) * cw + std::min(cw - 1, i / kScale)];
                double best = 1e9;
                for (const auto& sgm : c) best = std::min(best, segDist(p, sgm.first, sgm.second));
                if (best < 1e8) d = (inside[k] ? best : -best) * kScale;
            }
            sd[k] = float(d / kScale);
        }
    return sd;
}

const char* kGlyphVS = R"(
uniform mat4 proj;
uniform vec4 box;     // x, y, w, h of the padded glyph box in buffer pixels
uniform vec4 local;   // the same box in font pixels: x0, y0, x1, y1
uniform vec4 cell;    // atlas u0, v0, u1, v1
out vec2 p;           // font pixel coordinates
out vec2 uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    p = mix(local.xy, local.zw, c);
    uv = mix(cell.xy, cell.zw, c);
    gl_Position = proj * vec4(box.xy + c * box.zw, 0.0, 1.0);
}
)";

const char* kGlyphFS = R"(
in vec2 p;
in vec2 uv;
out vec4 o;
uniform sampler2D sdf;
uniform float rows;    // font rows drawn (0xedc: 25)
uniform float value;
uniform vec3 color;
uniform int over;
void main() {
    float d = texture(sdf, uv).r;
    float aa = 0.5 * (length(vec2(dFdx(p.x), dFdy(p.x))) + length(vec2(dFdx(p.y), dFdy(p.y))));
    aa = max(aa, 1e-4);
    float cov = clamp(d / aa + 0.5, 0.0, 1.0);
    cov *= clamp((rows - p.y) / aa + 0.5, 0.0, 1.0);
    o = over != 0 ? vec4(color, cov) : vec4(vec3(value * cov), value * cov);
}
)";

}  // namespace

Font::Font(Ctx& c) : data_(*c.exe) {
    // build all fields, then pack them into rows of an atlas
    std::vector<std::vector<float>> sdf(FontData::kGlyphs);
    std::vector<int> ws(FontData::kGlyphs), hs(FontData::kGlyphs);
    for (int g = 0; g < FontData::kGlyphs; g++) sdf[g] = glyphSdf(data_.glyph(g), ws[g], hs[g]);
    const int atlasW = 4096;
    std::vector<int> ox(FontData::kGlyphs), oy(FontData::kGlyphs);
    int x = 0, y = 0, rowH = 0;
    for (int g = 0; g < FontData::kGlyphs; g++) {
        if (x + ws[g] > atlasW) { x = 0; y += rowH + 2; rowH = 0; }
        ox[g] = x;
        oy[g] = y;
        x += ws[g] + 2;
        rowH = std::max(rowH, hs[g]);
    }
    const int atlasH = y + rowH;
    std::vector<float> img(size_t(atlasW) * atlasH, -float(kPad));
    for (int g = 0; g < FontData::kGlyphs; g++) {
        for (int r = 0; r < hs[g]; r++)
            std::copy(sdf[g].begin() + size_t(r) * ws[g], sdf[g].begin() + size_t(r + 1) * ws[g],
                      img.begin() + size_t(oy[g] + r) * atlasW + ox[g]);
        cells_[g] = {float(ox[g]) / atlasW, float(oy[g]) / atlasH, float(ox[g] + ws[g]) / atlasW,
                     float(oy[g] + hs[g]) / atlasH};
    }
    atlas_.createRaw(atlasW, atlasH, GL_R16F, GL_RED, GL_FLOAT, img.data(), true, false, false);
    shader_.build(kGlyphVS, kGlyphFS, "paper font");
}

Mat4 Font::pixelProj(float w, float h) { return Mat4::ortho(0, w, h, 0, -1, 1); }

void Font::draw(const GlyphPos& g, const Mat4& proj, GlyphOp op, float value, Vec3 color) {
    draw(std::vector<GlyphPos>{g}, proj, op, value, color);
}

void Font::draw(const std::vector<GlyphPos>& glyphs, const Mat4& proj, GlyphOp op, float value, Vec3 color) {
    gl::Enable(GL_BLEND);
    switch (op) {
        case GlyphOp::Add: gl::BlendEquation(GL_FUNC_ADD); gl::BlendFunc(GL_ONE, GL_ONE); break;
        case GlyphOp::Sub: gl::BlendEquation(GL_FUNC_REVERSE_SUBTRACT); gl::BlendFunc(GL_ONE, GL_ONE); break;
        case GlyphOp::Max: gl::BlendEquation(GL_MAX); gl::BlendFunc(GL_ONE, GL_ONE); break;
        case GlyphOp::Over:
            gl::BlendEquation(GL_FUNC_ADD);
            gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
    shader_.use();
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindTexture(GL_TEXTURE_2D, atlas_.id);
    shader_.set("sdf", 0);
    shader_.set("proj", proj);
    shader_.set("value", value);
    shader_.set("color", color);
    shader_.set("over", op == GlyphOp::Over ? 1 : 0);
    for (const GlyphPos& g : glyphs) {
        if (g.glyph < 0 || g.glyph >= FontData::kGlyphs) continue;
        float w = float(data_.glyph(g.glyph).width);
        const Cell& c = cells_[g.glyph];
        shader_.set("box", g.x - kPad * g.sx, g.y - kPad * g.sy, (w + 2 * kPad) * g.sx, (32 + 2 * kPad) * g.sy);
        shader_.set("local", float(-kPad), float(-kPad), w + kPad, float(32 + kPad));
        shader_.set("cell", c.u0, c.v0, c.u1, c.v1);
        shader_.set("rows", g.rows);
        drawQuad();
    }
    gl::BlendEquation(GL_FUNC_ADD);
    gl::Disable(GL_BLEND);
}

}  // namespace paper

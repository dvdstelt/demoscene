// Checker for the 3D engine on the GPU (src/clone/engine*.cpp).
//
//   enginetest [--exe FILE] dumps F1.bin F2.bin ... [--pngdir DIR] [--scale N] [--samples N] [--painter]
//              [--maps dump|smooth|texel] [--nomip]
//       draws dumped from the emulator at the entry of 0x4169f8 (clone/re/models/engine/run_engine.py, exported by
//       export_dumps.py): the geosphere generator against the int16 geosphere in memory (parts 7, 8), the matrix
//       routines (Mat34::part of the part's angles applied to the source vertices) against the transformed vertices
//       (parts 9, 10), the vertex normals against 0x416b61's (7, 8), the back-face test, the far cut and the painter's
//       order against the bucket lists; then the frame drawn by cmc::Engine from the dumped camera-space vertices,
//       uv, shades and light over the dumped 'before' page at 320 N x 200 N (--scale, default 4), box-filtered to
//       320 x 200 and compared with the original's 'after' page (mean |difference| in 0..255 per channel over the
//       pixels the original drew and over their interior, 99th percentile, maximum). The map is the original's bytes
//       (--maps dump, default: the engine alone; --nomip without mipmaps, read at points as the original does) or the
//       remake's maps (Log form, smooth or texel style).
//       DIR/dump_<part>_<tick>_c<call>.png: original | engine | difference x 4. --painter: Order::Painter;
//       `--scale 1 --samples 1 --nomip` gives the original's coverage and point samples: the closed form of notes-engine
//       10 evaluated by the GPU.
//   enginetest [--exe FILE] [--art DIR | --original-art] --shots T1,T2,... --shotdir DIR [--shotsize WxH]
//              [--painter] [--style smooth|texel] [--samples N] [--geo LEVEL] [--exact] [--logmips] [--bench N]
//       renders the intro at music times T with the test scenes of scenes.cpp (parts 7..10 ported minimally from the
//       notes and drawn with cmc::Engine) in place of the parts, as DIR/shot_<T>.png like clone-remake --shots:
//       compare with the capture by tools/compare.py. --geo N: the geosphere at level N with exact floats (--exact at
//       level 4); --logmips: Engine::Debug::logMips; --bench N: time N frames at each T instead.
//
// Build (a separate project: the framework, the core library and these files):
//   cmake -S tools/enginetest -B build-enginetest -G Ninja && cmake --build build-enginetest
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "clone/engine.h"
#include "demo.h"
#include "exe.h"
#include "gl.h"
#include "music.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef CLONE_DIR
#define CLONE_DIR "."
#endif

using namespace cmc;

extern Order g_testOrder;     // scenes.cpp
extern TexStyle g_testStyle;
extern int g_geoLevel;
extern bool g_geoExact;
extern bool g_logMips;

namespace {

// ------------------------------------------------------------------------------------------------- dumps
struct Dump {
    int part = 0, tick = 0, call = 0;
    float light = 1, wa = 0, wb = 0;
    int map = -1;
    std::vector<uint32_t> texels;          // 256 x 256, 0x00RRGGBB log levels
    std::vector<Vec3> xf, src, nrm;
    float ang[3] = {0, 0, 0};
    struct F {
        uint32_t v[3];
        int32_t n[3];
        float uv[6];
        int32_t shade[3];
        uint32_t hidden;
        int32_t map;
    };
    std::vector<F> faces;
    std::vector<uint32_t> order;           // faces in the buckets, in draw order
    std::vector<int16_t> geoV, geoF;       // the int16 geosphere (x, y, z), (a, b, c)
    std::vector<uint32_t> before, after;   // 320 x 200

    bool read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t p = 0;
        auto need = [&](size_t n) { if (p + n > d.size()) throw std::runtime_error("short file"); };
        auto raw = [&](void* out, size_t n) { need(n); std::memcpy(out, &d[p], n); p += n; };
        auto u32 = [&]() { uint32_t v; raw(&v, 4); return v; };
        auto i32 = [&]() { int32_t v; raw(&v, 4); return v; };
        auto f32 = [&]() { float v; raw(&v, 4); return v; };
        auto vecs = [&](std::vector<Vec3>& out) {
            uint32_t n = u32();
            out.resize(n);
            for (Vec3& v : out) v.x = f32(), v.y = f32(), v.z = f32();
        };
        try {
            char magic[8];
            raw(magic, 8);
            if (std::memcmp(magic, "CLONENG1", 8) != 0) return false;
            part = i32(), tick = i32(), call = i32();
            light = f32(), wa = f32(), wb = f32();
            map = i32();
            texels.resize(65536);
            raw(texels.data(), 4 * 65536);
            vecs(xf);
            vecs(src);
            for (float& a : ang) a = f32();
            vecs(nrm);
            faces.resize(u32());
            for (F& fc : faces) {
                for (auto& v : fc.v) v = u32();
                for (auto& n : fc.n) n = i32();
                for (auto& u : fc.uv) u = f32();
                for (auto& s : fc.shade) s = i32();
                fc.hidden = u32();
                fc.map = i32();
            }
            order.resize(u32());
            raw(order.data(), 4 * order.size());
            geoV.resize(size_t(u32()) * 3);
            raw(geoV.data(), 2 * geoV.size());
            geoF.resize(size_t(u32()) * 3);
            raw(geoF.data(), 2 * geoF.size());
            before.resize(64000);
            after.resize(64000);
            raw(before.data(), 4 * 64000);
            raw(after.data(), 4 * 64000);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }
};

Vec3 rgb(uint32_t c) { return Vec3(float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)); }

void readTarget(const Target& t, std::vector<float>& out) {
    out.resize(size_t(t.w) * size_t(t.h) * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl::ReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_FLOAT, out.data());
}

// An RGBA8 texture of 0x00RRGGBB dwords (w x h, rows top to bottom).
void dwordTexture(Texture& t, const std::vector<uint32_t>& px, int w, int h, bool linear, bool repeat, bool mip) {
    std::vector<uint8_t> b(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
        b[i * 4] = uint8_t(px[i] >> 16), b[i * 4 + 1] = uint8_t(px[i] >> 8), b[i * 4 + 2] = uint8_t(px[i]);
        b[i * 4 + 3] = 255;
    }
    t.createRaw(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, b.data(), linear, repeat, mip);
}

int g_scale = 4;
std::string g_maps = "dump";
bool g_noMip = false;   // --nomip: the dumped map without mipmaps (bilinear point samples, as the original reads it)

int checkDump(Ctx& c, const std::string& path, const std::string& pngDir, Order order, int samples) {
    Dump d;
    if (!d.read(path)) {
        std::fprintf(stderr, "%s: not an exported dump\n", path.c_str());
        return 1;
    }
    std::printf("part %d tick %d call %d: %zu vertices, %zu faces, light %.4f, window (%g, %g)\n", d.part, d.tick,
                d.call, d.xf.size(), d.faces.size(), d.light, d.wa, d.wb);
    int rc = 0;
    // ---- geosphere (0x4174c4) against the int16 arrays in memory
    if (!d.geoV.empty()) {
        Geosphere g = Geosphere::make(*c.exe, 4), ge = Geosphere::make(*c.exe, 4, true);
        size_t nv = d.geoV.size() / 3, nf = d.geoF.size() / 3, vd = 0, fd = 0;
        double rmin = 1e9, rmax = 0, moved = 0;
        for (size_t i = 0; i < std::min(nv, g.verts.size()); i++) {
            const Vec3& v = g.verts[i];
            if (v.x != d.geoV[i * 3] || v.y != d.geoV[i * 3 + 1] || v.z != d.geoV[i * 3 + 2]) vd++;
            rmin = std::min(rmin, double(v.len())), rmax = std::max(rmax, double(v.len()));
            moved = std::max(moved, double((ge.verts[i] - v).len()));
        }
        for (size_t k = 0; k < std::min(nf, g.faces.size()); k++)
            for (int j = 0; j < 3; j++) fd += g.faces[k][size_t(j)] != uint32_t(d.geoF[k * 3 + size_t(j)]);
        std::printf("  geosphere: %zu / %zu vertices, %zu / %zu faces; %zu vertices and %zu face indices differ; radius "
                    "%.2f..%.2f; the exact one within %.2f\n",
                    g.verts.size(), nv, g.faces.size(), nf, vd, fd, rmin, rmax, moved);
        if (vd || fd || g.verts.size() != nv || g.faces.size() != nf) rc = 1;
    }
    // ---- matrices (parts 9, 10): Mat34::part of the dumped angles applied to the source vertices
    if (d.part == 9 || d.part == 10) {
        Mat34 m;
        if (d.part == 9) {
            float a0 = d.ang[0], a1 = d.ang[1], a2 = d.ang[2];
            m = Mat34::part(float(double(a0) + a1 - a2), float(1.1 * (double(a0) - a1 + a2)),
                            float(0.8 * (double(a1) - a0 + a2)), 4.0f / 3.0f, 180.0f);
        } else {
            float a = d.ang[0];
            m = Mat34::part(a, float(1.1 * a), float(0.8 * a), 4.0f / 3.0f, 150.0f);
        }
        double md = 0;
        for (size_t i = 0; i < d.src.size(); i++) md = std::max(md, double((m.apply(d.src[i]) - d.xf[i]).len()));
        std::printf("  matrix: Mat34::part applied to %zu source vertices: largest distance to the original's %.2g\n",
                    d.src.size(), md);
        if (md > 1e-3) rc = 1;
    }
    // ---- the mesh as a part hands it to the engine
    EngineMesh mesh;
    mesh.pos = d.xf;
    bool normals = !d.faces.empty() && d.faces[0].n[0] >= 0;
    mesh.shade = normals ? Shade::Normals : Shade::Corners;
    for (const Dump::F& f : d.faces) {
        EngineFace e;
        for (int i = 0; i < 3; i++) {
            e.v[i] = f.v[i];
            e.uv[i][0] = f.uv[2 * i];
            e.uv[i][1] = f.uv[2 * i + 1];
            e.shade[i] = float(f.shade[i] / 65536.0);
            if (normals && uint32_t(f.n[i]) != f.v[i]) std::printf("  face normal index != vertex index\n");
        }
        mesh.faces.push_back(e);
    }
    if (normals) {
        std::vector<Vec3> n;
        vertexNormals(mesh.pos, mesh.faces, n);
        double md = 0;
        for (size_t i = 0; i < n.size() && i < d.nrm.size(); i++) md = std::max(md, double((n[i] - d.nrm[i]).len()));
        std::printf("  normals (0x416b61): %zu, largest difference %.2g\n", n.size(), md);
        if (md > 1e-4) rc = 1;
    }
    // ---- 0x416929: back faces, far cut, the painter's order
    {
        int hidDiff = 0, cut = 0, keyMax = -1;
        std::vector<std::pair<int, uint32_t>> drawn;
        for (size_t k = 0; k < mesh.faces.size(); k++) {
            const EngineFace& f = mesh.faces[k];
            FaceSort s = sortFace(mesh.pos[f.v[0]], mesh.pos[f.v[1]], mesh.pos[f.v[2]]);
            hidDiff += int(s.hidden) != int(d.faces[k].hidden);
            if (!s.hidden && s.cut) cut++;
            if (s.drawn()) {
                drawn.push_back({s.key, uint32_t(k)});
                keyMax = std::max(keyMax, s.key);
            }
        }
        std::sort(drawn.begin(), drawn.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second > b.second; });
        bool same = drawn.size() == d.order.size();
        for (size_t i = 0; same && i < drawn.size(); i++) same = drawn[i].second == d.order[i];
        std::printf("  sort (0x416929): hidden flags differ for %d faces; %zu drawn (the original %zu), %d cut by the far "
                    "cut, key max %d; painter's order %s\n",
                    hidDiff, drawn.size(), d.order.size(), cut, keyMax, same ? "identical" : "DIFFERS");
        if (hidDiff || !same) rc = 1;
    }
    if (d.order.empty()) return rc;

    // ---- the frame over the 'before' page
    Texture dumpMap, bg;
    if (g_maps == "dump") {
        dwordTexture(dumpMap, d.texels, 256, 256, true, true, !g_noMip);
        mesh.texture = &dumpMap;
    } else {
        mesh.map = d.map == 0 ? Tex::MapA : d.map == 1 ? Tex::MapB : Tex::MapC;
    }
    dwordTexture(bg, d.before, 320, 200, false, false, false);
    const int S = g_scale, W = 320 * S, H = 200 * S;
    Target t;
    t.create(W, H, false, GL_RGBA16F);
    t.bind();
    setBlend(Blend::None);
    c.drawTexture(bg.id, 0, 0, 320, 200);
    Engine& eng = c.shared<Engine>();
    eng.samples = samples;
    eng.order = order;
    eng.style = g_maps == "texel" ? TexStyle::Texel : TexStyle::Smooth;
    eng.begin(t, Window{d.wa, d.wb});
    eng.draw(mesh, d.light);
    eng.end();
    std::vector<float> px;
    readTarget(t, px);
    t.destroy();
    bg.destroy();
    dumpMap.destroy();
    // box filter to 320 x 200 (GL rows from the bottom), against the original's page
    std::vector<Vec3> mine(64000), ref(64000);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            double s[3] = {0, 0, 0};
            for (int j = 0; j < S; j++)
                for (int i = 0; i < S; i++) {
                    size_t k = (size_t(H - 1 - (y * S + j)) * size_t(W) + size_t(x * S + i)) * 4;
                    for (int ch = 0; ch < 3; ch++) s[ch] += px[k + size_t(ch)];
                }
            double n = double(S * S) / 255.0;
            mine[size_t(y) * 320 + size_t(x)] = Vec3(float(s[0] / n), float(s[1] / n), float(s[2] / n));
            ref[size_t(y) * 320 + size_t(x)] = rgb(d.after[size_t(y) * 320 + size_t(x)]);
        }
    auto drawn = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= 320 || y >= 200) return false;
        size_t k = size_t(y) * 320 + size_t(x);
        return d.after[k] != d.before[k];
    };
    std::vector<double> all, inner;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            if (!drawn(x, y)) continue;
            size_t k = size_t(y) * 320 + size_t(x);
            Vec3 e = mine[k] - ref[k];
            double dd = (std::fabs(e.x) + std::fabs(e.y) + std::fabs(e.z)) / 3.0;
            all.push_back(dd);
            bool in = true;
            for (int j = -1; j <= 1 && in; j++)
                for (int i = -1; i <= 1 && in; i++) in = drawn(x + i, y + j);
            if (in) inner.push_back(dd);
        }
    auto summary = [](std::vector<double>& v, double* mean, double* p99, double* mx) {
        *mean = *p99 = *mx = 0;
        if (v.empty()) return;
        for (double x : v) *mean += x;
        *mean /= double(v.size());
        std::sort(v.begin(), v.end());
        *p99 = v[size_t(double(v.size() - 1) * 0.99)];
        *mx = v.back();
    };
    double m0, p0, x0, m1, p1, x1;
    summary(all, &m0, &p0, &x0);
    summary(inner, &m1, &p1, &x1);
    std::printf("  frame (%s, %s maps, %d x, %d samples): %zu drawn pixels, mean |engine - original| %.2f (99th "
                "percentile %.1f, max %.0f); interior %zu, mean %.2f (99th %.1f, max %.0f)\n",
                order == Order::Painter ? "painter" : "depth buffer", g_maps.c_str(), S, samples, all.size(), m0, p0, x0,
                inner.size(), m1, p1, x1);
    if (!pngDir.empty()) {
        const int Z = 2, PW = 320 * Z;
        std::vector<uint8_t> out(size_t(PW) * 3 * size_t(200 * Z) * 3);
        for (int y = 0; y < 200 * Z; y++)
            for (int x = 0; x < PW; x++) {
                size_t k = size_t(y / Z) * 320 + size_t(x / Z);
                Vec3 a = ref[k], b = mine[k];
                float va[3] = {a.x, a.y, a.z}, vb[3] = {b.x, b.y, b.z};
                for (int ch = 0; ch < 3; ch++) {
                    auto put = [&](int panel, float v) {
                        out[(size_t(y) * size_t(PW * 3) + size_t(panel * PW + x)) * 3 + size_t(ch)] =
                            uint8_t(std::clamp(v, 0.0f, 255.0f) + 0.5f);
                    };
                    put(0, va[ch]);
                    put(1, vb[ch]);
                    put(2, std::fabs(va[ch] - vb[ch]) * 4);
                }
            }
        char name[512];
        std::snprintf(name, sizeof name, "%s/dump_p%d_t%d_c%d%s.png", pngDir.c_str(), d.part, d.tick, d.call,
                      order == Order::Painter ? "_painter" : "");
        stbi_write_png(name, PW * 3, 200 * Z, 3, out.data(), PW * 3 * 3);
    }
    return rc;
}

// ------------------------------------------------------------------------------------------------- shots
std::vector<double> parseList(const std::string& s) {
    std::vector<double> v;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos) q = s.size();
        v.push_back(std::atof(s.substr(p, q - p).c_str()));
        p = q + 1;
    }
    return v;
}

void readFrame(const Target& f, std::vector<uint8_t>& out) {
    std::vector<uint8_t> px(size_t(f.w) * size_t(f.h) * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, f.w, f.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    out.resize(px.size());
    for (int y = 0; y < f.h; y++)
        std::memcpy(&out[size_t(y) * size_t(f.w) * 4], &px[size_t(f.h - 1 - y) * size_t(f.w) * 4], size_t(f.w) * 4);
}

}  // namespace

int main(int argc, char** argv) {
    std::string exePath = std::string(CLONE_DIR) + "/original/CLONE.EXE", artDir = std::string(CLONE_DIR) + "/remake/art";
    std::string shotDir = ".", pngDir, cmd;
    std::vector<std::string> files;
    std::vector<double> shots;
    int W = 960, H = 720, samples = 4, bench = 0;
    bool painter = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--exe") exePath = next();
        else if (a == "--art") artDir = next();
        else if (a == "--original-art") artDir.clear();
        else if (a == "--shots") shots = parseList(next());
        else if (a == "--shotdir") shotDir = next();
        else if (a == "--shotsize") std::sscanf(next().c_str(), "%dx%d", &W, &H);
        else if (a == "--samples") samples = std::atoi(next().c_str());
        else if (a == "--painter") painter = true;
        else if (a == "--pngdir") pngDir = next();
        else if (a == "--bench") bench = std::atoi(next().c_str());
        else if (a == "--scale") g_scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--maps") g_maps = next();
        else if (a == "--nomip") g_noMip = true;
        else if (a == "--style") g_testStyle = next() == "texel" ? TexStyle::Texel : TexStyle::Smooth;
        else if (a == "--geo") g_geoLevel = std::max(0, std::atoi(next().c_str()));
        else if (a == "--exact") g_geoExact = true;
        else if (a == "--logmips") g_logMips = true;
        else if (cmd.empty()) cmd = a;
        else files.push_back(a);
    }
    if (cmd != "dumps" && shots.empty()) {
        std::fprintf(stderr, "usage: enginetest dumps F.bin ... [--pngdir DIR] | --shots T1,T2 --shotdir DIR\n");
        return 1;
    }
    CloneExe exe;
    std::string err;
    if (!exe.load(exePath, &err)) {
        std::fprintf(stderr, "%s: %s\n", exePath.c_str(), err.c_str());
        return 1;
    }
    Soundtrack music = renderSoundtrack(exe.image(), 44100);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("enginetest", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) return 1;
    SDL_GLContext glc = SDL_GL_CreateContext(win);
    if (!glc || !gl::load()) return 1;
    int rc = 0;
    {
        Demo demo;
        Demo::Options o;
        o.art = !artDir.empty();
        o.artDir = artDir;
        if (!demo.init(exe, music, W, H, o, &err)) {
            std::fprintf(stderr, "init: %s\n", err.c_str());
            return 1;
        }
        g_testOrder = painter ? Order::Painter : Order::Depth;
        demo.ctx.shared<Engine>().samples = samples;
        if (cmd == "dumps") {
            for (const std::string& f : files)
                rc |= checkDump(demo.ctx, f, pngDir, painter ? Order::Painter : Order::Depth, samples);
        } else {
            std::vector<uint8_t> px;
            for (double t : shots) {
                if (bench > 0) {
                    demo.render(t);
                    gl::Finish();
                    std::vector<double> ms;
                    for (int i = 0; i < bench; i++) {
                        uint64_t b0 = SDL_GetTicksNS();
                        demo.render(t + i * 1e-4);
                        gl::Finish();
                        ms.push_back(double(SDL_GetTicksNS() - b0) * 1e-6);
                    }
                    std::sort(ms.begin(), ms.end());
                    std::printf("bench %dx%d t %.3f (%s): median %.2f ms, max %.2f ms\n", W, H, t, demo.partAt(t),
                                ms[ms.size() / 2], ms.back());
                    continue;
                }
                readFrame(demo.render(t), px);
                char name[512];
                std::snprintf(name, sizeof name, "%s/shot_%07.3f.png", shotDir.c_str(), t);
                if (!stbi_write_png(name, W, H, 4, px.data(), W * 4)) {
                    rc = 1;
                    break;
                }
                std::printf("wrote %s (%s)\n", name, demo.partAt(t));
            }
        }
        demo.release();
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}

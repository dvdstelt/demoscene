// Checker for the 3D engine on the GPU (src/dope/engine*.cpp).
//
//   enginetest [--exe FILE] model F1.bin F2.bin ... [--pngdir DIR] [--painter] [--samples N]
//       frames of the bit-exact engine model (export_model.py: the records, maps, bump table and DAC of an emulator
//       dump, the model's vertex arrays, face list and render target): the float transform of dope::projectObject
//       against the model's integer screen positions, env coordinates, texture words and depth words; the engine's
//       back-face test (float winding, the 16-bit test where it overflows: EnginePart::cull16, notes-engine 5.4)
//       against the model's face list; then the frame drawn
//       by dope::Engine with the same maps over the same background at 1280 x 800 (--scale 4), box-filtered to 320 x 200 and
//       compared with the model's target (mean colour difference in 0..255 over the 3D pixels and their interior;
//       --scale N: drawn at 320 N x 200 N instead;
//       DIR/model_<part>_<frame>.png: model | engine | difference x 4). --nearest: the maps read nearest as the
//       original reads them; --affine, --single: Engine::Debug (screen-linear interpolation, one bump read per
//       pixel); --truedepth: every object's depth is its distance (default: Object::depthScale = f / the largest
//       f, the original's order between p2's discs and "C"; in the shots: p2's "C" with depthScale 1). `--nearest --affine --single --samples 1 --scale 1` is as close to the fillers as the engine gets:
//       it checks the map addressing (the remaining differences are edges, rounding and the fillers' integer steps).
//   enginetest [--exe FILE] [--art DIR | --original-art] --shots T1,T2,... --shotdir DIR [--shotsize WxH]
//       renders the demo at music times T with the test scenes of scenes.cpp (p2, p3, p5, p6, p7 ported minimally
//       from the notes and drawn with dope::Engine) in place of the parts, as DIR/shot_<T>.png and DIR/shots.txt like
//       dope-remake --shots: compare with the capture by tools/compare.py. --painter: Order::Painter; --bench N:
//       time N frames at each T instead.
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
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "dope/engine.h"
#include "exe.h"
#include "gl.h"
#include "music.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef DOPE_DIR
#define DOPE_DIR "."
#endif

using namespace dope;

extern Order g_testOrder;   // scenes.cpp

namespace {

// ------------------------------------------------------------------------------------------------- model frames
struct ModelFrame {
    int part = 0, frame = 0;
    uint16_t objs = 0, objlast = 0;
    std::vector<std::vector<uint8_t>> recs;
    int envAdd = 0, texAdd = 0;
    uint16_t envSeg[4] = {}, texSeg[4] = {};
    int16_t bump[256] = {};
    uint8_t dac[768] = {};
    std::vector<uint8_t> env[4], tex[4];
    struct V { int16_t sx, sy; uint32_t env, tex; int16_t dep; };
    std::vector<V> verts;
    struct F { uint16_t a, b, c; };
    std::vector<F> faces;
    std::vector<uint16_t> sorted;
    std::vector<uint8_t> before, after;   // 320 x 201

    bool read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t p = 0;
        auto need = [&](size_t n) { if (p + n > d.size()) throw std::runtime_error("short file"); };
        auto u8 = [&]() { need(1); return d[p++]; };
        auto u16 = [&]() { need(2); uint16_t v = uint16_t(d[p] | d[p + 1] << 8); p += 2; return v; };
        auto u32 = [&]() { need(4); uint32_t v = d[p] | d[p + 1] << 8 | d[p + 2] << 16 | uint32_t(d[p + 3]) << 24; p += 4; return v; };
        auto bytes = [&](size_t n) { need(n); std::vector<uint8_t> v(d.begin() + long(p), d.begin() + long(p + n)); p += n; return v; };
        try {
            if (bytes(8) != std::vector<uint8_t>{'D', 'O', 'P', 'E', 'E', 'N', 'G', '1'}) return false;
            part = int(u32()), frame = int(u32()), objs = uint16_t(u32()), objlast = uint16_t(u32());
            uint32_t n = u32();
            for (uint32_t i = 0; i < n; i++) recs.push_back(bytes(0x3a));
            envAdd = u8(), texAdd = u8();
            for (auto& s : envSeg) s = u16();
            for (auto& s : texSeg) s = u16();
            for (auto& b : bump) b = int16_t(u16());
            std::vector<uint8_t> pal = bytes(768);
            std::memcpy(dac, pal.data(), 768);
            for (auto& e : env) e = bytes(128 * 128);
            for (auto& t : tex) t = bytes(128 * 128);
            uint32_t nv = u32();
            for (uint32_t i = 0; i < nv; i++) {
                V v;
                v.sx = int16_t(u16()), v.sy = int16_t(u16()), v.env = u32(), v.tex = u32(), v.dep = int16_t(u16());
                verts.push_back(v);
            }
            uint32_t nf = u32();
            for (uint32_t i = 0; i < nf; i++) {
                F fc;
                fc.a = u16(), fc.b = u16(), fc.c = u16();
                faces.push_back(fc);
            }
            uint32_t ns = u32();
            for (uint32_t i = 0; i < ns; i++) sorted.push_back(u16());
            before = bytes(320 * 201);
            after = bytes(320 * 201);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }
};

// The record bytes as dope::ObjectRecord (the same fields as ObjectRecord::read).
ObjectRecord parseRecord(const std::vector<uint8_t>& r, uint16_t addr) {
    auto w = [&](int o) { return uint16_t(r[size_t(o)] | r[size_t(o) + 1] << 8); };
    auto sw = [&](int o) { return double(int16_t(w(o))); };
    auto sd = [&](int o) { return double(int32_t(uint32_t(w(o)) | uint32_t(w(o + 2)) << 16)); };
    ObjectRecord o;
    o.addr = addr;
    o.seg = w(0);
    o.off = w(2) != 0;
    o.f = int16_t(w(4));
    o.pose = Pose{sd(0x06), sd(0x0a), sd(0x0e), double(w(0x1e)), double(w(0x20)), double(w(0x22))};
    o.vel = Pose{sd(0x12), sd(0x16), sd(0x1a), sw(0x24), sw(0x26), sw(0x28)};
    o.envAdd = w(0x2a) & 0xff, o.texAdd = w(0x2a) >> 8;
    o.cullFlip = w(0x2c) != 0;
    for (int i = 0; i < 3; i++) o.pivot[i] = sd(0x2e + 4 * i);
    return o;
}

struct Stats {
    double maxD = 0, sum = 0;
    long n = 0;
    void add(double d) { maxD = std::max(maxD, std::fabs(d)); sum += std::fabs(d); n++; }
    double mean() const { return n ? sum / double(n) : 0; }
};

void readTarget(const Target& t, std::vector<float>& out) {
    out.resize(size_t(t.w) * size_t(t.h) * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl::ReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_FLOAT, out.data());
}

int g_scale = 4;          // --scale N: the frame drawn at 320 N x 200 N, box-filtered to 320 x 200
bool g_nearest = false;   // --nearest: the maps read nearest, as the original (checks the addressing)
bool g_affine = false, g_single = false;   // --affine, --single: Engine::Debug
}  // namespace
// --truedepth: Object::depthScale = 1 for every object (else f / the largest f; scenes.cpp reads it too)
bool g_keyDepth = true;
namespace {

int checkModel(Ctx& c, const DopeExe& exe, const std::string& path, const std::string& pngDir, Order order, int samples) {
    ModelFrame m;
    if (!m.read(path)) {
        std::fprintf(stderr, "%s: not a model frame\n", path.c_str());
        return 1;
    }
    const DopeImage& img = exe.part(m.part);
    EnginePart part = EnginePart::of(m.part);
    std::map<uint16_t, std::unique_ptr<EngineMesh>> meshes;
    // the drawn objects in list order (354a: slot 0 is the camera; records past objlast, off or without faces skipped)
    Pose cam = parseRecord(m.recs[0], m.objs).pose;
    std::vector<Object> objects;
    for (size_t i = 1; i < m.recs.size(); i++) {
        uint16_t addr = uint16_t(m.objs + 0x3a * i);
        ObjectRecord r = parseRecord(m.recs[i], addr);
        if (r.seg == 0xffff || addr > m.objlast || r.off) continue;
        auto& mesh = meshes[r.seg];
        if (!mesh) {
            mesh = std::make_unique<EngineMesh>();
            if (!mesh->load(img, r.seg)) {
                std::fprintf(stderr, "p%d: no mesh at %04x\n", m.part, r.seg);
                return 1;
            }
        }
        if (mesh->faces.empty()) continue;
        objects.push_back(Object::from(r, mesh.get()));
    }
    // the depth compared as the original's key (local depth times f) relative to the largest f: p2's "C"
    double fmax = 0;
    for (const Object& o : objects) fmax = std::max(fmax, o.f);
    if (g_keyDepth)
        for (Object& o : objects) o.depthScale = o.f / fmax;
    // ---- vertices
    Stats scr, env, tex, dep;
    std::vector<ProjectedVertex> all, pv;
    for (const Object& o : objects) {
        projectObject(part, cam, o, pv);
        all.insert(all.end(), pv.begin(), pv.end());
    }
    if (all.size() != m.verts.size()) {
        std::fprintf(stderr, "p%d frame %d: %zu vertices, the model has %zu\n", m.part, m.frame, all.size(), m.verts.size());
        return 1;
    }
    size_t g = 0;
    for (const Object& o : objects)
        for (size_t i = 0; i < o.mesh->verts.size(); i++, g++) {
            const ProjectedVertex& q = all[g];
            const ModelFrame::V& v = m.verts[g];
            if (std::fabs(q.x - 160) < 400 && std::fabs(q.y - 100) < 300) {   // near the screen
                scr.add(q.x - v.sx);
                scr.add(q.y - (v.sy - 1));
            }
            env.add(q.eu - double(v.env >> 16) / 256.0);
            env.add(q.ev - double(v.env & 0xffff) / 256.0);
            auto wrap = [](double d) { return std::fmod(std::fmod(d, 65536.0) + 98304.0, 65536.0) - 32768.0; };
            tex.add(wrap(q.thi - double(v.tex >> 16)));
            tex.add(wrap(q.tlo - double(v.tex & 0xffff)));
            dep.add(q.vz * o.f / 32768.0 + 16384.0 - v.dep);
        }
    std::printf("p%d frame %d: %zu vertices: screen |float - model| max %.2f mean %.3f px; env max %.3f mean %.3f "
                "texels; texture words max %.1f; depth words max %.2f\n",
                m.part, m.frame, all.size(), scr.maxD, scr.mean(), env.maxD, env.mean(), tex.maxD, dep.maxD);
    // ---- faces: float winding against the model's list (3377), and the 16-bit test
    std::set<std::array<uint32_t, 3>> kept;
    std::map<std::array<uint32_t, 3>, std::pair<int, int>> sel;
    for (const ModelFrame::F& f : m.faces) {
        std::array<uint32_t, 3> k{uint32_t(f.a / 4), uint32_t((f.b & ~3) / 4), uint32_t((f.c & ~3) / 4)};
        kept.insert(k);
        sel[k] = {f.c & 3, f.b & 3};
    }
    int both = 0, modelOnly = 0, floatOnly = 0, sixteen = 0, selBad = 0, engine16 = 0;
    double worst = 0;
    g = 0;
    std::vector<FaceCull> fc;
    for (const Object& o : objects) {
        // the engine's 16-bit decisions (EnginePart::cull16) on its own integer positions
        std::vector<ProjectedVertex> pv(all.begin() + std::ptrdiff_t(g),
                                        all.begin() + std::ptrdiff_t(g + o.mesh->verts.size()));
        if (part.cull16) engine16 += overflowFaces(o, pv, fc);
        else fc.assign(o.mesh->faces.size(), FaceCull::Float);
        for (size_t j = 0; j < o.mesh->faces.size(); j++) {
            const EngineMesh::Face& f = o.mesh->faces[j];
            const ProjectedVertex &a = all[g + f.a], &b = all[g + f.b], &cc = all[g + f.c];
            double cr = (b.x - a.x) * (cc.y - a.y) - (cc.x - a.x) * (b.y - a.y);
            bool drawF = fc[j] == FaceCull::Float ? (o.cullFlip ? cr < 0 : cr >= 0) : fc[j] == FaceCull::Force;
            std::array<uint32_t, 3> k{uint32_t(g + f.a), uint32_t(g + f.b), uint32_t(g + f.c)};
            bool drawM = kept.count(k) != 0;
            // the model's 16-bit products on its integer vertices
            const ModelFrame::V &ma = m.verts[k[0]], &mb = m.verts[k[1]], &mc = m.verts[k[2]];
            int16_t p1 = int16_t((mb.sx - ma.sx) * (mc.sy - ma.sy)), p2 = int16_t((mc.sx - ma.sx) * (mb.sy - ma.sy));
            int16_t ax = int16_t(p1 - p2);
            int c32 = (mb.sx - ma.sx) * (mc.sy - ma.sy) - (mc.sx - ma.sx) * (mb.sy - ma.sy);
            if ((ax >= 0) != (c32 >= 0)) sixteen++;
            if (drawF && drawM) both++;
            else if (drawM) modelOnly++, worst = std::max(worst, std::fabs(cr) / 2);
            else if (drawF) floatOnly++, worst = std::max(worst, std::fabs(cr) / 2);
            if (drawM) {
                auto s = sel[k];
                int es = ((f.attr & 0xff) + o.envAdd + m.envAdd) & 3, ts = ((f.attr >> 8) + o.texAdd + m.texAdd) & 3;
                if (s.first != es || s.second != ts) selBad++;
            }
        }
        g += o.mesh->verts.size();
    }
    std::printf("  faces: %d drawn by both, %d only by the model, %d only by the engine's test (largest %.0f px^2); "
                "16-bit products wrong for %d (the engine's 16-bit test decides %d); selectors wrong for %d\n",
                both, modelOnly, floatOnly, worst, sixteen, engine16, selBad);

    // ---- the frame: the same maps, bump table, palette and background through dope::Engine
    Palette pal = vgaPalette(std::span<const uint8_t>(m.dac, 768));
    EnvTexture envT[4];
    BumpMap bumpM[4];
    BumpOffsets offs{};
    for (int t = 0; t < 256; t++) {
        int w = m.bump[t], dx = ((w + 128) & 255) - 128;
        offs[size_t(t)] = {float(dx), float((w - dx) / 256)};
    }
    Materials mat;
    for (int i = 0; i < 4; i++) {
        envT[i].set(m.env[i], 128, 128, 128, pal);
        mat.env[i] = envT[i].map();
        if (m.texSeg[i]) {
            bumpM[i].create(m.tex[i], 128, offs, part);
            mat.tex[i] = &bumpM[i];
        }
    }
    mat.envAdd = m.envAdd, mat.texAdd = m.texAdd;
    if (g_nearest)
        for (int i = 0; i < 4; i++)
            for (GLuint id : {envT[i].tex.id, bumpM[i].tex.id}) {
                if (!id) continue;
                gl::BindTexture(GL_TEXTURE_2D, id);
                gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
    const int S4 = g_scale, W = 320 * S4, H = 200 * S4;
    Target t;
    t.create(W, H, false, GL_RGBA16F);
    // the background: target rows 1..200 = screen rows 0..199
    std::vector<uint8_t> bg(320 * 200 * 4);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            const Vec3& col = pal[m.before[size_t(y + 1) * 320 + size_t(x)]];
            uint8_t* o = &bg[(size_t(y) * 320 + size_t(x)) * 4];
            o[0] = uint8_t(std::lround(col.x * 255)), o[1] = uint8_t(std::lround(col.y * 255)),
            o[2] = uint8_t(std::lround(col.z * 255)), o[3] = 255;
        }
    Texture bgT;
    bgT.createRaw(320, 200, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, bg.data(), false, false, false);
    t.bind();
    setBlend(Blend::None);
    c.drawTexture(bgT.id, 0, 0, 320, 200);
    Engine& eng = c.shared<Engine>();
    eng.samples = samples;
    eng.debug.affine = g_affine;
    eng.debug.singleSample = g_single;
    eng.begin(t, part, cam, View{}, order);
    for (const Object& o : objects) eng.draw(o, mat);
    eng.end();
    std::vector<float> px;
    readTarget(t, px);
    t.destroy();
    bgT.destroy();
    // box filter to 320 x 200 (GL rows from the bottom), compare with the model's target
    std::vector<float> mine(320 * 200 * 3), ref(320 * 200 * 3);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            double s[3] = {0, 0, 0};
            for (int j = 0; j < S4; j++)
                for (int i = 0; i < S4; i++) {
                    size_t k = (size_t(H - 1 - (y * S4 + j)) * W + size_t(x * S4 + i)) * 4;
                    for (int ch = 0; ch < 3; ch++) s[ch] += px[k + size_t(ch)];
                }
            const Vec3& col = pal[m.after[size_t(y + 1) * 320 + size_t(x)]];
            float rc[3] = {col.x, col.y, col.z};
            for (int ch = 0; ch < 3; ch++) {
                mine[(size_t(y) * 320 + size_t(x)) * 3 + size_t(ch)] = float(s[ch] / double(S4 * S4));
                ref[(size_t(y) * 320 + size_t(x)) * 3 + size_t(ch)] = rc[ch];
            }
        }
    auto is3d = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= 320 || y >= 200) return false;
        size_t k = size_t(y + 1) * 320 + size_t(x);
        return m.after[k] != m.before[k];
    };
    Stats all3d, inner;
    long innerBig = 0, innerSame = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            if (!is3d(x, y)) continue;
            double d = 0;
            for (int ch = 0; ch < 3; ch++)
                d += std::fabs(mine[(size_t(y) * 320 + size_t(x)) * 3 + size_t(ch)] - ref[(size_t(y) * 320 + size_t(x)) * 3 + size_t(ch)]);
            d *= 255.0 / 3.0;
            all3d.add(d);
            bool in = true;
            for (int j = -1; j <= 1 && in; j++)
                for (int i = -1; i <= 1 && in; i++) in = is3d(x + i, y + j);
            if (in) {
                inner.add(d);
                if (d > 16) innerBig++;
                if (d < 1.5) innerSame++;
            }
        }
    std::printf("  frame (%s): %ld 3D pixels, mean |engine - model| %.1f / 255; interior %ld pixels, mean %.1f, "
                "%.1f %% above 16, %.1f %% the same colour\n",
                order == Order::Painter ? "painter" : "depth buffer", all3d.n, all3d.mean(), inner.n, inner.mean(),
                inner.n ? 100.0 * double(innerBig) / double(inner.n) : 0.0,
                inner.n ? 100.0 * double(innerSame) / double(inner.n) : 0.0);
    if (!pngDir.empty()) {
        const int S = 2, PW = 320 * S;
        std::vector<uint8_t> out(size_t(PW) * 3 * size_t(200 * S) * 3);
        for (int y = 0; y < 200 * S; y++)
            for (int x = 0; x < 320 * S; x++) {
                size_t k = (size_t(y / S) * 320 + size_t(x / S)) * 3;
                for (int ch = 0; ch < 3; ch++) {
                    float a = ref[k + size_t(ch)], b = mine[k + size_t(ch)];
                    auto put = [&](int panel, float v) {
                        out[(size_t(y) * size_t(PW * 3) + size_t(panel * PW + x)) * 3 + size_t(ch)] =
                            uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                    };
                    put(0, a);
                    put(1, b);
                    put(2, std::fabs(a - b) * 4);
                }
            }
        char name[512];
        std::snprintf(name, sizeof name, "%s/model_p%d_%05d%s%s.png", pngDir.c_str(), m.part, m.frame,
                      order == Order::Painter ? "_painter" : "", g_nearest ? "_nearest" : "");
        stbi_write_png(name, PW * 3, 200 * S, 3, out.data(), PW * 3 * 3);
    }
    return 0;
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
    std::string exePath = std::string(DOPE_DIR) + "/original/DOPE.EXE", artDir = std::string(DOPE_DIR) + "/remake/art";
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
        else if (a == "--nearest") g_nearest = true;
        else if (a == "--affine") g_affine = true;
        else if (a == "--single") g_single = true;
        else if (a == "--truedepth") g_keyDepth = false;
        else if (a == "--scale") g_scale = std::max(1, std::atoi(next().c_str()));
        else if (cmd.empty()) cmd = a;
        else files.push_back(a);
    }
    DopeExe exe;
    std::string err;
    if (!exe.load(exePath, "", &err)) {
        std::fprintf(stderr, "%s: %s\n", exePath.c_str(), err.c_str());
        return 1;
    }
    if (cmd != "model" && shots.empty()) {
        std::fprintf(stderr, "usage: enginetest model F.bin ... [--pngdir DIR] | --shots T1,T2 --shotdir DIR\n");
        return 1;
    }
    Soundtrack music = renderSoundtrack(exe.mod(), exe.loader().all(), 44100);
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
        if (cmd == "model") {
            for (const std::string& f : files)
                rc |= checkModel(demo.ctx, exe, f, pngDir, painter ? Order::Painter : Order::Depth, samples);
        } else {
            std::vector<uint8_t> px;
            FILE* list = bench ? nullptr : std::fopen((shotDir + "/shots.txt").c_str(), "a");
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
                if (list) std::fprintf(list, "%.3f %d %s\n", t, demo.ctx.captureFrame(t), *demo.partAt(t) ? demo.partAt(t) : "-");
                std::printf("wrote %s (%s)\n", name, demo.partAt(t));
            }
            if (list) std::fclose(list);
        }
        demo.release();
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}

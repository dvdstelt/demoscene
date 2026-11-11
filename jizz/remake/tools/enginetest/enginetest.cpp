// Check tool for the vector engine (src/jizz/engine*.cpp, a port of JIZZ.EXE 0x14394..0x1f641).
//
// Works on "case" files: one render of the original engine, exported from a render dump of the emulator
// (jizz/re/models/engine/run_engine.py --dump-render) with the scene number, engine time, window, colour-table
// parameters, the page before and after the render, the 17 texture buffers and the engine's vertex / face arrays
// (format: see loadCase; export_case.py next to this file writes them). Case files are generated from the original
// and are not part of the repository.
//
//   enginetest --exe JIZZ.EXE geometry CASE...       detail-1 objects against the original's buffers (ulps)
//   enginetest --exe JIZZ.EXE render CASE OUT.png [--size WxH] [--detail K] [--painter] [--dropnear]
//              [--grid] [--dx X --dy Y] [--samples N] [--uvnormal] [--original]
//                                                    render the case's frame; at 320x200 also the difference
//                                                    to the page the original drew
//   enginetest --exe JIZZ.EXE order CASE OUT.png [--size WxH] [--detail K] [--t T]
//                                                    depth buffer vs the original's painter's order + near drop
//   enginetest --exe JIZZ.EXE near CASE T0 T1        CPU scan of engine times T0..T1: faces the original drops at
//                                                    the near limit while a corner is on screen
//   enginetest --exe JIZZ.EXE bench CASE [--size WxH] [--detail K] [--frames N]   GPU time of the engine pass
//
// --grid places the projection centre where the original's rasteriser shows it: the truncated window centre
// (W >> 1, H >> 1) moved by (+0.5, +0.5) pixels: pixel i of a span shows the texture at x = i and row j the scene at
// y = j (the span's coverage is taken at x = i + 1, so the edges are 0.5 pixels left of that). The remake uses
// the exact window centre; the difference is under half an original pixel.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "exe.h"
#include "gl.h"
#include "jizz/engine.h"
#include "render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace jizz;

namespace {

struct Case {
    uint32_t scene = 0, W = 320, H = 200, wx = 0, wy = 0, wobble = 0;
    float t = 0, focal = 0;
    int E = 0, S = 256, F = 0;
    float mat[13] = {};
    int tick = 0, capture = -1;
    std::vector<uint8_t> pre, post, tex[17];
    struct Obj { uint32_t f0, nf, v0, nv, flags; };
    struct V { float p[3], n[3], u, v; };
    struct Fc { uint32_t a, b, c; uint8_t fc, type, tex, env; float uv[6]; };
    std::vector<Obj> objs;
    std::vector<V> verts;
    std::vector<Fc> faces;
};

bool loadCase(const char* path, Case& c) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    auto rd = [&](void* p, size_t n) { return std::fread(p, 1, n, f) == n; };
    char magic[4];
    uint32_t ver = 0, n = 0;
    bool ok = rd(magic, 4) && !std::memcmp(magic, "JZEC", 4) && rd(&ver, 4) && ver == 1;
    uint32_t hdr[6];
    ok = ok && rd(hdr, sizeof hdr) && rd(&c.t, 4) && rd(&c.focal, 4) && rd(&c.E, 4) && rd(&c.S, 4) && rd(&c.F, 4) &&
         rd(c.mat, sizeof c.mat) && rd(&c.tick, 4) && rd(&c.capture, 4);
    c.scene = hdr[0]; c.W = hdr[1]; c.H = hdr[2]; c.wx = hdr[3]; c.wy = hdr[4]; c.wobble = hdr[5];
    c.pre.resize(128000);
    c.post.resize(128000);
    ok = ok && rd(c.pre.data(), 128000) && rd(c.post.data(), 128000);
    for (auto& t : c.tex) {
        t.resize(0x30000);
        ok = ok && rd(t.data(), t.size());
    }
    ok = ok && rd(&n, 4);
    c.objs.resize(n);
    ok = ok && rd(c.objs.data(), n * sizeof(Case::Obj)) && rd(&n, 4);
    c.verts.resize(n);
    ok = ok && rd(c.verts.data(), n * sizeof(Case::V)) && rd(&n, 4);
    c.faces.resize(n);
    for (auto& fc : c.faces) ok = ok && rd(&fc.a, 12) && rd(&fc.fc, 4) && rd(fc.uv, 24);
    std::fclose(f);
    return ok;
}

int ulps(float a, float b) {
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if ((ia < 0) != (ib < 0)) return a == b ? 0 : 1 << 30;
    return std::abs(ia - ib);
}

// ---------------------------------------------------------------- geometry check
int checkGeometry(const JizzExe& exe, const Case& c) {
    EngineGeometry g = buildEngineGeometry(exe, int(c.scene), 1);
    std::printf("scene %u: %zu objects %zu vertices %zu faces (time scale %.9g, F %g)\n", c.scene, g.objects.size(),
                g.vertices.size(), g.faces.size(), g.timeScale, g.focal);
    int bad = 0;
    for (size_t o = 0; o < g.objects.size() && o < c.objs.size(); o++) {
        const EngineObject& m = g.objects[o];
        const Case::Obj& d = c.objs[o];
        bool hdr = m.firstFace == d.f0 && m.faceCount == d.nf && m.firstVertex == d.v0 && m.vertexCount == d.nv;
        int wp = 0, wn = 0, wuv = 0, wtab = 0, fbad = 0, seams = 0;
        for (uint32_t k = 0; hdr && k < m.vertexCount; k++) {
            const EngineVertex& a = g.vertices[m.firstVertex + k];
            const Case::V& b = c.verts[m.firstVertex + k];
            for (int i = 0; i < 3; i++) {
                wp = std::max(wp, ulps(a.p[i], b.p[i]));
                wn = std::max(wn, ulps(a.n[i] * 8388608.0f, b.n[i]));
            }
            wuv = std::max({wuv, ulps(a.u * 65536.0f, b.u), ulps(a.v * 65536.0f, b.v)});
        }
        for (uint32_t k = 0; hdr && k < m.faceCount; k++) {
            const EngineFace& a = g.faces[m.firstFace + k];
            const Case::Fc& b = c.faces[m.firstFace + k];
            if (a.a != b.a || a.b != b.b || a.c != b.c || a.type != b.type || a.tex != b.tex || a.ownUV != bool(b.fc & 0x80))
                fbad++;
            if (a.ownUV) {
                seams++;
                for (int i = 0; i < 6; i++) wtab = std::max(wtab, ulps(a.uv[i] * 65536.0f, b.uv[i]));
            }
        }
        std::printf("  object %zu: %s, %u vertices: worst ulps position %d normal %d uv %d; %u faces: %d differ; "
                    "%d seam faces, worst ulps %d\n", o, hdr ? "header equal" : "HEADER DIFFERS", m.vertexCount, wp, wn,
                    wuv, m.faceCount, fbad, seams, wtab);
        bad += !hdr || wp || wn || wuv || fbad || wtab;
    }
    return bad;
}

// ---------------------------------------------------------------- rendering
struct Gl {
    SDL_Window* win = nullptr;
    bool init() {
        if (!SDL_Init(SDL_INIT_VIDEO)) return false;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        win = SDL_CreateWindow("enginetest", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        return win && SDL_GL_CreateContext(win) && gl::load();
    }
};

const char* kBgFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uTex;
void main() { o = vec4(texture(uTex, vec2(uv.x, 1.0 - uv.y)).rgb, 1.0); }
)";

void rgb565(const uint8_t* page, int i, uint8_t* o) {
    uint16_t p = uint16_t(page[2 * i] | page[2 * i + 1] << 8);
    o[0] = uint8_t(((p >> 11) & 31) * 255 / 31);
    o[1] = uint8_t(((p >> 5) & 63) * 255 / 63);
    o[2] = uint8_t((p & 31) * 255 / 31);
}

struct Renderer {
    const JizzExe& exe;
    const Case& c;
    Texture tex[17], bg;
    Shader bgShader;
    Engine eng;
    EngineScene scene;
    EngineMesh blob;
    bool hasBlob = false;
    int detail = 1;

    Renderer(const JizzExe& e, const Case& cs, int det, bool uvNormal) : exe(e), c(cs), detail(det) {
        for (int s = 0; s < 17; s++) {
            Image im;
            im.w = im.h = 256;
            im.rgba.resize(256 * 256 * 4);
            for (int i = 0; i < 65536; i++) {
                for (int k = 0; k < 3; k++) im.rgba[size_t(i) * 4 + size_t(k)] = c.tex[s][size_t(i) * 3 + size_t(k)];
                im.rgba[size_t(i) * 4 + 3] = 255;
            }
            tex[s].create(im, true, true);
        }
        std::vector<uint8_t> px(320 * 200 * 4);
        for (int i = 0; i < 64000; i++) {
            rgb565(c.pre.data(), i, &px[size_t(i) * 4]);
            px[size_t(i) * 4 + 3] = 255;
        }
        bg.createRaw(320, 200, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, px.data(), false, false, false);
        bgShader.build(kFullscreenVS, kBgFS, "background");
        scene.build(exe, int(c.scene), detail);
        for (size_t o = 0; o < c.objs.size() && o < scene.geometry().objects.size(); o++)
            scene.setVisible(int(o), !(c.objs[o].flags & 0x40));
        // objects beyond the script's: the metaball mesh the part wrote (notes-parts3d-b 5.4)
        for (size_t o = scene.geometry().objects.size(); o < c.objs.size(); o++) {
            const Case::Obj& ob = c.objs[o];
            std::vector<float> pos, nrm, uv;
            std::vector<uint32_t> idx;
            for (uint32_t k = 0; k < ob.nv; k++) {
                const Case::V& v = c.verts[ob.v0 + k];
                pos.insert(pos.end(), v.p, v.p + 3);
                nrm.insert(nrm.end(), v.n, v.n + 3);
                uv.push_back(v.u / 65536.0f);
                uv.push_back(v.v / 65536.0f);
            }
            for (uint32_t k = 0; k < ob.nf; k++) {
                const Case::Fc& f = c.faces[ob.f0 + k];
                idx.insert(idx.end(), {f.a - ob.v0, f.b - ob.v0, f.c - ob.v0});
                blob.texSlot = f.tex;
            }
            blob.uvFromNormal = uvNormal;
            blob.upload(pos.data(), nrm.data(), uv.data(), ob.nv, idx.data(), idx.size());
            hasBlob = true;
        }
    }

    EngineCamera camera() const { return scene.path().at(double(c.t), c.wobble && c.scene != 4); }

    EngineViewport viewport(bool grid) const {
        EngineViewport vp = EngineViewport::rect(float(c.wx), float(c.wy), float(c.W), float(c.H));
        if (grid) {
            vp.cx = float(c.wx + (c.W >> 1)) + 0.5f;
            vp.cy = float(c.wy + (c.H >> 1)) + 0.5f;
        }
        return vp;
    }

    void draw(Target& out, const EngineCamera& cam, const EngineViewport& vp) {
        out.bind();
        gl::Disable(GL_BLEND);
        bgShader.use();
        bg.bind(0);
        bgShader.set("uTex", 0);
        drawFullscreen();
        EngineShading sh;
        for (int s = 0; s < 17; s++) sh.tex[size_t(s) + 1] = tex[s].id;
        sh.E = float(c.E);
        sh.S = float(c.S);
        sh.F = float(c.F);
        eng.begin(out, cam, vp, sh);
        eng.draw(scene);
        if (hasBlob) eng.draw(blob);
        eng.end();
    }
};

std::vector<uint8_t> readTarget(const Target& t) {
    std::vector<uint8_t> px(size_t(t.w) * size_t(t.h) * 4), out(px.size());
    gl::BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < t.h; y++)
        std::memcpy(&out[size_t(y) * size_t(t.w) * 4], &px[size_t(t.h - 1 - y) * size_t(t.w) * 4], size_t(t.w) * 4);
    return out;
}

// camera against the matrix 0x154bc wrote into the objects (rows, translation M * -(eye + F dir), F)
void checkCamera(const Case& c, const EngineCamera& cam) {
    const Vec3 rows[3] = {cam.right, cam.up, cam.fwd};
    float wm = 0, wt = 0;
    for (int r = 0; r < 3; r++) {
        wm = std::max({wm, std::fabs(rows[r].x - c.mat[3 * r]), std::fabs(rows[r].y - c.mat[3 * r + 1]),
                       std::fabs(rows[r].z - c.mat[3 * r + 2])});
        Vec3 T = cam.eye + cam.fwd * cam.focal;
        wt = std::max(wt, std::fabs(-rows[r].dot(T) - c.mat[9 + r]));
    }
    std::printf("camera t %.5f: matrix max |diff| %.2g, translation max |diff| %.3g (of up to %.0f), F %g / %g\n",
                double(c.t), double(wm), double(wt), double(std::max({std::fabs(c.mat[9]), std::fabs(c.mat[10]), std::fabs(c.mat[11])})),
                double(cam.focal), double(c.mat[12]));
}

// mean absolute difference (0..255) to the original's page inside the window, as compare.py expands 5:6:5
void diffToOriginal(const Case& c, const std::vector<uint8_t>& px) {
    double sum = 0, sumAll = 0;
    long n = 0, big = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            uint8_t o[3];
            rgb565(c.post.data(), y * 320 + x, o);
            double d = 0;
            for (int k = 0; k < 3; k++) d += std::abs(int(px[size_t(y * 320 + x) * 4 + size_t(k)]) - int(o[k]));
            sumAll += d / 3;
            if (x >= int(c.wx) && x < int(c.wx + c.W) && y >= int(c.wy) && y < int(c.wy + c.H)) {
                sum += d / 3;
                n++;
                big += d / 3 > 32;
            }
        }
    std::printf("vs the original's page: window mean |diff| %.2f / 255 (%.1f%% of pixels > 32), whole page %.2f\n",
                sum / double(n), 100.0 * double(big) / double(n), sumAll / 64000.0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string exePath, cmd;
    std::vector<std::string> pos;
    int W = 320, H = 200, detail = 1, samples = 4, frames = 120;
    bool painter = false, dropNear = false, grid = false, uvNormal = false;
    float dx = 0, dy = 0;
    bool original = false;
    double tOverride = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--exe") exePath = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &W, &H);
        else if (a == "--detail") detail = std::atoi(next().c_str());
        else if (a == "--samples") samples = std::atoi(next().c_str());
        else if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--painter") painter = true;
        else if (a == "--dropnear") dropNear = true;
        else if (a == "--grid") grid = true;
        else if (a == "--dx") dx = float(std::atof(next().c_str()));
        else if (a == "--dy") dy = float(std::atof(next().c_str()));
        else if (a == "--uvnormal") uvNormal = true;
        else if (a == "--original") original = true;
        else if (a == "--t") tOverride = std::atof(next().c_str());
        else if (cmd.empty()) cmd = a;
        else pos.push_back(a);
    }
    JizzExe exe;
    std::string err;
    if (exePath.empty() || !exe.load(exePath, &err)) {
        std::fprintf(stderr, "enginetest: --exe JIZZ.EXE required (%s)\n", err.c_str());
        return 1;
    }
    if (cmd == "geometry") {
        int bad = 0;
        for (const auto& p : pos) {
            Case c;
            if (!loadCase(p.c_str(), c)) { std::fprintf(stderr, "cannot read %s\n", p.c_str()); return 1; }
            bad += checkGeometry(exe, c);
        }
        return bad ? 2 : 0;
    }
    if (pos.empty()) { std::fprintf(stderr, "usage: see the top of enginetest.cpp\n"); return 1; }
    Case c;
    if (!loadCase(pos[0].c_str(), c)) { std::fprintf(stderr, "cannot read %s\n", pos[0].c_str()); return 1; }
    if (cmd == "near") {  // CPU scan: faces the original drops (a corner at z' < 16.5) while a corner is on screen
        EngineCameraPath path;
        path.load(exe, int(c.scene));
        EngineGeometry g = buildEngineGeometry(exe, int(c.scene), 1);
        double t0 = pos.size() > 1 ? std::atof(pos[1].c_str()) : 0, t1 = pos.size() > 2 ? std::atof(pos[2].c_str()) : 20;
        int frames = 0, hit = 0;
        double first = -1, last = -1;
        size_t worst = 0;
        EngineViewport vpf = EngineViewport::full();
        for (double t = t0; t <= t1; t += 0.002, frames++) {
            EngineCamera cm = path.at(t, c.wobble && c.scene != 4);
            size_t n = 0;
            for (size_t o = 0; o < g.objects.size(); o++) {
                if (o < c.objs.size() && (c.objs[o].flags & 0x40)) continue;
                const EngineObject& ob = g.objects[o];
                for (uint32_t k = 0; k < ob.faceCount; k++) {
                    const EngineFace& f = g.faces[ob.firstFace + k];
                    bool nearC = false, vis = false;
                    for (uint32_t vi : {f.a, f.b, f.c}) {
                        const float* p = g.vertices[vi].p;
                        Vec3 w(p[0], p[1], p[2]);
                        float z = cm.toCamera(w).z, x, y;
                        nearC |= z < 16.5f;
                        if (z >= 16.5f && cm.toVGA(w, vpf, &x, &y)) vis |= x >= 0 && x < 320 && y >= 0 && y < 200;
                    }
                    n += nearC && vis;
                }
            }
            if (n && (last < 0 || t - last > 0.003)) std::printf("  from t %.3f", t);
            if (!n && last >= 0 && t - last < 0.003) std::printf(" to %.3f\n", last);
            if (n) {
                hit++;
                if (first < 0) first = t;
                last = t;
                worst = std::max(worst, n);
            }
        }
        std::printf("scene %u, t %.2f..%.2f (%d camera positions, step 0.002): %d with near-dropped faces that have a "
                    "corner on screen (first t %.3f, last t %.3f, at most %zu faces)\n", c.scene, t0, t1, frames, hit,
                    first, last, worst);
        return 0;
    }
    Gl glw;
    if (!glw.init()) { std::fprintf(stderr, "no OpenGL 3.3: %s\n", SDL_GetError()); return 1; }
    Renderer r(exe, c, detail, uvNormal);
    r.eng.samples = samples;
    Target out;
    out.create(W, H, false, GL_RGBA16F);
    EngineCamera cam = tOverride >= 0 ? r.scene.path().at(tOverride, c.wobble && c.scene != 4) : r.camera();
    EngineViewport vp = r.viewport(grid);
    vp.cx += dx;
    vp.cy += dy;

    if (cmd == "render") {
        checkCamera(c, cam);
        r.eng.debug.painter = painter;
        r.eng.debug.dropNear = dropNear;
        r.eng.debug.original = original;
        r.draw(out, cam, vp);
        auto px = readTarget(out);
        if (pos.size() > 1) stbi_write_png(pos[1].c_str(), W, H, 4, px.data(), W * 4);
        if (W == 320 && H == 200) diffToOriginal(c, px);
        return 0;
    }
    if (cmd == "order") {  // depth buffer + near clipping vs the original's painter's order + near drop
        r.draw(out, cam, vp);
        auto a = readTarget(out);
        r.eng.debug.painter = r.eng.debug.dropNear = true;
        r.draw(out, cam, vp);
        auto b = readTarget(out);
        r.eng.debug.painter = false;  // near drop alone
        r.draw(out, cam, vp);
        auto d = readTarget(out);
        long nOrder = 0, nNear = 0;
        std::vector<uint8_t> vis(a.size());
        for (size_t i = 0; i < a.size(); i += 4) {
            int m = 0, mn = 0;
            for (int k = 0; k < 3; k++) {
                m = std::max(m, std::abs(int(a[i + size_t(k)]) - int(b[i + size_t(k)])));
                mn = std::max(mn, std::abs(int(a[i + size_t(k)]) - int(d[i + size_t(k)])));
            }
            nOrder += m > 24;
            nNear += mn > 24;
            for (int k = 0; k < 3; k++) vis[i + size_t(k)] = uint8_t(a[i + size_t(k)] / 3);
            if (m > 24) { vis[i] = 255; vis[i + 1] = 0; vis[i + 2] = 255; }
            vis[i + 3] = 255;
        }
        std::printf("painter's order + near drop vs depth buffer: %ld of %d pixels differ by > 24/255 (%.3f%%); "
                    "near drop alone: %ld\n", nOrder, W * H, 100.0 * double(nOrder) / (double(W) * H), nNear);
        if (pos.size() > 1) stbi_write_png(pos[1].c_str(), W, H, 4, vis.data(), W * 4);
        return 0;
    }
    if (cmd == "bench") {
        using GetString = const unsigned char*(GLAPIENTRY*)(GLenum);
        if (auto gs = (GetString)SDL_GL_GetProcAddress("glGetString")) std::printf("GL renderer: %s\n", (const char*)gs(0x1F01));
        uint64_t b0 = SDL_GetTicksNS();
        for (int s = 0; s < 5; s++) {
            EngineScene sc;
            sc.build(exe, s, detail);
        }
        std::printf("building all five scenes at detail %d: %.1f ms\n", detail, double(SDL_GetTicksNS() - b0) / 1e6);
        r.draw(out, cam, vp);
        gl::Finish();
        uint64_t t0 = SDL_GetTicksNS();
        for (int f = 0; f < frames; f++) {
            EngineCamera cm = r.scene.path().at(double(c.t) + f * 0.02, c.wobble && c.scene != 4);
            r.draw(out, cm, vp);
        }
        gl::Finish();
        double ms = double(SDL_GetTicksNS() - t0) / 1e6 / frames;
        std::printf("scene %u detail %d (%zu faces) at %dx%d, %d samples: %.3f ms per frame (background + engine)\n",
                    c.scene, detail, r.scene.geometry().faces.size(), W, H, samples, ms);
        return 0;
    }
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 1;
}

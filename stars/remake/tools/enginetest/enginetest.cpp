// Checker for BARTi's engine on the GPU (src/stars/engine*.cpp).
//
//   enginetest [--exe FILE] transform
//       the float transform, projection and env coordinates of stars::Engine against an integer port of 0x7984e
//       (matrices with the 1023-amplitude COS table 0x5599c), 0x79d25 (object x camera), 0x79b16 (projection with
//       truncating idiv) and 0x79a17 / 0x4d4b4 (env coordinates), for every vertex of files 18, 24 and 28 at a range
//       of poses: prints the largest and mean differences in 320x200 pixels (vertices on the screen) and env texels
//       (the original truncates both: about 0.5 on average).
//   enginetest [--exe FILE] [--art DIR | --original-art] --shots T1,T2,... --shotdir DIR [--shotsize WxH]
//       renders the demo at music times T with test scenes for the spike ball, the textured wall and the bee
//       (scenes.cpp: their motion ported from the notes, drawn with stars::Engine) in place of the parts, as
//       DIR/shot_<T>.png like stars-remake --shots; compare with the capture by tools/compare.py. Also the star and
//       the torso (modes 2, 20, 21 and the reflection map). --sharp: texels as squares; --bench N: time N frames
//       at each T instead; --dropnear / --nocull / --affine / --pixelenv: the Engine::Debug switches; --samples N:
//       MSAA.
//
// Build (a separate project: the framework, the core library and these files):
//   cmake -S tools/enginetest -B build-enginetest -G Ninja && cmake --build build-enginetest
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "gl.h"
#include "music.h"
#include "stars/engine.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef STARS_DIR
#define STARS_DIR "."
#endif

using namespace stars;

extern bool g_sharpTexels;   // scenes.cpp

namespace {

// ------------------------------------------------------------------------------------------------- integer port
struct IntEngine {
    const StarsExe& exe;
    int cosT(int a) const { return exe.s16(0x5599c + 2 * uint32_t(a & 0xfff)); }
    int sinT(int a) const { return cosT(a - 0x400); }
    static int32_t mul10(int64_t a, int64_t b) { return int32_t((a * b) >> 10); }
    struct M { int32_t m[4][4] = {}; };
    M build(const int32_t pos[3], const int ang[3]) const {  // 0x7984e
        int ca = cosT(ang[0]), sa = sinT(ang[0]), cb = cosT(ang[1]), sb = sinT(ang[1]), cc = cosT(ang[2]),
            sc = sinT(ang[2]);
        M r;
        r.m[0][0] = mul10(cc, cb);
        r.m[0][1] = mul10(-sc, cb);
        r.m[0][2] = sb;
        r.m[1][0] = mul10(sc, ca) + mul10(mul10(sb, cc), sa);
        r.m[1][1] = mul10(cc, ca) - mul10(mul10(sa, sb), sc);
        r.m[1][2] = mul10(-sa, cb);
        r.m[2][0] = mul10(sc, sa) - mul10(mul10(sb, cc), ca);
        r.m[2][1] = mul10(cc, sa) + mul10(mul10(ca, sb), sc);
        r.m[2][2] = mul10(ca, cb);
        for (int k = 0; k < 3; k++) r.m[3][k] = pos[k];
        return r;
    }
    static M mul(const M& a, const M& b) {  // 0x79d25
        M r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                int32_t s = 0;
                for (int k = 0; k < 4; k++) s += mul10(a.m[i][k], b.m[k][j]);
                r.m[i][j] = s;
            }
        return r;
    }
};

struct Stats {
    double maxD = 0, sum = 0;
    long n = 0;
    void add(double d) { maxD = std::max(maxD, std::fabs(d)); sum += std::fabs(d); n++; }
    double mean() const { return n ? sum / double(n) : 0; }
};

// One object at one pose, in a scene (camera) or drawn directly; accumulates the differences.
void compareObject(const StarsExe& exe, const std::vector<uint8_t>& file, size_t off, const Pose& pose,
                   const Camera& cam, bool scene, Stats& px, Stats& env) {
    IntEngine ie{exe};
    Object3D o;
    if (!o.parse(std::span<const uint8_t>(file).subspan(off))) return;
    // integer: M (0x78a35), normal matrix from object + env angles
    int ang[3] = {int(pose.a), int(pose.b), int(pose.c)};
    int nang[3] = {int(pose.a + cam.envA), int(pose.b + cam.envB), int(pose.c + cam.envC)};
    int32_t zero[3] = {0, 0, 0};
    IntEngine::M M;
    if (scene) {
        int cang[3] = {int(cam.pose.a), int(cam.pose.b), int(cam.pose.c)};
        IntEngine::M C = ie.build(zero, cang);  // 0x79483: row 3 cleared
        int32_t rel[3] = {int32_t(int32_t(pose.x - cam.pose.x) << 10), int32_t(int32_t(pose.y - cam.pose.y) << 10),
                          int32_t(int32_t(pose.z - cam.pose.z) << 10)};
        M = IntEngine::mul(ie.build(rel, ang), C);
    } else {
        int32_t pos[3] = {int32_t(pose.x * 1024), int32_t(pose.y * 1024), int32_t(pose.z * 1024)};
        M = ie.build(pos, ang);
    }
    IntEngine::M N = ie.build(zero, nang);
    // float, as stars::Engine
    Mat3d R = engineRotation(pose.a, pose.b, pose.c), Rn = engineRotation(nang[0], nang[1], nang[2]);
    Vec3 T(float(pose.x), float(pose.y), float(pose.z));
    if (scene) {
        Mat3d Rc = engineRotation(cam.pose.a, cam.pose.b, cam.pose.c);
        R = R * Rc;
        T = Rc.apply(Vec3(float(pose.x - cam.pose.x), float(pose.y - cam.pose.y), float(pose.z - cam.pose.z)));
    }
    double f = cam.focal();
    for (const EngineVertex& v : o.verts) {
        int x = int(v.x), y = int(v.y), z = int(v.z);
        int32_t X = x * M.m[0][0] + y * M.m[1][0] + M.m[3][0] + z * M.m[2][0];
        int32_t Y = x * M.m[0][1] + y * M.m[1][1] + M.m[3][1] + z * M.m[2][1];
        int32_t Z = x * M.m[0][2] + y * M.m[1][2] + M.m[3][2] + z * M.m[2][2];
        int32_t D = int32_t(int32_t(cam.F) - (Z >> 10)) << cam.shift;
        Vec3 p = R.apply(Vec3(v.x, v.y, v.z)) + T;
        double w = cam.F - p.z;
        double sx = 160 + f * p.x / w, sy = 100 + f * p.y / w;
        if (D > 0 && w > 2 * cam.F && sx > -20 && sx < 340 && sy > -20 && sy < 220) {   // on the screen
            px.add(double(X / D + 160) - sx);
            px.add(double(Y / D + 100) - sy);
        }
        int nx = int(v.nx), ny = int(v.ny), nz = int(v.nz);
        int32_t NX = nx * N.m[0][0] + ny * N.m[1][0] + nz * N.m[2][0];
        int32_t NY = nx * N.m[0][1] + ny * N.m[1][1] + nz * N.m[2][1];
        Vec3 n = Rn.apply(Vec3(v.nx, v.ny, v.nz)).norm();
        if (std::fabs(v.nx) < 300 && std::fabs(v.ny) < 300 && std::fabs(v.nz) < 300) {   // file 46's box corners
            env.add(double((NX >> 12) + 128) - (128 + 63.75 * n.x));
            env.add(double((NY >> 12) + 128) - (128 + 63.75 * n.y));
        }
    }
}

int transformCheck(const StarsExe& exe) {
    Stats px, env;
    // spike ball (direct poses of 0x6905 / 0x6888 in a scene with the camera at the origin)
    for (int n = 0; n < 2300; n += 37) {
        double a = 4.0 * std::max(0, n - 624) * 6.283185307179586 / 4096;
        Pose p{std::floor(70 * std::sin(a)), std::floor(180 * std::cos(2 * a)), std::floor(380 * std::cos(a)) - 810,
               double((2 * n) & 0xfff), double((3 * n) & 0xfff), double((4 * n) & 0xfff)};
        Camera cam;
        cam.F = 80;
        cam.envA = (33 * n) & 0xfff;
        compareObject(exe, exe.file(18), 0, p, cam, true, px, env);
    }
    std::printf("file 18 (scene, F 80, shift 1):  screen max %.2f mean %.3f px   env max %.2f mean %.3f texels\n",
                px.maxD, px.mean(), env.maxD, env.mean());
    Stats px2, env2;
    for (int n = 0; n < 2000; n += 41) {
        Camera cam;
        cam.F = 100;
        cam.shift = 3;
        Pose p{double(n % 60), double(n % 80), -1100.0 - n % 330, double((n * 7) % 700) - 300, 1700.0 + n % 660,
               double(n % 300) - 60};
        compareObject(exe, exe.file(24), 0, p, cam, true, px2, env2);
    }
    std::printf("file 24 (scene, F 100, shift 3): screen max %.2f mean %.3f px   env max %.2f mean %.3f texels\n",
                px2.maxD, px2.mean(), env2.maxD, env2.mean());
    Stats px3, env3;
    {
        World w;
        w.load(exe, 28);
        const std::vector<uint8_t>& f = exe.file(28);
        for (int n = 0; n < 1000; n += 53)
            for (int i = 0; i < int(w.objects.size()); i++) {
                Camera cam;
                cam.F = 30;
                cam.pose = Pose{230.0 + n % 400, -110.0 + n % 90, 340.0 + n % 300, -380.0 + n, 180.0 + n % 400, 60};
                cam.envA = 100 + n % 128;
                cam.envB = 400 + n % 64;
                uint32_t off = uint32_t(f[8 + 0x20 * size_t(i) + 0x12] | f[8 + 0x20 * size_t(i) + 0x13] << 8 |
                                        f[8 + 0x20 * size_t(i) + 0x14] << 16);
                compareObject(exe, f, off, w.poses[size_t(i)], cam, true, px3, env3);
            }
    }
    std::printf("file 28 (scene with camera, F 30): screen max %.2f mean %.3f px   env max %.2f mean %.3f texels\n",
                px3.maxD, px3.mean(), env3.maxD, env3.mean());
    Stats px4, env4;
    for (int n = 0; n < 2400; n += 29) {
        Camera cam;
        cam.F = 300;
        Pose p{std::min(0.757 * n, 400.0), 22.9, -2400, double((n * 13) % 512) - 256, double((43 - 12 * n) & 0xfff), 0};
        compareObject(exe, exe.file(46), 0, p, cam, false, px4, env4);
    }
    std::printf("file 46 (direct, F 300):          screen max %.2f mean %.3f px   env max %.2f mean %.3f texels\n",
                px4.maxD, px4.mean(), env4.maxD, env4.mean());
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
    std::string exePath = std::string(STARS_DIR) + "/original/STARS.EXE", artDir = std::string(STARS_DIR) + "/remake/art";
    std::string shotDir = ".", cmd;
    std::vector<double> shots;
    int W = 960, H = 720, samples = 4;
    bool dropNear = false, affine = false, pixelEnv = false, noCull = false;
    int bench = 0;
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
        else if (a == "--dropnear") dropNear = true;
        else if (a == "--affine") affine = true;
        else if (a == "--pixelenv") pixelEnv = true;
        else if (a == "--nocull") noCull = true;
        else if (a == "--sharp") g_sharpTexels = true;
        else if (a == "--bench") bench = std::atoi(next().c_str());
        else cmd = a;
    }
    StarsExe exe;
    std::string err;
    if (!exe.load(exePath, &err)) {
        std::fprintf(stderr, "%s: %s\n", exePath.c_str(), err.c_str());
        return 1;
    }
    if (cmd == "transform") return transformCheck(exe);
    if (shots.empty()) {
        std::fprintf(stderr, "usage: enginetest [--exe FILE] transform | --shots T1,T2 --shotdir DIR [--shotsize WxH]\n");
        return 1;
    }
    Soundtrack music;
    try {
        music = renderSoundtrack(exe.fileSpan(1), std::span<const uint8_t>(exe.image().data(), exe.size()), 48000);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "music: %s\n", e.what());
        return 1;
    }
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
        Engine& eng = demo.ctx.shared<Engine>();
        eng.samples = samples;
        eng.debug.dropNearFaces = dropNear;
        if (affine) eng.debug.env = Engine::Debug::Env::Affine;
        if (pixelEnv) eng.debug.env = Engine::Debug::Env::Pixel;
        eng.debug.noObjectCull = noCull;
        std::vector<uint8_t> px;
        for (double t : shots) {
            if (bench > 0) {   // GPU time of the frame at t (the part's whole frame, background included)
                demo.render(t);
                gl::Finish();
                std::vector<double> ms;
                for (int i = 0; i < bench; i++) {
                    uint64_t b0 = SDL_GetTicksNS();
                    demo.render(t + i * 1e-4);
                    gl::Finish();
                    ms.push_back((SDL_GetTicksNS() - b0) * 1e-6);
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
        demo.release();
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}

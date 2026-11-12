// DOPE by Complex (The Gathering '95; code jmagic, graphics and music jugi, boxes reward) - high resolution remake.
// Reads everything from the original DOPE.EXE and DOPE.MOD; shows the redrawn pictures of dope/remake/art unless
// --original-art. Options, the playback loop, shots, video, bench.
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
#include "player.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
constexpr int kAudioRate = 48000;

struct Options {
    std::string exePath, modPath, artPath;
    bool art = true;
    bool windowed = false;
    int winW = 1280, winH = 960;
    double start = 0;
    float supersample = 1.0f;
    bool vsync = true, loop = false, info = false, cues = false;
    std::string wavOut;
    std::vector<double> shots;
    std::vector<std::string> picShots;
    std::string shotDir = ".";
    int shotW = 960, shotH = 720;
    std::string videoOut;
    double vFrom = 0, vTo = -1, vFps = 60;
    std::string vCodec = "-c:v libopenh264 -b:v 16M";
    bool vAudio = true;
    double bFrom = -1, bTo = -1;
};

void usage() {
    SDL_Log(
        "dope-remake [options]\n"
        "  --exe FILE          the original DOPE.EXE (default: next to the program, its parents or the current dir, or in an 'original' folder there)\n"
        "  --mod FILE          the original DOPE.MOD (default: next to DOPE.EXE)\n"
        "  --art DIR           the redrawn pictures (default: an 'art' folder next to the program or in its parents, or remake/art there)\n"
        "  --original-art      show the original's pixels instead of the redraws\n"
        "  --window            run in a window instead of fullscreen\n"
        "  --size WxH          window size (default 1280x960)\n"
        "  --start SECONDS     start position in seconds of music time\n"
        "  --ss FACTOR         supersampling factor (default 1)\n"
        "  --loop              restart at the end\n"
        "  --info              show the time, music position and part in the window title\n"
        "  --cues              print the running order (music times and the capture's records) and exit\n"
        "  --wav FILE          write the soundtrack to a WAV file and exit\n"
        "  --shots T1,T2,..    render the frames at these music times (s) to DIR/shot_T.png (and DIR/shots.txt: the\n"
        "                      matching capture record per shot, for tools/compare.py) and exit\n"
        "  --shotdir DIR       output directory for --shots and --picshots\n"
        "  --picshots N1,N2,.. draw catalog pictures (pictures.cpp, e.g. p3-nebula; 'all') to DIR/pic_NAME.png and exit\n"
        "  --shotsize WxH      frame size for --shots and --video (default 960x720)\n"
        "  --video FILE --from T0 --to T1 [--fps F] [--noaudio] [--vcodec \"ffmpeg args\"]\n"
        "                      render music times T0..T1 (s) to a video through ffmpeg (default 60 fps)\n"
        "  --bench T0,T1       render music times T0..T1 (s, step 0.0061) at --shotsize offscreen and report the time per frame\n"
        "keys: esc quit, space pause, left/right seek 5 s, down/up seek 30 s, f fullscreen");
}

bool fileExists(const std::string& p) {
    if (FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}

bool dirExists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

// The program's folder and its parents, then the current folder and its parents.
std::vector<std::string> searchDirs() {
    std::vector<std::string> dirs;
    if (const char* base = SDL_GetBasePath()) {
        std::string b = base;
        if (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
        for (const char* up : {"", "/..", "/../..", "/../../.."}) dirs.push_back(b + up);
    }
    for (const char* d : {".", "..", "../.."}) dirs.push_back(d);
    return dirs;
}

std::string findExe(const std::string& hint) {
    std::vector<std::string> dirs = searchDirs();
    if (!hint.empty()) {
        if (fileExists(hint)) return hint;
        dirs.insert(dirs.begin(), hint);  // a directory
    }
    for (const auto& d : dirs)
        for (const char* sub : {"", "/original"})  // the repository's dope/original folder
            for (const char* n : {"/DOPE.EXE", "/dope.exe", "/Dope.exe"})
                if (fileExists(d + sub + n)) return d + sub + n;
    return "";
}

// The redraws: an 'art' folder next to the program (a release) or in the source tree (dope/remake/art).
std::string findArt(const std::string& hint) {
    if (!hint.empty()) return dirExists(hint) ? hint : "";
    for (const auto& d : searchDirs())
        for (const char* sub : {"/art", "/remake/art"})
            if (dirExists(d + sub)) return d + sub;
    return "";
}

void writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { SDL_Log("cannot write %s", path.c_str()); return; }
    uint32_t bytes = uint32_t(pcm.size() * 2);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(uint32_t(rate)); w32(uint32_t(rate) * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        v.push_back(s.substr(p, q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return v;
}

std::vector<double> parseList(const std::string& s) {
    std::vector<double> v;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(',', p);
        v.push_back(std::atof(s.substr(p, q - p).c_str()));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return v;
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--exe") o.exePath = next();
        else if (a == "--mod") o.modPath = next();
        else if (a == "--art") o.artPath = next();
        else if (a == "--original-art") o.art = false;
        else if (a == "--window") o.windowed = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.winW, &o.winH);
        else if (a == "--start") o.start = std::atof(next().c_str());
        else if (a == "--ss") o.supersample = std::max(0.5f, float(std::atof(next().c_str())));
        else if (a == "--loop") o.loop = true;
        else if (a == "--info") o.info = true;
        else if (a == "--cues") o.cues = true;
        else if (a == "--novsync") o.vsync = false;
        else if (a == "--wav") o.wavOut = next();
        else if (a == "--shots") o.shots = parseList(next());
        else if (a == "--shotdir") o.shotDir = next();
        else if (a == "--picshots") o.picShots = splitList(next());
        else if (a == "--shotsize") std::sscanf(next().c_str(), "%dx%d", &o.shotW, &o.shotH);
        else if (a == "--video") o.videoOut = next();
        else if (a == "--from") o.vFrom = std::atof(next().c_str());
        else if (a == "--to") o.vTo = std::atof(next().c_str());
        else if (a == "--fps") o.vFps = std::atof(next().c_str());
        else if (a == "--vcodec") o.vCodec = next();
        else if (a == "--noaudio") o.vAudio = false;
        else if (a == "--bench") { auto v = parseList(next()); if (v.size() == 2) { o.bFrom = v[0]; o.bTo = v[1]; } }
        else if (a == "--help" || a == "-h") { usage(); return false; }
        else { SDL_Log("unknown option %s", a.c_str()); usage(); return false; }
    }
    return true;
}

const char* kPresentFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D frame;
uniform vec2 texel;
uniform float ss;
void main() {
    vec3 c;
    if (ss > 1.01) {
        vec3 acc = vec3(0.0);
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) acc += texture(frame, uv + vec2(x, y) * texel * ss * 0.33).rgb;
        c = acc / 9.0;
    } else {
        c = texture(frame, uv).rgb;
    }
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0;  // dither against banding in the 8-bit output
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// Reads the frame target as top-down RGBA8.
void readFrame(const Target& f, std::vector<uint8_t>& out) {
    std::vector<uint8_t> px(size_t(f.w) * f.h * 4);
    gl::BindFramebuffer(GL_FRAMEBUFFER, f.fbo);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, f.w, f.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    out.resize(px.size());
    for (int y = 0; y < f.h; y++)
        std::memcpy(&out[size_t(y) * f.w * 4], &px[size_t(f.h - 1 - y) * f.w * 4], size_t(f.w) * 4);
}

bool inToolbox() { return fileExists("/run/.containerenv") || fileExists("/run/.toolboxenv"); }

int renderVideo(Demo& demo, const Soundtrack& music, const Options& o) {
    double t1 = o.vTo >= 0 ? o.vTo : demo.length();
    std::string out = o.videoOut;
    std::string wav = out + ".audio.wav";
    bool audio = o.vAudio && !music.pcm.empty();
    if (audio) {
        // the soundtrack from T0 to T1, silence after the end of the music
        size_t a = size_t(std::max(0.0, o.vFrom * music.rate)), b = size_t(std::max(0.0, t1 * music.rate));
        std::vector<int16_t> part(b > a ? (b - a) * 2 : 0, 0);
        for (size_t i = a; i < b && i * 2 + 1 < music.pcm.size(); i++) {
            part[(i - a) * 2] = music.pcm[i * 2];
            part[(i - a) * 2 + 1] = music.pcm[i * 2 + 1];
        }
        if (!part.empty()) writeWav(wav, part, music.rate);
        else audio = false;
    }
    char cmd[2048];
    std::snprintf(cmd, sizeof cmd,
                  "%sffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %.6f -i - %s%s%s "
                  "%s -pix_fmt yuv420p %s%s \"%s\"",
                  inToolbox() ? "flatpak-spawn --host " : "", demo.ctx.w, demo.ctx.h, o.vFps,
                  audio ? "-i \"" : "", audio ? wav.c_str() : "", audio ? "\"" : "", o.vCodec.c_str(),
                  audio ? "-c:a aac -b:a 320k " : "", audio ? "-shortest" : "", out.c_str());
    SDL_Log("%s", cmd);
#ifdef _WIN32
    FILE* p = _popen(cmd, "wb");
#else
    FILE* p = popen(cmd, "w");
#endif
    if (!p) { SDL_Log("cannot start ffmpeg"); return 1; }
    std::vector<uint8_t> px;
    int n = 0;
    for (double t = o.vFrom; t < t1; t = o.vFrom + (++n) / o.vFps) {
        readFrame(demo.render(t), px);
        std::fwrite(px.data(), 1, px.size(), p);
        if (n % 600 == 0) SDL_Log("%.0f s", t);
    }
#ifdef _WIN32
    int rc = _pclose(p);
#else
    int rc = pclose(p);
#endif
    if (audio) std::remove(wav.c_str());
    SDL_Log("wrote %s (%d frames, ffmpeg %d)", out.c_str(), n, rc);
    return rc == 0 ? 0 : 1;
}

void fail(const std::string& msg, SDL_Window* win = nullptr) {
    SDL_Log("%s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DOPE", msg.c_str(), win);
}
}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    uint64_t t0 = SDL_GetTicksNS();
    DopeExe exe;
    std::string err;
    std::string path = findExe(opt.exePath);
    if (path.empty() || !exe.load(path, opt.modPath, &err)) {
        fail(path.empty() ? "Could not find DOPE.EXE.\nPut it and DOPE.MOD next to the program, in a folder named "
                            "'original' next to it or in a parent folder,\nor pass --exe <file>."
                          : "Could not read " + path + ":\n" + err);
        return 1;
    }
    uint64_t t1 = SDL_GetTicksNS();
    Soundtrack music;
    try {
        // Each part's PIT calibration holds the music's interrupts at the part's start (music.h). The remake starts
        // p6 48 and p7 74 retraces later than the capture (timeline.cpp, kShift5 / kShift6: the three fades at VGA
        // speed), so those two holds move with their parts.
        std::vector<IrqHold> holds = captureHolds();
        const double shift[] = {0, 0, 0, 0, 0, 48 / kRetraceHz, 74 / kRetraceHz};
        for (size_t i = 0; i < holds.size() && i < std::size(shift); i++) {
            holds[i].from += shift[i];
            holds[i].to += shift[i];
        }
        music = renderSoundtrack(exe.mod(), exe.loader().all(), kAudioRate, holds);
    } catch (const std::exception& e) {
        fail(std::string("music: ") + e.what());
        return 1;
    }
    SDL_Log("unpacked DOPE.EXE in %.0f ms, soundtrack in %.0f ms", (t1 - t0) * 1e-6, (SDL_GetTicksNS() - t1) * 1e-6);
    if (!opt.wavOut.empty()) {
        // the music stops with the demo: the loader stops the player right after p7 sets text mode
        Ctx probe;
        probe.music = &music;
        probe.clock.zero = kCaptureFirstTick - music.timeOfTick(0);
        double end = buildTimeline(probe).back().end;
        std::vector<int16_t> pcm(music.pcm.begin(),
                                 music.pcm.begin() + std::min(music.pcm.size(), size_t(end * music.rate) * 2));
        writeWav(opt.wavOut, pcm, music.rate);
        SDL_Log("wrote %s (%.3f s)", opt.wavOut.c_str(), double(pcm.size() / 2) / music.rate);
        return 0;
    }

    const bool offline =
        !opt.shots.empty() || !opt.picShots.empty() || !opt.videoOut.empty() || opt.bFrom >= 0 || opt.cues;
    if (!SDL_Init(SDL_INIT_VIDEO | (offline ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (offline) flags |= SDL_WINDOW_HIDDEN;
    else if (!opt.windowed) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* win = SDL_CreateWindow("DOPE - Complex (remake)", opt.winW, opt.winH, flags);
    if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
    SDL_GLContext glc = SDL_GL_CreateContext(win);
    if (!glc || !gl::load()) { SDL_Log("OpenGL 3.3 core is required: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(opt.vsync ? 1 : 0);
    if (!offline) SDL_HideCursor();

    auto renderSize = [&](int dw, int dh, int& rw, int& rh) {
        int h = std::min(dh, dw * 3 / 4);  // 4:3 frame fitted into the drawable, times the supersampling factor
        rh = std::max(240, int(h * opt.supersample));
        rw = rh * 4 / 3;
    };
    int dw = opt.shotW, dh = opt.shotH, rw, rh;
    if (offline) { rw = opt.shotW; rh = opt.shotH; }
    else { SDL_GetWindowSizeInPixels(win, &dw, &dh); renderSize(dw, dh, rw, rh); }

    Demo demo;
    Demo::Options dopt;
    dopt.art = opt.art;
    dopt.artDir = opt.art ? findArt(opt.artPath) : "";
    if (opt.art && dopt.artDir.empty()) SDL_Log("no art folder found: showing the original's pixels");
    else if (opt.art) SDL_Log("art: %s", dopt.artDir.c_str());
    if (!demo.init(exe, music, rw, rh, dopt, &err)) {
        fail("init: " + err, win);
        return 1;
    }
    SDL_Log("ready in %.0f ms", (SDL_GetTicksNS() - t0) * 1e-6);

    int rc = 0;
    if (opt.cues) {
        const CaptureClock& k = demo.ctx.clock;
        std::printf("music time 0 = capture program time %.6f (first player tick at %.6f)\n", k.zero,
                    demo.ctx.tickTime(0));
        std::printf("%-5s %10s %10s %10s %10s   remake records (load start end hold)   capture records\n", "part",
                    "load", "start", "end", "hold");
        auto rec = [&](double t) { return int(std::lround(k.record(t))); };
        auto cap = [&](double t) { return int(std::lround(k.toCapture(k.record(t)))); };
        for (const Cue& c : demo.cues())
            std::printf("%-5s %10.3f %10.3f %10.3f %10.3f   %6d %6d %6d %6d   %6d %6d %6d %6d\n", c.scene.c_str(),
                        c.load, c.start, c.end, c.hold, rec(c.load), rec(c.start), rec(c.end), rec(c.hold),
                        cap(c.load), cap(c.start), cap(c.end), cap(c.hold));
        std::printf("end of the demo %.3f s\n", demo.length());
    } else if (!opt.picShots.empty()) {
        // each picture over the whole frame (labels, strips and maps stretched), for checking the art
        std::vector<std::string> names = opt.picShots;
        if (names.size() == 1 && names[0] == "all") {
            names.clear();
            for (const PictureDef& d : pictureCatalog()) names.push_back(d.name);
        }
        std::vector<uint8_t> px;
        Target frame;
        frame.create(rw, rh, false, GL_RGBA16F);
        for (const std::string& n : names) {
            frame.bind();
            demo.ctx.clear(Vec3(0.25f, 0.25f, 0.25f));
            const Picture& p = demo.ctx.picture(n);
            demo.ctx.drawPicture(p);
            readFrame(frame, px);
            std::string name = opt.shotDir + "/pic_" + n + ".png";
            if (!stbi_write_png(name.c_str(), rw, rh, 4, px.data(), rw * 4)) { rc = 1; break; }
            SDL_Log("wrote %s (%s, %dx%d texels for %dx%d)", name.c_str(), p.redraw ? "redraw" : "original pixels",
                    p.tex.w, p.tex.h, p.srcW, p.srcH);
        }
        frame.destroy();
    } else if (!opt.shots.empty()) {
        std::vector<uint8_t> px;
        FILE* list = std::fopen((opt.shotDir + "/shots.txt").c_str(), "a");  // music time, capture record, part
        for (double t : opt.shots) {
            readFrame(demo.render(t), px);
            char name[512];
            std::snprintf(name, sizeof name, "%s/shot_%07.3f.png", opt.shotDir.c_str(), t);
            if (!stbi_write_png(name, rw, rh, 4, px.data(), rw * 4)) {
                SDL_Log("cannot write %s", name);
                rc = 1;
                break;
            }
            SDL_Log("wrote %s (%s)", name, demo.partAt(t));
            if (list) std::fprintf(list, "%.3f %d %s\n", t, demo.ctx.captureFrame(t), *demo.partAt(t) ? demo.partAt(t) : "-");
        }
        if (list) std::fclose(list);
    } else if (!opt.videoOut.empty()) {
        rc = renderVideo(demo, music, opt);
    } else if (opt.bFrom >= 0) {
        std::vector<double> ms;
        for (double t = opt.bFrom; t < opt.bTo; t += 0.37 / 60.39) {
            uint64_t b0 = SDL_GetTicksNS();
            demo.render(t);
            gl::Finish();
            ms.push_back((SDL_GetTicksNS() - b0) * 1e-6);
        }
        std::sort(ms.begin(), ms.end());
        if (!ms.empty())
            SDL_Log("bench %dx%d %.2f-%.2f s: %d frames, median %.2f ms, 95%% %.2f ms, max %.2f ms", rw, rh,
                    opt.bFrom, opt.bTo, int(ms.size()), ms[ms.size() / 2], ms[ms.size() * 95 / 100], ms.back());
    } else {
        Shader present;
        present.build(kFullscreenVS, kPresentFS, "present");
        Player player;
        player.setSound(&music.pcm, music.rate, demo.length());
        player.seek(opt.start);
        if (!player.start()) SDL_Log("no audio device: running silently");
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    switch (e.key.key) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_SPACE: player.setPaused(!player.paused()); break;
                        case SDLK_LEFT: player.seek(std::max(0.0, player.time() - 5)); break;
                        case SDLK_RIGHT: player.seek(player.time() + 5); break;
                        case SDLK_DOWN: player.seek(std::max(0.0, player.time() - 30)); break;
                        case SDLK_UP: player.seek(player.time() + 30); break;
                        case SDLK_F:
                        case SDLK_F11: SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0); break;
                        default: break;
                    }
                } else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    SDL_GetWindowSizeInPixels(win, &dw, &dh);
                    renderSize(dw, dh, rw, rh);
                    demo.resize(rw, rh);
                }
            }
            double t = player.time();
            if (t >= demo.length() || player.ended()) {
                if (opt.loop) { player.seek(0); continue; }
                break;
            }
            const Target& f = demo.render(t);
            if (opt.info) {
                char title[160];
                int pos = demo.ctx.position(t);
                const Cue* cue = demo.cueAt(t);
                std::snprintf(title, sizeof title, "DOPE - %.2f s - %02X:%02X - %s %.0f", t, (pos >> 8) & 0xff,
                              pos & 0xff, demo.partAt(t), cue ? std::floor((t - cue->start) * kRetraceHz) : 0.0);
                SDL_SetWindowTitle(win, title);
            }
            gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl::Viewport(0, 0, dw, dh);
            gl::ClearColor(0, 0, 0, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
            int vh = std::min(dh, dw * 3 / 4), vw = vh * 4 / 3;
            gl::Viewport((dw - vw) / 2, (dh - vh) / 2, vw, vh);
            setBlend(Blend::None);
            present.use();
            f.bindTex(0);
            present.set("frame", 0);
            present.set("texel", 1.0f / f.w, 1.0f / f.h);
            present.set("ss", float(f.h) / float(vh));
            drawFullscreen();
            SDL_GL_SwapWindow(win);
        }
        player.stop();
    }
    demo.release();  // the parts' GL objects go before the context
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}

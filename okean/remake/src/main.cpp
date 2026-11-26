// The Sea Robot of Love by Orange (The Party 1995 PC 64k intro; code hoplite and dune, music dune, font and graphics
// der piipo) - high resolution remake. Reads everything from the original KORSO2.EXE; shows the redrawn pictures of
// okean/remake/art unless --original-art. Options, the playback loop, shots, video, bench.
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
#include "schedule.h"
#include "player.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
constexpr int kAudioRate = 48000;

struct Options {
    std::string exePath, artPath;
    bool art = true;
    bool windowed = false;
    int winW = 1280, winH = 960;
    double start = 0;       // music time (s)
    double startRefresh = -1;  // --refresh: start at this refresh of the capture
    float supersample = 1.0f;
    bool vsync = true, loop = false, info = false, cues = false;
    std::string wavOut;
    std::vector<double> shots, refreshShots;
    std::vector<std::string> picShots;
    std::string shotDir = ".";
    int shotW = 960, shotH = 720;
    std::string videoOut;
    double vFrom = 0, vTo = -1, vFps = 60;
    bool fpsSet = false;   // --fps given: --bench steps by 1 / fps (as playback at that rate)
    std::string vCodec = "-c:v libopenh264 -b:v 16M";
    bool vAudio = true;
    double bFrom = -1, bTo = -1;
};

void usage() {
    SDL_Log(
        "okean-remake [options]\n"
        "  --data DIR          the folder with the original KORSO2.EXE (default: next to the program, its parents or the\n"
        "                      current dir, or in an 'original' folder there)\n"
        "  --art DIR           the redrawn pictures (default: an 'art' folder next to the program or in its parents, or remake/art there)\n"
        "  --original-art      show the original's pixels instead of the redraws\n"
        "  --window            run in a window instead of fullscreen\n"
        "  --size WxH          window size (default 1280x960)\n"
        "  --start SECONDS     start position in seconds of music time (or --refresh R: at refresh R of the capture)\n"
        "  --ss FACTOR         supersampling factor (default 1)\n"
        "  --loop              restart at the end\n"
        "  --info              show the time, music position and part in the window title\n"
        "  --novsync           do not wait for the display's vertical refresh\n"
        "  --cues              print the running order and exit\n"
        "  --wav FILE          write the soundtrack to a WAV file and exit\n"
        "  --shots T1,T2,..    render the frames at these music times (s) to PNG and exit\n"
        "  --refshots R1,R2,.. render the frames at these refreshes of the capture (okean/re/full.frames records) to PNG\n"
        "  --shotdir DIR       output directory for --shots, --refshots and --picshots (created if missing)\n"
        "  --picshots N1,N2,.. draw the named pictures (redraw or original pixels; 'all') to DIR/pic_NAME.png and exit\n"
        "  --shotsize WxH      frame size for --shots and --video (default 960x720)\n"
        "  --video FILE --from T0 --to T1 [--fps F] [--noaudio] [--vcodec \"ffmpeg args\"]\n"
        "                      render music times T0..T1 (s) to a video through ffmpeg (default 60 fps)\n"
        "  --bench T0,T1 [--fps F]\n"
        "                      render music times T0..T1 (s, step 1 / 60, or 1 / F) at --shotsize offscreen and report the\n"
        "                      time per frame\n"
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

// The folder with KORSO2.EXE (any case): the hint, the program's folder and its parents, the current folder and its
// parents, and an 'original' folder in each (the repository's okean/original).
std::string findFile(const std::string& dir, const char* name) {
    std::string lower = name;
    for (char& ch : lower) ch = char(std::tolower(uint8_t(ch)));
    for (const std::string& n : {std::string(name), lower})
        if (fileExists(dir + "/" + n)) return dir + "/" + n;
    return "";
}

std::string findData(const std::string& hint) {
    std::vector<std::string> dirs = searchDirs();
    if (!hint.empty()) dirs.insert(dirs.begin(), hint);
    for (const auto& d : dirs)
        for (const char* sub : {"", "/original"})
            if (!findFile(d + sub, "KORSO2.EXE").empty()) return d + sub;
    return "";
}

// The redraws: an 'art' folder next to the program (a release) or in the source tree (okean/remake/art).
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
        v.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return v;
}

std::vector<double> parseList(const std::string& s) {
    std::vector<double> v;
    for (const std::string& x : splitList(s)) v.push_back(std::atof(x.c_str()));
    return v;
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data" || a == "--exe") o.exePath = next();
        else if (a == "--art") o.artPath = next();
        else if (a == "--original-art") o.art = false;
        else if (a == "--window") o.windowed = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.winW, &o.winH);
        else if (a == "--start") o.start = std::atof(next().c_str());
        else if (a == "--refresh") o.startRefresh = std::atof(next().c_str());
        else if (a == "--refshots") o.refreshShots = parseList(next());
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
        else if (a == "--fps") { o.vFps = std::atof(next().c_str()); o.fpsSet = o.vFps > 0; }
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
    double t1 = o.vTo >= 0 ? o.vTo : music.timeOf(demo.length());
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
        readFrame(demo.render(music.refreshOf(t)), px);
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

bool makeDir(const std::string& d) {
    if (dirExists(d)) return true;
    if (SDL_CreateDirectory(d.c_str())) return true;
    SDL_Log("cannot create %s: %s", d.c_str(), SDL_GetError());
    return false;
}

void printCues(const Demo& demo, const Soundtrack& music) {
    std::printf("%-10s %8s %7s %7s %7s %10s %8s\n", "part", "entry", "enter", "first", "exit", "first (s)", "music");
    for (const Cue& c : demo.cues()) {
        int pos = demo.ctx.position(c.start);
        std::printf("%-10s %8X %7.0f %7.0f %7.0f %10.3f %5d:%02d\n", c.scene.c_str(), c.entry, c.enter, c.start,
                    c.end, music.timeOf(c.start), pos / 64, pos % 64);
    }
    std::printf("end of the intro: refresh %.2f (%.3f s)\n", demo.length(), music.timeOf(demo.length()));
}

void fail(const std::string& msg, SDL_Window* win = nullptr) {
    SDL_Log("%s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The Sea Robot of Love", msg.c_str(), win);
}
}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    uint64_t t0 = SDL_GetTicksNS();
    OkeanExe exe;
    std::string err;
    std::string dir = findData(opt.exePath);
    if (dir.empty()) {
        fail("Could not find KORSO2.EXE.\nPut it next to the program, in a folder named 'original' next to it or in a "
             "parent folder,\nor pass --data <folder>.");
        return 1;
    }
    if (!exe.load(findFile(dir, "KORSO2.EXE"), &err)) {
        fail("Could not read the original file in " + dir + ":\n" + err);
        return 1;
    }
    uint64_t t1 = SDL_GetTicksNS();
    Soundtrack music;
    IntroSchedule sched;
    try {
        // the song's positions tick by tick (more than the intro plays), the clocks, then the sound
        SongTicks song = songTicks(exe, 9400);
        sched = buildSchedule(song);
        music = renderSoundtrack(exe, song, sched.music, kAudioRate);
    } catch (const std::exception& e) {
        fail(std::string("music: ") + e.what());
        return 1;
    }
    SDL_Log("unpacked KORSO2.EXE in %.0f ms, soundtrack in %.0f ms", (t1 - t0) * 1e-6, (SDL_GetTicksNS() - t1) * 1e-6);
    if (!opt.wavOut.empty()) {
        writeWav(opt.wavOut, music.pcm, music.rate);
        SDL_Log("wrote %s", opt.wavOut.c_str());
        return 0;
    }

    const bool offline =
        !opt.shots.empty() || !opt.refreshShots.empty() || !opt.picShots.empty() || !opt.videoOut.empty() ||
        opt.bFrom >= 0 || opt.cues;
    if ((!opt.shots.empty() || !opt.refreshShots.empty() || !opt.picShots.empty()) && !makeDir(opt.shotDir)) return 1;
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
    SDL_Window* win = SDL_CreateWindow("The Sea Robot of Love - Orange (remake)", opt.winW, opt.winH, flags);
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
    if (!demo.init(exe, music, sched, rw, rh, dopt, &err)) {
        fail("init: " + err, win);
        return 1;
    }
    SDL_Log("ready in %.0f ms", (SDL_GetTicksNS() - t0) * 1e-6);

    int rc = 0;
    if (opt.cues) {
        printCues(demo, music);
    } else if (!opt.picShots.empty()) {
        // each picture filling the frame at its own aspect (one original pixel 1.2 times as tall as wide), on grey
        if (opt.picShots.size() == 1 && opt.picShots[0] == "all") {
            opt.picShots.clear();
            for (const PictureInfo& p : pictureList()) opt.picShots.push_back(p.name);
        }
        std::vector<uint8_t> px;
        Target frame;
        frame.create(rw, rh, false, GL_RGBA16F);
        for (const std::string& name : opt.picShots) {
            frame.bind();
            demo.ctx.clear(Vec3(0.25f, 0.25f, 0.25f));
            const Picture& p = demo.ctx.picture(name);
            if (p.srcW <= 0) { rc = 1; continue; }
            float s = std::min(300.0f / p.srcW, 180.0f / p.srcH);
            demo.ctx.drawPicture(p, {.x = 160 - 0.5f * s * p.srcW, .y = 100 - 0.5f * s * p.srcH, .w = s * p.srcW,
                                     .h = s * p.srcH});
            readFrame(frame, px);
            std::string out = opt.shotDir + "/pic_" + name + ".png";
            if (!stbi_write_png(out.c_str(), rw, rh, 4, px.data(), rw * 4)) { rc = 1; break; }
            SDL_Log("wrote %s (%s, %dx%d)", out.c_str(), p.redraw ? "redraw" : "original pixels", p.tex.w, p.tex.h);
        }
        frame.destroy();
    } else if (!opt.shots.empty() || !opt.refreshShots.empty()) {
        std::vector<uint8_t> px;
        std::vector<std::pair<double, bool>> list;
        for (double t : opt.shots) list.push_back({t, false});
        for (double r : opt.refreshShots) list.push_back({r, true});
        for (auto [v, isRefresh] : list) {
            double r = isRefresh ? v : music.refreshOf(v);
            readFrame(demo.render(r), px);
            char name[512];
            if (isRefresh) std::snprintf(name, sizeof name, "%s/shot_r%07.2f.png", opt.shotDir.c_str(), v);
            else std::snprintf(name, sizeof name, "%s/shot_%07.3f.png", opt.shotDir.c_str(), v);
            if (!stbi_write_png(name, rw, rh, 4, px.data(), rw * 4)) {
                SDL_Log("cannot write %s", name);
                rc = 1;
                break;
            }
            SDL_Log("wrote %s (refresh %.2f, %s)", name, r, demo.partAt(r));
        }
    } else if (!opt.videoOut.empty()) {
        rc = renderVideo(demo, music, opt);
    } else if (opt.bFrom >= 0) {
        std::vector<double> ms;
        using GetStringFn = const unsigned char* (*)(unsigned);
        if (auto getString = reinterpret_cast<GetStringFn>(SDL_GL_GetProcAddress("glGetString")))
            SDL_Log("bench on %s", reinterpret_cast<const char*>(getString(0x1F01)));   // GL_RENDERER
        const double step = opt.fpsSet ? 1.0 / opt.vFps : 1.0 / 60.0;
        for (double t = opt.bFrom; t < opt.bTo; t += step) {
            uint64_t b0 = SDL_GetTicksNS();
            demo.render(music.refreshOf(t));
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
        const double length = music.timeOf(demo.length());
        player.setSound(&music.pcm, music.rate, length);
        player.seek(opt.startRefresh >= 0 ? music.timeOf(opt.startRefresh) : opt.start);
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
            if (t >= length || player.ended()) {
                if (opt.loop) { player.seek(0); continue; }
                break;
            }
            double r = music.refreshOf(t);
            const Target& f = demo.render(r);
            if (opt.info) {
                char title[160];
                int pos = demo.ctx.position(r);
                std::snprintf(title, sizeof title, "The Sea Robot of Love - %.2f s - %d:%02d - %s - refresh %.1f", t, pos / 64,
                              pos % 64, demo.partAt(r), r);
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

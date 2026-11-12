// Paper by Psychic Link (code Statix, music Vic / Acme; Wired '96 64k intro) - high resolution remake.
// Reads everything from the original PAPER.EXE, and the redrawn pictures of paper/remake/art unless --original-art.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "gl.h"
#include "music.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
struct Options {
    std::string exePath, imagePath, artPath;
    bool art = true;
    bool windowed = false;
    int winW = 1280, winH = 960;
    double startTick = 0;
    float supersample = 1.0f;
    bool vsync = true, loop = false, info = false;
    std::string wavOut;
    std::vector<double> shots;
    std::string shotDir = ".";
    int shotW = 960, shotH = 720;
    std::string videoOut;
    double vFrom = 0, vTo = -1, vFps = Music::kTickHz;
    std::string vCodec = "-c:v libopenh264 -b:v 16M";
    bool vAudio = true;
    double bFrom = -1, bTo = -1;
};

void usage() {
    SDL_Log(
        "paper-remake [options]\n"
        "  --exe FILE          the original PAPER.EXE (default: next to the program, its parents or the current dir, or in an 'original' folder there)\n"
        "  --art DIR           the redrawn pictures (default: an 'art' folder next to the program or in its parents, or remake/art there)\n"
        "  --original-art      show the original's pixels instead of the redraws\n"
        "  --window          run in a window instead of fullscreen\n"
        "  --size WxH          window size (default 1280x960)\n"
        "  --start SECONDS     start position (or --tick T: start at tick T of the 70 Hz clock)\n"
        "  --ss FACTOR         supersampling factor (default 1)\n"
        "  --loop              restart at the end\n"
        "  --info              show the tick and part in the window title\n"
        "  --wav FILE          write the soundtrack to a WAV file and exit\n"
        "  --shots T1,T2,..    render the frames at these ticks to PNG (shot_<tick>.png) and exit\n"
        "  --shotdir DIR       output directory for --shots\n"
        "  --shotsize WxH      frame size for --shots and --video (default 960x720)\n"
        "  --video FILE --from T0 --to T1 [--fps F] [--noaudio] [--vcodec \"ffmpeg args\"]\n"
        "                      render ticks T0..T1 to a video through ffmpeg (default 70.0018 fps = one frame per tick)\n"
        "  --bench T0,T1       render ticks T0..T1 (step 0.37) at --shotsize offscreen and report the time per frame\n"
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
        for (const char* sub : {"", "/original"})  // the repository's paper/original folder
            for (const char* n : {"/PAPER.EXE", "/paper.exe", "/Paper.exe"})
                if (fileExists(d + sub + n)) return d + sub + n;
    return "";
}

// The redraws: an 'art' folder next to the program (a release) or in the source tree (paper/remake/art).
std::string findArt(const std::string& hint) {
    if (!hint.empty()) return dirExists(hint) ? hint : "";
    for (const auto& d : searchDirs())
        for (const char* sub : {"/art", "/remake/art"})
            if (dirExists(d + sub)) return d + sub;
    return "";
}

void writeWav(const std::string& path, const float* s, size_t frames, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { SDL_Log("cannot write %s", path.c_str()); return; }
    uint32_t bytes = uint32_t(frames * 4);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(uint32_t(rate)); w32(uint32_t(rate) * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    std::vector<int16_t> q(frames * 2);
    for (size_t i = 0; i < q.size(); i++) q[i] = int16_t(std::lround(std::clamp(s[i], -1.0f, 1.0f) * 32767.0f));
    std::fwrite(q.data(), 2, q.size(), f);
    std::fclose(f);
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
        else if (a == "--image") o.imagePath = next();  // development: an already unpacked image
        else if (a == "--art") o.artPath = next();
        else if (a == "--original-art") o.art = false;
        else if (a == "--window") o.windowed = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.winW, &o.winH);
        else if (a == "--start") o.startTick = std::atof(next().c_str()) * Music::kTickHz;
        else if (a == "--tick") o.startTick = std::atof(next().c_str());
        else if (a == "--ss") o.supersample = std::max(0.5f, float(std::atof(next().c_str())));
        else if (a == "--loop") o.loop = true;
        else if (a == "--info") o.info = true;
        else if (a == "--novsync") o.vsync = false;
        else if (a == "--wav") o.wavOut = next();
        else if (a == "--shots") o.shots = parseList(next());
        else if (a == "--shotdir") o.shotDir = next();
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

int renderVideo(Demo& demo, Music& music, const Options& o) {
    double t1 = o.vTo >= 0 ? o.vTo : demo.length();
    std::string out = o.videoOut;
    std::string wav = out + ".audio.wav";
    bool audio = o.vAudio && !music.pcm().empty();
    if (audio) {
        size_t a = size_t(std::max(0.0, o.vFrom / Music::kTickHz * music.rate()));
        size_t b = std::min(music.pcm().size() / 2, size_t(t1 / Music::kTickHz * music.rate()));
        if (b > a) writeWav(wav, music.pcm().data() + a * 2, b - a, int(std::lround(music.rate())));
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
    for (double t = o.vFrom; t < t1; t = o.vFrom + (++n) * (Music::kTickHz / o.vFps)) {
        readFrame(demo.render(t), px);
        std::fwrite(px.data(), 1, px.size(), p);
        if (n % 700 == 0) SDL_Log("tick %.0f", t);
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
}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    PaperExe exe;
    std::string err;
    if (!opt.imagePath.empty()) {
        if (!exe.loadImage(opt.imagePath, &err)) { SDL_Log("%s", err.c_str()); return 1; }
    } else {
        std::string path = findExe(opt.exePath);
        if (path.empty() || !exe.load(path, &err)) {
            std::string msg = path.empty() ? "Could not find PAPER.EXE.\nPut it next to the program, in a folder named 'original' next to it or in a parent folder,\n"
                                             "or pass --exe <file>."
                                           : "Could not read " + path + ":\n" + err;
            SDL_Log("%s", msg.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Paper", msg.c_str(), nullptr);
            return 1;
        }
    }

    Music music;
    if (!music.load(exe, &err)) { SDL_Log("music: %s", err.c_str()); return 1; }
    if (!opt.wavOut.empty()) {
        writeWav(opt.wavOut, music.pcm().data(), music.pcm().size() / 2, int(std::lround(music.rate())));
        SDL_Log("wrote %s", opt.wavOut.c_str());
        return 0;
    }

    const bool offline = !opt.shots.empty() || !opt.videoOut.empty() || opt.bFrom >= 0;
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
    SDL_Window* win = SDL_CreateWindow("Paper - Psychic Link (remake)", opt.winW, opt.winH, flags);
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
    demo.ctx.artDir = opt.art ? findArt(opt.artPath) : "";
    if (opt.art && demo.ctx.artDir.empty()) SDL_Log("no art folder found: showing the original's pixels");
    else if (opt.art) SDL_Log("art: %s", demo.ctx.artDir.c_str());
    if (!demo.init(exe, music, rw, rh, &err)) {
        SDL_Log("init: %s", err.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Paper", err.c_str(), win);
        return 1;
    }

    int rc = 0;
    if (!opt.shots.empty()) {
        std::vector<uint8_t> px;
        for (double t : opt.shots) {
            readFrame(demo.render(t), px);
            char name[512];
            if (t == std::floor(t)) std::snprintf(name, sizeof name, "%s/shot_%05d.png", opt.shotDir.c_str(), int(t));
            else std::snprintf(name, sizeof name, "%s/shot_%08.2f.png", opt.shotDir.c_str(), t);
            stbi_write_png(name, rw, rh, 4, px.data(), rw * 4);
            SDL_Log("wrote %s (%s)", name, partNameAt(t));
        }
    } else if (!opt.videoOut.empty()) {
        rc = renderVideo(demo, music, opt);
    } else if (opt.bFrom >= 0) {
        std::vector<double> ms;
        for (double t = opt.bFrom; t < opt.bTo; t += 0.37) {
            uint64_t t0 = SDL_GetTicksNS();
            demo.render(t);
            gl::Finish();
            ms.push_back((SDL_GetTicksNS() - t0) * 1e-6);
        }
        std::sort(ms.begin(), ms.end());
        if (!ms.empty())
            SDL_Log("bench %dx%d ticks %.0f-%.0f: %d frames, median %.2f ms, 95%% %.2f ms, max %.2f ms", rw, rh,
                    opt.bFrom, opt.bTo, int(ms.size()), ms[ms.size() / 2], ms[ms.size() * 95 / 100], ms.back());
    } else {
        Shader present;
        present.build(kFullscreenVS, kPresentFS, "present");
        music.seekTick(opt.startTick);
        if (!music.startPlayback()) SDL_Log("no audio device: running silently");
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    double sec = Music::kTickHz;
                    switch (e.key.key) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_SPACE: music.setPaused(!music.paused()); break;
                        case SDLK_LEFT: music.seekTick(std::max(0.0, music.tick() - 5 * sec)); break;
                        case SDLK_RIGHT: music.seekTick(music.tick() + 5 * sec); break;
                        case SDLK_DOWN: music.seekTick(std::max(0.0, music.tick() - 30 * sec)); break;
                        case SDLK_UP: music.seekTick(music.tick() + 30 * sec); break;
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
            double t = music.tick();
            if (t >= demo.length() || music.ended()) {
                if (opt.loop) { music.seekTick(0); continue; }
                break;
            }
            const Target& f = demo.render(t);
            if (opt.info) {
                char title[128];
                std::snprintf(title, sizeof title, "Paper - tick %.1f - %s", t, partNameAt(t));
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
        music.stopPlayback();
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}

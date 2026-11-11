// 303 by Acme (X'97) - high resolution remake.
// Reads the original data files (MUSTARD.PAK, GARLIC.PAK, KETCHUP.PAK, *.LZW).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "assets.h"
#include "audio.h"
#include "demo.h"
#include "gl.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
struct Options {
    std::string dataDir;
    bool windowed = false;
    int winW = 1280, winH = 960;
    double start = 0;
    float supersample = 1.0f;
    std::string wavOut;
    std::vector<double> shots;
    std::string shotDir = ".";
    int shotW = 960, shotH = 720;
    float warmup = 1.5f;
    bool vsync = true;
    bool loop = false;
};

void usage() {
    SDL_Log(
        "acme303 [options]\n"
        "  --data DIR        directory with the original 303 files (default: auto)\n"
        "  --window          run in a window instead of fullscreen\n"
        "  --size WxH        window size (default 1280x960)\n"
        "  --start SECONDS   start position\n"
        "  --ss FACTOR       supersampling factor (default 1)\n"
        "  --loop            restart at the end\n"
        "  --wav FILE        render the soundtrack to a WAV file and exit\n"
        "  --shots T1,T2,..  render frames at these times to PNG and exit\n"
        "  --shotdir DIR     output directory for --shots\n"
        "  --shotsize WxH    size for --shots (default 960x720)\n"
        "keys: esc quit, space pause, left/right seek 5s, up/down seek 30s, f fullscreen");
}

bool hasData(const std::string& dir) {
    for (const char* n : {"/MUSTARD.PAK", "/mustard.pak"}) {
        if (FILE* f = std::fopen((dir + n).c_str(), "rb")) { std::fclose(f); return true; }
    }
    return false;
}

std::string findData(const std::string& hint) {
    std::vector<std::string> c;
    if (!hint.empty()) c.push_back(hint);
    if (const char* base = SDL_GetBasePath()) {
        std::string b = base;
        if (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
        c.push_back(b);
        c.push_back(b + "/data");
        c.push_back(b + "/..");
        c.push_back(b + "/../..");
        c.push_back(b + "/../../..");
    }
    c.push_back(".");
    c.push_back("data");
    c.push_back("..");
    for (const auto& d : c) {
        if (hasData(d)) return d;
        if (hasData(d + "/original")) return d + "/original";  // the repository's <demo>/original folder
    }
    return "";
}

void writeWav(const std::string& path, const std::vector<float>& s, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t n = uint32_t(s.size()), bytes = n * 2;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(uint32_t(rate)); w32(uint32_t(rate) * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (float v : s) { int16_t q = int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f); std::fwrite(&q, 2, 1, f); }
    std::fclose(f);
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") o.dataDir = next();
        else if (a == "--window") o.windowed = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.winW, &o.winH);
        else if (a == "--start") o.start = std::atof(next().c_str());
        else if (a == "--ss") o.supersample = std::max(0.5f, float(std::atof(next().c_str())));
        else if (a == "--wav") o.wavOut = next();
        else if (a == "--loop") o.loop = true;
        else if (a == "--novsync") o.vsync = false;
        else if (a == "--shots") {
            std::string s = next();
            size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p);
                o.shots.push_back(std::atof(s.substr(p, q - p).c_str()));
                if (q == std::string::npos) break;
                p = q + 1;
            }
        } else if (a == "--shotdir") o.shotDir = next();
        else if (a == "--shotsize") std::sscanf(next().c_str(), "%dx%d", &o.shotW, &o.shotH);
        else if (a == "--warmup") o.warmup = float(std::atof(next().c_str()));
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
        // box filter over the supersampled footprint
        vec3 acc = vec3(0.0);
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) acc += texture(frame, uv + vec2(x, y) * texel * ss * 0.33).rgb;
        c = acc / 9.0;
    } else {
        c = texture(frame, uv).rgb;
    }
    // tiny dither against banding in the 8-bit output
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0;
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";
}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) return 1;
    std::string dir = findData(opt.dataDir);
    if (dir.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "303",
                                 "Could not find the original 303 data files (MUSTARD.PAK etc).\n"
                                 "Put them next to the executable, in a folder named 'original' next to it\n"
                                 "or in a parent folder, or pass --data <dir>.", nullptr);
        return 1;
    }
    setDataDir(dir);

    Music music;
    std::string err;
    if (!music.load(dataPath("GARLIC.PAK"), dataPath("KETCHUP.PAK"), &err)) {
        SDL_Log("music: %s", err.c_str());
        return 1;
    }
    if (!opt.wavOut.empty()) {
        writeWav(opt.wavOut, music.renderAll(Music::kRate), Music::kRate);
        SDL_Log("wrote %s (%.1f s)", opt.wavOut.c_str(), music.duration());
        return 0;
    }

    const bool shotMode = !opt.shots.empty();
    if (!SDL_Init(SDL_INIT_VIDEO | (shotMode ? 0 : SDL_INIT_AUDIO))) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (shotMode) flags |= SDL_WINDOW_HIDDEN;
    else if (!opt.windowed) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* win = SDL_CreateWindow("303 - acme (remake)", opt.winW, opt.winH, flags);
    if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
    SDL_GLContext glc = SDL_GL_CreateContext(win);
    if (!glc || !gl::load()) { SDL_Log("OpenGL 3.3 core is required: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(opt.vsync ? 1 : 0);
    if (!shotMode) SDL_HideCursor();

    auto renderSize = [&](int dw, int dh, int& rw, int& rh) {
        // 4:3 frame fitted into the drawable, times the supersampling factor
        int h = std::min(dh, dw * 3 / 4);
        rh = std::max(240, int(h * opt.supersample));
        rw = rh * 4 / 3;
    };

    Demo demo;
    int dw = opt.shotW, dh = opt.shotH;
    if (!shotMode) SDL_GetWindowSizeInPixels(win, &dw, &dh);
    int rw, rh;
    if (shotMode) { rw = opt.shotW; rh = opt.shotH; }
    else renderSize(dw, dh, rw, rh);
    if (!demo.init(rw, rh, &err)) {
        SDL_Log("init: %s", err.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "303", err.c_str(), win);
        return 1;
    }

    if (shotMode) {
        std::vector<uint8_t> px(size_t(rw) * rh * 4), flip(px.size());
        for (double t : opt.shots) {
            // run a short warm-up so feedback effects reach their steady state
            int steps = int(opt.warmup * 60.0f);
            for (int i = steps; i >= 1; i--) demo.render(float(t - i / 60.0), 1.0f / 60.0f);
            const Target& f = demo.render(float(t), 1.0f / 60.0f);
            gl::BindFramebuffer(GL_FRAMEBUFFER, f.fbo);
            gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < rh; y++) std::memcpy(&flip[size_t(y) * rw * 4], &px[size_t(rh - 1 - y) * rw * 4], size_t(rw) * 4);
            char name[512];
            std::snprintf(name, sizeof(name), "%s/shot_%07.2f.png", opt.shotDir.c_str(), t);
            stbi_write_png(name, rw, rh, 4, flip.data(), rw * 4);
            SDL_Log("wrote %s", name);
        }
        SDL_GL_DestroyContext(glc);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    Shader present;
    present.build(kFullscreenVS, kPresentFS, "present");

    music.seek(opt.start);
    music.startPlayback();
    bool running = true;
    double lastT = music.time();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                switch (e.key.key) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE: music.setPaused(!music.paused()); break;
                    case SDLK_LEFT: music.seek(music.time() - 5); break;
                    case SDLK_RIGHT: music.seek(music.time() + 5); break;
                    case SDLK_DOWN: music.seek(music.time() - 30); break;
                    case SDLK_UP: music.seek(music.time() + 30); break;
                    case SDLK_F:
                    case SDLK_F11: {
                        bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(win, !fs);
                        break;
                    }
                    default: break;
                }
            } else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                SDL_GetWindowSizeInPixels(win, &dw, &dh);
                renderSize(dw, dh, rw, rh);
                demo.resize(rw, rh);
            }
        }
        double t = music.time();
        float dt = float(std::clamp(t - lastT, 0.0, 0.1));
        lastT = t;
        if (t >= demo.length() || music.ended()) {
            if (opt.loop) { music.seek(0); continue; }
            running = false;
        }
        const Target& f = demo.render(float(t), dt);
        // present: letterbox the 4:3 frame into the window
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
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

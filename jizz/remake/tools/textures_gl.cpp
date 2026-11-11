// Test tool for src/jizz/textures.cpp: creates a hidden GL 3.3 window, builds jizz::Textures as the parts get it
// (c.shared<jizz::Textures>()), reads every GL texture's level 0 back through a framebuffer and compares it with the
// generator's data (exact bytes, or the area-downsampled high-resolution texture), and reports the times.
//
//   jizz-textest [--exe JIZZ.EXE]
#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "jizz/textures.h"

namespace fs = std::filesystem;

static std::string findExe() {
    fs::path d = fs::current_path();
    for (int i = 0; i < 6; i++, d = d.parent_path())
        for (const char* sub : {"original", "jizz/original"})
            if (fs::exists(d / sub / "JIZZ.EXE")) return (d / sub / "JIZZ.EXE").string();
    return {};
}

int main(int argc, char** argv) {
    std::string exePath = argc > 2 && std::string(argv[1]) == "--exe" ? argv[2] : findExe();
    JizzExe exe;
    std::string err;
    if (exePath.empty() || !exe.load(exePath, &err)) {
        std::fprintf(stderr, "cannot load JIZZ.EXE: %s\n", err.c_str());
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* win = SDL_CreateWindow("textest", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext glc = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!glc || !gl::load()) {
        std::fprintf(stderr, "OpenGL 3.3: %s\n", SDL_GetError());
        return 1;
    }
    int bad = 0;
    {
        Ctx c;
        c.exe = &exe;
        const auto t0 = std::chrono::steady_clock::now();
        const jizz::Textures& tx = c.shared<jizz::Textures>();
        gl::Finish();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("jizz::Textures: %.1f ms (generator %.1f ms, upload %.1f ms)\n", ms, tx.data().msTotal,
                    ms - tx.data().msTotal);
        GLuint fbo = 0;
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        for (int i = 1; i < jizz::kTexCount; i++) {
            const jizz::Tex t = jizz::Tex(i);
            const int n = tx.size(t), s = n / 256;
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx.id(t), 0);
            if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::printf("%-12s framebuffer incomplete\n", jizz::Textures::name(t));
                bad++;
                continue;
            }
            std::vector<float> px(size_t(n) * n * 4);
            gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
            gl::ReadPixels(0, 0, n, n, GL_RGBA, GL_FLOAT, px.data());
            // area-downsample, compare with the exact bytes (BlobHeights G / B signed)
            double sum = 0, mx = 0;
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++)
                    for (int p = 0; p < 3; p++) {
                        double a = 0;
                        for (int j = 0; j < s; j++)
                            for (int k = 0; k < s; k++) a += px[(size_t(y * s + j) * n + x * s + k) * 4 + p];
                        a = a / (s * s) * 255;
                        const uint8_t b = tx.texel(t, p, x, y);
                        const double e = t == jizz::Tex::BlobHeights && p > 0 ? double(int8_t(b)) : double(b);
                        sum += std::fabs(a - e);
                        mx = std::max(mx, std::fabs(a - e));
                    }
            const double mean = sum / (3.0 * 65536);
            const bool ok = s == 1 ? mx < 0.01 : mean < 4.5;
            bad += !ok;
            std::printf("%-12s %4dx%-4d %s  mean |GL - exact| %.3f, max %.1f  %s\n", jizz::Textures::name(t), n, n,
                        tx.hires(t) ? "hi-res" : "exact ", mean, mx, ok ? "OK" : "MISMATCH");
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl::DeleteFramebuffers(1, &fbo);
        const bool lookups = jizz::Textures::byPointer(0x6556) == jizz::Tex::Map3 &&
                             jizz::Textures::bySlot(14) == jizz::Tex::Map14 &&
                             jizz::Textures::bySlot(1, true) == jizz::Tex::Map5 &&
                             jizz::Textures::byName("map1a") == jizz::Tex::Map1a &&
                             jizz::Textures::pointerVar(jizz::Tex::Logo2) == 0x658e &&
                             tx.rgb565(jizz::Tex::Logo1, 0, 0) == 0;
        std::printf("lookups %s\n", lookups ? "OK" : "WRONG");
        bad += !lookups;
        c.shared_.clear();  // GL objects go while the context is current
    }
    SDL_GL_DestroyContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf(bad ? "FAILED\n" : "ALL OK\n");
    return bad ? 1 : 0;
}

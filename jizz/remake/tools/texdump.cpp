// Test tool for the texture generator (src/jizz/texgen*.cpp, no GL): builds all textures from JIZZ.EXE and writes
//   OUT/final_<pointer variable>.bin   each buffer as the original leaves it after 0x12c50 (compare with
//                                      jizz/re/snaps/t440.snap: tools/check_textures.py)
//   OUT/<name>.tga                     W at every op 0x26, as the /pandora mode writes it (compare with re/dump)
//   OUT/png/<name>.png                 with --png: exact texture (nearest, scaled up) | high resolution | difference x4
// and prints per texture the build times and the check of the high-resolution texture against the exact one.
//
//   jizz-texdump [--exe JIZZ.EXE] [--out DIR] [--scale 4] [--png] [--threads N] [--max-diff D] [--all-hires]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "exe.h"
#include "jizz/texgen.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;
using namespace jizz;

static bool writeFile(const fs::path& p, const std::vector<uint8_t>& d) {
    FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

static std::string findExe() {
    fs::path d = fs::current_path();
    for (int i = 0; i < 6; i++, d = d.parent_path())
        for (const char* sub : {"original", "jizz/original"}) {
            const fs::path p = d / sub / "JIZZ.EXE";
            if (fs::exists(p)) return p.string();
        }
    return {};
}

// exact (nearest) | high resolution | |difference| x 4 of the area-downsampled high-resolution texture
static void writePng(const fs::path& p, const texgen::Texture& t, int id) {
    const int N = t.size, s = N / 256, W = 3 * N;
    std::vector<uint8_t> img(size_t(W) * N * 3, 0);
    auto put = [&](int x, int y, int c, double v) {
        img[(size_t(y) * W + x) * 3 + c] = uint8_t(std::clamp(v, 0.0, 255.0) + 0.5);
    };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            for (int c = 0; c < 3; c++) {
                const uint8_t b = t.planar[c * texgen::kPlane + (y / s) * 256 + x / s];
                const bool sgn = id == texgen::kBlobHeights && c > 0;
                const double e = sgn ? double(int8_t(b)) + 128 : b;
                put(x, y, c, e);
                const double h = t.hires[c][size_t(y) * N + x];
                put(N + x, y, c, sgn ? h + 128 : h);
            }
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            for (int c = 0; c < 3; c++) {
                double a = 0;
                for (int j = 0; j < s; j++)
                    for (int i = 0; i < s; i++) a += t.hires[c][size_t(y * s + j) * N + x * s + i];
                a /= s * s;
                const uint8_t b = t.planar[c * texgen::kPlane + y * 256 + x];
                const double e = id == texgen::kBlobHeights && c > 0 ? double(int8_t(b)) : b;
                const double d = std::fabs(a - e) * 4;
                for (int j = 0; j < s; j++)
                    for (int i = 0; i < s; i++) put(2 * N + x * s + i, y * s + j, c, d);
            }
    stbi_write_png(p.string().c_str(), W, N, 3, img.data(), W * 3);
}

int main(int argc, char** argv) {
    std::string exePath, out = "texdump";
    texgen::Options opt;
    opt.captureSaves = true;
    opt.keepFloats = true;
    bool png = false;
    std::string traceName;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--exe") exePath = next();
        else if (a == "--out") out = next();
        else if (a == "--scale") opt.scale = std::atoi(next().c_str());
        else if (a == "--threads") opt.threads = std::atoi(next().c_str());
        else if (a == "--max-diff") opt.maxMeanDiff = std::atof(next().c_str());
        else if (a == "--all-hires") opt.maxMeanDiff = 1e9;
        else if (a == "--png") png = true;
        else if (a == "--lean") opt.keepFloats = false;  // as the remake runs it (memory and time)
        else if (a == "--trace") traceName = next();
        else {
            std::fprintf(stderr, "usage: %s [--exe JIZZ.EXE] [--out DIR] [--scale K] [--png] [--threads N] "
                                 "[--max-diff D] [--all-hires] [--lean] [--trace NAME]\n", argv[0]);
            return 2;
        }
    }
    if (exePath.empty()) exePath = findExe();
    JizzExe exe;
    std::string err;
    if (exePath.empty() || !exe.load(exePath, &err)) {
        std::fprintf(stderr, "cannot load JIZZ.EXE (%s): %s\n", exePath.c_str(), err.c_str());
        return 1;
    }
    if (!traceName.empty()) {
        std::fputs(texgen::trace(exe, texgen::byName(traceName), opt.scale > 0 ? opt.scale : 4).c_str(), stdout);
        return 0;
    }
    texgen::Result r;
    try {
        r = texgen::build(exe, opt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "texgen: %s\n", e.what());
        return 1;
    }
    fs::create_directories(fs::path(out) / "png");
    std::printf("%-12s %6s %8s %8s %9s %9s %8s %8s  %s\n", "texture", "ptr", "exact ms", "hires ms", "mean diff",
                "(centre)", "max diff", ">16", "used");
    for (int id = 1; id < texgen::kCount; id++) {
        const texgen::Texture& t = r.tex[id];
        if (const uint32_t v = texgen::pointerVar(id)) {
            char n[64];
            std::snprintf(n, sizeof n, "final_%04x.bin", v);
            writeFile(fs::path(out) / n, t.original);
        }
        char ptr[16] = "-";
        if (texgen::pointerVar(id)) std::snprintf(ptr, sizeof ptr, "0x%04x", texgen::pointerVar(id));
        std::printf("%-12s %6s %8.1f %8.1f %9.3f %9.3f %8.1f %7.3f%%  %s\n", texgen::name(id), ptr, t.msExact,
                    t.msHires, t.meanDiff, t.meanDiffCentre, t.maxDiff, t.over16 * 100,
                    t.hiresUsed ? "high resolution" : t.hiresBuilt ? "exact (high resolution rejected)" : "exact");
        if (t.peakPlanes) std::printf("%-12s peak %d planes\n", "", t.peakPlanes);
        if (png && t.hiresBuilt && !t.hires[0].empty())
            writePng(fs::path(out) / "png" / (std::string(texgen::name(id)) + ".png"), t, id);
    }
    for (const auto& s : r.saves) writeFile(fs::path(out) / s.first, texgen::tgaBytes(exe, s.second));
    std::printf("high-resolution time per op (ms, all scripts):");
    for (int i = 0; i < 0x27; i++)
        if (r.msPerOp[i] >= 1) std::printf(" %02x:%.0f", i, r.msPerOp[i]);
    std::printf("\nlibm close calls: %d, total %.1f ms\n", r.libmCloseCalls, r.msTotal);
    return 0;
}

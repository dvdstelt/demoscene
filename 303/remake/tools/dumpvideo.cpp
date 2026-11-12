// Dumps every frame of the 303 video clips as lossless PNG, decoded like the remake (src/assets.cpp: the 4x4 DCT in
// floating point, YIQ -> RGB with the original matrix, no dithering): OUTDIR/<CLIP>/NNNN.png, NNNN = frame index in
// the file from 0000. PARANOID is the greyscale luma clip.
//   g++ -std=c++20 -O2 -I src -I third_party tools/dumpvideo.cpp src/assets.cpp -o dumpvideo
//   ./dumpvideo ../original ../re/dump/video [CLIP ...]
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/assets.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dumpvideo DATADIR OUTDIR [ALEX HARL VS PARANOID]\n");
        return 1;
    }
    setDataDir(argv[1]);
    std::vector<std::string> clips;
    for (int i = 3; i < argc; i++) clips.push_back(argv[i]);
    if (clips.empty()) clips = {"ALEX", "HARL", "VS", "PARANOID"};
    for (const std::string& n : clips) {
        Video v;
        if (!v.load(dataPath(n + ".LZW"), n == "PARANOID")) {
            std::fprintf(stderr, "cannot read %s.LZW\n", n.c_str());
            return 1;
        }
        std::filesystem::path dir = std::filesystem::path(argv[2]) / n;
        std::filesystem::create_directories(dir);
        for (int f = 0; f < v.frames; f++) {
            char name[16];
            std::snprintf(name, sizeof name, "%04d.png", f);
            std::vector<unsigned char> rgb(size_t(v.w) * v.h * 3);
            for (int i = 0; i < v.w * v.h; i++)
                for (int k = 0; k < 3; k++) rgb[size_t(i) * 3 + size_t(k)] = v.frame(f)[i * 4 + k];
            if (!stbi_write_png((dir / name).string().c_str(), v.w, v.h, 3, rgb.data(), v.w * 3)) {
                std::fprintf(stderr, "cannot write %s\n", (dir / name).string().c_str());
                return 1;
            }
        }
        std::printf("%s: %d frames of %dx%d\n", n.c_str(), v.frames, v.w, v.h);
    }
    return 0;
}

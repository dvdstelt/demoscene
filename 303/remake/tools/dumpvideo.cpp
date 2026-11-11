// Dumps every Nth frame of the 303 videos as PPM for inspection.
#include <cstdio>
#include <string>
#include "../src/assets.h"
int main(int argc, char** argv) {
    setDataDir(argv[1]);
    int step = std::stoi(argv[3]);
    for (const char* n : {"PARANOID"}) {
        Video v;
        v.load(dataPath(std::string(n) + ".LZW"), std::string(n) == "PARANOID");
        for (int f = 0; f < v.frames; f += step) {
            char p[512];
            std::snprintf(p, sizeof p, "%s/%s_%03d.ppm", argv[2], n, f);
            FILE* o = std::fopen(p, "wb");
            std::fprintf(o, "P6 %d %d 255\n", v.w, v.h);
            for (int i = 0; i < v.w * v.h; i++) std::fwrite(v.frame(f) + i * 4, 1, 3, o);
            std::fclose(o);
        }
        std::printf("%s %d frames\n", n, v.frames);
    }
}

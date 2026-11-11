// Writes unpklite's image of argv[1] to argv[2].
#include "exe.h"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> exe((std::istreambuf_iterator<char>(f)), {}), img;
    std::string err;
    if (!unpklite(exe, img, &err)) { std::printf("error: %s\n", err.c_str()); return 1; }
    std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(img.data()), img.size());
    return 0;
}

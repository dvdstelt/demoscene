#include "exe.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv) {
    if (argc > 1) {  // guard-page self test: must crash
        std::vector<uint8_t> v(63845);
        volatile const uint8_t* p = v.data();
        std::printf("reading past the end...\n");
        std::printf("%d\n", p[v.size()]);
        return 0;
    }
    std::ifstream f("../PAPER.EXE", std::ios::binary);
    std::vector<uint8_t> exe((std::istreambuf_iterator<char>(f)), {}), img;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) unpklite(exe, img, nullptr);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("%.3f ms per unpack\n", std::chrono::duration<double, std::milli>(t1 - t0).count() / 100);
}

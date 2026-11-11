// Prints the start time in seconds of every (order, row) of GARLIC.PAK, for mapping
// the exe's music-position counter (2 * (order * 64 + row)) to time.
#include <libopenmpt/libopenmpt.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "GARLIC.PAK", std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
    openmpt::module m(d);
    for (int o = 0; o < m.get_num_orders(); o++) {
        int pat = m.get_order_pattern(o);
        int rows = m.get_pattern_num_rows(pat);
        for (int r = 0; r < rows; r++) std::printf("%d %d %d %.4f\n", o, r, 2 * (o * 64 + r), m.set_position_order_row(o, r));
    }
}

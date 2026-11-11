// PLACEHOLDER until the MXMPlay port lands: silence, and rows at the module's nominal speed (6 ticks of 1/60.39 s).
// The real player replaces this file.
#include "mxmplay.h"

#include "exe.h"

bool renderSoundtrack(const JizzExe&, Soundtrack& out, std::string*) {
    const double row = 6 * 19759.0 / 1193182.0;
    out.rate = 48000;
    out.timer0 = 0;
    out.rows.clear();
    double t = 0;
    for (int o = 0; o < 0x16; o++)
        for (int r = 0; r < 64; r++, t += row) out.rows.push_back({o << 8 | r, t});
    out.length = 147.75;
    out.pcm.assign(size_t(out.length * out.rate) * 2, 0.0f);
    return true;
}

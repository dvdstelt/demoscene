// Inside's soundtrack (music.h). Placeholder until the port of DSMI's AMP 2.00 and the GUS driver is in: silence, and
// the row starts of the player's timing model (inside/re/notes-core.md 1.3).
#include "music.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "demo.h"
#include "exe.h"

int Soundtrack::positionAtCallback(double m) const {
    if (m < 0) return -1;
    auto it = std::upper_bound(rowStart.begin(), rowStart.end(), int(std::floor(m)));
    return std::max(0, int(it - rowStart.begin()) - 1);   // 0:00 from the music start (ampPlayModule) on
}

int Soundtrack::positionAt(double refresh) const { return positionAtCallback(4.0 * std::floor(refresh) - 150.0); }

double Soundtrack::rowCallback(int row) const {
    return row >= 0 && size_t(row) < rowStart.size() ? double(rowStart[size_t(row)])
                                                     : std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return startRefresh + t * kRefreshHz; }
double Soundtrack::timeOf(double refresh) const { return (refresh - startRefresh) / kRefreshHz; }

Soundtrack renderSoundtrack(const InsideExe&, std::span<const uint8_t> amf, int rate) {
    if (amf.size() < 4 || amf[0] != 'A' || amf[1] != 'M' || amf[2] != 'F') throw std::runtime_error("EYE.AMF: not AMF");
    Soundtrack s;
    s.rate = rate;
    s.startRefresh = 37;
    // the player's row starts (notes-core 1.3): 6 ticks of 200 DSMI units, then 4 ticks of 192 per row, 35 units per
    // callback; the rows of order 36 take three times as long
    for (int R = 0; R <= 2328; R++) {
        long long t = 1200 + 768LL * std::min(R, 2304) + 2304LL * std::max(0, R - 2304);
        s.rowStart.push_back(int(t / 35) + 1);
    }
    s.endRefresh = 13081;
    s.pcm.assign(size_t((s.endRefresh - s.startRefresh) / kRefreshHz * rate) * 2, 0);
    return s;
}

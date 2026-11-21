// Inside's soundtrack (music.h). Placeholder until the port of DSMI's AMP 2.00 and the GUS driver is in: silence,
// and the music position at the song's measured pace (one row per 5.5 refreshes; inside/re/PARTS.md).
#include "music.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "demo.h"
#include "exe.h"

int Soundtrack::positionAt(double refresh) const {
    if (positions.empty() || refresh < firstRefresh) return -1;
    size_t i = size_t(std::floor(refresh)) - size_t(firstRefresh);
    return i < positions.size() ? positions[i] : positions.back();
}

double Soundtrack::positionRefresh(int pos) const {
    for (size_t i = 0; i < positions.size(); i++)
        if (positions[i] >= pos) return double(firstRefresh) + double(i);
    return std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return startRefresh + t * kRefreshHz; }
double Soundtrack::timeOf(double refresh) const { return (refresh - startRefresh) / kRefreshHz; }

Soundtrack renderSoundtrack(const InsideExe&, std::span<const uint8_t> amf, int rate) {
    if (amf.size() < 4 || amf[0] != 'A' || amf[1] != 'M' || amf[2] != 'F') throw std::runtime_error("EYE.AMF: not AMF");
    Soundtrack s;
    s.rate = rate;
    s.startRefresh = 37;
    s.firstRefresh = 37;
    const double perRow = 5.5;  // placeholder pace
    for (int k = 0; k < 13100; k++) s.positions.push_back(int(k / perRow));
    s.endRefresh = 13081;
    s.pcm.assign(size_t((s.endRefresh - s.startRefresh) / kRefreshHz * rate) * 2, 0);
    return s;
}

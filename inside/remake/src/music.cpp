// Inside's soundtrack (music.h). Placeholder until the port of DSMI's AMP 2.00 and the GUS driver is in: silence,
// and the music positions of the capture (inside/re/full.ticks, read at run time when the repository has it;
// otherwise one row per 5.6 refreshes).
#include "music.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
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
#ifdef INSIDE_RE_DIR
    if (FILE* f = std::fopen(INSIDE_RE_DIR "/full.ticks", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, f)) {
            int frame, order, row;
            double t;
            if (line[0] == '#' || std::sscanf(line, "%d %lf %d %d", &frame, &t, &order, &row) != 4) continue;
            if (frame >= s.firstRefresh) s.positions.push_back(order < 0 ? -1 : order * 64 + row);
        }
        std::fclose(f);
    }
#endif
    if (s.positions.empty())
        for (int k = 0; k < 13100; k++) s.positions.push_back(int(k / 5.6));
    s.endRefresh = 13081;
    s.pcm.assign(size_t((s.endRefresh - s.startRefresh) / kRefreshHz * rate) * 2, 0);
    return s;
}

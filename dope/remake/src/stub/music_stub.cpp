// The silent soundtrack stand-in (stub/music.h): tick times and positions from dope/re/notes-music.md 7.2.
#include "music.h"

#include <cmath>
#include <limits>

namespace {
constexpr double kZeroToTick0 = 0.01984;  // the first player tick, 62 x 320 us after music time 0
constexpr int kSlowTicks = 1728;          // orders 0..2: 192 rows at speed 9

// notes-music 7.2: T(k) after the first row tick
double closedForm(int k) {
    if (k <= 2) return 0.01984 * k;
    if (k <= 1730) return 0.03968 + (k - 2) * 0.0224;
    return 38.74688 + (k - 1730) * 0.02048;
}

// the inverse: the last tick at or before music time t (-1 before tick 0)
int tickAt(double t) {
    double s = t - kZeroToTick0;
    if (s < 0) return -1;
    int k;
    if (s < 0.03968) k = int(std::floor(s / 0.01984 + 1e-9));
    else if (s < 38.74688) k = 2 + int(std::floor((s - 0.03968) / 0.0224 + 1e-9));
    else k = 1730 + int(std::floor((s - 38.74688) / 0.02048 + 1e-9));
    while (k > 0 && closedForm(k) > s) k--;
    while (closedForm(k + 1) <= s) k++;
    return k;
}

// rows read after tick k (row r is read at its first tick)
int rowsRead(int k) { return k < kSlowTicks ? k / 9 + 1 : 192 + (k - kSlowTicks) / 3 + 1; }
int firstTickOfRow(int r) { return r < 192 ? r * 9 : kSlowTicks + (r - 192) * 3; }
}  // namespace

double Soundtrack::timeOfTick(int tick) const { return kZeroToTick0 + closedForm(tick); }

int Soundtrack::positionAt(double t) const {
    int k = tickAt(t);
    if (k < 0) return 0;
    int r = rowsRead(k) - 1;
    return (r / 64) << 8 | (r % 64);
}

double Soundtrack::timeOfPosition(int pos) const {
    int r = ((pos >> 8) & 0xff) * 64 + (pos & 0xff);
    if (r <= 0) return -std::numeric_limits<double>::infinity();
    return timeOfTick(firstTickOfRow(r));
}

Soundtrack renderSoundtrack(std::span<const uint8_t>, std::span<const uint8_t>, int rate) {
    Soundtrack s;
    s.rate = rate;
    return s;
}

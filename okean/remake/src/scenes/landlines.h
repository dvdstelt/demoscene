// What the canyon (src/scenes/web.cpp) and the landscape (src/scenes/landscape.cpp) share: the rotation by a sine
// table angle and the points their line drawers visit (0x350c8 and 0x3801c, okean/re/notes-landscapes.md 0.3 and
// 1.4), evaluated in floats.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace landlines {

constexpr double kTurn = 2.0 * std::numbers::pi / 4096.0;   // one table step in radians
constexpr double kAmp = 1022.0 / 1024.0;   // a product with a table value (peak 1022), shifted right by 10

// R(t, p, q) of both parts: p' = (p cos t - sin t q) >> 10, q' = (q cos t + sin t p) >> 10, t in table steps.
inline void rotate(double t, double& p, double& q) {
    double s = std::sin(t * kTurn) * kAmp, c = std::cos(t * kTurn) * kAmp;
    double p2 = p * c - q * s, q2 = q * c + p * s;
    p = p2;
    q = q2;
}

// The points a line drawer visits: (x, y) + i (dx, dy), i = 0..count-1, in VGA coordinates (each is a blob's
// top-left corner). count = 0: nothing.
struct Lattice {
    double x = 0, y = 0, dx = 0, dy = 0;
    int count = 0;
};

// 0x350c8 / 0x3801c: a line with both ends above the screen is skipped; the drawer starts at the upper end (compared
// unsigned: an end above the screen counts as the lower one) and visits n = max(|dx|, |dy|) / 2 points, the last one
// step short of the other end. A line that is not entirely on the screen is counted with an 8-bit register: n mod 256
// points (256 for 0). The remake visits min(n, 256): the 256 steps reach past the screen from any point on it, so
// the spokes towards vertices behind the eye run to the screen edge (the original's counter cuts them short at random
// lengths), and a line whose drawer starts far off the screen stays invisible, as in the original. (Drawing every
// line whole gives the same frames in both parts; the limit keeps the work small on spokes millions of pixels long.)
inline Lattice lattice(double x1, double y1, double x2, double y2) {
    Lattice L;
    if (!(y1 > 0) && y2 < 0) return L;
    auto key = [](double y) { return y >= 0 ? y : std::numeric_limits<double>::infinity(); };
    if (!(key(y1) < key(y2))) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    double n = std::floor(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1)) / 2.0 + 0.5);
    if (!(n >= 1.0)) return L;
    L.x = x1;
    L.y = y1;
    L.dx = (x2 - x1) / n;
    L.dy = (y2 - y1) / n;
    L.count = int(std::min(n, 256.0));
    return L;
}

// A blob with its top-left corner at (x, y) reaches into the 320x200 frame (the kernel's surface runs from -0.5 to
// +8.5 around its pixel grid).
inline bool reaches(double x, double y) { return x > -8.5 && x < 320.5 && y > -8.5 && y < 200.5; }

}  // namespace landlines

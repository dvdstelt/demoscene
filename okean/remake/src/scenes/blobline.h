// A blob line with end points anywhere: the line drawers of the sixth part (0x3c46e) and the seventh (0x52fed)
// (okean/re/notes-morph-star.md 1.2, notes-sheet.md 5) for edges whose ends the projections throw thousands or
// millions of pixels off the screen (the tunnel's spokes, the sheet's vertices next to the camera). The blobs sit
// where the drawers put them: from the end with the smaller y (a negative y counts as huge: the drawers compare
// unsigned), n = max(|dx|, |dy|) / 2 of them at P1 + i (P2 - P1) / n, i = 0..n-1; only the run of them that can
// reach the screen is handed to the canvas. The whole line is drawn (the drawers' 8-bit step counter, which keeps
// n mod 256 blobs of a line that is not entirely on the screen, and their whole-blob clipping at x 311 / y 191 are
// left out).
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "okean/glow.h"

namespace okean_scenes {

inline void blobLine(okean::Canvas& canvas, double x1, double y1, double x2, double y2) {
    auto key = [](double y) { return y < 0.0 ? std::numeric_limits<double>::infinity() : y; };
    if (key(y1) >= key(y2)) { std::swap(x1, x2); std::swap(y1, y2); }
    double dx = x2 - x1, dy = y2 - y1;
    double n = std::floor(std::max(std::fabs(dx), std::fabs(dy)) / 2.0 + 0.5);
    if (!(n >= 1.0)) return;
    // Liang-Barsky: the part of the segment whose blobs (top-left at the point, the kernel's surface from -0.5 to
    // +8.5) can touch the 320x200 frame
    double t0 = 0.0, t1 = 1.0;
    auto clip = [&](double p, double q) {
        if (p == 0.0) return q >= 0.0;
        double t = q / p;
        if (p < 0.0) { if (t > t1) return false; t0 = std::max(t0, t); }
        else { if (t < t0) return false; t1 = std::min(t1, t); }
        return true;
    };
    const double x0 = -9.0, xe = 321.0, ytop = -9.0, ye = 201.0;
    if (!clip(-dx, x1 - x0) || !clip(dx, xe - x1) || !clip(-dy, y1 - ytop) || !clip(dy, ye - y1)) return;
    double i0 = std::ceil(t0 * n), i1 = std::min(n - 1.0, std::floor(t1 * n));
    for (double i = i0; i <= i1; i += 1.0) {
        double t = i / n;
        canvas.blob(float(x1 + dx * t), float(y1 + dy * t));
    }
}

}  // namespace okean_scenes

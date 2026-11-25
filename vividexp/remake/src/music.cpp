// The soundtrack (music.h). TEMPORARY: until the port of MIDAS's XM player lands (vividexp/re/notes-music.md), this
// only times the songs' rows from the modules' speed and tempo (Fxx, Bxx, Dxx, EEx; one tick = 2.5 / BPM s) and
// renders silence, so that the framework and the scenes can be built and checked against the capture's timeline.
#include "music.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "demo.h"
#include "exe.h"
#include "vivid/timeline.h"

std::pair<int, int> Soundtrack::Song::positionAt(double r) const {
    std::pair<int, int> p{0, 0};
    for (const Row& w : rows) {
        if (w.refresh > r) break;
        p = {w.order, w.row};
    }
    return p;
}

double Soundtrack::Song::reached(int pos, int pat, double from) const {
    // WaitSYNC polls once per refresh: the first whole refresh >= from whose position satisfies the condition
    double k = std::ceil(from - 1e-9);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& w = rows[i];
        double next = i + 1 < rows.size() ? rows[i + 1].refresh : stopRefresh;
        if (next <= k) continue;
        if (w.order >= pos && w.row >= pat) return std::max(k, std::ceil(w.refresh - 1e-9));
    }
    return std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return song[0].startRefresh + t * kRefreshHz; }
double Soundtrack::timeOf(double refresh) const { return (refresh - song[0].startRefresh) / kRefreshHz; }

namespace {
uint16_t rd16(std::span<const uint8_t> d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t rd32(std::span<const uint8_t> d, size_t o) {
    return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24);
}

struct Cell { uint8_t note = 0, ins = 0, vol = 0, fx = 0, param = 0; };

// The rows the song plays, timed by speed and tempo only (no sound).
std::vector<Soundtrack::Row> timeRows(std::span<const uint8_t> d, double start, double stop) {
    if (d.size() < 80 || std::string(reinterpret_cast<const char*>(d.data()), 17) != "Extended Module: ")
        throw std::runtime_error("not an XM module");
    const uint32_t hsize = rd32(d, 60);
    const int length = rd16(d, 64), channels = rd16(d, 68), npat = rd16(d, 70);
    int speed = rd16(d, 76), bpm = rd16(d, 78);
    std::vector<std::vector<std::vector<Cell>>> pats;
    size_t p = 60 + hsize;
    for (int i = 0; i < npat; ++i) {
        uint32_t hl = rd32(d, p);
        int rows = rd16(d, p + 5), packed = rd16(d, p + 7);
        std::vector<std::vector<Cell>> pat(size_t(rows), std::vector<Cell>(size_t(channels), Cell{}));
        size_t q = p + hl, e = q + size_t(packed);
        for (int r = 0; r < rows && packed; ++r)
            for (int c = 0; c < channels && q < e; ++c) {
                Cell cell;
                uint8_t b = d[q++];
                if (b & 0x80) {
                    if (b & 1) cell.note = d[q++];
                    if (b & 2) cell.ins = d[q++];
                    if (b & 4) cell.vol = d[q++];
                    if (b & 8) cell.fx = d[q++];
                    if (b & 16) cell.param = d[q++];
                } else {
                    cell.note = b;
                    cell.ins = d[q++];
                    cell.vol = d[q++];
                    cell.fx = d[q++];
                    cell.param = d[q++];
                }
                pat[size_t(r)][size_t(c)] = cell;
            }
        pats.push_back(std::move(pat));
        p += hl + size_t(packed);
    }
    std::vector<Soundtrack::Row> out;
    double t = start;
    int order = 0, row = 0;
    for (int guard = 0; guard < 100000 && order < length && t < stop; ++guard) {
        int pi = d[80 + size_t(order)];
        static const std::vector<std::vector<Cell>> empty(64, std::vector<Cell>(1, Cell{}));
        const auto& pat = pi < npat ? pats[size_t(pi)] : empty;   // a missing pattern plays 64 empty rows
        if (row >= int(pat.size())) { ++order; row = 0; continue; }
        out.push_back({order, row, t});
        int nextOrder = order, nextRow = row + 1, delay = 0;
        bool jump = false;
        for (const Cell& c : pat[size_t(row)]) {
            if (c.fx == 0x0f && c.param) { if (c.param < 32) speed = c.param; else bpm = c.param; }
            if (c.fx == 0x0b) { nextOrder = c.param; nextRow = 0; jump = true; }
            if (c.fx == 0x0d) { if (!jump) nextOrder = order + 1; nextRow = (c.param >> 4) * 10 + (c.param & 15); jump = true; }
            if (c.fx == 0x0e && (c.param >> 4) == 0x0e) delay = c.param & 15;
        }
        t += speed * (1 + delay) * 2.5 / bpm * kRefreshHz;
        if (jump) { order = nextOrder; row = nextRow; }
        else row = nextRow;
    }
    out.push_back({length, 0, t});   // the end of the song
    return out;
}
}  // namespace

Soundtrack renderSoundtrack(const VividExe& exe, std::span<const uint8_t> xm1, std::span<const uint8_t> xm2, int rate) {
    Soundtrack s;
    s.rate = rate;
    // the capture: part I's module starts at refresh 23; part II's 12 refreshes after part I's script ends
    s.song[0].startRefresh = 23;
    s.song[0].stopRefresh = 1e9;
    s.song[0].rows = timeRows(xm1, s.song[0].startRefresh, 1e9);
    std::vector<vivid::TimedLine> p1 = vivid::timeScript(exe, 1, s);
    s.song[0].stopRefresh = p1.empty() ? s.song[0].startRefresh : p1.back().end;
    s.song[1].startRefresh = s.song[0].stopRefresh + 12;
    s.song[1].stopRefresh = 1e9;
    s.song[1].rows = timeRows(xm2, s.song[1].startRefresh, 1e9);
    std::vector<vivid::TimedLine> p2 = vivid::timeScript(exe, 2, s);
    s.song[1].stopRefresh = p2.empty() ? s.song[1].startRefresh : p2.back().end;
    s.endRefresh = s.song[1].stopRefresh;
    return s;
}

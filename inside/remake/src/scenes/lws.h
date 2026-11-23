// The LightWave scene reader of the flower part (inside/re/notes-flower.md 3 and 7, notes-engine.md "The LightWave
// scene reader"): 0x408030 takes the .lws text, 0x408480 parses it (keyword search 0x409050, numbers 0x408d70 /
// 0x408dfc / 0x408f9c), 0x408588 samples every motion once per LightWave frame with the cardinal spline 0x4090cc into
// rows of 9 floats, and 0x4080d8 evaluates a motion at a fractional frame with the same spline over the rows.
//
// The arithmetic is the original's: float32 where it stores floats (the numbers it reads, the rows, the spline's t
// and results), doubles in between (the x87 runs at 53 bits), in its operation order, so the rows are those of the
// original's table 0x426618 (inside/re/models/engine/lws.py checks the same model against memory). Keys keep only
// x y z H P B sx sy sz and the frame: their linear / tension / continuity / bias values are read and ignored, every
// key is a plain spline point. The keywords are passed in (the caller reads them from INSIDE.EXE).
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace inside::lws {

// The words 0x408480 looks for, in INSIDE.EXE at 0x42286c.. ("LWSC", "FirstFrame", "LastFrame", "ObjectMotion",
// "CameraMotion").
struct Keywords {
    std::string scene, firstFrame, lastFrame, objectMotion, cameraMotion;
};

// One sample of a motion as 0x408588 stores it: x * scale, -y * scale, z * scale, pitch, heading, bank (radians
// through the code's degree factor), sx, sy, sz.
struct Row {
    float v[9] = {};
};

struct Table {
    std::vector<std::vector<Row>> motions;   // 0: the camera, then every ObjectMotion in file order; rows 0..N + 2
    int frames = 0;                          // N: the camera motion's samples (rows 1..N) = [0x4b6858]
};

// 0x4090cc: cardinal spline through p1 (t = 0) and p2 (t = 1), a = (1 - c) / 2 (c = 0: Catmull-Rom).
inline double spline(float tf, float c, float p0, float p1, float p2, float p3) {
    double t = tf, a = (1.0 - double(c)) * 0.5, t2 = t * t, t3 = t2 * t;
    return double(p1) * ((2.0 - a) * t3 + (a - 3.0) * t2 + 1.0) + a * double(p0) * (2.0 * t2 - t3 - t) +
           double(p2) * ((a - 2.0) * t3 + (3.0 - 2.0 * a) * t2 + a * t) + a * double(p3) * (t3 - t2);
}

// The text cursor [0x4b6850] and its readers.
class Cursor {
public:
    explicit Cursor(std::string text) : s_(std::move(text)) {}
    size_t pos = 0;

    // 0x409050: to the end of the next occurrence of the word; false at the end of the text.
    bool find(const std::string& w) {
        size_t i = s_.find(w, pos);
        if (i == std::string::npos) { pos = s_.size(); return false; }
        pos = i + w.size();
        return true;
    }
    // 0x408d70: skip to a digit or '-', then [-]digits.
    int integer() {
        int c = skipToNumber(), sign = 1;
        if (c == '-') { sign = -1; c = next(); }
        int v = 0;
        do {
            v = v * 10 + (c - '0');
            c = next();
        } while (digit(c));
        return sign * v;
    }
    // 0x408dfc: [-]digits[.digits][e[-]ddd]; the mantissa and the fraction's divisor accumulate in float32.
    float real() {
        int c = skipToNumber();
        float sign = 1.0f, ip = 0.0f, div = 1.0f;
        double e = 0.0;
        if (c == '-') { sign = -1.0f; c = next(); }
        while (c != '.' && c != 'e') {
            ip = float(double(ip) * 10.0 + (c - '0'));
            c = next();
            if (c == 'e' || c == '.' || digit(c)) continue;
            return float(double(sign) * double(ip));
        }
        if (c == 'e') return float(double(sign) * double(ip) * std::pow(10.0, exponent()));
        c = next();
        while (digit(c)) {
            ip = float(double(ip) * 10.0 + (c - '0'));
            div = float(double(div) * 10.0);
            c = next();
        }
        if (c == 'e') e = exponent();
        return float(double(sign) * double(ip) / double(div) * std::pow(10.0, e));
    }

private:
    std::string s_;
    static bool digit(int c) { return c >= '0' && c <= '9'; }
    int next() {
        if (pos >= s_.size() + 16) throw std::runtime_error("lws: number past the end of the text");
        return pos < s_.size() ? (unsigned char)s_[pos++] : (pos++, 0);
    }
    int skipToNumber() {
        int c;
        do c = next();
        while (c != '-' && !digit(c));
        return c;
    }
    double exponent() {   // 0x408f9c: a sign and exactly three digits
        int c = next(), sign = 1;
        if (c == '-') { sign = -1; c = next(); }
        int d1 = c - '0', d2 = next() - '0', d3 = next() - '0';
        pos++;
        return sign * (d1 * 100 + d2 * 10 + d3);
    }
};

// 0x408588: one motion (9 channels, n keys of 14 numbers), sampled at every frame from `first` to its last key.
inline std::vector<Row> readMotion(Cursor& cur, int first, int last, float scale, float c, double deg, int& frames) {
    struct Key {
        float v[10];   // x y z H P B frame sx sy sz
    };
    if (cur.integer() != 9) throw std::runtime_error("lws: a motion without 9 channels");
    int n = cur.integer();
    std::vector<Key> slots(1);
    for (int i = 0; i < n; i++) {
        float v[9];
        for (float& x : v) x = cur.real();
        float frame = cur.real();
        for (int k = 0; k < 4; k++) cur.real();   // linear, tension, continuity, bias: ignored
        slots.push_back({{v[0], v[1], v[2], v[3], v[4], v[5], frame, v[6], v[7], v[8]}});
    }
    if (n < 1) throw std::runtime_error("lws: a motion without keys");
    if (int(std::trunc(slots[1].v[6])) != first) {   // a copy of the first key at FirstFrame, before it
        Key k = slots[1];
        k.v[6] = float(first);
        slots.insert(slots.begin() + 1, k);
        n++;
    }
    if (int(std::trunc(slots[size_t(n)].v[6])) != last) {   // a copy of the last key at LastFrame
        Key k = slots[size_t(n)];
        k.v[6] = float(last);
        slots.push_back(k);
        n++;
    }
    for (int i = 0; i < 3; i++) {   // three copies of the last key at its frame + 1
        Key k = slots[size_t(n)];
        k.v[6] = float(double(slots[size_t(n)].v[6]) + 1.0);
        slots.push_back(k);
    }
    slots[0] = slots[1];
    std::vector<Row> rows(1);
    double s = double(scale);
    for (int j = 1; j <= n; j++) {   // segment (slot j, slot j + 1) with the neighbours j - 1 and j + 2
        const Key &k0 = slots[size_t(j - 1)], &k1 = slots[size_t(j)], &k2 = slots[size_t(j + 1)],
                  &k3 = slots[size_t(j + 2)];
        int steps = int(std::trunc(float(double(k2.v[6]) - double(k1.v[6]))));
        if (steps <= 0) continue;
        float dt = float(1.0 / steps), t = 0.0f;
        for (int i = 0; i < steps; i++) {
            auto ch = [&](int m) { return spline(t, c, k0.v[m], k1.v[m], k2.v[m], k3.v[m]); };
            rows.push_back({{float(ch(0) * s), float(ch(1) * -s), float(ch(2) * s), float(ch(4) * deg),
                             float(ch(3) * deg), float(ch(5) * deg), float(ch(7)), float(ch(8)), float(ch(9))}});
            t = float(double(t) + double(dt));
        }
    }
    if (rows.size() < 2) throw std::runtime_error("lws: a motion without samples");
    rows[0] = rows[1];
    frames = int(rows.size()) - 1;
    Row lastRow = rows.back();
    rows.push_back(lastRow);
    rows.push_back(lastRow);
    return rows;
}

// 0x408030 / 0x408480: the camera motion first, then every ObjectMotion after LastFrame. `deg` is the code's degree
// factor ([0x42149d]), `c` the spline tension ([0x422868]).
inline Table read(const std::string& text, const Keywords& kw, float scale, float c, double deg) {
    Cursor cur(text);
    if (!cur.find(kw.scene)) throw std::runtime_error("lws: not a scene file");
    cur.find(kw.firstFrame);
    int first = cur.integer();
    cur.find(kw.lastFrame);
    int last = cur.integer();
    size_t mark = cur.pos;
    Table t;
    if (!cur.find(kw.cameraMotion)) throw std::runtime_error("lws: no camera motion");
    t.motions.push_back(readMotion(cur, first, last, scale, c, deg, t.frames));
    cur.pos = mark;
    while (cur.find(kw.objectMotion)) {
        int frames = 0;
        t.motions.push_back(readMotion(cur, first, last, scale, c, deg, frames));
    }
    return t;
}

// 0x4080d8 for one motion at the frame f (float [0x4b685c]): the spline over rows k .. k + 3, k = trunc(f) % N.
inline Row evaluate(const std::vector<Row>& rows, int frames, float f, float c) {
    int i = int(std::trunc(f));
    int k = i % frames;
    if (k < 0) k += frames;
    float t = float(double(f) - double(i));
    k = std::min(k, int(rows.size()) - 4);
    Row r;
    for (int m = 0; m < 9; m++)
        r.v[m] = float(spline(t, c, rows[size_t(k)].v[m], rows[size_t(k + 1)].v[m], rows[size_t(k + 2)].v[m],
                              rows[size_t(k + 3)].v[m]));
    return r;
}

}  // namespace inside::lws

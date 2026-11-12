// Shared by p3 ("7800 PHONG polys") and p5 ("phong ENV mapping", "BUMP glow"), dope/re/notes-p3p5.md: their main
// scripts (p3 2c22, p5 30e2) read from the part images, and the camera slot's per-retrace motion they drive.
//
// Both scripts are a straight list of `mov word / dword ptr cs:[a], imm` (velocities, angles, positions, the label
// speed, p5's bump speed, material add and label segment), `mov cx, N` and `call run` (run N retraces: p3 2ba4,
// p5 2b66), ended by any other call (p3's exit 1b63, p5's hand-over 168f). ScriptLog runs that list once at start-up
// and keeps every write with the number of retraces done before it (notes-p3p5 3.2 and 4.2 list the same steps), so
// none of the scripts' values are in this source. The per-retrace routines (p3 1f60, p5 1a70) add the velocities
// once per retrace: dope::Integrator, exact at whole retraces and linear between them.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"

namespace p3p5 {

// One write of the script: cs:[addr] = value (size 2 or 4 bytes) before the call that starts the step at k.
struct ScriptWrite {
    int k;
    uint16_t addr;
    uint32_t value;
};

struct ScriptLog {
    std::vector<ScriptWrite> writes;
    int retraces = 0;   // the sum of the steps' N: the part's length in retraces

    // Decode the script from cs:from; `run` is the cs offset of the "run N retraces" routine.
    static ScriptLog read(const DopeImage& img, uint16_t from, uint16_t run) {
        ScriptLog s;
        int cx = 0;
        uint16_t ip = from;
        for (int guard = 0; guard < 4096; guard++) {
            uint8_t op = img.cs8(ip);
            if (op == 0x2e && img.cs8(uint16_t(ip + 1)) == 0xc7 && img.cs8(uint16_t(ip + 2)) == 0x06) {
                // 2E C7 06 a16 imm16: mov word ptr cs:[a], imm16
                s.writes.push_back({s.retraces, img.cs16(uint16_t(ip + 3)), img.cs16(uint16_t(ip + 5))});
                ip = uint16_t(ip + 7);
            } else if (op == 0x66 && img.cs8(uint16_t(ip + 1)) == 0x2e && img.cs8(uint16_t(ip + 2)) == 0xc7 &&
                       img.cs8(uint16_t(ip + 3)) == 0x06) {
                // 66 2E C7 06 a16 imm32: mov dword ptr cs:[a], imm32
                s.writes.push_back({s.retraces, img.cs16(uint16_t(ip + 4)), img.cs32(uint16_t(ip + 6))});
                ip = uint16_t(ip + 10);
            } else if (op == 0xb9) {   // mov cx, imm16
                cx = img.cs16(uint16_t(ip + 1));
                ip = uint16_t(ip + 3);
            } else if (op == 0xe8) {   // call rel16: a step, or the end of the script
                uint16_t to = uint16_t(ip + 3 + img.cs16(uint16_t(ip + 1)));
                if (to != run) break;
                s.retraces += cx;
                ip = uint16_t(ip + 3);
            } else if (op == 0x0e || op == 0x1f) {   // push cs / pop ds
                ip = uint16_t(ip + 1);
            } else {
                break;
            }
        }
        return s;
    }

    // The writes to one word (or dword) of the image, in order: (k, value).
    template <class F> void each(uint16_t addr, F&& f) const {
        for (const ScriptWrite& w : writes)
            if (w.addr == addr) f(w.k, w.value);
    }
    // The value of a word after k retraces' worth of script (the image's value before the first write).
    uint32_t valueAt(uint16_t addr, int k, uint32_t initial) const {
        uint32_t v = initial;
        for (const ScriptWrite& w : writes)
            if (w.addr == addr && w.k <= k) v = w.value;
        return v;
    }
};

// The camera slot (object 0) moved by its velocities once per retrace: positions +06 / +0a / +0e (s32, velocities
// +12 / +16 / +1a), angles +1e / +20 / +22 (u16, velocities +24 / +26 / +28, s16), with the script's writes.
struct Camera {
    dope::Integrator x, y, z, a, b, c;

    void build(const DopeImage& img, uint16_t rec, const ScriptLog& s) {
        const dope::ObjectRecord r = dope::ObjectRecord::read(img, rec);
        dope::Integrator* pos[3] = {&x, &y, &z};
        dope::Integrator* ang[3] = {&a, &b, &c};
        const double p0[3] = {r.pose.x, r.pose.y, r.pose.z}, v0[3] = {r.vel.x, r.vel.y, r.vel.z};
        const double a0[3] = {r.pose.a, r.pose.b, r.pose.c}, w0[3] = {r.vel.a, r.vel.b, r.vel.c};
        for (int i = 0; i < 3; i++) {
            *pos[i] = dope::Integrator{p0[i], v0[i]};
            *ang[i] = dope::Integrator{a0[i], w0[i]};
        }
        for (const ScriptWrite& w : s.writes) {   // in script order (Integrator wants its sets in order of k)
            for (int i = 0; i < 3; i++) {
                uint16_t o = uint16_t(4 * i), h = uint16_t(2 * i);
                if (w.addr == rec + 0x06 + o) pos[i]->value(w.k, double(int32_t(w.value)));
                if (w.addr == rec + 0x12 + o) pos[i]->velocity(w.k, double(int32_t(w.value)));
                if (w.addr == rec + 0x1e + h) ang[i]->value(w.k, double(uint16_t(w.value)));
                if (w.addr == rec + 0x24 + h) ang[i]->velocity(w.k, double(int16_t(w.value)));
            }
        }
    }
    // After r per-retrace calls, linear between whole calls (a value the script sets stays on its retrace).
    dope::Pose at(double r) const { return {x.at(r), y.at(r), z.at(r), a.at(r), b.at(r), c.at(r)}; }
};

// The drawn object after r per-retrace calls: its record's position and angles plus its own velocities (zero for
// p3's object 4 and p5's object 1: only the camera slot moves), the camera slot's added by the engine.
inline dope::Object objectAt(const dope::ObjectRecord& rec, const dope::EngineMesh* mesh, double r) {
    dope::Object o = dope::Object::from(rec, mesh);
    const dope::Pose& v = rec.vel;
    o.pose = rec.pose + dope::Pose{v.x * r, v.y * r, v.z * r, v.a * r, v.b * r, v.c * r};
    return o;
}

// The label slide (p3 [19c4] / [19c6], p5 [1558] / [155a]): the image's position, then the script's writes.
inline dope::LabelSlide labelSlide(const DopeImage& img, const ScriptLog& s, uint16_t pos, uint16_t speed) {
    dope::LabelSlide l;
    l.a0 = img.css16(pos);
    s.each(pos, [&](int k, uint32_t v) {
        if (k == 0) l.a0 = int16_t(v);   // set before the first call (p3: -1)
    });
    s.each(speed, [&](int k, uint32_t v) { l.speeds.push_back({k, int16_t(v)}); });
    return l;
}

// A script that did not decode to the length the notes give (a different DOPE.EXE would fail the hash check
// first; this only guards the decoder).
inline void checkLength(const ScriptLog& s, int expected, const char* part) {
    if (s.retraces != expected)
        std::fprintf(stderr, "%s: the script decodes to %d retraces, expected %d\n", part, s.retraces, expected);
}

}  // namespace p3p5

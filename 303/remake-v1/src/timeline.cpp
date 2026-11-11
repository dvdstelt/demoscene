#include "timeline.h"

#include <cmath>

#include "fx.h"

// Scene boundaries were measured on a capture of the original running on a Pentium MMX
// and converted to music time with V() (the capture runs 0.126% slow and starts 1.4 s in).
const std::vector<TimelineEntry>& timeline() {
    static const std::vector<TimelineEntry> t = {
        {0.0f, V(40.95f), "intro"},
        {V(40.95f), V(54.7f), "aap"},
        {V(54.7f), V(58.15f), "robot"},
        {V(58.15f), V(65.3f), "spoon"},
        {V(65.3f), V(71.65f), "alex"},
        {V(71.65f), V(81.95f), "harl"},
        {V(81.95f), V(92.5f), "web"},
        {V(92.5f), V(103.0f), "rings"},
        {V(103.0f), V(120.0f), "face"},
        {V(120.0f), V(124.9f), "facebox"},
        {V(124.9f), V(129.95f), "typo"},
        {V(129.95f), V(146.5f), "blobs"},
        {V(146.5f), V(149.9f), "wireblobs"},
        {V(149.9f), V(156.6f), "spiral"},
        {V(156.6f), V(162.95f), "vic"},
        {V(162.95f), V(169.95f), "star"},
        {V(169.95f), V(191.3f), "plasma"},
        {V(191.3f), V(197.4f), "paranoid"},
        {V(197.4f), V(208.4f), "facefloor"},
        {V(208.4f), V(217.9f), "dance"},
        {V(217.9f), V(230.6f), "icons"},
        {V(230.6f), V(253.4f), "outro"},
    };
    return t;
}

namespace {
// Lyric words are read from 303.EXE by the address of their string pointer: the cycle list at 0x57dc0 and the
// song table at 0x57f30.
constexpr uint32_t cyc(uint32_t i) { return 0x57dc0 + 4 * i; }
constexpr uint32_t song(uint32_t i) { return 0x57f30 + 4 * i; }

struct Lyric {
    float t0, t1;      // video time
    uint32_t w0, w1;   // exe words (w1 = 0: none; two words are joined by a space)
    const char* text;  // only for the lines heard on the capture that are not in the exe (w0 = 0)
    float cx, cy, h;   // centre and glyph cell height (frame units)
    float stretch;     // horizontal stretch
};

// Lyrics as they appear in the original (positions read off the capture).
const Lyric kLyrics[] = {
    {55.05f, 56.35f, cyc(0), 0, nullptr, 0.27f, 0.885f, 0.15f, 1.15f},
    {56.40f, 58.10f, cyc(1), 0, nullptr, 0.34f, 0.855f, 0.15f, 1.15f},
    {58.55f, 59.75f, cyc(2), 0, nullptr, 0.42f, 0.86f, 0.19f, 1.25f},
    {59.90f, 61.10f, cyc(3), 0, nullptr, 0.76f, 0.52f, 0.16f, 1.25f},
    {61.25f, 62.60f, 0, 0, "IT MOVES", 0.40f, 0.17f, 0.15f, 1.2f},
    {62.80f, 64.80f, 0, 0, "ON EARTH", 0.42f, 0.80f, 0.42f, 1.15f},
    {71.75f, 73.90f, cyc(7), 0, nullptr, 0.55f, 0.53f, 0.13f, 1.15f},
    {73.95f, 75.20f, cyc(8), 0, nullptr, 0.73f, 0.80f, 0.13f, 1.15f},
    {75.20f, 76.75f, cyc(9), 0, nullptr, 0.40f, 0.17f, 0.13f, 1.15f},
    {76.80f, 78.60f, cyc(0), 0, nullptr, 0.63f, 0.26f, 0.13f, 1.15f},
    {78.60f, 80.60f, cyc(1), 0, nullptr, 0.17f, 0.17f, 0.13f, 1.15f},
    {80.60f, 81.90f, cyc(2), 0, nullptr, 0.60f, 0.29f, 0.13f, 1.15f},
    {96.45f, 97.40f, 0, 0, "NO REGRETS", 0.45f, 0.62f, 0.12f, 1.15f},
    {97.40f, 98.55f, song(1), 0, nullptr, 0.66f, 0.13f, 0.12f, 1.15f},
    {98.55f, 99.70f, song(2), 0, nullptr, 0.30f, 0.86f, 0.12f, 1.15f},
    {99.70f, 100.75f, song(3), 0, nullptr, 0.87f, 0.27f, 0.12f, 1.15f},
    {100.75f, 101.60f, song(4), 0, nullptr, 0.45f, 0.30f, 0.40f, 1.1f},
    {101.60f, 102.80f, song(4), 0, nullptr, 0.58f, 0.12f, 0.12f, 1.15f},
    {210.10f, 211.60f, song(22), 0, nullptr, 0.80f, 0.60f, 0.13f, 1.15f},
    {211.60f, 212.70f, song(23), 0, nullptr, 0.20f, 0.27f, 0.13f, 1.15f},
    {212.70f, 213.80f, song(24), song(25), nullptr, 0.62f, 0.70f, 0.13f, 1.15f},
    {213.80f, 215.30f, song(26), 0, nullptr, 0.55f, 0.25f, 0.13f, 1.15f},
    {215.30f, 216.30f, song(27), 0, nullptr, 0.32f, 0.50f, 0.13f, 1.15f},
    {216.30f, 217.60f, song(28), 0, nullptr, 0.52f, 0.27f, 0.13f, 1.15f},
};

std::string lyricText(const Ctx& c, const Lyric& l) {
    if (!l.w0) return l.text;
    std::string s = c.exe.str(l.w0);
    if (l.w1) s += " " + c.exe.str(l.w1);
    return s;
}
}  // namespace

void drawOverlays(Ctx& c, float T) {
    const float t = VT(T);
    setBlend(Blend::Alpha);
    for (const Lyric& l : kLyrics) {
        if (t < l.t0 || t > l.t1) continue;
        const std::string text = lyricText(c, l);
        float in = smoothstep(l.t0, l.t0 + 0.12f, t);
        float out = 1.0f - smoothstep(l.t1 - 0.15f, l.t1, t);
        float a = in * out;
        // arrives with a short vertical smear, like the blurred rows of the original font
        float smear = (1.0f - in) * 0.6f;
        // the original shows the font stretched vertically (320x200 pixels are tall)
        const float h = l.h * 1.3f, st = l.stretch * 0.66f;
        for (int g = 3; g >= 1; g--) {
            float dy = g * 0.012f * (0.3f + smear * 4.0f);
            c.drawText("lyrics", text, l.cx, l.cy - dy, h, a * 0.12f, Vec3(1, 1, 1), 0.0f, st);
            c.drawText("lyrics", text, l.cx, l.cy + dy, h, a * 0.12f, Vec3(1, 1, 1), 0.0f, st);
        }
        c.drawText("lyrics", text, l.cx + 0.004f, l.cy + 0.006f, h, a * 0.55f, Vec3(0, 0, 0), 0.0f, st);
        c.drawText("lyrics", text, l.cx, l.cy, h, a, Vec3(1, 1, 1), 0.0f, st);
    }
}

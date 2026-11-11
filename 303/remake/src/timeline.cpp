#include "timeline.h"

#include <cmath>

#include "fx.h"

// Scene boundaries were measured on a capture of the original running on a Pentium MMX
// and converted to music time with V() (the capture runs 0.126% slow and starts 1.4 s in).
const std::vector<TimelineEntry>& timeline() {
    static const std::vector<TimelineEntry> t = {
        {0.0f, V(40.95f), "intro"},
        {V(40.95f), V(54.7f), "aap"},
        {V(54.7f), V(65.3f), "blobby"},
        {V(65.3f), V(71.65f), "alex"},
        {V(71.65f), V(81.95f), "harl"},
        {V(81.95f), V(92.39f), "web"},
        {V(92.39f), V(102.70f), "rings"},
        {V(102.70f), V(119.51f), "face"},
        {V(119.51f), V(124.67f), "facebox"},
        {V(124.67f), V(129.60f), "typo"},
        {V(129.60f), V(146.44f), "blobs"},
        {V(146.44f), V(149.90f), "wireblobs"},
        {V(149.90f), V(156.6f), "spiral"},
        {V(156.6f), V(162.95f), "vic"},
        {V(162.95f), V(169.95f), "star"},
        {V(169.95f), V(191.3f), "plasma"},
        {V(191.3f), V(197.4f), "paranoid"},
        {V(197.4f), V(208.9f), "facefloor"},
        {V(208.9f), V(217.85f), "dance"},
        {V(217.85f), V(230.6f), "icons"},
        {V(230.6f), V(253.4f), "outro"},
    };
    return t;
}

// All lyrics are drawn by the parts themselves with the exe's lyric renderer (LyricLayer in exefx.h:
// cycle mode in blobby and harl, song mode in rings and the dance), so there is no overlay left.
void drawOverlays(Ctx&, float) {}

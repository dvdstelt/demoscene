// The demo's running order (music time in seconds) and the text overlays (lyrics, credits).
#pragma once
#include <string>
#include <vector>

struct Ctx;

struct TimelineEntry {
    float start, end;
    std::string scene;
};

const std::vector<TimelineEntry>& timeline();
void drawOverlays(Ctx& c, float T);

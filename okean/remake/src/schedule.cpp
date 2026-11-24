// The intro's clocks (schedule.h; okean/re/notes-core.md section 4, notes-music.md section 8): the tick times with
// the held and lost ticks of the parts' timer calibrations, the parts' exits (main loop 0x101c1.. calls them in
// turn), and the end part's fade of the master volume.
#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "demo.h"

namespace {

constexpr double kT = 1.0 / kRefreshHz;

enum class Exit { Order, MorphFade, SheetFade, EndFade };
struct PartRule {
    const char* scene;
    uint32_t entry;
    Exit exit;
    int order;      // Exit::Order: the order the main loop waits for
    bool clears;    // the set-up clears video memory (black until the first frame)
};
// clang-format off
constexpr PartRule kParts[] = {
    {"title",     0x2c26f, Exit::Order,      1, true},    // 0x2c458: order == 1
    {"objects",   0x2e779, Exit::Order,      7, true},
    {"web",       0x3554b, Exit::Order,     11, false},
    {"morph",     0x367e5, Exit::MorphFade,  0, false},   // notes-morph-star.md 2.4: fade == 0
    {"landscape", 0x38520, Exit::Order,     19, true},
    {"star",      0x3c8da, Exit::Order,     23, false},   // part 5's last frame, its DAC black: shows black
    {"sheet",     0x5334f, Exit::SheetFade,  0, true},    // notes-sheet.md: b == 0
    {"end",       0x4d370, Exit::EndFade,    0, false},   // notes-core.md 7: fade == 0, then text mode
};
// clang-format on

// The interrupts of GUS timer 2 (expiries every 19.84 ms from program time 0) as they are served, given the windows
// in which interrupts are off: the first expiry in a window is served at its end, the others are lost.
struct Ticks {
    struct Window { double a, b; bool used = false; };
    std::vector<Window> windows;
    std::vector<double> time;   // served time of player tick j
    const SongTicks* song = nullptr;
    long m = 0;                 // last expiry looked at
    long served = 0;            // interrupts served

    void until(double t) {      // every expiry up to grid time t
        while (double(m + 1) * kTickSeconds <= t) {
            m++;
            double tm = double(m) * kTickSeconds, at = tm;
            bool lost = false;
            for (Window& w : windows)
                if (tm >= w.a && tm < w.b) {
                    if (w.used) lost = true;
                    w.used = true;
                    at = w.b;
                }
            if (lost) continue;
            served++;
            if (served >= 4) time.push_back(at);   // interrupts 1..3 come before the player starts
        }
    }
    // The song position at program time t (-1 before the first tick).
    int pos(double t) {
        until(t);
        auto it = std::upper_bound(time.begin(), time.end(), t);
        if (it == time.begin()) return -1;
        size_t j = size_t(it - time.begin()) - 1;
        if (j >= song->pos.size()) throw std::runtime_error("schedule: the song is shorter than the intro");
        return song->pos[j];
    }
};

double at(int refresh, double line) { return (double(refresh) + line * kLine) * kT; }

}  // namespace

IntroSchedule buildSchedule(const SongTicks& song) {
    IntroSchedule s;
    Ticks ticks;
    ticks.song = &song;
    // the title's calibration: from refresh 3 line 106 (its set-up, the start of the player) to refresh 4 line 504
    ticks.windows.push_back({at(3, 106), at(4, kCopyLine)});
    int enter = 0, first = 5;
    for (const PartRule& rule : kParts) {
        PartSchedule p;
        p.scene = rule.scene;
        p.entry = rule.entry;
        p.enter = enter;
        p.first = first;
        p.clears = rule.clears;
        int fade = 64, b = 1, endFade = 32, volume = 63;
        for (int k = first;; k++) {
            if (k > first + 100000) throw std::runtime_error("schedule: a part never ends");
            int cb = ticks.pos(at(k, kCallbackLine));   // the position the callback of refresh k sees
            int order = cb / 64, row = cb % 64;
            bool done = false;
            switch (rule.exit) {
                case Exit::Order:
                    done = ticks.pos(at(k, kCopyLine)) / 64 == rule.order;
                    break;
                case Exit::MorphFade:      // callback 0x36af4
                    if (order == 15) {
                        if (row == 5) fade = 48;
                        if (fade) fade--;
                    }
                    done = fade == 0;
                    break;
                case Exit::SheetFade:      // callback 0x5355a
                    if (order != 27 && b != 64) b++;
                    if (order == 27 && b != 0) b--;
                    done = b == 0;
                    break;
                case Exit::EndFade:        // callback 0x4d645
                    if (row >= 32 && order == 30) {
                        if (endFade) {
                            endFade--;
                            if (volume > 0) s.music.master.push_back({at(k, kCallbackLine), --volume});
                        }
                    } else if (endFade != 32) {
                        endFade++;
                    }
                    done = endFade == 0;
                    break;
            }
            if (done) {
                p.end = k;
                break;
            }
        }
        if (rule.exit == Exit::EndFade) {
            // the loop leaves before its copy (the last record is the refresh before); main stops the music and sets
            // text mode right after the retrace
            s.exitRefresh = p.end;
            s.music.stopTime = at(s.exitRefresh, kCopyLine);
        } else {
            // the next part's calibration: interrupts off from the exit test to line 504 two refreshes later
            ticks.windows.push_back({at(p.end, kCopyLine), at(p.end + 2, kCopyLine)});
        }
        enter = p.end;
        first = p.end + 3;
        s.parts.push_back(p);
    }
    ticks.until(s.music.stopTime);
    for (double t : ticks.time)
        if (t <= s.music.stopTime) s.music.tickTime.push_back(t);
    return s;
}

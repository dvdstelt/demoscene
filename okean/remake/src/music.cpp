// The soundtrack (music.h). PROVISIONAL: the positions from the module's speed, break and jump commands, no sound; the
// port of the intro's player and GUS driver (okean/re/notes-music.md) replaces it.
#include "music.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "demo.h"
#include "exe.h"

namespace {
constexpr uint32_t kModule = 0xae0;      // the MOD in code32 (okean/re/NOTES.md "Layout")
}  // namespace

int Soundtrack::positionAtTime(double tp) const {
    auto it = std::upper_bound(rowTime.begin(), rowTime.end(), tp);
    if (it == rowTime.begin()) return -1;
    return rowPos[size_t(it - rowTime.begin() - 1)];
}

int Soundtrack::positionAt(double refresh) const { return positionAtTime((refresh + 1.0) / kRefreshHz); }

double Soundtrack::timeOfPosition(int pos, double after) const {
    for (size_t i = 0; i < rowTime.size(); i++)
        if (rowPos[i] >= pos && rowTime[i] >= after) return rowTime[i];
    return std::numeric_limits<double>::infinity();
}

double Soundtrack::refreshOf(double t) const { return t * kRefreshHz - 1.0; }
double Soundtrack::timeOf(double refresh) const { return (refresh + 1.0) / kRefreshHz; }

SongTicks songTicks(const OkeanExe& exe, int ticks) {
    SongTicks s;
    const uint32_t m = kModule;
    if (exe.u32(m + 1080) != 0x4e484338u) throw std::runtime_error("the module is not an 8-channel MOD");
    const int channels = 8;
    const int songLen = exe.u8(m + 950);
    const uint32_t pat0 = m + 1084;
    int speed = 6, order = 0, row = 0;
    while (int(s.pos.size()) < ticks) {
        int pat = exe.u8(m + 952 + uint32_t(order % songLen));
        int nextOrder = order + 1, nextRow = 0;
        bool jump = false;
        int delay = 0;
        for (int c = 0; c < channels; c++) {
            uint32_t a = pat0 + uint32_t(((pat * 64 + row) * channels + c) * 4);
            int fx = exe.u8(a + 2) & 15, arg = exe.u8(a + 3);
            if (fx == 0xf && arg && arg < 32) speed = arg;
            else if (fx == 0xb) { nextOrder = arg; nextRow = 0; jump = true; }
            else if (fx == 0xd) { if (!jump) nextOrder = order + 1; nextRow = (arg >> 4) * 10 + (arg & 15); jump = true; }
            else if (fx == 0xe && (arg >> 4) == 0xe) delay = arg & 15;
        }
        for (int k = 0; k < speed * (1 + delay); k++) {
            s.pos.push_back(order * 64 + row);
            s.newRow.push_back(k == 0 ? 1 : 0);
        }
        if (jump) { order = nextOrder; row = nextRow; }
        else if (++row == 64) { row = 0; order++; }
    }
    s.pos.resize(size_t(ticks));
    s.newRow.resize(size_t(ticks));
    return s;
}

Soundtrack renderSoundtrack(const OkeanExe&, const SongTicks& song, const MusicSchedule& sch, int rate) {
    Soundtrack s;
    s.rate = rate;
    s.song = song;
    s.schedule = sch;
    s.endTime = sch.stopTime;
    for (size_t j = 0; j < sch.tickTime.size() && j < song.pos.size(); j++)
        if (song.newRow[j]) {
            s.rowTime.push_back(sch.tickTime[j]);
            s.rowPos.push_back(song.pos[j]);
        }
    s.pcm.assign(size_t(std::ceil(sch.stopTime * rate)) * 2, 0);
    return s;
}

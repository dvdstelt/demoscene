// p2: the credits, the two discs and the Complex "C", motion blur (dope/re/notes-p2.md). Two blue pentagonal discs
// swing around the vertical axis behind a band of credits ("jmagic code", "jugi music", "jugi graphics", "dope")
// that fade in and cross-fade; the discs slide onto the axis, the "C" appears between them in front of the
// pre-rendered stand, the camera flies in and rolls, the label "phong BLUR motion" slides in, the blur dies away and
// grows back, and the picture fades to white.
//
// Ports: the script 0010:377f..43ed (153 "run N retraces" steps, decoded from the image at start-up: every value it
// writes is read from its instruction), the per-retrace routine 2580 (the camera record 1810's motion, the label
// slide [1955] / [1957], the white fade [193f] / [1941]), the frame build 354a / 348b / 3585 / 3310 through the
// shared engine (the discs 184a with mesh 1e71 and the bump map 23d1 through the table at 0010:0000, the "C" 1884
// with mesh 2121; the env maps 6538 / 6540 by the selectors [194f] / [194d] and the records' +2a), and the frame copy
// 360c (credit band, blend, label, copy; p2_blur.cpp).
//
// Timing: the step starts are the capture's (notes-p2 9.2: steps 99..152 start up to 12 retraces late). Measured in
// the capture, the frame copy 360c made after call m (after its blend of 64000 pixels) reaches record start + m,
// while the DAC writes of call m (the white fade) show in record start + m - 1: so at r = c.retraces(t) the screen
// shows the frame built after r - 1 calls (the camera record's state then, continuous), its label at the position
// after r calls, and the fade after r + 1 calls (one retrace later than dope/re/PORT.md's r and r + 1 for the engine
// parts). What the script switches (pivot, "C", backdrop, env maps, band, blur weight) applies from the step's first
// frame. The blend of the original's frame n is spread over the frame clock (n - 1, n] (P2Blur), so at whole r = n + 1
// the screen is what record start + n + 1 shows.
//
// Evaluated exactly instead of the original's shortcuts (besides the engine's, engine.h, and the blend's, p2_blur.cpp):
//   - the credit text's grey-ramp blend y' = (3 x + 13 y) / 16 per frame (1998, whose integer steps stop white text
//     a few shades short of white) is the closed form of that recursion for each strip and band position: the
//     screen shows sum_k c_k(f) x_k with c_k a difference of two exponentials of the frame clock f, x_k the strip's
//     coverage mask, through the palette's grey ramp up to full white;
//   - the white fade's DAC table (2550, +1 per retrace) is continuous between retraces.
// Kept: the "C" drawn with f = 1300 against the discs' 4000 and sorted with its depth times 1300 / 4000 (notes-p2
// 7.2), the backdrop from the frame after step 25 starts (360c restores it after the copy), the label's black trail,
// the band hiding the scene while it is on, the black screen row 199, the bump noise from the "C" mesh bytes the
// discs read at one corner (the bump map 23d1 read as it is), the frames of white before p3.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <SDL3/SDL.h>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"
#include "scenes/p2.h"

using namespace dope;

namespace {

// ------------------------------------------------------------------------------------------------- the script
// 0010:37ec .. 43ed: after the set-up (mode 13h, the DAC, the int 8 handler 2580, the timer calibration 1a5f), up
// to the exit `call 1b17`.
constexpr uint16_t kScriptBegin = 0x37ec, kScriptEnd = 0x43ed;
constexpr uint16_t kRunRetraces = 0x371e;   // run CX retraces (the frame loop)
constexpr uint16_t kBlendTable = 0x1967;    // build the scene part of the blend table for s = [1965]
constexpr uint16_t kBlendSeg = 0x3527;      // 3a5d: push 3527h ... rep stosd: the grey rows of the table to 0
// object records (0x3a bytes) and their fields
constexpr uint16_t kCamera = 0x1810, kDiscs = 0x184a, kCee = 0x1884;
constexpr uint16_t kRecOff = 0x02, kRecF = 0x04, kRecPos = 0x06, kRecVel = 0x12, kRecAng = 0x1e, kRecAngVel = 0x24,
                   kRecAttr = 0x2a, kRecPivot = 0x2e;
// variables
constexpr uint16_t kStrip = 0x1904;     // the credit strip: -2 none, -1 zeros, 0 grey 0, n the strip n
constexpr uint16_t kBand = 0x3626;      // the band's byte offset in the render buffer (the immediate of 3625)
constexpr uint16_t kBlendS = 0x1965;    // the blur weight s
constexpr uint16_t kLabelPos = 0x1955, kLabelDir = 0x1957;
constexpr uint16_t kBackdrop = 0x1947;  // 1: the render buffer is restored to the backdrop 0ec3 after each copy
constexpr uint16_t kFadeOn = 0x193f;    // 1: the white fade runs in 2580
constexpr uint16_t kEnvSel = 0x194f, kTexSel = 0x194d;   // the global selector adds (bytes)

// notes-p2 9.2, measured in the capture (stats.py): steps 99.. start late, by (first step, retraces)
constexpr int kDelays[][2] = {{99, 1},   {101, 2},  {104, 3},  {119, 4},  {136, 5},  {137, 6},
                              {138, 7},  {140, 8},  {142, 9},  {145, 10}, {146, 11}, {148, 12}};
// the last step ends after its N-th retrace + 1 (retrace 7309, the capture's frame in progress), then the exit 43ed
// writes 63 to the whole DAC
constexpr int kLastOvershoot = 1;
// 1998: grey' = (x * 60h + grey * 1A0h) / 200h: the text level moves 60h / 200h of the way to the strip per frame
constexpr double kTextRate = double(0x60) / double(0x60 + 0x1a0);
// 360c: the band is 32 rows of 320 bytes (rep movsd of 2800h bytes)
constexpr int kBandRows = 0x2800 / 320;

struct Step {
    int n = 0, start = 0;    // retraces; the measured start
    std::set<uint16_t> written;
    bool clear = false;      // 3a5d ran before this step
    int s = 0;               // the blend table's weight (the [1965] of the last call 1967)
    int strip = 0, bandRow = 0;
    bool ceeShown = false, backdrop = false, fade = false;
    int discsF = 0, ceeF = 0, discsAttr = 0, ceeAttr = 0, envSel = 0, texSel = 0;
    double pivot = 0;
    int labelDir = 0;
};

// The script's memory: values written so far, else the image's.
struct Mem {
    const DopeImage& img;
    std::map<uint16_t, int32_t> v;
    int32_t s16(uint16_t a) const { auto it = v.find(a); return it != v.end() ? int16_t(it->second) : img.css16(a); }
    int32_t s32(uint16_t a) const { auto it = v.find(a); return it != v.end() ? it->second : img.css32(a); }
};

// Decodes the script: the instruction forms it is made of (mov word / dword cs:[a], imm; mov cx, imm; call; dec word
// cs:[a]; the table clear at 3a5d). Returns false on anything else.
bool decodeScript(const DopeImage& img, std::vector<Step>& steps, std::vector<Mem>& states) {
    Mem mem{img, {}};
    Step cur;
    int cx = 0, s = 0;
    uint16_t pc = kScriptBegin;
    auto b = [&](int o) { return img.cs8(uint16_t(pc + o)); };
    while (pc < kScriptEnd) {
        if (b(0) == 0x2e && b(1) == 0xc7 && b(2) == 0x06) {   // mov word ptr cs:[a], imm16
            uint16_t a = img.cs16(uint16_t(pc + 3));
            mem.v[a] = img.css16(uint16_t(pc + 5));
            cur.written.insert(a);
            pc += 7;
        } else if (b(0) == 0x66 && b(1) == 0x2e && b(2) == 0xc7 && b(3) == 0x06) {   // mov dword ptr cs:[a], imm32
            uint16_t a = img.cs16(uint16_t(pc + 4));
            mem.v[a] = img.css32(uint16_t(pc + 6));
            cur.written.insert(a);
            pc += 10;
        } else if (b(0) == 0xb9) {   // mov cx, imm16
            cx = img.cs16(uint16_t(pc + 1));
            pc += 3;
        } else if (b(0) == 0x2e && b(1) == 0xff && b(2) == 0x0e) {   // dec word ptr cs:[a]
            uint16_t a = img.cs16(uint16_t(pc + 3));
            mem.v[a] = int16_t(mem.s16(a) - 1);
            cur.written.insert(a);
            pc += 5;
        } else if (b(0) == 0x68 && img.cs16(uint16_t(pc + 1)) == kBlendSeg) {   // 3a5d .. rep stosd
            int k = 3;
            while (k < 40 && !(b(k) == 0xf3 && b(k + 1) == 0x66 && b(k + 2) == 0xab)) k++;
            if (k >= 40) return false;
            cur.clear = true;
            pc = uint16_t(pc + k + 3);
        } else if (b(0) == 0xe8) {   // call rel16
            uint16_t to = uint16_t(pc + 3 + img.css16(uint16_t(pc + 1)));
            pc += 3;
            if (to == kBlendTable) {
                s = mem.s16(kBlendS);
            } else if (to == kRunRetraces) {
                cur.n = cx;
                cur.s = s;
                steps.push_back(cur);
                states.push_back(mem);
                cur = Step{};
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return !steps.empty();
}

// ------------------------------------------------------------------------------------------------- the scene
class BlurScene : public Scene, public P2Source {
public:
    ~BlurScene() override { blur_.release(); }

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(2);
        std::vector<Mem> states;
        if (!decodeScript(img, steps_, states)) {
            SDL_Log("p2: the script at 0010:%04x is not as expected", kScriptBegin);
            steps_.clear();
            return;
        }
        script(states);
        part_ = EnginePart::of(2);
        rDiscs_ = ObjectRecord::read(img, kDiscs);
        rCee_ = ObjectRecord::read(img, kCee);
        discs_.load(img, rDiscs_.seg);
        cee_.load(img, rCee_.seg);
        materials(c, img);
        // the DAC (377f): colours 0..239 from 6507:0010, 240..255 the label's from 2bd2:02da
        Palette pal = vgaPalette(img.span(0x6507, 0x0010, 720));
        pal = vgaPalette(img.span(0x2bd2, 0x02da, 48), 240, pal);
        blur_.init(c, pal);
        c.picture("p2-background");
    }

    void render(Ctx& c, double t) override {
        if (steps_.empty()) return;
        double r = c.retraces(t);
        if (r >= lastCall_ + 1) {   // 43ed: the whole DAC 63 until p3 sets its mode
            c.clear(Vec3(1, 1, 1));
            return;
        }
        // the copy after call m (360c, after its 64000-pixel blend) reaches the capture's record start + m, one
        // record after the DAC writes of the same call: the screen at r shows the frame built after r - 1 calls
        // (first strip of step 1: r = 439; the "C": r = 3124, the backdrop 3125) and the label after r calls
        double f = std::max(r, 0.0) - 1.0;
        blur_.render(c, *this, f, show(f, r));
    }

    // ---- P2Source
    void drawScene(Ctx& c, Target& dst, double r) override {
        int n = frameOf(r);
        const Step& st = step(n);
        dst.bind();
        // 360c restores the render buffer to the backdrop after the copy (from the frame after [1947] is set), else
        // clears it
        if (n >= 1 && step(n - 1).backdrop) {
            c.drawPicture(c.picture("p2-background"));   // opaque over the whole screen: no clear needed
        } else {
            gl::ClearColor(0, 0, 0, 1);
            gl::Clear(GL_COLOR_BUFFER_BIT);
        }
        Object d = Object::from(rDiscs_, &discs_);
        d.f = st.discsF;
        d.pivot[0] = st.pivot;
        d.envAdd = st.discsAttr & 0xff;
        d.texAdd = st.discsAttr >> 8;
        mat_.envAdd = int8_t(st.envSel & 0xff);
        mat_.texAdd = int8_t(st.texSel & 0xff);
        Engine& eng = c.shared<Engine>();
        eng.begin(dst, part_, camera(std::max(r, 0.0)), View{}, order_);
        eng.draw(d, mat_);
        if (st.ceeShown) {
            Object e = Object::from(rCee_, &cee_);
            e.f = st.ceeF;
            e.envAdd = st.ceeAttr & 0xff;
            e.texAdd = st.ceeAttr >> 8;
            // the sort key is the local depth times f (notes-p2 7.2): the "C" sorts with its depth times 1300 / 4000
            e.depthScale = double(st.ceeF) / double(st.discsF);
            eng.draw(e, mat_);
        }
        eng.end();
    }

    P2Blend blend(int m) const override {
        // blend m belongs to the original's frame n = ceil(m / kSub)
        constexpr int K = P2Blur::kSub;
        int n = m <= 0 ? 0 : (m + K - 1) / K;
        double r = double(m) / K;
        return params(r, n, 1.0 / K);
    }

    P2Blend partial(double r) const override {
        constexpr int K = P2Blur::kSub;
        double m = std::floor(r * K);
        return params(r, frameOf(r), (r * K - m) / K);
    }

private:
    // ---- script state
    void script(const std::vector<Mem>& states) {
        auto delay = [](int i) {
            int d = 0;
            for (const auto& e : kDelays)
                if (i >= e[0]) d = e[1];
            return d;
        };
        int nominal = 0;
        for (size_t i = 0; i < steps_.size(); i++) {
            Step& st = steps_[i];
            const Mem& m = states[i];
            st.start = nominal + delay(int(i));
            nominal += st.n;
            st.strip = m.s16(kStrip);
            st.bandRow = (m.s16(kBand) & 0xffff) / 320 - 1;   // buffer row -> screen row
            st.ceeShown = m.s16(kCee + kRecOff) == 0;
            st.backdrop = m.s16(kBackdrop) == 1;
            st.fade = m.s16(kFadeOn) == 1;
            st.discsF = m.s16(kDiscs + kRecF);
            st.ceeF = m.s16(kCee + kRecF);
            st.discsAttr = m.s16(kDiscs + kRecAttr) & 0xffff;
            st.ceeAttr = m.s16(kCee + kRecAttr) & 0xffff;
            st.envSel = m.s16(kEnvSel) & 0xff;
            st.texSel = m.s16(kTexSel) & 0xff;
            st.pivot = m.s32(kDiscs + kRecPivot);
            st.labelDir = m.s16(kLabelDir);
            // 2580 moves the camera record (the only one with velocities in p2)
            for (int a = 0; a < 3; a++) {
                uint16_t pos = uint16_t(kCamera + kRecPos + 4 * a), vel = uint16_t(kCamera + kRecVel + 4 * a);
                uint16_t ang = uint16_t(kCamera + kRecAng + 2 * a), av = uint16_t(kCamera + kRecAngVel + 2 * a);
                if (i == 0) {
                    pos_[a] = Integrator(m.s32(pos), m.s32(vel));
                    ang_[a] = Integrator(m.s16(ang) & 0xffff, m.s16(av));
                    continue;
                }
                if (st.written.count(pos)) pos_[a].value(st.start, m.s32(pos));
                if (st.written.count(vel)) pos_[a].velocity(st.start, m.s32(vel));
                if (st.written.count(ang)) ang_[a].value(st.start, m.s16(ang) & 0xffff);
                if (st.written.count(av)) ang_[a].velocity(st.start, m.s16(av));
            }
            if (i == 0) label_.a0 = m.s16(kLabelPos);
            else if (st.written.count(kLabelDir)) label_.speeds.push_back({st.start, st.labelDir});
            if (st.fade && fadeStart_ < 0) fadeStart_ = st.start;
        }
        lastCall_ = steps_.back().start + steps_.back().n + kLastOvershoot;
        // the step of every frame
        stepOf_.assign(size_t(lastCall_ + 2), 0);
        for (size_t i = 0; i < steps_.size(); i++)
            for (int n = steps_[i].start; n < int(stepOf_.size()); n++) stepOf_[size_t(n)] = int(i);
        greyRows();
        textSlots();
    }

    // The rows whose pixels are in the grey ramp after frame n: the band's rows while a strip >= 0 is written
    // (strip 0 is grey 0), kept while other values arrive (grey or scene: T[grey][*] is grey), all of them 0 in the
    // frame after 3a5d (T[grey][*] = 0). Rows [y0, y1); the zeroed rows of frame n are those of n - 1 and n.
    void greyRows() {
        grey_.assign(stepOf_.size(), {0.0f, 0.0f});
        zero_.assign(stepOf_.size(), {0.0f, 0.0f});
        std::pair<float, float> g{0, 0}, prev{0, 0};
        for (size_t n = 0; n < grey_.size(); n++) {
            const Step& st = steps_[size_t(stepOf_[n])];
            bool first = int(n) == st.start;
            if (first && st.clear) g = {0, 0};
            else if (st.strip >= 0) g = unite(g, {float(st.bandRow), float(st.bandRow + kBandRows)});
            grey_[n] = g;
            zero_[n] = unite(prev, g);
            prev = g;
        }
    }
    static std::pair<float, float> unite(std::pair<float, float> a, std::pair<float, float> b) {
        if (a.second <= a.first) return b;
        if (b.second <= b.first) return a;
        return {std::min(a.first, b.first), std::max(a.second, b.second)};   // the bands overlap in p2
    }

    // The strips written at each band position, as frame intervals [f0, f1].
    void textSlots() {
        for (size_t i = 0; i < steps_.size(); i++) {
            const Step& st = steps_[i];
            if (st.strip < 1) continue;
            int f1 = i + 1 < steps_.size() ? steps_[i + 1].start - 1 : lastCall_;
            Slot* slot = nullptr;
            for (Slot& s : slots_)
                if (s.strip == st.strip && s.row == st.bandRow) slot = &s;
            if (!slot) {
                slots_.push_back({st.strip, st.bandRow, {}});
                slot = &slots_.back();
            }
            slot->frames.push_back({st.start, f1});
        }
    }

    // The text level of a strip shown in frames f0..f1 is x c(r): the frame n's blend pulls over r in (n - 1, n],
    // y' = y + a (x - y) per frame is dy/dr = L (x - y) with L = -ln(1 - a), exact at whole r.
    static double textCoef(const std::vector<std::pair<int, int>>& frames, double r) {
        const double L = -std::log(1.0 - kTextRate);
        double c = 0;
        for (const auto& [f0, f1] : frames)
            if (r > f0 - 1) c += std::exp(-L * (r - std::min(r, double(f1)))) - std::exp(-L * (r - (f0 - 1)));
        return c;
    }

    // f: the frame clock (the state the screen's frame was built from), r: the retraces (the DAC)
    P2Show show(double f, double r) const {
        P2Show s;
        int n = frameOf(f);
        s.greyY0 = grey_[size_t(n)].first;
        s.greyY1 = grey_[size_t(n)].second;
        for (const Slot& sl : slots_) {
            double k = textCoef(sl.frames, f);
            if (k < 1e-5 || s.slots >= 4) continue;
            s.strip[size_t(s.slots)] = float(sl.strip);
            s.row[size_t(s.slots)] = float(sl.row);
            s.coef[size_t(s.slots)] = float(k);
            s.slots++;
        }
        // 2580 with 2550: after the n-th retrace of the fade the DAC is min(63, v + n - 2); it runs from the call
        // after the step's start until the last call
        if (fadeStart_ >= 0) {
            double k = std::clamp(r - fadeStart_ - 1, 0.0, double(lastCall_ - fadeStart_ - 2));
            s.fadeAdd = float(k / 63.0);
        }
        return s;
    }

    P2Blend params(double r, int n, double dt) const {
        P2Blend b;
        b.r = r;
        // 1967: T[old][new] = (new s + old (256 - s)) >> 8 per frame; per dt retraces
        double s = step(n).s / 256.0;
        b.weight = float(s >= 1 ? (dt > 0 ? 1.0 : 0.0) : 1.0 - std::pow(1.0 - s, dt));
        b.label = label_.position(r + 1);
        b.zeroY0 = zero_[size_t(n)].first;
        b.zeroY1 = zero_[size_t(n)].second;
        return b;
    }

    int frameOf(double r) const {
        return std::clamp(int(std::ceil(r)), 0, int(stepOf_.size()) - 1);
    }
    const Step& step(int n) const {
        return steps_[size_t(stepOf_[size_t(std::clamp(n, 0, int(stepOf_.size()) - 1))])];
    }
    Pose camera(double r) const {
        return Pose{pos_[0].at(r), pos_[1].at(r), pos_[2].at(r), ang_[0].at(r), ang_[1].at(r), ang_[2].at(r)};
    }

    // ---- the engine's data
    void materials(Ctx& c, const DopeImage& img) {
        // the env segment table 0010:0400: 6538 / 6540 are the left / right 128 columns of the env-map picture
        // (pixels at 6538:0000); 6d38 lies past it (never selected in p2)
        const PictureDef* def = findPicture("p2-envmaps");
        if (!def) return;
        const Picture& env = c.picture("p2-envmaps");
        uint32_t pixels = uint32_t(def->seg) * 16 + def->off + 0x30a;
        auto envSeg = segmentTable(img, part_.envTable);
        for (int i = 0; i < 4; i++) {
            int32_t d = int32_t(uint32_t(envSeg[size_t(i)]) * 16 - pixels);
            if (d == 0 || d == 128) mat_.env[i] = EnvMap::picture(env, d / 128);
        }
        // the texture segment table 0010:0408: the bump map 23d1, texel codes through the table at 0010:0000
        auto texSeg = segmentTable(img, part_.texTable);
        BumpOffsets offs = bumpTable(img, uint16_t(part_.bumpTable));
        for (int i = 0; i < 4; i++) {
            if (!texSeg[size_t(i)]) continue;
            bump_[i].create(img, texSeg[size_t(i)], offs, part_);
            mat_.tex[i] = &bump_[i];
        }
    }

    struct Slot {
        int strip = 0, row = 0;
        std::vector<std::pair<int, int>> frames;
    };

    std::vector<Step> steps_;
    std::vector<int> stepOf_;
    std::vector<std::pair<float, float>> grey_, zero_;
    std::vector<Slot> slots_;
    Integrator pos_[3], ang_[3];
    LabelSlide label_;
    int fadeStart_ = -1, lastCall_ = 0;

    EnginePart part_;
    ObjectRecord rDiscs_, rCee_;
    EngineMesh discs_, cee_;
    BumpMap bump_[4];
    Materials mat_;
    // the depth buffer with the "C"'s depth scaled gives the original's order between the objects (checked against
    // Order::Painter through the part: the same picture apart from a few pixels where faces intersect)
    Order order_ = Order::Depth;
    P2Blur blur_;
};

}  // namespace

REGISTER_SCENE(BlurScene, "p2");

// The sprites part (11:63..14:00 and 27:48..29:63, inside/re/notes-sprites.md): four digitised photographs of people,
// each drawn as it is over a full-screen background, either the warp (pictures 2 and 1: a swirl texture read twice
// through a slowly turning 8 x 8 grid of warped texture coordinates and blended 50 / 50, rasterizer 0x4172e0) or the
// room (pictures 4 and 3: the inside of the box poksi.sro turning about its vertical axis, textured through the .qnt
// light table, lit by a moving point light, with the man's shadow on the floor through the .sdw table). Ports the
// picture inits 0x404fe8 / 0x40506c / 0x405110 / 0x4051d4, the scene init 0x405298, the script's routines 0x4057bc /
// 0x40571c (warp pictures), 0x40585c / 0x405ba0 (room pictures), 0x404680 (the second warp parameters), 0x405f00
// (camera drift) and 0x404728 (off), the tick 0x404738 and the frame 0x4047a0. The flashes are the framework's.
//
// Evaluated exactly instead of the original's shortcuts: the warp's texture coordinates, which the original computes
// at the 8 x 8 grid vertices and interpolates affinely over 98 triangles, are evaluated per output pixel from the
// grid coordinates of the point of the plane the pixel sees (the formulas of 0x4047a0 with continuous i, j; they also
// exist past the grid's edge, which removes its sub-pixel corner gaps); both texture reads are filtered in colour and
// the .shd table (a 50 % blend, notes 9.1) is their RGB average. The phases (float32 sums), the turning (a rotation
// matrix multiplied once per refresh) and the camera drift (30 units per drawn frame, one frame per refresh) are
// closed forms of the refresh, continuous between refreshes; the light, its intensity and the shadow corner are
// evaluated from the music clock as floats instead of truncated integers. The room, the light and the shadow quad
// go through the engine (inside/engine.h: perspective-correct, filtered, continuous light levels, the .sdw rows as
// fitted blends, the sort bias that draws the shadow over the floor, picture 4's mirrored cell halves). The photos
// are high-resolution redraws (art/spriteK.png) where they exist, at the original's position and size; with
// --original-art the original pixels, filtered, index 0 transparent.
//
// With INSIDE_ENGINE=original (Fidelity::exact) the warp goes through the engine's 0x4172e0 on the 8 x 8 grid with
// the original's truncated words and float32 phase sums, for pixel checks at 320 x 200.
//
// Every parameter is read from INSIDE.EXE: the file names, sizes, positions, rotations, materials, flags and the
// scene's values are what the routines store and pass to the engine (followed by a small symbolic tracer of their
// code, below), the warp parameters are the image's floats and 0x404680's immediates, the light's frequencies and
// amplitudes the doubles at 0x420c1c..0x420ca4.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "demo.h"
#include "exe.h"
#include "inside/engine.h"
#include "inside/indexed.h"

namespace {

namespace sr = inside::sr;

// ------------------------------------------------------------------------------------------------ addresses
// Variables (notes 2.4); the slots are indexed by the picture k = 1..4 (+ 4 k).
constexpr uint32_t kPicVar = 0x4227fc, kStepVar = 0x422800;
constexpr uint32_t kWarpVars = 0x422810;   // 15 floats: phase, speed, amplitude, i-, j-factor of A, B and C
constexpr uint32_t kPalSlots = 0x425f70, kTexSlots = 0x425f88, kTabSlots = 0x425f9c, kSprSlots = 0x425f34,
                   kShadowSlots = 0x425f5c, kSdwSlots = 0x425f48, kBitmapSlots = 0x425f20;
// The engine objects' pointers.
constexpr uint32_t kScene = 0x425fc4, kCamera = 0x425fc0, kLightObj = 0x425fd0, kMaterial = 0x425fd4,
                   kMaterial2 = 0x425fdc, kShadowMap = 0x425fbc, kGrid = 0x425fd8, kBox = 0x425fcc,
                   kQuad = 0x425fc8, kGridSro = 0x425fb4, kSquareSro = 0x425fb8, kBoxSro = 0x425fb0;
constexpr uint32_t kTurn = 0x425ed8;   // the turning step (3 x 3 doubles)

// Routines: the picture inits, the straight runs of the scene init 0x405298 that set things up (between them it
// computes allocation sizes and loops over the box's faces: every even face gets material2), the script's routines
// and the tick routine they install.
constexpr uint32_t kPicInit[5] = {0, 0x404fe8, 0x40506c, 0x405110, 0x4051d4};
constexpr uint32_t kSceneInitRuns[4][2] = {{0x40532c, 0x405391}, {0x4053f3, 0x405582}, {0x4055a3, 0x4055ee},
                                           {0x40561a, 0x405716}};
constexpr uint32_t kWarp2 = 0x4057bc, kWarp1 = 0x40571c, kRoom3 = 0x40585c, kRoom4 = 0x405ba0, kParams = 0x404680,
                   kDrift = 0x405f00, kOff = 0x404728;
constexpr uint32_t kRoutines[] = {kWarp2, kWarp1, kRoom3, kRoom4, kParams, kDrift, kOff};
constexpr uint32_t kTick = 0x404738;
// The core's and the engine's API as the routines call them.
constexpr uint32_t kFileCall = 0x400614, kSetTick = 0x400af4, kMakeBitmap = 0x415e50, kMakeGrid = 0x410f34,
                   kLoadSro = 0x410bb0, kSetRaster = 0x410850, kSetPos = 0x410868, kSetRot = 0x4108dc,
                   kRotMatrix = 0x4145f4, kViewport = 0x410618;

// The frame routine 0x4047a0: the warp grid (mov eax, -1400 the first row; add esi, 350 the spacing; cmp edx, 8;
// add ecx, 0x1000 then sar 3: words per cell), the photos' positions (warp: x = push 0, y = 200 - height; picture
// 3: 320 - width, 200 - height; picture 4: push 94.0f, push 28.0f).
constexpr uint32_t kGridX0 = 0x4048c6, kGridDx = 0x404b6d, kGridN = 0x404b76, kWordStep = 0x404b38,
                   kWordShift = 0x4048e9;
constexpr uint32_t kWarpSprX = 0x404bec, kWarpSprY = 0x404bd2, kRoom3SprX = 0x404f8f, kRoom3SprY = 0x404f7a,
                   kRoom4SprX = 0x404fb9, kRoom4SprY = 0x404fae;

// ------------------------------------------------------------------------------------------------ tracer
// The routines store their parameters through the objects' pointers ([[0x425fc8] + 0xd0] + 0x48 = the shadow quad's
// vertex 1 x) and pass them to the engine in registers and on the stack. Trace follows straight code symbolically: a
// register holds a constant, the dword at an address path (an absolute address, then the offsets of dereferenced
// pointers), or the result of a call; stores are recorded by path, calls with their arguments. Only the instruction
// forms these routines use are understood; anything else throws.
using Path = std::vector<uint32_t>;

struct Val {
    enum Kind : uint8_t { Unknown, Const, At, Ret } kind = Unknown;
    uint32_t c = 0;   // Const: the value; Ret: the index of the call that returned it
    Path path;        // At: the address path
    int ret = -1;     // At: the call whose result was stored there (-1: none)
    static Val cst(uint32_t v) {
        Val r;
        r.kind = Const;
        r.c = v;
        return r;
    }
    bool is(uint32_t v) const { return kind == Const && c == v; }
    bool at(const Path& p) const { return kind == At && path == p; }
};

struct Call {
    uint32_t target = 0;
    Val reg[8];               // eax ecx edx ebx esp ebp esi edi
    std::vector<Val> stack;   // stack[0] = [esp]
    // the n-th double argument on the stack (pushed high, low)
    double stackDouble(size_t n) const {
        if (stack.size() < 2 * n + 2 || stack[2 * n].kind != Val::Const || stack[2 * n + 1].kind != Val::Const)
            throw std::runtime_error("sprites: no constant double argument");
        uint64_t b = uint64_t(stack[2 * n + 1].c) << 32 | stack[2 * n].c;
        double d;
        std::memcpy(&d, &b, 8);
        return d;
    }
    sr::V3 stackTriple() const { return {stackDouble(0), stackDouble(1), stackDouble(2)}; }
    sr::V3 regTriple() const {   // edx, ebx, ecx (0x410868)
        auto i = [&](int r) {
            if (reg[r].kind != Val::Const) throw std::runtime_error("sprites: no constant position");
            return double(int32_t(reg[r].c));
        };
        return {i(2), i(3), i(1)};
    }
};

struct Trace {
    std::map<Path, Val> stores;
    std::vector<Call> calls;

    const Val* stored(const Path& p) const {
        auto it = stores.find(p);
        return it == stores.end() || it->second.kind != Val::Const ? nullptr : &it->second;
    }
    std::optional<uint32_t> value(const Path& p) const {
        const Val* v = stored(p);
        return v ? std::optional<uint32_t>(v->c) : std::nullopt;
    }
    // The last call of `target` whose register r holds the dword at `object`.
    const Call* call(uint32_t target, const Path& object = {}, int r = 0) const {
        const Call* found = nullptr;
        for (const Call& c : calls)
            if (c.target == target && (object.empty() || c.reg[r].at(object))) found = &c;
        return found;
    }
    // The file name (0x400614's argument) whose data was stored at `slot`.
    std::string file(const InsideExe& x, uint32_t slot) const {
        auto it = stores.find(Path{slot});
        if (it == stores.end() || it->second.ret < 0) throw std::runtime_error("sprites: no file stored there");
        const Call& c = calls[size_t(it->second.ret)];
        if (c.target != kFileCall || c.reg[0].kind != Val::Const) throw std::runtime_error("sprites: not a file");
        return x.cstr(c.reg[0].c);
    }
};

int calleePops(uint32_t target) {   // dwords the callee removes from the stack (ret n)
    switch (target) {
        case kSetRot: case kRotMatrix: return 6;
        case kViewport: return 3;
        case kMakeGrid: case kMakeBitmap: return 1;
        default: return 0;
    }
}

void trace(const InsideExe& x, uint32_t a, uint32_t end, Trace& t) {
    Val r[8];
    std::vector<Val> st;   // top at the back
    std::vector<double> fpu;
    auto fail = [&](const char* what) {
        char b[96];
        std::snprintf(b, sizeof b, "sprites: trace: %s at 0x%x", what, a);
        throw std::runtime_error(b);
    };
    auto load = [&](const Path& p) {
        Val v;
        if (p.empty()) return v;
        auto it = t.stores.find(p);
        if (it != t.stores.end() && it->second.kind == Val::Const) return it->second;
        v.kind = Val::At;
        v.path = p;
        if (it != t.stores.end()) v.ret = it->second.ret;
        return v;
    };
    // A store of a call's result keeps its origin; the register then also holds "the dword at" that path.
    auto store = [&](const Path& p, Val& v) {
        if (p.empty()) return;
        if (v.kind == Val::Const) { t.stores[p] = v; return; }
        Val s;
        s.kind = Val::At;
        s.path = p;
        s.ret = v.kind == Val::Ret ? int(v.c) : v.kind == Val::At ? v.ret : -1;
        t.stores[p] = s;
        if (v.kind == Val::Ret) v = s;
    };
    struct Mem {
        int mod = 0, reg = 0, rm = 0;
        uint32_t len = 1;   // modrm, sib, displacement
        Path path;          // empty: register or unknown
        int slot = -1;      // [esp + 4 slot]
    };
    auto modrm = [&](uint32_t p) {
        Mem m;
        uint8_t b = x.u8(p);
        m.mod = b >> 6;
        m.reg = (b >> 3) & 7;
        m.rm = b & 7;
        if (m.mod == 3) return m;
        int base = m.rm, index = 4;
        if (m.rm == 4) {
            uint8_t s = x.u8(p + 1);
            m.len++;
            base = s & 7;
            index = (s >> 3) & 7;
        }
        int32_t disp = 0;
        bool absolute = false;
        if (m.mod == 0 && base == 5) { disp = x.s32(p + m.len); m.len += 4; absolute = true; }
        else if (m.mod == 1) { disp = x.s8(p + m.len); m.len += 1; }
        else if (m.mod == 2) { disp = x.s32(p + m.len); m.len += 4; }
        if (index != 4) return m;   // a scaled index: an address the trace does not know
        if (absolute) { m.path = {uint32_t(disp)}; return m; }
        if (base == 4) { if (disp >= 0 && disp % 4 == 0) m.slot = disp / 4; return m; }
        const Val& v = r[base];
        if (v.kind == Val::At) { m.path = v.path; m.path.push_back(uint32_t(disp)); }
        else if (v.kind == Val::Const) m.path = {v.c + uint32_t(disp)};
        return m;
    };
    auto slotRef = [&](int s) -> Val& {
        if (s < 0 || size_t(s) >= st.size()) fail("stack slot outside the trace");
        return st[st.size() - 1 - size_t(s)];
    };
    auto readMem = [&](const Mem& m) { return m.slot >= 0 ? slotRef(m.slot) : load(m.path); };
    auto writeMem = [&](const Mem& m, Val& v) {
        if (m.slot >= 0) slotRef(m.slot) = v;
        else store(m.path, v);
    };
    auto writeConst = [&](const Mem& m, uint32_t c) {
        Val v = Val::cst(c);
        writeMem(m, v);
    };
    auto call = [&](uint32_t target) {
        Call c;
        c.target = target;
        for (int i = 0; i < 8; i++) c.reg[i] = r[i];
        for (size_t i = 0; i < st.size() && i < 8; i++) c.stack.push_back(st[st.size() - 1 - i]);
        t.calls.push_back(c);
        r[0] = Val();   // Watcom's convention: every register but EAX is preserved
        r[0].kind = Val::Ret;
        r[0].c = uint32_t(t.calls.size() - 1);
        for (int i = calleePops(target); i > 0 && !st.empty(); i--) st.pop_back();
    };
    while (a < end) {
        uint8_t op = x.u8(a);
        if (op == 0xc3) return;                                                          // ret
        if (op == 0x90) { a += 1; continue; }                                            // nop
        if (op >= 0x50 && op <= 0x57) { st.push_back(r[op - 0x50]); a += 1; continue; } // push r32
        if (op >= 0x58 && op <= 0x5f) {                                                  // pop r32
            if (!st.empty()) { r[op - 0x58] = st.back(); st.pop_back(); }
            a += 1;
            continue;
        }
        if (op == 0x68) { st.push_back(Val::cst(x.u32(a + 1))); a += 5; continue; }     // push imm32
        if (op == 0x6a) { st.push_back(Val::cst(uint32_t(int32_t(x.s8(a + 1))))); a += 2; continue; }
        if (op >= 0xb8 && op <= 0xbf) { r[op - 0xb8] = Val::cst(x.u32(a + 1)); a += 5; continue; }
        if (op == 0xa1) { r[0] = load({x.u32(a + 1)}); a += 5; continue; }              // mov eax, [abs]
        if (op == 0xa3) { store({x.u32(a + 1)}, r[0]); a += 5; continue; }             // mov [abs], eax
        if (op == 0xe8) { call(a + 5 + uint32_t(x.s32(a + 1))); a += 5; continue; }     // call
        if (op == 0xe9) { call(a + 5 + uint32_t(x.s32(a + 1))); return; }               // jmp: a tail call
        bool word = op == 0x66;                                                          // operand size prefix
        if (word) op = x.u8(++a);
        Mem m = modrm(a + 1);
        uint32_t n = 1 + m.len;
        bool ok = true;
        switch (op) {
            case 0x31: case 0x33:   // xor r, r
                if (m.mod != 3) { ok = false; break; }
                r[m.reg] = m.reg == m.rm ? Val::cst(0) : Val();
                break;
            case 0x89:   // mov r/m32, r32
                if (m.mod == 3) r[m.rm] = r[m.reg];
                else writeMem(m, r[m.reg]);
                break;
            case 0x8b: r[m.reg] = m.mod == 3 ? r[m.rm] : readMem(m); break;              // mov r32, r/m32
            case 0x8d: r[m.reg] = Val(); break;                                          // lea (not followed)
            case 0xc7:                                                                   // mov r/m, imm
                if (m.reg != 0) { ok = false; break; }
                writeConst(m, word ? x.u16(a + n) : x.u32(a + n));
                n += word ? 2 : 4;
                break;
            case 0xc6:                                                                   // mov r/m8, imm8
                if (m.reg != 0) { ok = false; break; }
                writeConst(m, x.u8(a + n));
                n += 1;
                break;
            case 0x80:                                                                   // or r/m8, imm8
                if (m.reg != 1 || m.mod == 3) { ok = false; break; }
                {
                    Val v = readMem(m);   // the objects were zeroed by their init
                    writeConst(m, (v.kind == Val::Const ? v.c : 0) | x.u8(a + n));
                }
                n += 1;
                break;
            case 0x83:                                                                   // sub esp, imm8
                if (m.reg != 5 || m.mod != 3 || m.rm != 4) { ok = false; break; }
                for (int i = 0; i < x.s8(a + n) / 4; i++) st.push_back(Val());
                n += 1;
                break;
            case 0xff:                                                                   // inc / dec r/m32
                if (m.reg > 1 || m.mod == 3) { ok = false; break; }
                {
                    Val v;
                    writeMem(m, v);
                }
                break;
            case 0xd9:                                                                   // fld / fstp m32
                if (m.mod == 3) { ok = false; break; }
                if (m.reg == 0) {
                    Val v = readMem(m);
                    uint32_t bits = 0;
                    if (v.kind == Val::Const) bits = v.c;
                    else if (v.kind == Val::At && v.path.size() == 1) bits = x.u32(v.path[0]);   // the image's value
                    else fail("fld of an unknown value");
                    float f;
                    std::memcpy(&f, &bits, 4);
                    fpu.push_back(double(f));
                } else if (m.reg == 3 && !fpu.empty()) {
                    float f = float(fpu.back());
                    fpu.pop_back();
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    writeConst(m, bits);
                } else {
                    ok = false;
                }
                break;
            case 0xdd:                                                                   // fstp m64 to the stack
                if (m.reg != 3 || m.slot < 0 || fpu.empty()) { ok = false; break; }
                {
                    uint64_t b;
                    std::memcpy(&b, &fpu.back(), 8);
                    fpu.pop_back();
                    slotRef(m.slot) = Val::cst(uint32_t(b));
                    slotRef(m.slot + 1) = Val::cst(uint32_t(b >> 32));
                }
                break;
            default: ok = false;
        }
        if (!ok) fail("unexpected instruction");
        a += n;
    }
}

Trace traceRoutine(const InsideExe& x, uint32_t begin, uint32_t end = 0xffffffffu) {
    Trace t;
    trace(x, begin, end, t);
    return t;
}

// ------------------------------------------------------------------------------------------------ immediates
uint32_t movImm(const InsideExe& x, uint32_t a) {   // mov r32, imm32
    if ((x.u8(a) & 0xf8) != 0xb8) throw std::runtime_error("sprites: no mov r32, imm32 at " + std::to_string(a));
    return x.u32(a + 1);
}
uint32_t addImm(const InsideExe& x, uint32_t a) {   // add r32, imm32
    if (x.u8(a) != 0x81 || (x.u8(a + 1) & 0xf8) != 0xc0) throw std::runtime_error("sprites: no add at " + std::to_string(a));
    return x.u32(a + 2);
}
float pushFloat(const InsideExe& x, uint32_t a) {   // push imm32 / imm8 as a float's bits
    uint32_t b;
    if (x.u8(a) == 0x68) b = x.u32(a + 1);
    else if (x.u8(a) == 0x6a) b = uint32_t(int32_t(x.s8(a + 1)));
    else throw std::runtime_error("sprites: no push at " + std::to_string(a));
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// ------------------------------------------------------------------------------------------------ the routines
// What one of the script's routines sets (from its trace); fields left empty it does not change.
struct Routine {
    uint32_t addr = 0;
    std::optional<int> picture, step;
    std::optional<double> fov, nearPlane, ambient;
    std::optional<sr::V3> camPos, camRot, quadRot, boxRot;
    std::optional<double> turn;   // the turning step Ry(turn) per tick
    bool quad = false;            // the shadow quad's vertices
    sr::V3 quadP[4];
    double quadUV[4][2] = {};
    std::optional<int> shadowLog2;
    bool corners = false;         // the box materials' fixed corners (material, material2)
    bool fixedUV[2] = {};         // their flag bit 0: the corners replace the vertices' u, v
    double uv[2][6] = {};
    std::optional<sr::Raster> boxRaster;
    bool params = false;          // 0x404680: the warp parameters (phases, speeds, amplitudes, factors)
    float warp[15] = {};
    std::optional<bool> runs;     // the tick routine set (true) or cleared (false)
};

Routine readRoutine(const InsideExe& x, uint32_t addr) {
    Trace t = traceRoutine(x, addr);
    Routine R;
    R.addr = addr;
    auto i32 = [](uint32_t v) { return int(int32_t(v)); };
    if (auto v = t.value({kPicVar})) R.picture = i32(*v);
    if (auto v = t.value({kStepVar})) R.step = i32(*v);
    if (auto v = t.value({kScene, 0x3c})) R.fov = i32(*v);
    if (auto v = t.value({kScene, 0x44})) R.nearPlane = i32(*v);
    if (auto v = t.value({kScene, 0x40})) R.ambient = i32(*v);
    if (const Call* c = t.call(kSetPos, {kCamera})) R.camPos = c->regTriple();
    if (const Call* c = t.call(kSetRot, {kCamera})) R.camRot = c->stackTriple();
    if (const Call* c = t.call(kSetRot, {kQuad})) R.quadRot = c->stackTriple();
    if (const Call* c = t.call(kSetRot, {kBox})) R.boxRot = c->stackTriple();
    if (const Call* c = t.call(kSetRaster, {kBox})) R.boxRaster = sr::rasterAt(c->reg[2].c);
    if (const Call* c = t.call(kRotMatrix)) {
        if (!c->reg[0].is(kTurn)) throw std::runtime_error("sprites: rotation matrix elsewhere");
        sr::V3 e = c->stackTriple();
        if (e.x != 0 || e.z != 0) throw std::runtime_error("sprites: the turning step is not about y");
        R.turn = e.y;
    }
    // the shadow quad's vertices: [[0x425fc8] + 0xd0] + 0x40 n + 8 / 0xc / 0x10 (x, y, z), + 0x28 / 0x2a (u, v)
    for (int n = 0; n < 4; n++) {
        uint32_t o = 0x40u * uint32_t(n);
        auto p = [&](uint32_t off) { return t.value({kQuad, 0xd0, o + off}); };
        if (!p(8)) continue;
        R.quad = true;
        R.quadP[n] = {double(int32_t(*p(8))), double(int32_t(*p(0xc))), double(int32_t(*p(0x10)))};
        R.quadUV[n][0] = double(int16_t(*p(0x28)));
        R.quadUV[n][1] = double(int16_t(*p(0x2a)));
    }
    if (auto v = t.value({kShadowMap, 0x40})) R.shadowLog2 = int(*v);
    if (t.value({kMaterial, 0x20})) {
        R.corners = true;
        const uint32_t mats[2] = {kMaterial, kMaterial2};
        for (int m = 0; m < 2; m++) {
            R.fixedUV[m] = (t.value({mats[m], 0x18}).value_or(0) & 1) != 0;
            for (int k = 0; k < 6; k++) R.uv[m][k] = double(int32_t(t.value({mats[m], 0x20u + 4u * uint32_t(k)}).value()));
        }
    }
    if (t.value({kWarpVars})) {
        R.params = true;
        for (int k = 0; k < 15; k++) {
            uint32_t b = t.value({kWarpVars + 4u * uint32_t(k)}).value();
            std::memcpy(&R.warp[k], &b, 4);
        }
    }
    if (const Call* c = t.call(kSetTick)) R.runs = c->reg[0].is(kTick);
    return R;
}

// ------------------------------------------------------------------------------------------------ the state
// What the frame shown at a refresh uses (notes 8): the routines that have run, the tick steps since.
struct State {
    int k = 2;
    const Routine* room = nullptr;   // the last room routine (the shadow quad, the corners)
    double fov = 256, nearPlane = 0x2000, ambient = 0;
    sr::V3 camPos, camRot;
    float warp[15] = {};
    double phase[3] = {};            // A, B, C
    float phase32[3] = {};           // the same as the original's float32 sums (exact mode)
    double turn = 0;                 // the turning step
    double gridAngle = 0, boxAngle = 0;
    sr::V3 boxRot;
    int step = 0;
    double driftFrom = 0;            // the first record whose frame moved the camera (step != 0)
    bool active = false;             // the tick routine runs
};

// Lighting of the room at music clock T (0x404c18 picture 3, 0x404d2f picture 4; light z for both at 0x404e61):
// the shadow quad's corner v1 (x, y), the light's position and intensity, in the original's operation order.
struct Lighting {
    sr::V3 light;
    double intensity = 0, cornerX = 0, cornerY = 0;
};

Lighting lighting(const InsideExe& x, int k, double T) {
    auto d = [&](uint32_t a) { return x.f64(a); };
    auto ac = [&](uint32_t amp, uint32_t fc, uint32_t fs) {   // (cos(T fc) * amp) * sin(T fs)
        return std::cos(T * d(fc)) * d(amp) * std::sin(T * d(fs));
    };
    Lighting L;
    if (k == 3) {
        L.cornerX = d(0x420c94) - ac(0x420c8c, 0x420c84, 0x420c2c);
        L.cornerY = ac(0x420c9c, 0x420c3c, 0x420ca4);
        L.light.x = ac(0x420c6c, 0x420c84, 0x420c2c);
        L.light.y = ac(0x420c6c, 0x420c3c, 0x420ca4);
    } else {
        L.cornerX = ac(0x420c24, 0x420c1c, 0x420c2c) + d(0x420c34);
        L.cornerY = ac(0x420c44, 0x420c3c, 0x420c4c) + d(0x420c44);
        L.light.x = ac(0x420c6c, 0x420c1c, 0x420c2c);
        L.light.y = ac(0x420c6c, 0x420c3c, 0x420c4c);
    }
    L.intensity = std::sin(T * d(0x420c54)) * d(0x420c5c) + d(0x420c64);
    L.light.z = ac(0x420c6c, 0x420c74, 0x420c7c);
    return L;
}

// ------------------------------------------------------------------------------------------------ the warp shader
const char* kWarpFS = R"(
in vec2 uv;
out vec4 o;
uniform usampler2D tex;   // the swirl texture, 256 x 256 indices
uniform sampler2D pal;
uniform vec2 res;         // output size in pixels
uniform mat3 toObj;       // camera-space directions -> the grid's object space
uniform vec3 camO;        // the camera in the grid's object space
uniform vec2 centre;      // the projection centre in VGA coordinates (with the rasterizers' sampling offset)
uniform vec2 focal;       // fov W / 320, fov H / 240
uniform vec2 grid;        // the first row / column (x = z), the spacing
uniform float wordStep;   // texture words (1/16 texel) per cell
uniform vec3 ph;          // the phases A, B, C
uniform vec3 amp;         // kA, kB, kC
uniform vec3 fi;          // the i-factors
uniform vec3 fj;          // the j-factors

vec3 texel(ivec2 t) { return palColor(pal, texelFetch(tex, t & 255, 0).r); }
vec3 bilinear(vec2 p) {   // p in texels (texel n covers [n, n + 1)), wrapping at 256
    vec2 q = p - 0.5;
    vec2 f = fract(q);
    ivec2 i = ivec2(floor(q));
    return mix(mix(texel(i), texel(i + ivec2(1, 0)), f.x), mix(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x),
               f.y);
}

void main() {
    vec2 p = vec2(gl_FragCoord.x / res.x * 320.0, (1.0 - gl_FragCoord.y / res.y) * 200.0);
    vec3 d = toObj * vec3((p - centre) / focal, 1.0);
    vec3 q = camO - camO.y / d.y * d;          // the point of the plane y = 0 seen through the pixel
    vec2 ij = (q.xz - grid.x) / grid.y;        // continuous grid coordinates (i, j)
    vec3 a = fi * ij.x + ph + fj * ij.y;       // a, b, c
    vec3 s = sin(a), c = cos(a);
    float u1 = wordStep * ij.x + dot(amp, s);                          // +0x28
    float v1 = wordStep * ij.y + dot(amp, c);                          // +0x2a
    float col2 = wordStep * ij.y + amp.z * s.x + amp.y * c.y + amp.x * s.z;   // +0x2c
    float row2 = wordStep * ij.x + amp.z * c.x + amp.y * c.y + amp.x * s.z;   // +0x2e
    // 0x4172e0: shd[tex[row2][col2]][tex[v1][u1]] with the rasterizers' +1 bias; the .shd is a 50 % blend
    vec3 c1 = bilinear((vec2(u1, v1) + 1.0) / 16.0);
    vec3 c2 = bilinear((vec2(col2, row2) + 1.0) / 16.0);
    o = vec4(0.5 * (c1 + c2), 1.0);
}
)";

// ------------------------------------------------------------------------------------------------ the scene
class SpritesScene : public Scene {
public:
    void init(Ctx& c) override {
        const InsideExe& x = *c.exe;
        eng_.init(c);
        for (uint32_t a : kRoutines) routines_[a] = readRoutine(x, a);
        // 0x405298: the scene
        Trace s;
        for (const auto& run : kSceneInitRuns) trace(x, run[0], run[1], s);
        initScene(c, s);
        // 0x404fe8 .. 0x4051d4: the pictures
        for (int k = 1; k <= 4; k++) initPicture(c, k);
        // the frame routine's grid and photo positions
        gridX0_ = double(int32_t(movImm(x, kGridX0)));
        gridDx_ = double(int32_t(addImm(x, kGridDx)));
        gridN_ = x.u8(kGridN + 2);
        wordStep_ = double(int32_t(addImm(x, kWordStep)) >> x.u8(kWordShift + 2));
        for (int k = 1; k <= 2; k++) {
            pics_[k].x = pushFloat(x, kWarpSprX);
            pics_[k].y = float(int32_t(movImm(x, kWarpSprY))) - float(pics_[k].bmp.h);
        }
        pics_[3].x = float(int32_t(movImm(x, kRoom3SprX))) - float(pics_[3].bmp.w);
        pics_[3].y = float(int32_t(movImm(x, kRoom3SprY))) - float(pics_[3].bmp.h);
        pics_[4].x = pushFloat(x, kRoom4SprX);
        pics_[4].y = pushFloat(x, kRoom4SprY);
        // the image's warp parameters (0x404680 writes the second set)
        for (int k = 0; k < 15; k++) warp0_[k] = x.f32(kWarpVars + 4u * uint32_t(k));
        shader_.build(kFullscreenVS, std::string(inside::kIndexedGLSL) + kWarpFS, "sprites warp");
    }

    void render(Ctx& c, double r) override {
        r = std::max(r, c.start);
        State s = stateAt(c, r);
        const Pic& p = pics_[s.k];
        bool exact = sr::fidelity().exact;
        if (s.k >= 3) drawRoom(c, s, r);
        else if (exact) drawWarpEngine(c, s, r);
        else drawWarp(c, s, r);
        const Picture& art = c.picture(p.art);
        if (art.redraw && !exact)
            c.drawPicture(art, {.x = p.x, .y = p.y, .w = float(p.bmp.w), .h = float(p.bmp.h)});
        else
            eng_.drawBitmap(c, p.bmp, p.x, p.y, p.pal);   // 0x415df0: type 3, index 0 transparent
    }

private:
    struct Pic {
        int pal = 0, tex = -1, table = -1, shadow = -1, sdw = -1;
        sr::Bitmap bmp;
        std::string art;   // the photo's name in the picture list (pictures.cpp): its file name, spriteN
        float x = 0, y = 0;
        Texture idx, palTex;   // the warp shader's copies
    };

    void initScene(Ctx& c, const Trace& s) {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        auto need = [](const Call* call, const char* what) {
            if (!call) throw std::runtime_error(std::string("sprites: the scene init has no ") + what);
            return call;
        };
        // 0x410618(scene, W, H, x0, stack: y0, x1, y1): the viewport; the near plane
        const Call* vp = need(s.call(kViewport, {kScene}), "viewport");
        scene_.width = int(vp->reg[2].c);
        scene_.height = int(vp->reg[3].c);
        scene_.x0 = int(vp->reg[1].c);
        scene_.y0 = int(vp->stack.at(0).c);
        scene_.x1 = int(vp->stack.at(1).c);
        scene_.y1 = int(vp->stack.at(2).c);
        if (auto v = s.value({kScene, 0x44})) initNear_ = double(int32_t(*v));
        initCamPos_ = need(s.call(kSetPos, {kCamera}), "camera position")->regTriple();
        // the warp grid: 0x410f34 (nx, nz, spacing x, z) loaded at its scale, material 1, its rasterizer and rotation
        const Call* g = need(s.call(kMakeGrid, {kGridSro}), "grid");
        matGrid_ = eng_.material({});
        grid_.makeGrid(int(g->reg[2].c), int(g->reg[3].c), double(int32_t(g->reg[1].c)),
                       double(int32_t(g->stack.at(0).c)), matGrid_);
        grid_.setRaster(sr::rasterAt(need(s.call(kSetRaster, {kGrid}), "grid rasterizer")->reg[2].c));
        sr::V3 ge = need(s.call(kSetRot, {kGrid}), "grid rotation")->stackTriple();
        gridRot0_ = sr::euler(ge.x, ge.y, ge.z);
        // the shadow quad: a 1 x 1 grid with the shadow map, two-sided, drawn as if nearer by its sort bias
        const Call* q = need(s.call(kMakeGrid, {kSquareSro}), "shadow square");
        matShadow_ = eng_.material({});
        quad_.makeGrid(int(q->reg[2].c), int(q->reg[3].c), double(int32_t(q->reg[1].c)), double(int32_t(q->stack.at(0).c)),
                       matShadow_);
        quad_.setRaster(sr::rasterAt(need(s.call(kSetRaster, {kQuad}), "quad rasterizer")->reg[2].c));
        quad_.pos = need(s.call(kSetPos, {kQuad}), "quad position")->regTriple();
        quadFlags_ = s.value({kQuad, 0xc4}).value();
        quad_.bias = double(int32_t(s.value({kQuad, 0xcc}).value()));
        // the room: poksi.sro at its scale with material, every even face material2, lit
        const Call* b = need(s.call(kLoadSro, {kBoxSro}, 2), "room");
        matA_ = eng_.material({});
        matB_ = eng_.material({});
        box_.loadSro(d.file(s.file(x, kBoxSro)), int32_t(b->reg[3].c), matA_);
        for (size_t f = 0; f < box_.faces.size(); f += 2) box_.faces[f].material = matB_;
        boxFlags_ = s.value({kBox, 0xc4}).value();
        // the light
        light_.range = double(int32_t(s.value({kLightObj, 0xc8}).value()));
        light_.intensity = double(int32_t(s.value({kLightObj, 0xcc}).value()));
        light_.flags = s.value({kLightObj, 0xc4}).value();
        // the turning step
        const Call* t = need(s.call(kRotMatrix), "turning step");
        initTurn_ = t->stackTriple().y;
        // the scene's list: camera, warp grid, shadow quad, room, light, each added at the head (0x411154)
        scene_.add(camera_);
        scene_.camera = &camera_;
        scene_.add(grid_);
        scene_.add(quad_);
        scene_.add(box_);
        scene_.add(light_);
    }

    void initPicture(Ctx& c, int k) {
        const InsideExe& x = *c.exe;
        const InsideDat& d = *c.dat;
        Trace t = traceRoutine(x, kPicInit[k]);
        Pic& p = pics_[k];
        uint32_t ku = uint32_t(k);
        uint32_t palSlot = kPalSlots + 4 * ku;
        // the palette, with entry 0 set to black where the init writes it (pictures 2, 3, 4; nigga1.pal's is black)
        bool black0 = t.value({palSlot, 0}) && t.value({palSlot, 1}) && t.value({palSlot, 2});
        auto colours = inside::palette(d, t.file(x, palSlot), black0);
        p.pal = eng_.palette(colours);
        auto raw = d.file(t.file(x, kTexSlots + 4 * ku));
        p.tex = eng_.texture(raw, 256, 256);
        p.table = eng_.table(d.file(t.file(x, kTabSlots + 4 * ku)));
        // the photo: 0x415e50(pixels, pitch, width, height), type 3 (index 0 transparent)
        const Call* m = t.call(kMakeBitmap);
        if (!m) throw std::runtime_error("sprites: no bitmap");
        int pitch = int(m->reg[2].c), w = int(m->reg[3].c), h = int(m->reg[1].c);
        std::string file = t.file(x, kSprSlots + 4 * ku);
        p.bmp = eng_.bitmap(d.file(file), pitch, w, h);
        p.art = file.substr(file.find('/') + 1, file.rfind('.') - file.find('/') - 1);
        c.picture(p.art);   // load it in the init
        p.bmp.type = int(t.value({kBitmapSlots + 4 * ku, 0}).value());
        p.bmp.key = 0;   // the bitmap's transparent index [0x24] (0x415e50)
        if (t.stores.count({kShadowSlots + 4 * ku})) {
            // the shadow map's size: the log2 the room routine of this picture sets
            int lg = 8;
            for (const auto& [a, R] : routines_)
                if (R.picture == k && R.shadowLog2) lg = *R.shadowLog2;
            p.shadow = eng_.texture(d.file(t.file(x, kShadowSlots + 4 * ku)), 1 << lg, 1 << lg);
            p.sdw = eng_.table(d.file(t.file(x, kSdwSlots + 4 * ku)));
        }
        if (k <= 2) {
            p.idx = inside::uploadIndices(raw, 256, 256);
            p.palTex = inside::uploadPalette(colours);
        }
    }

    // The routines that have run by the frame shown at refresh r, and the tick steps since (notes 8.2). A routine
    // shows from its entry's refresh; the tick at a record runs after the routines of that record, so the frame at r
    // has seen the ticks of records < r: continuous between refreshes.
    State stateAt(const Ctx& c, double r) const {
        State s;
        s.nearPlane = initNear_;
        s.camPos = initCamPos_;
        s.turn = initTurn_;
        for (int i = 0; i < 15; i++) s.warp[i] = warp0_[i];
        for (int i = 0; i < 3; i++) s.phase[i] = s.phase32[i] = warp0_[5 * i];
        const bool exact = sr::fidelity().exact;
        double cur = 0;
        auto advance = [&](double dt) {   // the tick routine 0x404738, dt times
            if (!s.active || dt <= 0) return;
            for (int i = 0; i < 3; i++) {
                s.phase[i] += dt * double(s.warp[5 * i + 1]);
                if (exact)   // the float32 sums
                    for (int n = 0; n < int(std::lround(dt)); n++)
                        s.phase32[i] = float(double(s.phase32[i]) + double(s.warp[5 * i + 1]));
            }
            s.gridAngle += dt * s.turn;
            s.boxAngle += dt * s.turn;
        };
        for (const ScriptEntry& e : c.script()) {
            auto it = routines_.find(e.routine);
            if (it == routines_.end()) continue;
            const Routine& R = it->second;
            if (e.refresh > r) {
                // the drift moves the camera once per drawn frame from its first record on: between the frame
                // before and that one the camera blends towards its first step
                if (R.addr == kDrift && e.refresh <= r + 1) apply(s, R, e.refresh);
                continue;
            }
            advance(e.refresh - cur);
            cur = e.refresh;
            apply(s, R, e.refresh);
        }
        advance(r - cur);
        return s;
    }

    void apply(State& s, const Routine& R, double at) const {
        if (R.picture) s.k = *R.picture;
        if (R.step) {
            s.step = *R.step;
            s.driftFrom = at;
        }
        if (R.fov) s.fov = *R.fov;
        if (R.nearPlane) s.nearPlane = *R.nearPlane;
        if (R.ambient) s.ambient = *R.ambient;
        if (R.camPos) s.camPos = *R.camPos;
        if (R.camRot) s.camRot = *R.camRot;
        if (R.boxRot) {
            s.boxRot = *R.boxRot;
            s.boxAngle = 0;
        }
        if (R.turn) s.turn = *R.turn;
        if (R.quad) s.room = &R;
        if (R.params) {
            for (int i = 0; i < 15; i++) s.warp[i] = R.warp[i];
            for (int i = 0; i < 3; i++) s.phase[i] = s.phase32[i] = R.warp[5 * i];
        }
        if (R.runs) s.active = *R.runs;
    }

    // 0x4047a0's start: the camera moves by the step per drawn frame (y; x by half of it except for picture 3)
    sr::V3 cameraAt(const State& s, double r) const {
        sr::V3 p = s.camPos;
        if (s.step != 0) {
            double n = std::max(0.0, r - s.driftFrom + 1);
            p.y -= n * s.step;
            if (s.k != 3) p.x -= n * double(s.step / 2);
        }
        return p;
    }

    sr::M3 gridRot(const State& s) const { return gridRot0_ * sr::rotY(s.gridAngle); }

    // 0x4047a0, pictures 1 and 2: the warp, per pixel
    void drawWarp(Ctx& c, const State& s, double r) {
        const Pic& p = pics_[s.k];
        sr::V3 cam = cameraAt(s, r);
        sr::M3 Rc = sr::euler(s.camRot.x, s.camRot.y, s.camRot.z);
        sr::M3 Rt = gridRot(s).transposed();
        sr::M3 toObj = Rt * Rc;
        sr::V3 camO = Rt * (cam - grid_.pos);
        float m[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m[3 * i + j] = float(toObj.m[i][j]);
        auto wrap = [](double a) { return float(std::remainder(a, 6.283185307179586)); };
        setBlend(Blend::None);
        shader_.use();
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, p.idx.id);
        gl::ActiveTexture(GL_TEXTURE1);
        gl::BindTexture(GL_TEXTURE_2D, p.palTex.id);
        shader_.set("tex", 0);
        shader_.set("pal", 1);
        shader_.set("res", float(c.w), float(c.h));
        gl::UniformMatrix3fv(shader_.loc("toObj"), 1, GL_TRUE, m);
        shader_.set("camO", float(camO.x), float(camO.y), float(camO.z));
        // 0x40c4e4: the centre of the viewport, less the rasterizers' sampling offset (0.5, 0.4375)
        shader_.set("centre", float(scene_.x0 + (scene_.x1 - scene_.x0) / 2) - 0.5f,
                    float(scene_.y0 + (scene_.y1 - scene_.y0) / 2) - 0.4375f);
        shader_.set("focal", float(s.fov * scene_.width / 320.0), float(s.fov * scene_.height / 240.0));
        shader_.set("grid", float(gridX0_), float(gridDx_));
        shader_.set("wordStep", float(wordStep_));
        const float* w = s.warp;
        shader_.set("ph", wrap(s.phase[0]), wrap(s.phase[1]), wrap(s.phase[2]));
        shader_.set("amp", w[2], w[7], w[12]);
        shader_.set("fi", w[3], w[8], w[13]);
        shader_.set("fj", w[4], w[9], w[14]);
        drawFullscreen();
        gl::ActiveTexture(GL_TEXTURE0);
    }

    // 0x4047a0, pictures 1 and 2, as the original: the 8 x 8 grid's words (truncated) through the engine
    void drawWarpEngine(Ctx& c, const State& s, double r) {
        const Pic& p = pics_[s.k];
        sr::Material& m = eng_.materialAt(matGrid_);
        m = sr::Material{};
        m.texture = m.texture2 = p.tex;
        m.table1 = p.table;
        const float* w = s.warp;
        for (int j = 0; j < gridN_; j++)
            for (int i = 0; i < gridN_; i++) {
                double fi = double(float(i)), fj = double(float(j));
                double a = double(w[3]) * fi + double(s.phase32[0]) + double(w[4]) * fj;
                double b = double(w[8]) * fi + double(s.phase32[1]) + double(w[9]) * fj;
                double cc = double(w[13]) * fi + double(s.phase32[2]) + double(w[14]) * fj;
                double kA = w[2], kB = w[7], kC = w[12], wi = wordStep_ * i, wj = wordStep_ * j;
                auto word = [](double v) { return double(int16_t(int32_t(std::trunc(v)))); };
                sr::Vertex& v = grid_.verts[size_t(j * gridN_ + i)];
                v.u = word(((std::sin(a) * kA + wi) + std::sin(b) * kB) + std::sin(cc) * kC);
                v.v = word(((std::cos(a) * kA + wj) + std::cos(b) * kB) + std::cos(cc) * kC);
                v.ev = word(((std::cos(a) * kC + wi) + std::cos(b) * kB) + std::sin(cc) * kA);   // +0x2e
                v.eu = word(((std::sin(a) * kC + wj) + std::cos(b) * kB) + std::sin(cc) * kA);   // +0x2c
                v.p = sr::V3(gridX0_ + gridDx_ * i, 0, gridX0_ + gridDx_ * j);
            }
        setObjects(s, r, false);
        eng_.render(c, scene_, p.pal, -1);
    }

    // The engine objects as the routines and 0x4047a0 left them for the frame shown at r.
    void setObjects(const State& s, double r, bool room) {
        scene_.fov = s.fov;
        scene_.near = s.nearPlane;
        scene_.ambient = s.ambient;
        bool exact = sr::fidelity().exact;
        auto ip = [&](sr::V3 v) { return exact ? sr::V3(std::trunc(v.x), std::trunc(v.y), std::trunc(v.z)) : v; };
        camera_.pos = ip(cameraAt(s, r));
        camera_.rot = sr::euler(s.camRot.x, s.camRot.y, s.camRot.z);
        grid_.rot = gridRot(s);
        grid_.flags = room || !exact ? uint32_t(sr::kHidden) : 0u;
        box_.rot = sr::euler(s.boxRot.x, s.boxRot.y, s.boxRot.z) * sr::rotY(s.boxAngle);
        box_.flags = boxFlags_ | (room ? 0u : uint32_t(sr::kHidden));
        // the shadow quad stays in the list in the warp pictures, far below the view (its texture is null there)
        quad_.flags = quadFlags_ | (room && s.room ? 0u : uint32_t(sr::kHidden));
    }

    // 0x4047a0, pictures 3 and 4: the room, the light and the shadow quad
    void drawRoom(Ctx& c, const State& s, double r) {
        const Pic& p = pics_[s.k];
        bool exact = sr::fidelity().exact;
        auto ti = [&](double v) { return exact ? std::trunc(v) : v; };
        setObjects(s, r, true);
        const Routine* R = s.room;
        if (R) {
            for (int n = 0; n < 4; n++) {
                quad_.verts[size_t(n)].p = R->quadP[n];
                quad_.verts[size_t(n)].u = R->quadUV[n][0];
                quad_.verts[size_t(n)].v = R->quadUV[n][1];
            }
            if (R->quadRot) quad_.rot = sr::euler(R->quadRot->x, R->quadRot->y, R->quadRot->z);
            for (int m = 0; m < 2; m++) {
                sr::Material& mat = eng_.materialAt(m == 0 ? matA_ : matB_);
                mat = sr::Material{};
                mat.texture = p.tex;
                mat.table1 = p.table;
                mat.fixedUV = R->fixedUV[m];
                for (int i = 0; i < 6; i++) mat.uv[i] = R->uv[m][i];
            }
            if (R->boxRaster) box_.setRaster(*R->boxRaster);
        }
        sr::Material& sm = eng_.materialAt(matShadow_);
        sm = sr::Material{};
        sm.texture = p.shadow;
        sm.table1 = p.sdw;
        // the music clock the frame routine reads: right after the retrace callback before the frame is shown
        double T = Ctx::callback(r) - 4;
        Lighting L = lighting(*c.exe, s.k, T);
        quad_.verts[1].p.x = ti(L.cornerX);
        quad_.verts[1].p.y = ti(L.cornerY);
        light_.intensity = ti(L.intensity);
        light_.pos = sr::V3(ti(L.light.x), ti(L.light.y), ti(L.light.z));
        eng_.render(c, scene_, p.pal, -1);
    }

    sr::Renderer eng_;
    sr::Scene scene_;
    sr::Object camera_, grid_, quad_, box_, light_;
    int matGrid_ = -1, matShadow_ = -1, matA_ = -1, matB_ = -1;
    uint32_t quadFlags_ = 0, boxFlags_ = 0;
    sr::M3 gridRot0_;
    sr::V3 initCamPos_;
    double initNear_ = 0x2000, initTurn_ = 0;
    double gridX0_ = 0, gridDx_ = 0, wordStep_ = 0;
    int gridN_ = 0;
    float warp0_[15] = {};
    Pic pics_[5];
    std::map<uint32_t, Routine> routines_;
    Shader shader_;
};

}  // namespace

REGISTER_SCENE(SpritesScene, "sprites");

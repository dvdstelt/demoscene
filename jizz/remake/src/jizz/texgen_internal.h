// Internal to the texture generator (texgen*.cpp): the random generator 0x141f6, the plane-mask table 0x12e4a,
// the random ops shared by the exact and the high-resolution interpreters (noise 0x132f1, blobs 0x1356a, clouds
// 0x13b5c: their random numbers do not depend on the image, so both paths replay the same sequence), the RLE
// pictures 0x13df4, and the exact and high-resolution interpreters' entry points.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "exe.h"
#include "jizz/texgen.h"

namespace jizz::texgen::detail {

// 0x141f6. The state words [0x141ea], [0x141ee], [0x141f2]; `ebx` = EBX after the call (the new first word),
// which the blobs use as the position.
struct Rng {
    uint32_t s0 = 0, s1 = 0, s2 = 0, ebx = 0;
    static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
    uint32_t next() {
        uint32_t a = s0 + 0x0e372471u, b = s1 + 0x00bc5ea9u, c = s2 + 0x00c8ade7u;
        a ^= b;
        int64_t p = int64_t(int32_t(a)) * int64_t(int32_t(c));  // imul ecx
        a = uint32_t(uint64_t(p));
        uint32_t d = uint32_t(uint64_t(p) >> 32);
        d = rol(d, 13) + a;
        c = rol(c, 14);
        d -= b;
        a ^= d;
        b += c;
        c ^= d;
        a -= b;
        b = rol(b, 9);
        uint64_t q = uint64_t(a) * c;  // mul ecx
        a = uint32_t(q);
        b ^= uint32_t(q >> 32);
        a += 0x01e42ac3u;
        b += 0x005edf04u;
        c += 0x011a2ec1u;
        s0 = b;
        s1 = c;
        s2 = a;
        ebx = b;
        return a;
    }
};

// The seed 0x12e72 stores before every script (the immediates of its first three instructions).
Rng scriptSeed(const JizzExe& exe);

// Planes of a mask byte (table 0x12e4a: first plane, planes skipped between, count). FIRST(m) = p[0].
struct Planes {
    int n = 0;
    int p[3] = {0, 0, 0};
};
Planes planesOf(const JizzExe& exe, uint8_t mask);

// Script addresses and destinations of the driver 0x12c50 (code constants of its mov/call sequence).
struct Build {
    uint32_t script;
    int id;
};
extern const std::array<Build, 16> kBuilds;  // map14 once (the driver runs it twice with the same result)

// noise 0x132f1: 0xc000 random dwords over the whole buffer (little endian)
void noise(Rng& r, uint8_t* buf);
// clouds 0x13b5c: midpoint displacement into one 256x256 plane
void clouds(Rng& r, int k, uint8_t* plane);
// blobs 0x1356a: one blob's position (x0, y0) and grey value
struct Blob {
    int x0, y0;
    uint8_t v;
};
Blob nextBlob(Rng& r);
// 0x13df4: 4-bit run-length picture, 51200 bytes (256 x 200) into the plane from its start
void rle4(const JizzExe& exe, uint32_t addr, uint8_t* plane);

// Length in bytes of the op at `a` (including the opcode). Throws on an unknown op.
uint32_t opLength(const JizzExe& exe, uint32_t a);

// ---- exact interpreter (texgen_exact.cpp)
struct ExactRun {
    std::vector<uint8_t> result;                     // W at op 0x01 (0x30000 bytes)
    int wEnd = 0;                                    // which buffer W was at op 0x01
    std::array<std::vector<uint8_t>, 4> content;     // per buffer: planes before its first format op, if any
    std::array<std::vector<uint8_t>, 4> buffers;     // B0..B3 at the end
    std::vector<std::pair<std::string, std::vector<uint8_t>>> saves;
    int closeCalls = 0;
};
// maxOps >= 0: stop after that many ops (development traces; result = W at that point)
ExactRun runExact(const JizzExe& exe, uint32_t script, bool captureSaves, int maxOps = -1);
// The driver's post-steps on exact buffers.
void stripesExact(const JizzExe& exe, uint8_t* map15);       // 0x12d87
void gradientsExact(uint8_t* blobHeights);                    // 0x12e08
int libmCloseCallsStripes(const JizzExe& exe);

// ---- high-resolution interpreter (texgen_hires.cpp)
struct HiresRun {
    bool ok = false;                                 // false: the script uses an op the float path does not have
    std::array<std::vector<float>, 3> result;        // W at op 0x01
    std::array<std::vector<float>, 3> b2;            // B2 at the end (logo2)
    int wEnd = 0;
    std::string why;
    std::array<double, 0x27> msPerOp{};              // time spent per opcode (profiling)
    int peakPlanes = 0;                              // most N x N float planes alive at once (B0..B3 + scratch)
};
HiresRun runHires(const JizzExe& exe, uint32_t script, int scale, bool emulateCarry, int maxOps = -1);
void stripesHires(int scale, std::array<std::vector<float>, 3>& map15);
void gradientsHires(int scale, std::array<std::vector<float>, 3>& blobHeights);
// The 256x256 plane (rows 200..255 zero) filtered up to N x N (Catmull-Rom, wrapping), clamped to 0..255.
std::vector<float> upsamplePicture(const uint8_t* plane, int scale);

}  // namespace jizz::texgen::detail

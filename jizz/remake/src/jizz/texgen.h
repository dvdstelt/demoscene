// The texture generator "decrunching gfx" on the CPU (no GL): the driver 0x12c50, the byte-code interpreter
// 0x12e72 with all its ops, the two RLE pictures (op 0x22, 0x13df4) and the driver's post-steps (map15's stripes
// 0x12d87, the blob gradients 0x12e08, the logo2 copy 0x12d44). See jizz/re/notes-textures.md.
//
// Two paths run every script:
//   - exact: 256x256 bytes, byte for byte what JIZZ.EXE leaves in its texture buffers (x87 at 24-bit precision
//     reproduced in portable C++: texgen_exact.cpp). The random generator 0x141f6 is replayed from the seed the
//     interpreter sets, so every texture is the original's.
//   - high resolution: the same script at N x N (N = 256 * scale) in float (texgen_hires.cpp, notes-textures §9):
//     random ops are replayed at 256x256 and filtered up, exact ops (sines, cones, angle field, curves, blobs,
//     blurs, warps, emboss, colour ops) are evaluated in continuous texture coordinates. Each result is checked
//     against the exact texture (area-downsampled mean difference) and only used where it matches.
//
// The scripts, pictures and constants are read from JIZZ.EXE by address; nothing of the original is in the source.
// Texture graphics API for the parts (GL textures): textures.h.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class JizzExe;

namespace jizz::texgen {

constexpr int kPlane = 0x10000;  // one 256x256 byte plane
constexpr int kBuf = 0x30000;    // a texture buffer: planes R (+0), G (+0x10000), B (+0x20000)

// The texture buffers by their slot in the engine's pointer table at 0x654a (pointer variable 0x654a + 4 * slot;
// slot 0 = [0x654a] is the core's light map 0x6592, not a script texture), plus the two RLE pictures as their own
// entries (not buffers of the original: the bitmaps the logo / testshit scripts decode, kept for the parts that need
// their coverage at high resolution).
enum Id : int {
    kMap1 = 1,      // [0x654e]
    kMap2,          // [0x6552]
    kMap3,          // [0x6556]
    kMap4,          // [0x655a]  original buffer interleaved
    kMap5,          // [0x655e]
    kWeirdtxt,      // [0x6562]  original buffer 16-bit 5:6:5 256x256
    kMap7,          // [0x6566]
    kMap8,          // [0x656a]
    kMap9,          // [0x656e]  never used by the intro
    kBlobHeights,   // [0x6572]  R = blob height field, G / B = signed gradients (0x12e08); no TGA
    kTestshit,      // [0x6576]  original buffer 320x200 planes (tile320); [0x662b] = its G plane
    kLogo1,         // [0x657a]  original buffer 16-bit 320x200 (tile320 + to16bit): the end picture
    kMap10,         // [0x657e]  original buffer interleaved
    kMap14,         // [0x6582]  original buffer interleaved
    kMap15,         // [0x6586]  with the stripes of 0x12d87
    kMap1a,         // [0x658a]  moved by (64, 64) after its TGA
    kLogo2,         // [0x658e]  0x20000 bytes: 16-bit 320x200, the "jizz!" picture (copied from B2 by 0x12d44)
    kPictureA,      // RLE picture at 0x275b8 ("~JIZZ~ TBL'97"), 256x200 in a 256x256 plane, grey in R = G = B
    kPictureB,      // RLE picture at 0x27d68 (Sjaak's "jizz" logo)
    kCount
};

const char* name(int id);            // "map1", ..., "blobheights", "testshit", "logo1", "pictureA", ...
uint32_t pointerVar(int id);         // 0x654a + 4 * id for the buffers, 0 for the pictures
int byPointerVar(uint32_t var);      // 0x654e..0x658e -> id, -1 otherwise
int byName(const std::string& n);    // -1 if unknown

struct Options {
    int scale = 4;              // high-resolution factor (1, 2, 4 or 8); 0 = exact textures only
    uint32_t hiresMask = ~0u;   // bit id: try the high-resolution path for this texture
    double maxMeanDiff = 4.0;   // accept a high-resolution texture when its area-downsampled mean |difference| to
                                // the exact one (0..255 units, all three planes) is at most this
    int threads = 0;            // worker threads (0 = hardware concurrency, at most 8)
    bool captureSaves = false;  // keep W at every op 0x26 (the /pandora TGA writes) for the checks
    bool keepFloats = false;    // keep Texture::hires for every high-resolution texture (else only kBlobHeights)
};

struct Texture {
    // Exact, as the original leaves its buffer after 0x12c50 (0x30000 bytes, logo2 0x20000; pictures: their plane).
    std::vector<uint8_t> original;
    // Exact content as 3 planar 256x256 byte planes R, G, B: the same as `original` for planar textures; for the
    // converted ones the planes just before the conversion (the format ops only change storage: interleave, 5:6:5,
    // 320-wide rows). For kBlobHeights G and B are int8 gradients. Pictures: the grey plane three times.
    std::vector<uint8_t> planar;
    // High resolution (when built and accepted): size x size texels; texel (x, y) of the 256x256 texture covers
    // hi-res texels [x*s, x*s+s) x [y*s, y*s+s), s = size / 256.
    //   rgb10: R, G, B / 255 as 10-bit unsigned normalised values, packed like GL_UNSIGNED_INT_2_10_10_10_REV
    //          (R in bits 0..9, alpha 3); not for kBlobHeights
    //   hires: 3 planes of floats in byte units (0..255; kBlobHeights G / B signed): kBlobHeights always, the
    //          others only with Options::keepFloats
    int size = 256;
    std::vector<uint32_t> rgb10;
    std::array<std::vector<float>, 3> hires;
    // Check of the high-resolution result against the exact one (area-downsampled to 256x256), -1 if not built.
    double meanDiff = -1, maxDiff = -1, over16 = -1;  // mean / max |difference| (0..255), fraction of values > 16
    double meanDiffCentre = -1;  // the same mean, sampled at the texel centres instead of area-downsampled
    bool hiresBuilt = false, hiresUsed = false;
    double msExact = 0, msHires = 0;
    int peakPlanes = 0;  // high-resolution working memory: most size x size float planes alive at once
};

struct Result {
    std::array<Texture, kCount> tex;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> saves;  // op 0x26: name, W (planar 0x30000 bytes)
    int libmCloseCalls = 0;  // roundings of fsin/fcos/fpatan results within 4 ulps of a rounding boundary (see
                             // texgen_exact.cpp: 0 means every libm with errors below 4 ulps gives the same bytes)
    double msTotal = 0;
    std::array<double, 0x27> msPerOp{};  // high-resolution time per opcode, all scripts (profiling)
};

// Runs everything (threads inside). Throws std::runtime_error on a bad script or exe address.
Result build(const JizzExe& exe, const Options& opt = {});

// Development: for the script that builds `id`, after each op the mean |difference| (circular, 0..128) between the
// exact W and the high-resolution W area-downsampled, per plane: one line per op.
std::string trace(const JizzExe& exe, int id, int scale);

// The TGA the /pandora mode writes for a saved W (header from 0x14349, B, G, R per pixel, top row first).
std::vector<uint8_t> tgaBytes(const JizzExe& exe, const std::vector<uint8_t>& w);

}  // namespace jizz::texgen

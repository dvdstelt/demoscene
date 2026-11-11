// The intro's textures for the parts: what the texture generator "decrunching gfx" (driver 0x12c50, interpreter
// 0x12e72, jizz/re/notes-textures.md) builds, as GL textures and as the exact bytes. The generator itself is
// texgen.h (CPU, no GL).
//
//   #include "jizz/textures.h"
//   const jizz::Textures& tx = c.shared<jizz::Textures>();   // built once (threads, ~0.3 s), in init()
//   tx.bind(jizz::Tex::Map5, 0);                             // or tx.id(jizz::Tex::Map5)
//   tx.byPointer(0x6556) == jizz::Tex::Map3;  tx.bySlot(14) == jizz::Tex::Map14;
//
// GL textures: GL_REPEAT, linear filtering with trilinear mipmaps and anisotropy, colours 0..1 (byte / 255), row 0
// of the original at t = 0 (texel (x, y) of the 256x256 original at (u, v) = ((x + 0.5) / 256, (y + 0.5) / 256)).
// Most are 1024x1024, evaluated at four times the resolution (texgen_hires.cpp; checked against the exact texture,
// see Textures::data()); the others the exact 256x256 bytes. Same content either way: sample in texture units.
//   - BlobHeights ([0x6572]): RGB16F, R = height / 255, G and B = the signed gradients (int8 / 255, about -0.5..0.5;
//     0x12e08: B(x, y) = R(x, y + 1) - R(x, y - 1), G(x, y) = R(x + 2, y) - R(x, y)).
//   - Testshit, Logo1, Logo2 are 320x200 pictures in the original (tile320 / 5:6:5): screen pixel (x, y) is texel
//     (x mod 256, y), so sample u = x / 256 (repeat), v = y / 256. Rows 200..255 are black.
//   - PictureA / PictureB: the two RLE bitmaps alone (grey in R = G = B), e.g. as coverage for the "jizz!" picture.
// Storage conversions of the original (interleaved RGB for the engine, 5:6:5 copies, 320-wide rows) are not
// separate textures: they hold the same content. Changes the parts make to a buffer are the parts' business:
//   - the tunnel part 0x169e7 copies map5 over map1 (engine slot 1 = map5 from 4:00 on): bySlot(1, true);
//   - the "jizz" part 0x1212a marks an outline into logo2 [0x658e] in place (black pixels next to non-black ones);
//   - the 16-bit pictures' transparency (0x174e5 weirdtxt sprites, 0x1212a logo2) is "5:6:5 word == 0": rgb565().
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "demo.h"
#include "jizz/texgen.h"

namespace jizz {

// Values = slot numbers of the engine's texture table at 0x654a (pointer variable 0x654a + 4 * slot).
enum class Tex : int {
    None = 0,         // slot 0: the light map [0x654a] (0x6592, core library), not a script texture
    Map1 = 1,         // [0x654e] ribbons ceiling from 2:00 (green/rainbow with glass logo)
    Map2,             // [0x6552] tube background
    Map3,             // [0x6556] the 2D swirl of the tunnel part
    Map4,             // [0x655a] spiky object
    Map5,             // [0x655e] tunnel torus, blob background, "jizz" warp, radial zoom
    Weirdtxt,         // [0x6562] glass-letter sprites (original 16-bit 256x256)
    Map7,             // [0x6566] radial zoom
    Map8,             // [0x656a] ribbons ceiling/floor, spiky background
    Map9,             // [0x656e] never used by the intro (exact only)
    BlobHeights,      // [0x6572] TBL bump map: height and signed gradients
    Testshit,         // [0x6576] TBL: R = three soft cones, G = the "TBL" mask ([0x662b]), 320x200
    Logo1,            // [0x657a] the end picture, 320x200
    Map10,            // [0x657e] tube torus, blobs, ribbons backdrop
    Map14,            // [0x6582] ribbons, tube ball and spikes
    Map15,            // [0x6586] field lines (with the stripes of 0x12d87)
    Map1a,            // [0x658a] ribbons floor from 2:00
    Logo2,            // [0x658e] the "jizz!" picture, 320x200
    PictureA,         // RLE bitmap 0x275b8 ("~JIZZ~ TBL'97")
    PictureB,         // RLE bitmap 0x27d68 (the "jizz" logo)
};
constexpr int kTexCount = int(Tex::PictureB) + 1;

class Textures {
public:
    explicit Textures(Ctx& c);  // for c.shared<jizz::Textures>(): builds everything (worker threads) and uploads
    ~Textures();
    // Optional: starts the CPU build (~0.3 s, no GL) on a background thread, e.g. right after JIZZ.EXE is loaded;
    // the constructor then waits for it and only uploads. `exe` must outlive the build.
    static void prefetch(const JizzExe& exe);
    Textures(const Textures&) = delete;
    Textures& operator=(const Textures&) = delete;

    // ---- GL
    GLuint id(Tex t) const;                  // 0 for Tex::None
    void bind(Tex t, int unit) const;
    int size(Tex t) const;                   // texels per side of the GL texture: 1024 (high resolution) or 256
    bool hires(Tex t) const;
    // GL textures of the engine's slots 1..17 (EngineShading::tex); slot 0 stays 0. map5InSlot1: from the tunnel
    // part on (0x169e7 copies map5 into slot 1).
    std::array<GLuint, 18> slotIds(bool map5InSlot1 = false) const;

    // ---- CPU, exact (byte for byte the original's)
    // Content as 3 planes of 256x256 bytes (R, G, B; texel (x, y) at plane * 65536 + y * 256 + x), before any format
    // conversion. BlobHeights G / B are int8.
    const uint8_t* planar(Tex t) const;
    uint8_t texel(Tex t, int plane, int x, int y) const { return planar(t)[plane * 65536 + (y & 255) * 256 + (x & 255)]; }
    // The buffer exactly as the original leaves it after 0x12c50 (interleaved / 16-bit / 320-wide as the notes say).
    const std::vector<uint8_t>& original(Tex t) const;
    // 16-bit 5:6:5 pixel of Weirdtxt (x, y < 256), Logo1 / Logo2 (320x200) as the original stores it.
    uint16_t rgb565(Tex t, int x, int y) const;

    // ---- lookups
    static Tex byPointer(uint32_t var);                    // 0x654e..0x658e; Tex::None otherwise
    static Tex bySlot(int slot, bool map5InSlot1 = false);  // 1..17
    static Tex byName(const std::string& n);               // "map1", "map1a", "weirdtxt", "testshit", "logo2", ...
    static const char* name(Tex t);
    static uint32_t pointerVar(Tex t);                     // 0x654a + 4 * slot; 0 for the pictures

    // Build details: times, the high-resolution check per texture (texgen::Texture::meanDiff ...).
    const texgen::Result& data() const { return *res_; }

private:
    std::unique_ptr<texgen::Result> res_;
    std::array<Texture, kTexCount> gl_{};
};

}  // namespace jizz

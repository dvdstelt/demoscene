// The original's pencil line and its darkening of the picture, at any resolution.
//
// Ports (paper/re/notes-init.md §5.3, §5.6, §8.1-8.3; notes-sketch.md §3.4-3.5, §7, §8):
//   init    darkness bytes [0x1ee09] (1024 x 26..31, shade-table rows) and endpoint jitter bytes [0x1ee0d] (1024 x -3..2)
//           from the random generator 0xaa2 (RngPos::kGreyTable, kJitterTable)
//   0x18b7  sketchy line: 3 passes of 0x1612 between the endpoints offset by per-endpoint jitter
//           J = jitter + ((x*y + y) & 0x1ff) (pass k: x += J[2k+1], y += J[2k]), all passes reading the darkness bytes
//           from offset (x0 + y0 + x1 + y1) & 0xff of the integer (pixel) endpoints
//   0x1612  2-pixel DDA: steps along the major axis from the top (y-major) or left (x-major) end, the last pixel not
//           drawn; per step the pixel and its right (y-major) / lower (x-major) neighbour become SHADE[d[2k]][pix],
//           SHADE[d[2k+1]][pix]; upward x-major lines round their row up (+1 px lower than downward ones)
//   0x144b  SHADE[row][c]: moves colour c down its palette ramp by (32 - row) steps (32-entry ramps: 1 entry per
//           step; the 64-entry grey ramp: 2 entries per step), clamped at the ramp's black end
//   0x1943  the paper: SHADE[paper[i]][pix] for every pixel, paper rows 28..31 (60..63 in the pencil's shadow)
//
// The darkening is a subtraction of ramp steps, clamped at the black end, so any number of pencil passes, the
// paper and the shadow add up (max(0, max(0, p - a) - b) = max(0, p - a - b)): they are accumulated in a
// darkness layer (PencilInk, 1 unit = 1 ramp step) and applied to the RGB frame once (PencilInk::resolve), which
// finds each pixel's palette ramp and moves the colour down that ramp. So it works on any RGB frame made of the
// palette's colours (sky, ground, faces, canyon): a pencil line over a red face becomes darker red, over the sky
// darker blue, as in the original.
//
// High resolution: each pass is a band 2 VGA pixels wide along the minor axis (the original's 2-pixel run) around
// the line through the (float) endpoints plus the original's integer jitter, antialiased analytically. The darkness
// along the band is the original's byte sequence at the original's spacing (1 VGA px per step, the two halves of the
// band taking d[2k] and d[2k+1]), with soft cell edges, plus a finer grain at the target resolution. The jitter
// and the darkness offset hash the integer pixel of each endpoint (floor of the VGA position), as the original does,
// so the strokes are where the original's are and "boil" when an endpoint crosses a pixel.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "demo.h"
#include "math3d.h"

namespace paper {

// ---- palette ramps used for face colours and for the darkening (any 256-colour palette, display floats 0..1)
struct ShadePalette {
    std::array<Vec3, 256> rgb{};
    ShadePalette() = default;
    explicit ShadePalette(const std::array<Vec3, 256>& c) : rgb(c) {}
    static ShadePalette fromExe(const PaperExe& exe);       // the default palette 0x169a3
    // Replace entries first..first+n-1 by 6-bit RGB triples (e.g. the teal part's 0x158e(0x681a): 32 at 128).
    void setEntries6(int first, const uint8_t* rgb6, int n);
    // Colour at a fractional palette index, linear inside the ramp of `rampIndex` (default: the ramp of `index`)
    // and clamped to it: grey ramp 0..63, then 32-entry ramps.
    Vec3 at(double index, int rampIndex = -1) const;
};

// ---- the init tables
struct PencilTables {
    uint8_t dark[1024];   // [0x1ee09]: shade rows 26..31 (darkening 32 - row = 1..6 steps)
    int8_t jitter[1024];  // [0x1ee0d]: -3..2
    explicit PencilTables(const PaperExe& exe);
};

// One 0x18b7 line. Endpoints in continuous VGA coordinates (a projected 3D point as it is; an integer pixel (X, Y) of
// the original's 2D code is (X + 0.5, Y + 0.5): use PencilLine::pixels).
struct PencilLine {
    Vec2f a, b;
    float t0 = 0, t1 = 1;   // the drawn part, as fractions of the way from a to b (partial strokes)
    float strength = 1;     // darkness multiplier (1 = the original)
    PencilLine() = default;
    PencilLine(Vec2f a_, Vec2f b_, float t0_ = 0, float t1_ = 1) : a(a_), b(b_), t0(t0_), t1(t1_) {}
    static PencilLine pixels(int x0, int y0, int x1, int y1) {
        return PencilLine(Vec2f{x0 + 0.5f, y0 + 0.5f}, Vec2f{x1 + 0.5f, y1 + 0.5f});
    }
};

// One pass of a line as the GPU draws it (built by addPencilPasses; also used by the 3D renderer).
struct PencilPass {
    float seg[4];    // ordered start (k = 0 end) and end, VGA
    float prm[4];    // x-major (0/1), minor-axis bias, drawn range on the major axis [lo, hi]
    float tex[4];    // darkness offset, major coordinate of step k = 0, strength, grain seed
    float plane[4];  // camera-space plane (n.xyz, d) of the face for the depth test (3D edges), else 0
    float zinv[2];   // 1/z at the start and end (3D edges), else 0
};
// Append the 3 passes of `line` (0x18b7). plane / zinv as above (nullptr: a 2D line).
void addPencilPasses(const PencilTables& t, const PencilLine& line, std::vector<PencilPass>& out,
                     const float* plane = nullptr, const float* zinv = nullptr);

// ---- GPU resources (ctx.shared<PencilGpu>()): tables, shaders, buffers
struct PencilGpu {
    PencilTables tables;
    Texture darkTex;             // 1024x1 R32F: 32 - dark[i]
    Shader strokeInk;            // passes into a darkness target (R16F)
    Shader strokeMrt;            // passes into the 3D renderer's MSAA target (colour + darkness, depth from the face plane)
    Shader resolveShader;        // frame = darken(frame copy, ink)
    Shader addShader;            // ink += texture * mul + add
    GLuint vao = 0, vbo = 0;
    ShadePalette defaultPalette;
    explicit PencilGpu(Ctx& c);
    // Upload and draw passes with the bound program (instanced quads). `pxVga` = one target pixel in VGA units.
    void drawPasses(const std::vector<PencilPass>& passes, const Shader& sh, int w, int h);
    void setRampUniforms(const Shader& sh, const ShadePalette& pal);
};

// GLSL: `vec3 rampDarken(vec3 c, float steps)` with the uniforms set by PencilGpu::setRampUniforms (prepend to a
// fragment shader to darken colours the way the shade table does).
extern const char* kRampDarkenGLSL;

// ---- the darkness layer
// Usage per frame:   PencilInk ink(ctx, frame);   ink.line(...);   [Renderer3D::end(..., &ink)]
//                    ink.addTexture(paper, -1, 32);   ink.resolve(ctx, frame);
class PencilInk {
public:
    PencilInk(Ctx& c, const Target& frame);   // borrows a frame-sized R16F target, cleared to 0
    void line(const PencilLine& l) { addPencilPasses(gpu_.tables, l, pending_, nullptr, nullptr); }
    void lines(const std::vector<PencilLine>& ls) { for (const auto& l : ls) line(l); }
    // Darkness from a texture over the whole frame: ink += mul * tex(vga).r + add. The texture covers VGA
    // 0..320 x 0..200 with its first row at the top (as uploaded images are); bicubic: Catmull-Rom sampling.
    void addTexture(GLuint tex, float mul, float add, bool bicubic = true);
    void addConstant(float steps) { addTexture(0, 0, steps, false); }
    // Apply the darkness to `frame` (in place) with the ramps of `pal` (default palette when null).
    void resolve(Ctx& c, Target& frame, const ShadePalette* pal = nullptr);
    Target& target() { flush(); return *t_; }
    void flush();                       // draw the queued lines

private:
    Ctx& c_;
    PencilGpu& gpu_;
    TargetPool::Lease t_;
    std::vector<PencilPass> pending_;
};

}  // namespace paper

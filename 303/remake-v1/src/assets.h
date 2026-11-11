// Loaders for the original 303 data files (MUSTARD.PAK, *.LZW, 3D objects).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

Bytes readFile(const std::string& path);

// Variable-width LZW as used by 303 (Nelson's LZW15V: 256=end, 257=bump width, 258=flush, first code 259).
Bytes unlzw(const uint8_t* data, size_t size, size_t maxOut = 0);

// MUSTARD.PAK: "ALEX", count, directory offset, then LZW-compressed entries.
class Pak {
public:
    bool load(const std::string& path);
    const Bytes& get(const std::string& name) const;
    bool has(const std::string& name) const { return files_.count(name) != 0; }
private:
    std::map<std::string, Bytes> files_;
};

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4
};

// 8-bit image with a 768-byte 6-bit VGA palette in front.
Image imagePaletted(const Bytes& raw, int w, int h, int yOffset = 0, int rows = -1);
// 8-bit grayscale (no palette); maps value to white with alpha = value.
Image imageGray(const Bytes& raw, int w, int h, int yOffset = 0, int rows = -1);
// Palette of a paletted .raw as float RGB 0..1.
std::vector<float> rawPalette(const Bytes& raw);

// Video codec: 160x96 luma + 2x 80x48 chroma (YIQ), 4x4 DCT blocks, 1-byte quality header.
struct Video {
    int w = 160, h = 96;
    int frames = 0;
    std::vector<uint8_t> rgba;  // frames * w * h * 4, decoded in floating point
    bool load(const std::string& path, bool lumaOnly = false);  // lumaOnly: greyscale 160x96 frames (PARANOID)
    const uint8_t* frame(int i) const { return rgba.data() + size_t(i) * w * h * 4; }
};

struct Mesh {
    std::vector<float> pos;     // xyz
    std::vector<float> nrm;     // xyz (computed)
    std::vector<float> extra;   // per-vertex scalar (radius for spikey, group for icosa)
    std::vector<uint32_t> idx;  // triangles
    float radius = 1.0f;
    void computeNormals();
    void normalize(float target = 1.0f);
};

Mesh meshSpikey(const Bytes& raw);  // dir.xyz + radius per vertex
Mesh meshIcosa(const Bytes& raw);   // header 3 ints, xyz + group per vertex
Mesh meshFace(const Bytes& raw);    // plain xyz

std::string dataPath(const std::string& name);  // resolves a file in the data directory
void setDataDir(const std::string& dir);

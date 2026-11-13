// clone meets clone's music module, built the way the intro builds it at start-up (0x414aee, clone/re/notes-music.md
// section 2): the stored part of an MXM module is copied from the program image and 20 16-bit samples are
// synthesised behind it by the drum synthesiser 0x4149b4 and the tone synthesiser 0x414a38.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace modsynth {

constexpr uint32_t kImageBase = 0x400000;  // image[0] is linear address 0x400000

// The 1,200,000-byte buffer of 0x414aee (the heap block at 0x428470 in the capture), byte for byte: the module
// (header, instruments, patterns, the 8-bit sample block), the 20 synthesised samples with one zero word after
// each, and zeros up to the end of the allocation. Throws std::runtime_error if the image is not CLONE.EXE's.
std::vector<uint8_t> buildModule(std::span<const uint8_t> image);

}  // namespace modsynth

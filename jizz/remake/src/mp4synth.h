// JIZZ's music module, built from JIZZ.EXE the way the intro builds it at "extracting music": Probe's "mp4
// cruncher" 0x1632b copies the stored part of the MXM module (0x1f644) and synthesises the 32 phrase samples from
// the 12 synth records at 0x16423 (jizz/re/notes-music.md section 2).
#pragma once
#include <cstdint>
#include <vector>

class JizzExe;

// The module as the intro writes it (dump/smokeout.mxm without the zeroed buffer behind it): 32,625 stored bytes
// followed by 32 x 28,001 synthesised bytes = 928,657 bytes. Throws std::out_of_range for a bad image.
std::vector<uint8_t> expandModule(const JizzExe& exe);

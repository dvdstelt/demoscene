// The demo's two scripts, `script/demo1.scr` (part I) and `script/demo2.scr` (part II), read from VIVID.EXE's
// archives as the original reads them (vividexp/re/NOTES.md "The script", notes-core.md):
//   0x40b6e4 reads the file into lines of 128 bytes, ending a line at each CR and skipping the byte after it (the
//   LF); 0x40b760 runs lines 1 .. count - 1 (line 0 names the module, 0x40b5a0), skips those whose first character
//   is '#', and reads each with sscanf "%s %s %s %s": POS (order), PAT (row), the command and the first argument;
//   "xxx" leaves POS at 999 and PAT at the value of the line before. Each command reads its own arguments again
//   with its own format. The interpreter compares the command word with its own strings, one after the other; the
//   remake reads those strings from VIVID.EXE (kCommandName) and gives each line its Cmd.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class VividExe;

namespace vivid {

// The commands the interpreter knows (0x40b760), in the order of kCommandName.
enum class Cmd {
    None, LoadXM, ScriptJMP, WaitSYNC, ClearSCR, InitBITMAPEFEK, DeinitBITMAPEFEK, RunBITMAPEFEK, ClearEFEKPARAMS,
    InitBUMP, DeinitBUMP, RunBUMPEFEK, InitPCX, DeinitPCX, PutPCXSYNC, InitZOOM, DeinitZOOM, RunZOOMSYNC, NegPAL,
    LoadPAL, InitFADE, WaterInit, WaterEffect, GenericInit, GenericEffect, Other
};
// The addresses of the interpreter's command strings, for Cmd::LoadXM .. Cmd::GenericEffect.
extern const uint32_t kCommandName[int(Cmd::Other) - 1];

struct ScriptLine {
    int part = 1;                   // 1 or 2
    int line = 0;                   // line number in the file (0 = the module's line)
    std::string text;               // the line as the original reads it (up to 127 characters)
    std::vector<std::string> tok;   // its whitespace-separated words: tok[0] POS, tok[1] PAT, tok[2] the command, ...
    int pos = 999;                  // POS, 999 for "xxx"
    int pat = 0;                    // PAT; "xxx" keeps the previous line's value, as the original's stack variable
    bool posGiven = false, patGiven = false;
    Cmd cmd = Cmd::None;            // the command word as the interpreter matches it (Other: a word it does not know)
    const std::string& command() const { static const std::string none; return tok.size() > 2 ? tok[2] : none; }
    // Argument k after the command (k = 0: the first), or "" if the line has fewer.
    std::string arg(size_t k) const { return tok.size() > 3 + k ? tok[3 + k] : std::string(); }
    int argInt(size_t k) const;
    double argFloat(size_t k) const;
};

// The lines the interpreter runs, in order (comments and line 0 left out), with their commands matched against the
// interpreter's strings in `exe`. `module` gets line 0's file name.
std::vector<ScriptLine> parseScript(const VividExe& exe, std::span<const uint8_t> file, int part,
                                    std::string* module = nullptr);

}  // namespace vivid

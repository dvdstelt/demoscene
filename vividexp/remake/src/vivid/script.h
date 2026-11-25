// The demo's two scripts, `script/demo1.scr` (part I) and `script/demo2.scr` (part II), read from VIVID.EXE's
// archives as the original reads them (vividexp/re/NOTES.md "The script", notes-core.md):
//   0x40b6e4 reads the file into lines of 128 bytes, ending a line at each CR and skipping the byte after it (the
//   LF); 0x40b760 runs lines 1 .. count - 1 (line 0 names the module, 0x40b5a0), skips those whose first character
//   is '#', and reads each with sscanf "%s %s %s %s": POS (order), PAT (row), the command and the first argument;
//   "xxx" leaves POS at 999 and PAT at the value of the line before. Each command reads its own arguments again
//   with its own format.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vivid {

struct ScriptLine {
    int part = 1;                   // 1 or 2
    int line = 0;                   // line number in the file (0 = the module's line)
    std::string text;               // the line as the original reads it (up to 127 characters)
    std::vector<std::string> tok;   // its whitespace-separated words: tok[0] POS, tok[1] PAT, tok[2] the command, ...
    int pos = 999;                  // POS, 999 for "xxx"
    int pat = 0;                    // PAT; "xxx" keeps the previous line's value, as the original's stack variable
    bool posGiven = false, patGiven = false;
    const std::string& command() const { static const std::string none; return tok.size() > 2 ? tok[2] : none; }
    // Argument k after the command (k = 0: the first), or "" if the line has fewer.
    std::string arg(size_t k) const { return tok.size() > 3 + k ? tok[3 + k] : std::string(); }
    int argInt(size_t k) const;
    double argFloat(size_t k) const;
};

// The lines the interpreter runs, in order (comments and line 0 left out). `module` gets line 0's file name.
std::vector<ScriptLine> parseScript(std::span<const uint8_t> file, int part, std::string* module = nullptr);

}  // namespace vivid

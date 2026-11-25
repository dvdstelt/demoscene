// Reads the scripts as 0x40b6e4 and 0x40b760 do (script.h).
#include "vivid/script.h"

#include <cstdlib>
#include <sstream>

namespace vivid {

int ScriptLine::argInt(size_t k) const { return std::atoi(arg(k).c_str()); }
double ScriptLine::argFloat(size_t k) const { return std::atof(arg(k).c_str()); }

std::vector<ScriptLine> parseScript(std::span<const uint8_t> file, int part, std::string* module) {
    // 0x40b6e4: a line ends at a CR, the byte after the CR is skipped; a last line without a CR is not counted
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < file.size(); ++i) {
        if (file[i] == 0x0d) {
            lines.push_back(cur.substr(0, 127));
            cur.clear();
            ++i;
        } else {
            cur.push_back(char(file[i]));
        }
    }
    std::vector<ScriptLine> out;
    int pat = 0;
    for (size_t n = 0; n < lines.size(); ++n) {
        ScriptLine l;
        l.part = part;
        l.line = int(n);
        l.text = lines[n];
        std::istringstream in(l.text);
        for (std::string w; in >> w;) l.tok.push_back(w);
        if (n == 0) {   // the module's line (0x40b5a0): "xxx xxx LoadXM <file>"
            if (module) *module = l.arg(0);
            continue;
        }
        if (!l.text.empty() && l.text[0] == '#') continue;
        if (l.tok.size() > 0 && l.tok[0] != "xxx") { l.pos = std::atoi(l.tok[0].c_str()); l.posGiven = true; }
        if (l.tok.size() > 1 && l.tok[1] != "xxx") { pat = std::atoi(l.tok[1].c_str()); l.patGiven = true; }
        l.pat = pat;
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace vivid

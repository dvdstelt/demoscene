// Reads the scripts as 0x40b6e4 and 0x40b760 do (script.h).
#include "vivid/script.h"

#include <cstdlib>
#include <sstream>

#include "exe.h"

namespace vivid {

const uint32_t kCommandName[int(Cmd::Other) - 1] = {
    0x8210ac, 0x8210e2, 0x8210ec, 0x8210f5, 0x8210fe, 0x82111c, 0x82112d, 0x82113b,   // LoadXM .. ClearEFEKPARAMS
    0x82114b, 0x821163, 0x82116e, 0x82117a, 0x821194, 0x8211ad, 0x8211d9, 0x8211f1,   // InitBUMP .. DeinitZOOM
    0x8211fc, 0x821220, 0x821227, 0x82122f, 0x8212c9, 0x8212f5, 0x82133f, 0x82135b,   // RunZOOMSYNC .. GenericEffect
};

namespace {
constexpr uint32_t kNoPosition = 0x8210da;
}  // namespace

int ScriptLine::argInt(size_t k) const { return std::atoi(arg(k).c_str()); }
double ScriptLine::argFloat(size_t k) const { return std::atof(arg(k).c_str()); }

std::vector<ScriptLine> parseScript(const VividExe& exe, std::span<const uint8_t> file, int part, std::string* module) {
    std::vector<std::string> names;
    for (uint32_t a : kCommandName) names.push_back(exe.cstr(a));
    const std::string none = exe.cstr(kNoPosition);   // "xxx", which 0x40b760 compares POS and PAT with
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
        if (l.tok.size() > 0 && l.tok[0] != none) { l.pos = std::atoi(l.tok[0].c_str()); l.posGiven = true; }
        if (l.tok.size() > 1 && l.tok[1] != none) { pat = std::atoi(l.tok[1].c_str()); l.patGiven = true; }
        l.pat = pat;
        l.cmd = Cmd::Other;
        for (size_t k = 0; k < names.size(); ++k)
            if (l.command() == names[k]) l.cmd = Cmd(k + 1);
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace vivid

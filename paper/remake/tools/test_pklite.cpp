// Test for src/pklite.cpp. Run from paper/remake:
//   test_pklite [PAPER.EXE] [paper_img.bin]        (defaults: ../PAPER.EXE ../re/paper_img.bin)
// 1. unpklite(PAPER.EXE) must equal paper_img.bin (the image made by running PKLITE's stub in an emulator) byte for
//    byte; the first mismatch is printed otherwise.
// 2. PaperExe::load must accept PAPER.EXE.
// 3. Damaged copies (not MZ, truncated, other versions and flags, bytes changed) must be rejected with a message or
//    unpack without crashing. Build with -fsanitize=address,undefined to check the "without crashing" part.
#include "exe.h"

#include <cstdio>
#include <random>
#include <map>
#include <string>
#include <vector>

namespace {

bool readFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

void put16(std::vector<uint8_t>& d, size_t at, uint16_t v) {
    d[at] = uint8_t(v);
    d[at + 1] = uint8_t(v >> 8);
}

struct Robustness {
    explicit Robustness(const std::vector<uint8_t>& reference) : ref(reference) {}
    const std::vector<uint8_t>& ref;
    int rejected = 0, same = 0, different = 0, silent = 0;
    std::map<std::string, std::pair<int, std::string>> messages;  // kind -> count, example

    void run(const std::vector<uint8_t>& exe) {
        std::vector<uint8_t> img;
        std::string err;
        if (!unpklite(exe, img, &err)) {
            ++rejected;
            if (err.empty()) ++silent;
            std::string kind = err.substr(0, err.find(" ("));  // and at most four words
            for (size_t at = 0, words = 0; (at = kind.find(' ', at + 1)) != std::string::npos;)
                if (++words == 4) kind.resize(at);
            auto& m = messages[kind];
            if (m.first++ == 0) m.second = err;
        } else if (img == ref) {
            ++same;
        } else {
            ++different;
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    const char* exePath = argc > 1 ? argv[1] : "../PAPER.EXE";
    const char* refPath = argc > 2 ? argv[2] : "../re/paper_img.bin";
    std::vector<uint8_t> exe, ref;
    if (!readFile(exePath, exe) || !readFile(refPath, ref)) {
        std::printf("cannot read %s or %s\n", exePath, refPath);
        return 2;
    }
    bool ok = true;

    // 1. Byte-identical with the reference.
    std::vector<uint8_t> img;
    std::string err;
    if (!unpklite(exe, img, &err)) {
        std::printf("FAIL unpklite: %s\n", err.c_str());
        return 1;
    }
    size_t n = img.size() < ref.size() ? img.size() : ref.size(), i = 0;
    while (i < n && img[i] == ref[i]) ++i;
    if (i < n) {
        std::printf("FAIL first mismatch at %05zx: got %02x, reference %02x\n", i, img[i], ref[i]);
        ok = false;
    }
    if (img.size() != ref.size()) {
        std::printf("FAIL size %05zx, reference %05zx\n", img.size(), ref.size());
        ok = false;
    }
    if (ok) std::printf("ok   unpklite: %05zx bytes, identical to %s\n", img.size(), refPath);

    // 2. The remake's loader.
    PaperExe paper;
    if (paper.load(exePath, &err)) {
        std::printf("ok   PaperExe::load\n");
    } else {
        std::printf("FAIL PaperExe::load: %s\n", err.c_str());
        ok = false;
    }

    // 3. Damaged inputs.
    Robustness r{ref};
    std::mt19937 rng(303);
    r.run({});
    r.run({'M', 'Z'});
    r.run(std::vector<uint8_t>(exe.begin(), exe.begin() + 0x1f));
    r.run(std::vector<uint8_t>(4096, 0));
    {
        std::vector<uint8_t> noise(70000);
        for (uint8_t& b : noise) b = uint8_t(rng());
        r.run(noise);
        noise[0] = 'M', noise[1] = 'Z';
        r.run(noise);
    }
    // Header fields: PKLITE version word, entry point, sizes, MZ relocations.
    for (uint16_t ver : {0x210e, 0x2110, 0x220f, 0x010f, 0x310f, 0x610f, 0xa10f})
        { auto d = exe; put16(d, 0x1c, ver); r.run(d); }
    for (size_t at : {0x02, 0x04, 0x06, 0x08, 0x0e, 0x10, 0x14, 0x16, 0x18, 0x1e, 0x52, 0x54})
        for (uint16_t v : {0x0000, 0x0001, 0x01ff, 0x0200, 0x7fff, 0x8000, 0xfff0, 0xffff})
            { auto d = exe; put16(d, at, v); r.run(d); }
    // Stub immediates: memParas, compParas, stackParas, copyParas, copyWords, copySrc, jump.
    for (size_t at : {0xa1, 0xa4, 0xad, 0xcb, 0xd0, 0xd7, 0xdd})
        for (uint16_t v : {0x0000, 0x0001, 0x0010, 0x0f8d, 0x1000, 0x269d, 0x8fff, 0x9000, 0xfffe, 0xffff})
            { auto d = exe; put16(d, at, v); r.run(d); }
    // Truncated files, and truncated files whose header claims the shorter size.
    for (size_t len = 0; len < exe.size(); len += len < 0x400 ? 1 : 17) {
        std::vector<uint8_t> d(exe.begin(), exe.begin() + len);
        r.run(d);
        if (len >= 0x20) {
            put16(d, 2, uint16_t(len % 512));
            put16(d, 4, uint16_t((len + 511) / 512));
            r.run(d);
        }
    }
    // Single bytes changed (every byte of header and stub, then a sample of the compressed data).
    for (size_t at = 0; at < exe.size(); at += at < 0x400 ? 1 : 11) {
        auto d = exe;
        d[at] ^= uint8_t(1 + rng() % 255);
        r.run(d);
    }
    // Random bursts in the compressed data and the relocation table at its end.
    for (int k = 0; k < 1000; ++k) {
        auto d = exe;
        size_t at = 0x330 + rng() % (d.size() - 0x330), len = 1 + rng() % 64;
        for (size_t j = at; j < at + len && j < d.size(); ++j) d[j] = uint8_t(rng());
        r.run(d);
    }
    std::printf("%s damaged inputs: %d rejected, %d unpacked to a different image, %d unpacked unchanged\n",
                r.silent ? "FAIL" : "ok  ", r.rejected, r.different, r.same);
    if (r.silent) {
        std::printf("FAIL %d rejections without a message\n", r.silent);
        ok = false;
    }
    std::printf("     messages (count, example):\n");
    for (const auto& [kind, m] : r.messages) std::printf("     %6d  %s\n", m.first, m.second.c_str());

    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}

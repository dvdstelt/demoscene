// STARS.EXE access: read the file, unpack it (unpack.cpp), check that it is the released demo, read by address and
// by overlay file number.
#include "exe.h"

#include <cstdio>
#include <stdexcept>

namespace {
bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint64_t fnv1a(const std::vector<uint8_t>& d) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : d) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

// Size and FNV-1a hash of the image and of every overlay file of the released STARS.EXE (1,224,140 bytes,
// NoooN 1995), i.e. of stars/re/unpacked/stars_img.bin and files/NN.bin.
struct Known { uint32_t size; uint64_t hash; };
constexpr Known kImage{0x840b0, 0x0cc5f2eca4cd8907ull};
constexpr Known kFileHash[StarsExe::kFiles + 1] = {
    {0, 0},
    {0xbe910, 0xb2d17c0586a70fc3ull}, {0x10000, 0xa34c6c84c842dd7eull}, {0x10000, 0x985bd74f9de64508ull},
    {0x300, 0x630b2f8151fa5390ull},   {0x512d, 0x1e57205c38409f27ull},  {0x9430, 0x87d5f7b862317078ull},
    {0x101c0, 0x785894c3be9b443dull}, {0xfd00, 0x9cd832977c4cb83eull},  {0xfd00, 0x1f41750fa2369047ull},
    {0xfd00, 0xd5a1e98c5644f483ull},  {0xfd00, 0xbf6e04d9460b5e80ull},  {0xfd00, 0xd6609294503d50f4ull},
    {0xfd00, 0xdc0af66d92015f82ull},  {0xfd00, 0x7c509a210754b0a9ull},  {0x300, 0x975e8b2ccfa64a49ull},
    {0x10000, 0x5a5be22d8b180dedull}, {0x10000, 0x6cc790de7bc6edddull}, {0x8038, 0xced7f8723df574bcull},
    {0xfa00, 0xe223230a765c3bb7ull},  {0x300, 0x7caf77ee827ea4c8ull},   {0x10000, 0x094698516c5718f5ull},
    {0x300, 0x6dadf90667c06cb5ull},   {0x10000, 0x5b0fd2ea01698a45ull}, {0x1a838, 0x9229de300352e14eull},
    {0xfa00, 0xe7eeab579f75e6d9ull},  {0x300, 0x1c3f273a36713252ull},   {0xfa00, 0x4c4ddcb5e3670f90ull},
    {0x22498, 0x3f58cf84d47598f4ull}, {0x102ff, 0x8890ce97d4e0aea9ull}, {0x1000a, 0x271f96909b5363e2ull},
    {0x300, 0x2d9f53fa0363b38bull},   {0xb40, 0x4538c476d597c132ull},   {0x1130, 0x0ac3ae6e480853d2ull},
    {0xd834, 0x5a8ad026ea514c42ull},  {0x122ff, 0xa04c838dfd179b6eull}, {0x47c, 0x92d0d1321821c652ull},
    {0x504, 0x3036bd24c9b1ba54ull},   {0x4d8, 0x479cb2b24152c979ull},   {0x7bbd, 0x7844402b44931c53ull},
    {0x673b, 0x8281c2e06944105dull},  {0x676e, 0x7d83458742e2ef45ull},  {0x300, 0x2fa382bcd0708cd9ull},
    {0x10000, 0x094698516c5718f5ull}, {0x102ff, 0x9082cf2fbf95bc2cull}, {0x2408, 0x7aa7d620c196f44full},
    {0x2bd8, 0x1a7cf251b51e9ef0ull},  {0xfa00, 0x7c1495fc81d6d5b6ull},  {0x300, 0xd6a9186fb9586577ull},
    {0xffff, 0xe1d8438dab9fbb61ull},  {0x1da8, 0x2db67c24b6434730ull},  {0x9388, 0x20b1066c2c251eb0ull},
    {0xfd00, 0xfbad90d68dc2f109ull},  {0xfd00, 0x7686ec02bd503988ull},  {0xfd00, 0x740962a8e25eda48ull},
    {0xfd00, 0x5bbc8f1a2ad71321ull},  {0xfd00, 0x67c2955162bfae79ull},  {0xfd00, 0x263f44780abd1f21ull},
    {0xfa00, 0xac648dd4efe5b034ull},
};

bool matches(const std::vector<uint8_t>& d, const Known& k) { return d.size() == k.size && fnv1a(d) == k.hash; }
}  // namespace

bool StarsExe::check(std::string* err) const {
    char b[200];
    if (!matches(u_.image, kImage)) {
        std::snprintf(b, sizeof b, "unexpected STARS.EXE contents (program image %zu bytes, hash %016llx)",
                      u_.image.size(), (unsigned long long)fnv1a(u_.image));
        if (err) *err = b;
        return false;
    }
    for (int n = 1; n <= kFiles; n++)
        if (!matches(u_.files[n], kFileHash[n])) {
            std::snprintf(b, sizeof b, "unexpected STARS.EXE contents (overlay file %d: %zu bytes, hash %016llx)", n,
                          u_.files[n].size(), (unsigned long long)fnv1a(u_.files[n]));
            if (err) *err = b;
            return false;
        }
    return true;
}

bool StarsExe::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> exe;
    if (!readAll(path, exe)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (!unpackStars(exe, u_, err)) return false;
    return check(err);
}

const uint8_t* StarsExe::at(uint32_t a, size_t n) const {
    if (size_t(a) + n > u_.image.size()) throw std::out_of_range("STARS.EXE address out of range");
    return u_.image.data() + a;
}

const std::vector<uint8_t>& StarsExe::file(int n) const {
    if (n < 1 || n > kFiles) throw std::out_of_range("STARS.EXE has no overlay file " + std::to_string(n));
    return u_.files[n];
}

std::string StarsExe::cstr(uint32_t a) const {
    std::string s;
    for (const uint8_t* p = at(a); *p; p = at(++a)) s.push_back(char(*p));
    return s;
}

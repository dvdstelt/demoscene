// The driver 0x12c50 of the texture generator: runs the 16 scripts (map14 once: the driver builds it twice from
// the same script and seed), its post-steps (0x12d44 logo2 copy, 0x12d87 map15 stripes, 0x12e08 blob gradients),
// both paths of every script on worker threads, and the check of each high-resolution texture against the exact
// one. Names and pointer variables of the texture buffers (notes-textures §5, §8).
#include "jizz/texgen.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "jizz/texgen_internal.h"

namespace jizz::texgen {

namespace {
const char* const kNames[kCount] = {"lightmap", "map1",     "map2",  "map3",  "map4",     "map5",     "weirdtxt",
                                    "map7",     "map8",     "map9",  "blobheights", "testshit", "logo1", "map10",
                                    "map14",    "map15",    "map1a", "logo2", "pictureA", "pictureB"};

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Area-downsampled high-resolution planes against the exact planar bytes (signed G / B for the blob gradients).
void compare(Texture& t, int id) {
    const int s = t.size / 256;
    double sum = 0, mx = 0, sumc = 0;
    long over = 0;
    for (int p = 0; p < 3; p++) {
        const bool sgn = id == kBlobHeights && p > 0;
        const std::vector<float>& h = t.hires[p];
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                double a = 0;
                for (int j = 0; j < s; j++)
                    for (int i = 0; i < s; i++) a += h[size_t(y * s + j) * t.size + x * s + i];
                a /= double(s * s);
                const uint8_t b = t.planar[p * kPlane + y * 256 + x];
                const double e = sgn ? double(int8_t(b)) : double(b);
                const double d = std::fabs(a - e);
                const int c0 = (s - 1) / 2, c1 = s / 2;  // the hi-res pixels around the texel centre
                const double c = (h[size_t(y * s + c0) * t.size + x * s + c0] + h[size_t(y * s + c0) * t.size + x * s + c1] +
                                  h[size_t(y * s + c1) * t.size + x * s + c0] + h[size_t(y * s + c1) * t.size + x * s + c1]) / 4;
                sumc += std::fabs(c - e);
                sum += d;
                mx = std::max(mx, d);
                over += d > 16;
            }
    }
    t.meanDiff = sum / (3.0 * kPlane);
    t.meanDiffCentre = sumc / (3.0 * kPlane);
    t.maxDiff = mx;
    t.over16 = double(over) / (3.0 * kPlane);
}

void planarFromGrey(Texture& t, const uint8_t* plane) {
    t.original.assign(plane, plane + kPlane);
    t.planar.resize(kBuf);
    for (int p = 0; p < 3; p++) std::memcpy(t.planar.data() + p * kPlane, plane, kPlane);
}

// The two RLE pictures (op 0x22 arguments of the logo script 0x12a7d: lettering first, then the logo).
std::vector<uint32_t> pictureAddresses(const JizzExe& exe, uint32_t script) {
    std::vector<uint32_t> r;
    for (uint32_t a = script; exe.u8(a) != 0x01; a += detail::opLength(exe, a))
        if (exe.u8(a) == 0x22) r.push_back(exe.u32(a + 2));
    return r;
}
}  // namespace

namespace detail {
// 0x12c50: (script, destination) in the driver's order, map14's second identical build left out
const std::array<Build, 16> kBuilds = {{{0x12b5d, kMap14}, {0x12439, kMap1}, {0x124c1, kMap1a}, {0x12599, kMap2},
                                        {0x12630, kMap3}, {0x126fc, kMap4}, {0x1276f, kMap5}, {0x127f0, kWeirdtxt},
                                        {0x12919, kMap7}, {0x12993, kMap8}, {0x129d6, kMap9}, {0x129ec, kBlobHeights},
                                        {0x129f7, kTestshit}, {0x12a7d, kLogo1}, {0x12adc, kMap10}, {0x12bd9, kMap15}}};
}  // namespace detail

const char* name(int id) { return id >= 0 && id < kCount ? kNames[id] : "?"; }

uint32_t pointerVar(int id) { return id >= 0 && id <= kLogo2 ? 0x654a + 4 * uint32_t(id) : 0; }

int byPointerVar(uint32_t var) {
    if (var < 0x654e || var > 0x658e || (var - 0x654a) % 4) return -1;
    return int((var - 0x654a) / 4);
}

int byName(const std::string& n) {
    for (int i = 1; i < kCount; i++)
        if (n == kNames[i]) return i;
    return -1;
}

std::string trace(const JizzExe& exe, int id, int scale) {
    using namespace detail;
    uint32_t script = 0;
    for (const Build& b : kBuilds)
        if (b.id == id || (id == kLogo2 && b.id == kLogo1)) script = b.script;
    if (!script) return "no script\n";
    std::string out;
    const int N = 256 * scale;
    uint32_t a = script;
    for (int n = 1;; n++) {
        const uint8_t op = exe.u8(a);
        const ExactRun ex = runExact(exe, script, false, op == 0x01 ? -1 : n);
        const HiresRun hr = runHires(exe, script, scale, id == kTestshit, op == 0x01 ? -1 : n);
        char line[160];
        int len = std::snprintf(line, sizeof line, "%3d %#07x op %02x  W=B%d", n, a, op, ex.wEnd);
        for (int p = 0; p < 3 && hr.ok; p++) {
            double sum = 0;
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++) {
                    double v = 0;
                    for (int j = 0; j < scale; j++)
                        for (int i = 0; i < scale; i++) v += hr.result[p][size_t(y * scale + j) * N + x * scale + i];
                    v /= scale * scale;
                    double d = std::fabs(v - ex.result[p * kPlane + y * 256 + x]);
                    d = std::fmod(d, 256.0);
                    sum += std::min(d, 256 - d);
                }
            len += std::snprintf(line + len, sizeof line - len, "  %7.3f", sum / kPlane);
        }
        out += line;
        out += '\n';
        if (op == 0x01) break;
        a += opLength(exe, a);
    }
    return out;
}

std::vector<uint8_t> tgaBytes(const JizzExe& exe, const std::vector<uint8_t>& w) {
    std::vector<uint8_t> out = exe.bytes(0x14349, 18);  // header (0x14355 / 0x14357 = 256 x 256)
    out[12] = 0;
    out[13] = 1;
    out[14] = 0;
    out[15] = 1;
    out.reserve(18 + kBuf);
    for (int i = 0; i < kPlane; i++) {
        out.push_back(w[2 * kPlane + i]);
        out.push_back(w[kPlane + i]);
        out.push_back(w[i]);
    }
    return out;
}

Result build(const JizzExe& exe, const Options& opt) {
    using namespace detail;
    const auto t0 = std::chrono::steady_clock::now();
    Result res;
    const int scale = opt.scale >= 2 ? std::min(opt.scale, 8) : 0;
    if (scale && (scale & (scale - 1))) throw std::runtime_error("texgen: scale must be a power of two");
    const bool hiresOn = scale >= 2;
    std::mutex mu;
    std::exception_ptr err;

    auto useHires = [&](int id) { return hiresOn && ((opt.hiresMask >> id) & 1u); };
    auto finish = [&](Texture& t, int id, std::array<std::vector<float>, 3>&& h) {
        t.size = 256 * scale;
        t.hires = std::move(h);
        t.hiresBuilt = true;
        compare(t, id);
        t.hiresUsed = t.meanDiff <= opt.maxMeanDiff;
        if (t.hiresUsed && id != kBlobHeights) {
            const size_t n = size_t(t.size) * t.size;
            t.rgb10.resize(n);
            auto q = [](float v) { return uint32_t(std::clamp(v * (1023.0f / 255.0f) + 0.5f, 0.0f, 1023.0f)); };
            for (size_t j = 0; j < n; j++)
                t.rgb10[j] = q(t.hires[0][j]) | q(t.hires[1][j]) << 10 | q(t.hires[2][j]) << 20 | 3u << 30;
        }
        if (!t.hiresUsed) t.size = 256;
        if (!t.hiresUsed || (id != kBlobHeights && !opt.keepFloats))
            for (auto& p : t.hires) std::vector<float>().swap(p);
    };

    auto job = [&](const Build& b) {
        const auto tj = std::chrono::steady_clock::now();
        ExactRun ex = runExact(exe, b.script, opt.captureSaves);
        Texture& t = res.tex[b.id];
        t.original = ex.result;
        t.planar = ex.content[ex.wEnd].empty() ? ex.result : ex.content[ex.wEnd];
        if (b.id == kMap15) {
            stripesExact(exe, t.original.data());
            t.planar = t.original;
        } else if (b.id == kBlobHeights) {
            gradientsExact(t.original.data());
            t.planar = t.original;
        } else if (b.id == kLogo1) {
            Texture& l2 = res.tex[kLogo2];  // 0x12d44: B2 (16-bit 320x200) -> [0x658e]
            l2.original.assign(ex.buffers[2].begin(), ex.buffers[2].begin() + 0x20000);
            l2.planar = ex.content[2];
            const std::vector<uint32_t> pics = pictureAddresses(exe, b.script);
            if (pics.size() != 2) throw std::runtime_error("texgen: logo script without its two pictures");
            for (int i = 0; i < 2; i++) {
                std::vector<uint8_t> plane(kPlane, 0);
                rle4(exe, pics[i], plane.data());
                planarFromGrey(res.tex[kPictureA + i], plane.data());
            }
        }
        t.msExact = msSince(tj);
        {
            std::lock_guard<std::mutex> lk(mu);
            res.libmCloseCalls += ex.closeCalls;
            if (opt.captureSaves)
                for (auto& s : ex.saves) res.saves.push_back(std::move(s));
        }
        if (useHires(b.id) || (b.id == kLogo1 && useHires(kLogo2))) {
            const auto th = std::chrono::steady_clock::now();
            HiresRun hr = runHires(exe, b.script, scale, b.id == kTestshit);
            t.peakPlanes = hr.peakPlanes;
            {
                std::lock_guard<std::mutex> lk(mu);
                for (int i = 0; i < 0x27; i++) res.msPerOp[i] += hr.msPerOp[i];
            }
            if (hr.ok) {
                if (b.id == kMap15) stripesHires(scale, hr.result);
                if (b.id == kBlobHeights) gradientsHires(scale, hr.result);
                if (useHires(b.id)) finish(t, b.id, std::move(hr.result));
                if (b.id == kLogo1 && useHires(kLogo2)) finish(res.tex[kLogo2], kLogo2, std::move(hr.b2));
            }
            t.msHires = msSince(th);
        }
        if (b.id == kLogo1)
            for (int i = 0; i < 2; i++)
                if (useHires(kPictureA + i)) {
                    Texture& p = res.tex[kPictureA + i];
                    std::array<std::vector<float>, 3> h;
                    h[0] = upsamplePicture(p.original.data(), scale);
                    h[1] = h[0];
                    h[2] = h[0];
                    finish(p, kPictureA + i, std::move(h));
                }
    };

    // the slowest high-resolution scripts first
    std::vector<Build> order(kBuilds.begin(), kBuilds.end());
    static const int kFirst[] = {kMap3, kMap2, kMap14, kMap7, kMap1a, kMap5, kMap4, kMap10, kWeirdtxt};
    std::stable_sort(order.begin(), order.end(), [](const Build& a, const Build& b) {
        auto rank = [](int id) {
            for (int i = 0; i < int(std::size(kFirst)); i++)
                if (kFirst[i] == id) return i;
            return 100;
        };
        return rank(a.id) < rank(b.id);
    });
    int nthreads = opt.threads > 0 ? opt.threads : int(std::thread::hardware_concurrency());
    nthreads = std::clamp(nthreads, 1, 8);
    std::atomic<int> next{0};
    auto worker = [&]() {
        for (;;) {
            const int i = next++;
            if (i >= int(order.size())) return;
            try {
                job(order[i]);
            } catch (...) {
                std::lock_guard<std::mutex> lk(mu);
                if (!err) err = std::current_exception();
            }
        }
    };
    std::vector<std::thread> pool;
    for (int i = 1; i < nthreads; i++) pool.emplace_back(worker);
    worker();
    for (auto& th : pool) th.join();
    if (err) std::rethrow_exception(err);
    res.libmCloseCalls += libmCloseCallsStripes(exe);
    std::sort(res.saves.begin(), res.saves.end(), [](auto& a, auto& b) { return a.first < b.first; });
    res.msTotal = msSince(t0);
    return res;
}

}  // namespace jizz::texgen

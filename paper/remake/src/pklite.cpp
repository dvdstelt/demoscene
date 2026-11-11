// PKLITE unpacker for PAPER.EXE (PKLITE 1.15, large model, no extra compression). It implements the decompression
// algorithm directly, no x86 emulation, and models the memory traffic of PKLITE's loader stub so that the result is
// the memory image the stub leaves behind (paper/re/paper_img.bin, made by running the stub in an emulator).
//
// How a PKLITE 1.15 executable unpacks itself
//   DOS loads the packed load module at L:0000 (L = load segment, PSP at P = L - 10h) and starts the stub at
//   P:0100, the first byte of the load module, with DS = ES = P. The stub
//   1. gives up ("Not enough memory") unless L + memParas < PSP:[2]; memParas (`mov ax,imm` at the entry) covers
//      the unpacked program, its uninitialized data and the stub's own needs;
//   2. sets SS = L + memParas - 20h (SP stays as in the header, so the stack is the top 20h paragraphs), copies
//      its decompressor (123h words from P:0144) to segment T = SS - 25h and jumps to T:0000;
//   3. moves the compressed data, compParas paragraphs (`mov dx,imm`) from segment P + 39h, upwards so that it
//      ends right below T (a backward copy);
//   4. decompresses it in place to L:0000 (the output grows upwards towards the unread input);
//   5. reads the relocation table that follows the compressed stream, adds L to every listed word, switches to
//      the program's SS:SP, pushes the program's CS:IP there and returns to it.
//
// Compressed stream (large model)
//   Flag bits come in little-endian 16-bit words, least significant bit first, interleaved with whole bytes. A new
//   word is fetched as soon as the last bit of the previous one is used, so it comes before any byte that is read
//   after that bit. Tokens:
//     0                                           literal: copy the next byte unchanged (no "extra" XOR)
//     1 <length> [<distance high>] <low byte>     copy `length` bytes from `distance` bytes back (may overlap);
//                                                 length 2 has no high part (distance < 256)
//   <length> (kLengthCodes) is 2..24, or an escape followed by a byte b: b < FEh means length 25 + b, b = FEh
//   renormalizes the stub's segment:offset pointers (no output) and b = FFh ends the stream.
//   <distance high> (kDistanceCodes) is 0..31, so distances go up to 8191.
// Relocation table (follows the end code)
//   Groups { count:u8 (0 ends the table), segment:u16, count x offset:u16 }, then the program's SS, SP, CS, IP
//   (segments relative to L). Each entry adds L to the word at (L + segment):offset.
//
// The image
//   `image` is memory from L:0000 for memParas paragraphs at the moment the stub jumps to the program, with
//   L = 1000h and everything DOS did not load zeroed (the setup of paper/re/unpklite.py). Past the unpacked program
//   it therefore still holds what the stub left there: the unread rest of the moved compressed data (with the
//   program's CS:IP written into it if its stack is there), the decompressor copy, zeros, and at the very top the
//   last words on the stub's stack (PSP segment; AX at the end code; DS and ES of the last match copy). That is why
//   the model keeps the stub's segment:offset pointers and stack pointer rather than plain positions.
#include "exe.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <new>
#include <string_view>
#include <utility>

namespace {

// The environment the image is defined for.
constexpr uint16_t kLoadSeg = 0x1000;          // L
constexpr uint16_t kPspSeg = kLoadSeg - 0x10;  // P
constexpr uint16_t kMemTop = 0xa000;           // PSP:[2], first segment past the memory DOS gives the program

// FNV-1a 64 of PKLITE 1.15's large-model decompressor, the 246h bytes the stub copies to T (code and code tables,
// the same in every file packed this way). The model below follows exactly this code, so anything else is refused.
constexpr uint64_t kDecompressorHash = 0x828355ffdabb52fdull;

struct Failure {
    std::string msg;
};

[[noreturn]] void fail(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    throw Failure{buf};
}

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// True when `d` holds `pattern` ("B8 ?? 26": hex bytes, ?? matches any byte) at `pos`.
bool matches(const uint8_t* d, size_t size, size_t pos, std::string_view pattern) {
    auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    size_t n = (pattern.size() + 1) / 3;
    if (pos > size || size - pos < n) return false;
    for (size_t i = 0; i < n; ++i) {
        std::string_view h = pattern.substr(3 * i, 2);
        if (h != "??" && d[pos + i] != hex(h[0]) * 16 + hex(h[1])) return false;
    }
    return true;
}

// ----------------------------------------------------------------------------------------------------------------
// Prefix codes of the large model, bits in the order they are read.
struct Code {
    std::string_view bits;
    int value;
};

constexpr int kLengthEscape = 0;  // followed by a byte: length 25 + b, or FEh (renormalize) / FFh (end)

constexpr Code kLengthCodes[] = {
    {"10", 2},         {"11", 3},         {"000", 4},         {"0010", 5},       {"0011", 6},
    {"0100", 7},       {"01010", 8},      {"01011", 9},       {"01100", 10},     {"011010", 11},
    {"011011", 12},    {"011100", kLengthEscape},             {"0111010", 13},   {"0111011", 14},
    {"0111100", 15},   {"01111010", 16},  {"01111011", 17},   {"01111100", 18},  {"011111010", 19},
    {"011111011", 20}, {"011111100", 21}, {"011111101", 22},  {"011111110", 23}, {"011111111", 24},
};

// High byte of the distance; the low byte follows as a whole byte.
constexpr Code kDistanceCodes[] = {
    {"1", 0},        {"0000", 1},     {"0001", 2},     {"00100", 3},    {"00101", 4},    {"00110", 5},
    {"00111", 6},    {"010000", 7},   {"010001", 8},   {"010010", 9},   {"010011", 10},  {"010100", 11},
    {"010101", 12},  {"010110", 13},  {"0101110", 14}, {"0101111", 15}, {"0110000", 16}, {"0110001", 17},
    {"0110010", 18}, {"0110011", 19}, {"0110100", 20}, {"0110101", 21}, {"0110110", 22}, {"0110111", 23},
    {"0111000", 24}, {"0111001", 25}, {"0111010", 26}, {"0111011", 27}, {"0111100", 28}, {"0111101", 29},
    {"0111110", 30}, {"0111111", 31},
};

// Every bit string starts exactly one code (so decoding always ends within the longest code).
template <size_t N>
constexpr bool isCompletePrefixCode(const Code (&t)[N]) {
    uint32_t kraft = 0;  // sum of 2^-length, in units of 2^-16
    for (size_t i = 0; i < N; ++i) {
        kraft += 1u << (16 - t[i].bits.size());
        for (size_t j = 0; j < N; ++j)
            if (i != j && t[j].bits.substr(0, t[i].bits.size()) == t[i].bits) return false;
    }
    return kraft == 1u << 16;
}
static_assert(isCompletePrefixCode(kLengthCodes));
static_assert(isCompletePrefixCode(kDistanceCodes));

// ----------------------------------------------------------------------------------------------------------------
struct Far {  // real-mode segment:offset
    uint16_t seg, off;
};

class Unpacker {
public:
    explicit Unpacker(const std::vector<uint8_t>& exe) : exe_(exe) {}

    std::vector<uint8_t> run() {
        readHeader();
        readStub();
        load();
        startStub();
        decompress();
        finish();
        return std::move(mem_);
    }

private:
    const std::vector<uint8_t>& exe_;
    const uint8_t* module_ = nullptr;  // load module (the part of the file DOS loads)
    size_t moduleSize_ = 0;
    uint16_t sp0_ = 0, relocCount_ = 0, relocTable_ = 0;  // from the MZ header

    // Stub parameters, all read from the stub.
    uint16_t memParas_ = 0, compParas_ = 0;    // memory needed from L / size of the compressed data
    uint16_t stackParas_ = 0, copyParas_ = 0;  // SS = L + memParas - stackParas, T = SS - copyParas
    uint16_t copyWords_ = 0, copySrc_ = 0;     // decompressor at P:copySrc, copyWords words
    uint16_t dataPara_ = 0;                    // compressed data starts at segment P + dataPara

    std::vector<uint8_t> mem_;  // memory from L:0000, memParas paragraphs
    Far stack_{};               // SS:SP
    Far in_{}, out_{};          // DS:SI (compressed data), ES:DI (output)
    int64_t inBegin_ = 0, inEnd_ = 0, outEnd_ = 0;  // compressed data / limit of writes (the decompressor)
    size_t inLeft_ = 0;         // bytes that may still be read: a valid stream reads each byte once
    uint16_t bits_ = 0;         // BP: flag bits not used yet
    int bitsLeft_ = 0;          // DX
    uint8_t ah_ = 0;            // high byte of AX, pushed with the FEh/FFh escape byte

    // Position of `p` relative to L:0000 (negative below it).
    static int64_t pos(Far p) { return int64_t(p.seg) * 16 + p.off - int64_t(kLoadSeg) * 16; }
    static bool inside(int64_t a, int64_t end) { return a >= 0 && a < end; }
    int64_t memEnd() const { return int64_t(mem_.size()); }

    // Memory at `p`, which must lie in [L:0000, end).
    uint8_t& at(Far p, int64_t end, const char* what) {
        if (!inside(pos(p), end))
            fail("corrupt compressed data (%s: %04X:%04X)", what, unsigned(p.seg), unsigned(p.off));
        return mem_[size_t(pos(p))];
    }

    // The stub's stack. Words that fall outside the modeled memory are not part of the image.
    void push(uint16_t v) {
        stack_.off -= 2;
        Far hi{stack_.seg, uint16_t(stack_.off + 1)};
        if (inside(pos(stack_), memEnd())) mem_[size_t(pos(stack_))] = uint8_t(v);
        if (inside(pos(hi), memEnd())) mem_[size_t(pos(hi))] = uint8_t(v >> 8);
    }
    void pop() { stack_.off += 2; }

    void readHeader();
    void readStub();
    void load();
    void startStub();
    void decompress();
    void finish();

    uint8_t lodsb() {
        int64_t a = pos(in_);
        if (a < inBegin_ || a >= inEnd_)
            fail("corrupt or truncated compressed data (read outside the data: %04X:%04X)", unsigned(in_.seg),
                 unsigned(in_.off));
        if (inLeft_ == 0) fail("corrupt compressed data (no end code)");
        --inLeft_;
        ++in_.off;
        return mem_[size_t(a)];
    }
    uint16_t lodsw() {
        uint8_t lo = lodsb();
        return uint16_t(lo | lodsb() << 8);
    }
    void stosb(uint8_t b) {
        at(out_, outEnd_, "output runs into the decompressor") = b;
        ++out_.off;
    }

    int bit() {
        int b = bits_ & 1;
        bits_ >>= 1;
        if (--bitsLeft_ == 0) {  // refill at once (see the stream description)
            bits_ = lodsw();
            bitsLeft_ = 16;
            ah_ = 0;  // `lodsw; xchg bp,ax` leaves the used-up word, 0, in AX
        }
        return b;
    }

    template <size_t N>
    int code(const Code (&table)[N]) {
        char seen[16];
        for (size_t n = 1; n <= sizeof seen; ++n) {
            seen[n - 1] = char('0' + bit());
            for (const Code& c : table)
                if (c.bits == std::string_view(seen, n)) return c.value;
        }
        fail("bad code table");  // unreachable, see isCompletePrefixCode
    }
};

void Unpacker::readHeader() {
    const std::vector<uint8_t>& d = exe_;
    if (d.size() < 0x20 || !((d[0] == 'M' && d[1] == 'Z') || (d[0] == 'Z' && d[1] == 'M')))
        fail("not an MZ executable");
    uint16_t cblp = le16(&d[2]), cp = le16(&d[4]), hdrParas = le16(&d[8]);
    uint16_t cs = le16(&d[0x16]), ip = le16(&d[0x14]);
    sp0_ = le16(&d[0x10]);
    relocCount_ = le16(&d[6]);
    relocTable_ = le16(&d[0x18]);
    if (cp == 0 || cblp >= 512) fail("bad MZ header (file size fields)");
    size_t fileSize = size_t(cp) * 512 - (cblp ? 512 - cblp : 0);
    size_t hdrSize = size_t(hdrParas) * 16;
    if (fileSize > d.size()) fail("truncated executable (%zu of %zu bytes)", d.size(), fileSize);
    if (hdrSize < 0x20 || hdrSize >= fileSize) fail("bad MZ header (header size)");
    if (size_t(relocTable_) + size_t(relocCount_) * 4 > fileSize) fail("bad MZ header (relocation table)");
    module_ = d.data() + hdrSize;
    moduleSize_ = fileSize - hdrSize;

    // PKLITE's own header: version word at 1Ch, then "PKLITE Copr. ...".
    uint16_t ver = le16(&d[0x1c]);
    if (d[0x1e] != 'P' || d[0x1f] != 'K') fail("not a PKLITE-compressed executable");
    if ((ver & 0x0fff) != 0x010f)
        fail("PKLITE version %u.%02u is not supported (only 1.15)", ver >> 8 & 15u, ver & 255u);
    if (ver & 0x1000) fail("PKLITE extra compression is not supported");
    if (!(ver & 0x2000)) fail("PKLITE small-model compression is not supported");
    if (ver & 0xc000) fail("unknown PKLITE flags %04X", unsigned(ver));
    // The stub runs with CS = P, IP = 100h: the first byte of the load module.
    if (cs != 0xfff0 || ip != 0x100) fail("unexpected PKLITE entry point %04X:%04X", unsigned(cs), unsigned(ip));
}

void Unpacker::readStub() {
    const uint8_t* m = module_;
    size_t n = moduleSize_;
    // mov ax,memParas; mov dx,compParas; add ax,L (relocated); cmp ax,[2]; jb ok
    if (!matches(m, n, 0, "B8 ?? ?? BA ?? ?? 05 ?? ?? 3B 06 02 00 72 ??")) fail("unrecognized PKLITE stub");
    memParas_ = le16(m + 1);
    compParas_ = le16(m + 4);
    size_t ok = 0x0f + size_t(int8_t(m[0x0e]));
    // ok: sub ax,stackParas; mov ss,ax; sub ax,copyParas; nop; mov es,ax; push ax; mov cx,copyWords;
    //     xor di,di; push di; mov si,copySrc; cld; rep movsw; retf
    if (!matches(m, n, ok, "2D ?? ?? 8E D0 2D ?? ?? 90 8E C0 50 B9 ?? ?? 33 FF 57 BE ?? ?? FC F3 A5 CB"))
        fail("unrecognized PKLITE stub");
    stackParas_ = le16(m + ok + 1);
    copyParas_ = le16(m + ok + 6);
    copyWords_ = le16(m + ok + 13);
    copySrc_ = le16(m + ok + 19);
    size_t copyPos = size_t(copySrc_) - 0x100, copySize = size_t(copyWords_) * 2;
    if (copySrc_ < 0x100 || copySize < 8 || copyPos + copySize > n || fnv1a(m + copyPos, copySize) != kDecompressorHash)
        fail("unsupported PKLITE decompressor (not 1.15 large model)");
    // T:0000  std; mov bx,ds; push bx; add bx,dataPara; ...
    dataPara_ = le16(m + copyPos + 6);
}

// What DOS does: load the module at L:0000 and relocate it. Memory DOS does not load stays zero.
void Unpacker::load() {
    if (uint32_t(kLoadSeg) + memParas_ >= kMemTop)
        fail("PKLITE stub needs %u paragraphs, more than DOS has below %04X", unsigned(memParas_), unsigned(kMemTop));
    mem_.assign(size_t(memParas_) * 16, 0);
    if (moduleSize_ > mem_.size()) fail("PKLITE stub reserves less memory than the packed program needs");
    std::copy(module_, module_ + moduleSize_, mem_.begin());
    for (size_t i = 0; i < relocCount_; ++i) {
        const uint8_t* r = exe_.data() + relocTable_ + 4 * i;
        size_t a = size_t(le16(r + 2)) * 16 + le16(r);
        if (a + 2 > mem_.size()) fail("bad MZ header (relocation outside the program)");
        uint16_t v = uint16_t(le16(&mem_[a]) + kLoadSeg);
        mem_[a] = uint8_t(v);
        mem_[a + 1] = uint8_t(v >> 8);
    }
}

// Stub steps 2 and 3: set up the stack, copy the decompressor up to T and move the compressed data below it.
void Unpacker::startStub() {
    int64_t top = int64_t(memParas_) * 16;
    int64_t stubPos = top - (int64_t(stackParas_) + copyParas_) * 16;  // T
    int64_t dataSize = int64_t(compParas_) * 16;
    int64_t dataSrc = (int64_t(dataPara_) - 0x10) * 16, dataDst = stubPos - dataSize;
    if (stubPos < 0 || copyWords_ * 2 > copyParas_ * 16 || sp0_ < 8 || sp0_ > stackParas_ * 16 || dataSrc < 0 ||
        dataSrc + dataSize > top || dataDst < 0)
        fail("unexpected PKLITE stub parameters");

    uint16_t ss = uint16_t(kLoadSeg + memParas_ - stackParas_);
    uint16_t t = uint16_t(ss - copyParas_);
    stack_ = {ss, sp0_};
    push(t);  // push ax; push di: far return address T:0000
    push(0);
    for (size_t i = 0; i < size_t(copyWords_) * 2; ++i)  // cld; rep movsw
        mem_[size_t(stubPos) + i] = mem_[size_t(copySrc_) - 0x100 + i];
    pop();  // retf
    pop();

    push(kPspSeg);  // T:0000  push bx (= DS = P), popped after decompression
    for (int64_t i = dataSize; i-- > 0;)  // std; rep movsw, in 64 KB steps
        mem_[size_t(dataDst + i)] = mem_[size_t(dataSrc + i)];

    in_ = {uint16_t(t - compParas_), 0};  // DS = BP = T - compParas, SI = 0
    out_ = {kPspSeg, 0x100};              // ES = P (pop es; push es), DI = 100h
    inBegin_ = dataDst;
    inEnd_ = outEnd_ = stubPos;
    inLeft_ = size_t(dataSize);
}

// Stub step 4.
void Unpacker::decompress() {
    bits_ = lodsw();  // lodsw; xchg bp,ax: AX = old BP = DS, until the first refill
    bitsLeft_ = 16;
    ah_ = uint8_t(in_.seg >> 8);
    for (;;) {
        if (!bit()) {
            stosb(lodsb());  // movsb
            continue;
        }
        int length = code(kLengthCodes);
        if (length == kLengthEscape) {
            uint8_t b = lodsb();
            if (b < 0xfe) {
                length = 25 + b;
            } else {
                // push ax; renormalize ES:DI to DI = 2000h..200Fh and DS:SI to SI = 0..0Fh; pop ax.
                // DI stays >= 2000h so that a back reference (< 2000h) never leaves the segment.
                push(uint16_t(ah_ << 8 | b));
                out_.seg = uint16_t(out_.seg + (uint16_t(out_.off - 0x2000) >> 4));
                out_.off = uint16_t((out_.off & 0x0f) + 0x2000);
                in_.seg = uint16_t(in_.seg + (in_.off >> 4));
                in_.off &= 0x0f;
                pop();
                if (b == 0xff) return;
                continue;
            }
        }
        unsigned distance = length == 2 ? 0 : unsigned(code(kDistanceCodes)) << 8;
        distance |= lodsb();
        // push si; mov si,di; sub si,bx; push ds; push es; pop ds; rep movsb; pop ds; pop si
        push(in_.off);
        push(in_.seg);
        push(out_.seg);
        pop();
        Far src{out_.seg, uint16_t(out_.off - distance)};
        for (int i = 0; i < length; ++i, ++src.off) stosb(at(src, memEnd(), "back reference outside the program"));
        pop();
        pop();
    }
}

// Stub step 5.
void Unpacker::finish() {
    pop();  // pop bx: P; BX = P + 10h = L
    while (uint8_t count = lodsb()) {
        uint16_t seg = uint16_t(lodsw() + kLoadSeg);
        for (; count; --count) {
            Far lo{seg, lodsw()}, hi{seg, uint16_t(lo.off + 1)};
            const char* what = "relocation outside the program";
            uint16_t v = uint16_t((at(lo, outEnd_, what) | at(hi, outEnd_, what) << 8) + kLoadSeg);
            at(lo, outEnd_, what) = uint8_t(v);
            at(hi, outEnd_, what) = uint8_t(v >> 8);
        }
    }
    uint16_t ss = uint16_t(lodsw() + kLoadSeg), sp = lodsw();
    uint16_t cs = uint16_t(lodsw() + kLoadSeg), ip = lodsw();
    stack_ = {ss, sp};  // cli; mov ss,dx; mov sp,ax; sti
    push(cs);           // push bx; push ax; ...; retf
    push(ip);
}

}  // namespace

bool unpklite(const std::vector<uint8_t>& exe, std::vector<uint8_t>& image, std::string* err) {
    std::string msg;
    try {
        image = Unpacker(exe).run();
        return true;
    } catch (Failure& f) {
        msg = std::move(f.msg);
    } catch (const std::bad_alloc&) {
        msg = "out of memory";
    } catch (...) {
        msg = "unexpected error";
    }
    if (err) *err = std::move(msg);
    return false;
}

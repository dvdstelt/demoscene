// traceStores (code.h): a tiny interpreter for the straight runs of constant stores in the parts' init routines.
#include "inside/code.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "exe.h"

namespace inside {

std::map<uint32_t, uint32_t> traceStores(const InsideExe& exe, uint32_t begin, uint32_t end) {
    std::optional<uint32_t> r[8];  // eax ecx edx ebx esp ebp esi edi
    std::map<uint32_t, uint32_t> out;
    auto fail = [&](uint32_t a, const char* what) {
        char b[96];
        std::snprintf(b, sizeof b, "traceStores: %s at 0x%x", what, a);
        throw std::runtime_error(b);
    };
    auto store = [&](uint32_t, uint32_t addr, int reg) {
        if (r[reg]) out[addr] = *r[reg];
        else out.erase(addr);   // a value the code computed (a pointer, a call's result): not a constant
    };
    uint32_t a = begin;
    while (a < end) {
        uint8_t op = exe.u8(a);
        if (op >= 0x50 && op <= 0x57) { a += 1; continue; }                          // push r32
        if (op >= 0xb8 && op <= 0xbf) { r[op - 0xb8] = exe.u32(a + 1); a += 5; continue; }  // mov r32, imm32
        if (op == 0xe8) { r[0].reset(); a += 5; continue; }                          // call rel32
        if (op == 0xa3) { store(a, exe.u32(a + 1), 0); a += 5; continue; }          // mov [abs32], eax
        if (op == 0xa1) { r[0].reset(); a += 5; continue; }                          // mov eax, [abs32]
        if (op == 0x05) {                                                            // add eax, imm32
            if (r[0]) r[0] = *r[0] + exe.u32(a + 1);
            a += 5;
            continue;
        }
        uint8_t m = exe.u8(a + 1);
        int mod = m >> 6, reg = (m >> 3) & 7, rm = m & 7;
        if ((op == 0x31 || op == 0x33) && mod == 3 && reg == rm) { r[reg] = 0; a += 2; continue; }  // xor r, r
        if (op == 0x89 && mod == 0 && rm == 5) { store(a, exe.u32(a + 2), reg); a += 6; continue; } // mov [abs], r
        if (op == 0x8b && mod == 0 && rm == 5) { r[reg].reset(); a += 6; continue; }               // mov r, [abs]
        if (op == 0x89 && mod == 3) { r[rm] = r[reg]; a += 2; continue; }                          // mov r, r
        if (op == 0xc7 && mod == 0 && rm == 5 && reg == 0) { out[exe.u32(a + 2)] = exe.u32(a + 6); a += 10; continue; }
        fail(a, "unexpected instruction");
    }
    return out;
}

float storedFloat(const std::map<uint32_t, uint32_t>& m, uint32_t addr) {
    uint32_t v = m.at(addr);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace inside

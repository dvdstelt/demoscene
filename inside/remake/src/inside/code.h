// Reading constants that the original's code stores (inside/re/notes-*.md give the routines). Some parts build their
// parameter tables in their init routine from immediate operands (mov reg, imm32 / mov [addr], reg), so the values
// are in the instructions, not in the data. traceStores() follows a straight run of such code in INSIDE.EXE and
// returns what it writes to fixed addresses, so the remake reads the parameters from the original by address.
#pragma once
#include <cstdint>
#include <map>

class InsideExe;

namespace inside {

// Follows the instructions from `begin` up to (not including) `end` and returns {address: dword} for every store
// to an absolute address. Understands the moves of constants Watcom emits (mov r32, imm32; xor r32, r32;
// mov [abs32], r32 / imm32; mov [abs32], eax), pushes, `add eax, imm32`, and calls (Watcom's convention: a call
// leaves every register but EAX as it was; EAX becomes unknown). Stores of unknown values (a call's result) are left
// out. Throws std::runtime_error on any other instruction.
std::map<uint32_t, uint32_t> traceStores(const InsideExe& exe, uint32_t begin, uint32_t end);

// A dword of a traced map as a float (throws std::out_of_range if the address was not written).
float storedFloat(const std::map<uint32_t, uint32_t>& m, uint32_t addr);

}  // namespace inside

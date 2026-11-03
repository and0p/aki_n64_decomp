// WWF No Mercy (USA Rev 1) decompilation: basic types and N64 memory access.
//
// This is a faithful C-style translation of the match overlay (ovl_e), function by function, kept
// as close to the original as is readable: same order of float operations, same integer widths,
// same data. Each function carries its N64 address (`// N64 0x800E5890`), and its symbol in
// _re_docs/symbols/nomercy/ names the file in `port`. It is the oracle for the later port, checked
// frame by frame against the emulator (tools/trace, _re_docs/state_comparison.md).
//
// State the decompiled code owns lives in host structs with the N64 layout (nm_types.h, generated
// from types.h); everything else the code reads (the overlay's data, the move sets, the attribute
// records, the model blocks) stays in an N64-order image of RDRAM, read through mem_*().
#pragma once
#include <cstdint>
#include <cstring>

namespace nm {

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef float f32;
typedef double f64;

// One run of a byte-swap table: `count` elements of `size` bytes at `off` (nm_types.h).
struct SwapRun {
    u32 off;
    u8 size;
    u16 count;
};

// ---- the RDRAM image (N64 byte order), 8 MB ----

enum { RAM_SIZE = 0x800000 };
extern u8 *g_ram;

inline u32 ram_off(u32 addr) { return addr & 0x7FFFFF; }
inline u8 mem_u8(u32 a) { return g_ram[ram_off(a)]; }
inline s8 mem_s8(u32 a) { return (s8)g_ram[ram_off(a)]; }
inline u16 mem_u16(u32 a) { const u8 *p = g_ram + ram_off(a); return (u16)(p[0] << 8 | p[1]); }
inline s16 mem_s16(u32 a) { return (s16)mem_u16(a); }
inline u32 mem_u32(u32 a) {
    const u8 *p = g_ram + ram_off(a);
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
inline s32 mem_s32(u32 a) { return (s32)mem_u32(a); }
inline f32 mem_f32(u32 a) { u32 v = mem_u32(a); f32 f; std::memcpy(&f, &v, 4); return f; }
inline f64 mem_f64(u32 a) {
    uint64_t v = (uint64_t)mem_u32(a) << 32 | mem_u32(a + 4);
    f64 d; std::memcpy(&d, &v, 8); return d;
}
inline void mem_w8(u32 a, u8 v) { g_ram[ram_off(a)] = v; }
inline void mem_w16(u32 a, u16 v) { u8 *p = g_ram + ram_off(a); p[0] = (u8)(v >> 8); p[1] = (u8)v; }
inline void mem_w32(u32 a, u32 v) {
    u8 *p = g_ram + ram_off(a);
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
inline void mem_wf32(u32 a, f32 f) { u32 v; std::memcpy(&v, &f, 4); mem_w32(a, v); }

// Converts `n` bytes of an N64-layout struct between N64 byte order and host order, in place
// (the same operation both ways).
void swap_runs(u8 *p, const SwapRun *runs, int nruns);

// A path of the original the decompilation has not translated yet. Reaching one stops a replay
// with the function and what is missing, instead of silently doing something else.
[[noreturn]] void unported(const char *func, const char *what);
#define NM_UNPORTED(what) ::nm::unported(__func__, what)

}  // namespace nm

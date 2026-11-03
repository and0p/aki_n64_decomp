// Host copies of the match globals and their exchange with the RDRAM image.
#include "nm_state.h"
#include <cstdio>
#include <cstdlib>

namespace nm {

u8 *g_ram;

Wrestler g_wrestlers[4];
WrestlerCtx g_wrestler_ctx;
InputSlot g_input[4];
Pad g_pads[4];
u32 g_rng_match_state;
u32 g_rng_match_inc;
s16 g_rand_pool[32];
u32 g_rng_main_state;
s32 g_update_first_slot;
s32 g_match_phase;
s32 g_phase_wait;
s32 g_phase_timer;
u8 g_match_flags[2];
s32 g_match_clock[3];
s32 g_run_clock[3];
u32 g_match_type;
u32 g_match_rules;
u32 g_frame_bits;
s16 g_state_history[4][16];
u8 g_cpu_flags[4];
s16 g_last_anim[4];
Vec3 g_head_look[4];
s32 g_anim_mode[4];
ModelBlock g_models[4];
AnimStream g_anim_streams[4];
ModelNode g_model_nodes[4][28];

void swap_runs(u8 *p, const SwapRun *runs, int nruns) {
    for (int i = 0; i < nruns; i++) {
        u8 *q = p + runs[i].off;
        for (int k = 0; k < runs[i].count; k++, q += runs[i].size) {
            if (runs[i].size == 2) {
                u8 t = q[0]; q[0] = q[1]; q[1] = t;
            } else if (runs[i].size == 4) {
                u8 t = q[0]; q[0] = q[3]; q[3] = t;
                t = q[1]; q[1] = q[2]; q[2] = t;
            }
        }
    }
}

namespace {

void (*g_unported_hook)(const char *, const char *);

// One host variable: `count` records of `size` bytes at N64 address `addr`, swapped by `runs` or as
// elements of `elem` bytes.
struct Var {
    u32 addr;
    void *p;
    u32 size;
    const SwapRun *runs;
    int nruns;
    u8 elem;
    int count = 1;
};

#define RUNS(T) k##T##Swap, (int)(sizeof(k##T##Swap) / sizeof(SwapRun))
const Var kVars[] = {
    {0x80159990, g_wrestlers, sizeof(Wrestler), RUNS(Wrestler), 0, 4},
    {0x80159518, g_input, sizeof(InputSlot), RUNS(InputSlot), 0, 4},
    {0x8009EDA0, g_pads, sizeof(Pad), RUNS(Pad), 0, 4},
    {0x800A1130, g_models, sizeof(ModelBlock), RUNS(ModelBlock), 0, 4},
    {0x80069F20, g_anim_streams, sizeof(AnimStream), RUNS(AnimStream), 0, 4},
    {0x80091CC0, g_model_nodes, sizeof(ModelNode), RUNS(ModelNode), 0, 4 * 28},
    {0x80159350, &g_rng_match_state, 4, nullptr, 0, 4},
    {0x80159354, &g_rng_match_inc, 4, nullptr, 0, 4},
    {0x80159358, g_rand_pool, sizeof(g_rand_pool), nullptr, 0, 2},
    {0x80053780, &g_rng_main_state, 4, nullptr, 0, 4},
    {0x801591F8, &g_update_first_slot, 4, nullptr, 0, 4},
    {0x801591FC, &g_match_phase, 4, nullptr, 0, 4},
    {0x801591E0, &g_phase_wait, 4, nullptr, 0, 4},
    {0x801591E4, &g_phase_timer, 4, nullptr, 0, 4},
    {0x8016B598, g_match_flags, 2, nullptr, 0, 1},
    {0x8016B930, g_match_clock, sizeof(g_match_clock), nullptr, 0, 4},
    {0x801595F8, g_run_clock, sizeof(g_run_clock), nullptr, 0, 4},
    {0x80097EC0, &g_match_type, 4, nullptr, 0, 4},
    {0x800A9950, &g_match_rules, 4, nullptr, 0, 4},
    {0x80162A7C, &g_frame_bits, 4, nullptr, 0, 4},
    {0x8016B5A0, g_state_history, sizeof(g_state_history), nullptr, 0, 2},
    {0x8009ED20, g_cpu_flags, 4, nullptr, 0, 1},
    {0x801593A0, g_last_anim, sizeof(g_last_anim), nullptr, 0, 2},
    {0x801593A8, g_head_look, sizeof(g_head_look), nullptr, 0, 4},
    {0x801593D8, g_anim_mode, sizeof(g_anim_mode), nullptr, 0, 4},
};

void copy_var(const Var &v, bool to_host) {
    int count = v.count;
    for (int i = 0; i < count; i++) {
        u8 *host = (u8 *)v.p + i * v.size;
        u8 *ram = g_ram + ram_off(v.addr) + i * v.size;
        if (to_host) {
            std::memcpy(host, ram, v.size);
        }
        u8 *swap = to_host ? host : ram;
        if (!to_host)
            std::memcpy(ram, host, v.size);
        if (v.runs) {
            swap_runs(swap, v.runs, v.nruns);
        } else if (v.elem > 1) {
            SwapRun r = {0, v.elem, (u16)(v.size / v.elem)};
            swap_runs(swap, &r, 1);
        }
    }
}

const u32 kCtx = 0x80159080;

}  // namespace

void set_unported_hook(void (*hook)(const char *, const char *)) { g_unported_hook = hook; }

void unported(const char *func, const char *what) {
    if (g_unported_hook)
        g_unported_hook(func, what);
    std::fprintf(stderr, "nomercy: unported path in %s: %s\n", func, what);
    std::abort();
}

void state_import() {
    for (const Var &v : kVars)
        copy_var(v, true);
    // The context holds host pointers; the N64 one holds addresses of g_wrestlers records.
    WrestlerCtx &c = g_wrestler_ctx;
    s32 *ints = &c.slot;
    for (int i = 0; i < 8; i++)
        ints[i] = mem_s32(kCtx + 4 * i);
    c.buttons = mem_u16(kCtx + 0x20);
    c.pressed = mem_u16(kCtx + 0x22);
    c.in_0a = mem_u16(kCtx + 0x24);
    c.in_0c = mem_u16(kCtx + 0x26);
    c.in_0e = mem_u16(kCtx + 0x28);
    c._pad2a = mem_u16(kCtx + 0x2A);
    Wrestler **recs = &c.self;
    for (int i = 0; i < 5; i++) {
        u32 a = mem_u32(kCtx + 0x2C + 4 * i);
        // A slot of -1 gives the address just before the array; keep such indices as they are.
        s32 d = (s32)(a - 0x80159990u);
        recs[i] = a != 0 && d % 856 == 0 ? g_wrestlers + d / 856 : nullptr;
    }
}

void state_import_external() {
    for (const Var &v : kVars)
        if (v.p == g_pads || v.p == &g_rng_main_state)
            copy_var(v, true);
}

void state_export() {
    for (const Var &v : kVars)
        copy_var(v, false);
    const WrestlerCtx &c = g_wrestler_ctx;
    const s32 *ints = &c.slot;
    for (int i = 0; i < 8; i++)
        mem_w32(kCtx + 4 * i, (u32)ints[i]);
    mem_w16(kCtx + 0x20, c.buttons);
    mem_w16(kCtx + 0x22, c.pressed);
    mem_w16(kCtx + 0x24, c.in_0a);
    mem_w16(kCtx + 0x26, c.in_0c);
    mem_w16(kCtx + 0x28, c.in_0e);
    mem_w16(kCtx + 0x2A, c._pad2a);
    Wrestler *const *recs = &c.self;
    for (int i = 0; i < 5; i++)
        mem_w32(kCtx + 0x2C + 4 * i, recs[i] ? 0x80159990u + (u32)((recs[i] - g_wrestlers) * 856) : 0);
}

}  // namespace nm

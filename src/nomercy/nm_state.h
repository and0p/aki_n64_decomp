// The match state the decompiled code keeps in host variables, and the N64 addresses of the rest.
//
// Host variables are the globals the code uses all the time (the wrestler records, the input
// entries, the context, the random numbers, the clock and the match flags). They mirror N64
// globals one to one; state_import() / state_export() copy them from / to the RDRAM image, so a
// replay can start from (or be compared with) a RAM snapshot. Globals used rarely stay in the image
// and are reached through mem_*() with the addresses below.
#pragma once
#include "nm_types.h"

namespace nm {

// ---- host variables (N64 address) ----
extern Wrestler g_wrestlers[4];      // 0x80159990
extern WrestlerCtx g_wrestler_ctx;   // 0x80159080
extern InputSlot g_input[4];         // 0x80159518
extern Pad g_pads[4];                // 0x8009EDA0, filled by the host (pads_read)
extern u32 g_rng_match_state;        // 0x80159350
extern u32 g_rng_match_inc;          // 0x80159354
extern s16 g_rand_pool[32];          // 0x80159358
extern u32 g_rng_main_state;         // 0x80053780 (main segment)
extern s32 g_update_first_slot;      // 0x801591F8
extern s32 g_match_phase;            // 0x801591FC
extern s32 g_phase_wait;             // 0x801591E0
extern s32 g_phase_timer;            // 0x801591E4
extern u8 g_match_flags[2];          // 0x8016B598 / 0x8016B599
extern s32 g_match_clock[3];         // 0x8016B930: g_match_minutes, seconds, frames
extern s32 g_run_clock[3];           // 0x801595F8: minutes, seconds, frames (always runs)
extern u32 g_match_type;             // 0x80097EC0 (set outside the match)
extern u32 g_match_rules;            // 0x800A9950 (set outside the match)
extern u32 g_frame_bits;             // 0x80162A7C: per-slot bits from wrestler_update; bit 1 pauses play
extern s16 g_state_history[4][16];   // 0x8016B5A0
extern u8 g_cpu_flags[4];            // 0x8009ED20: 1 for each slot the CPU does not control
extern s16 g_last_anim[4];           // 0x801593A0: each slot's animation before the last wrestler_set_anim
extern Vec3 g_head_look[4];          // 0x801593A8: each slot's head-look lean (head_look_update)
extern s32 g_anim_mode[4];           // 0x801593D8: each slot's posing mode (wrestler_set_anim)
extern ModelBlock g_models[4];       // 0x800A1130: the model blocks model_animate poses (main segment)
extern AnimStream g_anim_streams[4]; // 0x80069F20: the animation streams (main segment)
extern ModelNode g_model_nodes[4][28];  // 0x80091CC0: the models' render trees (main segment)

// ---- addresses of globals left in the RDRAM image ----
enum : u32 {
    A_RUNIN_FLAGS = 0x8016B650,      // u16: run-in arming bits (0x30 armed by the clock roll, 0x400 the one-time roll)
    A_COUNTOUT = 0x80162A80,         // g_countout (u8)
    A_COUNTOUT_LEFT = 0x8016B594,    // g_countout_left (s32): counts left before a count-out
    A_COUNTOUT_SHOWN = 0x8016B578,   // s32: the count-out number shown
    A_COUNTOUT_CLOCK = 0x801591EC,   // s32[3]: the count-out's clock (minutes, seconds, frames)
    A_COUNTOUT_OPTION = 0x800A9462,  // s8: the match's count-out setting (0 off, 1 10 counts, 2 20)
    A_COUNTOUT_COUNTS = 0x80146568,  // s32[3]: counts by option: 0, 10, 20
    A_TIME_LIMITS = 0x80146574,      // s8[6]: minutes by time-limit option (1-5: 5, 10, 15, 30, 60)
    A_PIN_SLOT = 0x80159204,         // s32: slot whose pin is being counted, -1 none
    A_PIN_SHOWN = 0x80159200,        // s32: the count last shown
    A_PIN_COUNTS = 0x8016B8F0,       // g_pin_counts s32[4][3]
    A_TIME_LIMIT = 0x8009FE6C,       // s8: time limit option (index into 0x80146574)
    A_ARENA = 0x80159628,            // g_arena (s32)
    A_SLOT_BITS = 0x8014DB60,        // u32[4]: per-slot bits wrestler_update ORs into g_frame_bits
    A_SLOT_PAD = 0x8016B9D0,         // s32[4]: the pad each slot reads
    A_AI_BLOCKS = 0x801596C0,        // g_ai_blocks u8[4][176]
};

inline Wrestler *wrestler(s32 slot) { return &g_wrestlers[slot]; }
inline u32 wrestler_n64_addr(const Wrestler *w) { return 0x80159990u + (u32)(w - g_wrestlers) * 856u; }

// RDRAM image <-> host variables.
void state_import();
void state_export();
// Only what the host supplies each frame: the pads and rng_main's state.
void state_import_external();

}  // namespace nm

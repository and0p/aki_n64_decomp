// The instant-replay recorder (0x8013092C; named wrestlers_collide before this read). Every frame it
// stores each wrestler's and object's pose in a 600-frame ring buffer, and starts a replay when a
// move's animation reaches its replay frame.
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_REPLAY_WRESTLERS = 0x801593E8,  // u32: buffer, 7200 bytes (600 x 12) per wrestler
    A_REPLAY_OBJECTS = 0x801593EC,    // u32: buffer, 7200 bytes per object
    A_REPLAY_SLOT = 0x801593F0,       // s32: the wrestler the replay follows
    A_REPLAY_OPPONENT = 0x801593F4,   // s32
    A_REPLAY_POS = 0x801593F8,        // s32: the frame being recorded (0-599)
    A_REPLAY_SPEED = 0x801593FC,      // s32
    A_REPLAY_BACK = 0x80159400,       // s32: frames to go back
    A_REPLAY_START = 0x80159404,      // s32
    A_REPLAY_FROM = 0x80159408,       // s32
    A_REPLAY_MARK = 0x8015940C,       // s32
    A_REPLAY_CAMERA = 0x80159410,     // s32
    A_REPLAY_ON = 0x80159414,         // s32
    A_REPLAY_TIME = 0x80159418,       // s32
};

s16 tenths(f32 v) { return (s16)(s32)((f64)v * 10.0); }

}  // namespace

// N64 0x8013092C
void replay_record() {
    if (!(g_frame_bits & 4)) {
        u8 tables = mem_u8(0x8016B928);  // g_tables_intact
        s32 arena = mem_s32(A_ARENA);
        s32 pos = mem_s32(A_REPLAY_POS);
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            u32 rec = mem_u32(A_REPLAY_WRESTLERS) + 7200 * i + 12 * pos;
            mem_w16(rec + 10, 0);
            if (w->roster_id == -1) {
                mem_w16(rec + 8, (u16)-1);
            } else {
                mem_w16(rec + 8, (u16)w->anim_id);
                s32 f = w->anim_frame - 1;
                mem_w16(rec + 10, (u16)((f & ~(f >> 31)) & 0x3FF));
                mem_w16(rec + 0, (u16)tenths(w->pos.x));
                mem_w16(rec + 2, (u16)tenths(w->pos.y));
                mem_w16(rec + 4, (u16)tenths(w->pos.z));
                u16 facing = (u16)((w->facing / 4) & 0x3FF);
                mem_w16(rec + 6, facing);
                u16 f8e = w->flags8e;
                u16 bits = (f8e & 2) ? 0x8000 : 0;
                if (f8e & 0x04) bits = 0x4000;
                if (f8e & 0x08) bits = 0x2000;
                if (f8e & 0x10) bits = 0x1000;
                if (f8e & 0x20) bits = 0x0800;
                mem_w16(rec + 6, facing | bits);
                u16 mode_bit = 0;
                switch (g_anim_mode[i]) {
                case 2: case 3: mode_bit = 0x8000; break;
                case 6: mode_bit = 0x4000; break;
                default: break;
                }
                mem_w16(rec + 10, mem_u16(rec + 10) | mode_bit);
            }
            if (i != 0)
                continue;
            // Slot 0's record also keeps which breakable tables are still standing.
            u16 t = 0;
            if (arena < 10) {
                if (tables & 0x04) t |= 0x2000;
                if (tables & 0x01) t |= 0x1000;
                if (tables & 0x02) t |= 0x0800;
            } else if (arena == 21) {
                if (tables & 0x08) t |= 0x2000;
            } else if (arena == 24) {
                if (tables & 0x10) t |= 0x2000;
                if (tables & 0x20) t |= 0x1000;
                if (tables & 0x40) t |= 0x0800;
            }
            mem_w16(rec + 10, mem_u16(rec + 10) | t);
        }
        for (s32 j = 0; j < 7; j++) {
            u32 obj = 0x8016AE10 + 180 * j;
            u32 rec = mem_u32(A_REPLAY_OBJECTS) + 7200 * j + 12 * mem_s32(A_REPLAY_POS);
            u8 holder = (u8)(mem_s32(obj + 8) & 7);
            mem_w8(rec, holder);
            if (j == 6 && (mem_u16(obj + 0x20) & 0x8000))
                mem_w8(rec, holder | 0x80);
            mem_w8(rec + 1, (u8)mem_s32(obj + 4));
            mem_w16(rec + 2, (u16)tenths(mem_f32(obj + 0x44)));
            mem_w16(rec + 4, (u16)tenths(mem_f32(obj + 0x48)));
            mem_w16(rec + 6, (u16)tenths(mem_f32(obj + 0x4C)));
            for (s32 k = 0; k < 4; k++)
                mem_w8(rec + 8 + k, (u8)(s32)((f64)mem_s32(obj + 0x50 + 4 * k) * 0.0625));
        }
        s32 next = mem_s32(A_REPLAY_POS) + 1;
        mem_w32(A_REPLAY_POS, (u32)next);
        if (!(next < 600)) {
            mem_w32(A_REPLAY_POS, 0);
            g_frame_bits |= 0x80000;
        }
    }

    if (mem_u16(A_RUNIN_FLAGS) & 0xF)
        return;
    if (!(g_frame_bits & 0x100) && !mem_u8(0x800A946A)) {
        s32 count = 0;
        for (s32 i = 0; i < 4; i++)
            if (g_wrestlers[i].roster_id != -1)
                count += (g_wrestlers[i].flags_e9 & 0x10) != 0;
        if ((g_match_type & 0x1000) || ((g_match_type & 0xC000) && count < 3)) {
            u32 bits_now = g_frame_bits;
            for (s32 i = 0; i < 4; i++) {
                const Wrestler *w = &g_wrestlers[i];
                if (w->roster_id == -1 || !(w->flags_e9 & 0x10))
                    continue;
                if (!(g_wrestlers[w->opponent].flags_e9 & 0x10))
                    continue;
                if (w->anim_frame != w->anim_player[AP_REPLAY_FRAME] + 15)
                    continue;
                u32 bits = mem_u32(A_SLOT_BITS + 4 * i);
                if (!(bits_now & bits))
                    continue;
                // A replay of this move: from 15 frames back, at half speed.
                mem_w32(A_REPLAY_SLOT, (u32)i);
                g_frame_bits = bits | ((bits_now & 0x80000) | 1);
                mem_w32(A_REPLAY_SPEED, 2);
                mem_w32(A_REPLAY_BACK, (u32)-15);
                mem_w32(A_REPLAY_ON, 1);
                mem_w32(A_REPLAY_OPPONENT, (u32)w->opponent);
                mem_w32(A_REPLAY_START, mem_u32(A_REPLAY_POS));
                mem_w32(A_REPLAY_MARK, mem_u32(A_REPLAY_POS));
                break;
            }
        }
    }
    if (mem_u16(A_RUNIN_FLAGS) & 0xF)
        return;
    if (!(g_frame_bits & 1))
        return;
    g_frame_bits = (g_frame_bits & 0xFFEFFFFE) | 0xFA;
    s32 start = mem_s32(A_REPLAY_START) + mem_s32(A_REPLAY_BACK);
    mem_w32(A_REPLAY_START, (u32)start);
    if (!(start < 600))
        mem_w32(A_REPLAY_START, (u32)(start - 600));
    start = mem_s32(A_REPLAY_START);
    if (start < 0)
        mem_w32(A_REPLAY_START, (g_frame_bits & 0x80000) ? (u32)(start + 600) : 0);
    if (g_frame_bits & 0x100) {
        // Back to where the move's animation started (a recorded frame number under 2).
        u32 buf = mem_u32(A_REPLAY_WRESTLERS) + 7200 * mem_s32(A_REPLAY_SLOT);
        if (!((mem_u16(buf + 12 * mem_s32(A_REPLAY_START) + 10) & 0x3FF) < 2)) {
            u32 wrapped = g_frame_bits & 0x80000;
            do {
                s32 s = mem_s32(A_REPLAY_START);
                mem_w32(A_REPLAY_START, (u32)(s - 1));
                if (s - 1 < 0) {
                    if (!wrapped) {
                        mem_w32(A_REPLAY_START, 0);
                        break;
                    }
                    mem_w32(A_REPLAY_START, (u32)(s + 599));
                }
            } while (!((mem_u16(buf + 12 * mem_s32(A_REPLAY_START) + 10) & 0x3FF) < 2));
        }
    }
    mem_w32(A_REPLAY_TIME, 0);
    mem_w32(A_REPLAY_CAMERA, 222);
    mem_w32(A_REPLAY_FROM, mem_u32(A_REPLAY_START));
    mem_w32(A_REPLAY_CAMERA, (u32)(mem_s16(0x8014DB70 + 2 * mem_s32(A_REPLAY_SLOT)) | 0xDE));
}

}  // namespace nm

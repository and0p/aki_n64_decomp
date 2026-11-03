// Animation bookkeeping on the wrestler record: starting an animation (the file 703 record copied
// into the animation player), the per-frame advance and posing, and the head-look lean.
#include "nm_funcs.h"

namespace nm {

namespace {

// The animation tables loaded from file 703 (pointers at 0x8006A220 / 0x8006A224), indexed from
// the first generic animation, 11737.
u32 anim_index(s32 anim) { return mem_u32(0x8006A220) + 4 * (u32)(anim - 11737); }
u32 anim_record(s32 anim) { return mem_u32(0x8006A224) + 36 * (u32)(anim - 11737); }

}  // namespace

// N64 0x80011A54
s32 anim_frame_count(s32 anim) { return mem_s16(anim_index((s16)anim) + 2); }

// N64 0x80011AC0
s32 anim_pose_class(s32 anim) { return mem_u16(anim_index((s16)anim)) >> 10; }

// N64 0x80011BD8: copies the animation's record into an animation player (Wrestler +0x2E8).
// The player's bytes keep the N64 order; byte 0-3 is a word.
void anim_player_set(u8 *p, s16 anim) {
    if (anim < 11737)
        return;
    u32 turn = 4u * (mem_u16(anim_index(anim)) & 0x3FF);
    p[0] = (u8)(turn >> 24);
    p[1] = (u8)(turn >> 16);
    p[2] = (u8)(turn >> 8);
    p[3] = (u8)turn;
    u32 r = anim_record(anim);
    p[4] = (u8)anim_frame_count(anim);
    p[7] = (u8)anim_pose_class(anim);
    p[5] = mem_u8(r + 1) >> 7;
    p[6] = mem_u8(r) & 0xF;
    p[8] = mem_u8(r + 3);
    p[9] = mem_u8(r + 4);
    p[10] = mem_u8(r + 18);
    p[11] = mem_u8(r + 19);
    p[12] = mem_u8(r + 5);
    p[13] = mem_u8(r + 6);
    p[14] = mem_u8(r + 7);
    p[15] = mem_u8(r + 8);
    p[16] = mem_u8(r + 9);
    p[17] = mem_u8(r + 10);
    p[18] = mem_u8(r + 11);
    p[19] = mem_u8(r + 12);
    p[20] = mem_u8(r + 17);
    p[21] = mem_u8(r + 16);
    p[22] = mem_u8(r + 2);
    p[23] = mem_u8(r) >> 4;
    p[24] = mem_u8(r + 13);
    p[25] = mem_u8(r + 14);
    p[26] = mem_u8(r + 15);
}

// N64 0x8012F83C
void wrestler_anim_invalidate(Wrestler *w) { w->anim_id = -1; }

// N64 0x8012F848: starts animation `anim` (a no-op when it is already playing): the player from
// file 703, frame 0, and the posing mode for model_animate.
void wrestler_set_anim(Wrestler *w, s32 anim, s32 mode) {
    s16 *last = &g_last_anim[w->slot];
    *last = w->anim_id;
    w->anim_id = (s16)anim;
    if ((s16)anim == *last)
        return;
    *last = (s16)anim;
    u8 *p = w->anim_player;
    anim_player_set(p, w->anim_id);
    if (p[8] == 0)
        p[8] = 1;
    if (p[8] == 255)
        p[8] = p[4];
    if (p[9] == 0)
        p[9] = 1;
    if (p[9] == 255)
        p[9] = p[4];
    if (p[0x13] == 255)
        p[0x13] = p[4];
    if (p[0x10] == 0)
        p[0x10] = 1;
    if (p[0x14] == 0)
        p[0x14] = 1;
    if (p[0x1A] == 0)
        p[0x1A] = p[4];
    w->anim_frame = 0;
    w->anim_frames_left = p[4];
    g_anim_mode[w->slot] = mode;
}

// N64 0x8012F980: plays the animation from its start even when it is the current one.
void wrestler_restart_anim(Wrestler *w, s32 anim, s32 mode) {
    wrestler_anim_invalidate(w);
    wrestler_set_anim(w, anim, mode);
}

// N64 0x8012F9CC: poses the model for the current frame. model_animate gets a copy of the
// position (with the facing), moves it when it anchors a new animation's root, and the copy is
// stored back. On a ladder (location 0x08) the body's tilts go with it.
void model_pose_in_place(Wrestler *w, Vec3 *lean, s32 mode) {
    Vec3 pos = w->pos;
    s32 facing = w->facing;
    s32 tilt_x = 0, tilt_z = 0;
    if (w->location & 8) {
        tilt_x = w->tilt_x;
        tilt_z = w->tilt_z;
    }
    model_animate(w->slot, w->anim_id, w->anim_frame, &pos, facing, lean, (s8)mode, tilt_x, tilt_z);
    w->pos = pos;
}

// N64 0x8012FBD0: the head-look lean (`lean`, degrees) of a standing wrestler toward its opponent:
// y follows the angle to it (8 degrees a frame, at most 67.5, only on the apron, a ladder or in
// animation 12092), x its height (4 a frame, at most 30). 0 (and no lean) when not standing.
s32 head_look_update(s32 slot, Vec3 *lean) {
    Wrestler *w = &g_wrestlers[slot];
    if (!(w->flags8a & 0x100)) {
        lean->x = 0.0f;
        lean->y = 0.0f;
        lean->z = 0.0f;
        return 0;
    }
    f32 out[9];
    segment_distance(&w->joints[3], &g_wrestlers[w->opponent].joints[3], out);
    f32 want = (f32)((f64)angle_diff((s32)out[7], w->facing) * 0.087890625);
    f32 step = want;
    if (8.0 < (f64)step)
        step = 8.0f;
    if ((f64)step < -8.0)
        step = -8.0f;
    lean->y = lean->y + step;
    if (absf(want) < absf(lean->y))
        lean->y = want;
    if (67.5 < (f64)lean->y)
        lean->y = 67.5f;
    if ((f64)lean->y < -67.5)
        lean->y = -67.5f;
    if (!(w->location & 0xA) && w->anim_id != 12092)
        lean->y = 0.0f;
    s32 up = vec_xz_angle((f32)((f64)out[4] * 1.25), out[1]);
    want = (f32)-((f64)angle_diff(up, 1024) * 0.087890625);
    step = want;
    if (4.0 < (f64)step)
        step = 4.0f;
    if ((f64)step < -4.0)
        step = -4.0f;
    lean->x = lean->x + step;
    if (absf(want) < absf(lean->x))
        lean->x = want;
    if (30.0 < (f64)lean->x)
        lean->x = 30.0f;
    if ((f64)lean->x < -30.0)
        lean->x = -30.0f;
    return 1;
}

// N64 0x8012FA78: each wrestler's lean, the restart of a looping animation that ran out, the
// posing, then the frame advance.
void wrestlers_ground_contact() {
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1)
            continue;
        Vec3 *lean = &g_head_look[i];
        Vec3 *use = head_look_update(i, lean) ? lean : nullptr;
        if (w->anim_frames_left <= 0) {
            if (!w->anim_player[AP_LOOP]) {
                model_pose_in_place(w, use, g_anim_mode[i]);
                continue;
            }
            wrestler_restart_anim(w, w->anim_id, 0);
        }
        model_pose_in_place(w, use, g_anim_mode[i]);
        if (w->anim_frames_left > 0) {
            w->anim_frame = w->anim_frame + 1;
            w->anim_frames_left = w->anim_frames_left - 1;
        }
    }
}

}  // namespace nm

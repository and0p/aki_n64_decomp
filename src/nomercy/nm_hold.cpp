// Holds and submissions: the hold states 0x48 / 0x49, on the ground or standing in the corner (a
// move whose record flags 0x400 / 0x800 / 0x1000 chain into it, move_chain_check), the held wrestler's crawl for the ropes, giving up and
// the submission fall. Read from the code (2026-11-03) and not trace-checked yet: no trace reaches
// a hold, so these are the annotated reading, not a replay-proven translation. Checked against the
// emulator (tools/trace/scripts/nm_ground_hold.json, nm_submission.json): the chain into 0x48 /
// 0x49, escape_time op 3, the 31-frame damage clock, the mash-out and the end animations.
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_FOLLOW_VARIANTS = 0x800A5720, // u8[4][548]: each move's follow-up variant by slot (the pin or hold move is 292 + it)
    A_HOLD_TICK = 0x801594C0,       // s32[4]: frames since the hold last applied its damage
};

// Move record flags (MoveRecord.flags, +0x20) the hold reads.
enum : u32 {
    MF_HOLD_ENDS_DOWN = 0x02,      // the holder lies down after it (wrestler_settle_pose)
    MF_HELD_ENDS_DOWN = 0x04,      // the held wrestler stays down after it
    MF_HOLD_FLAGS80 = 0x08,        // flags80 0x400 on both while held
    MF_SUBMISSION = 0x80,          // the held wrestler can give up
    MF_ROPE_BREAK = 0x100,         // the ropes break it (rule 0x02)
    MF_NO_CRAWL = 0x200000,        // the held wrestler cannot crawl for the ropes
    MF_LATE_BOUNDS = 0x400000,     // no bounds while held; after it, only past the player's frame 0x11
};

u32 move_flags(const Wrestler *w) { return mem_u32(w->move + 0x20); }

// The region of the held wrestler that a submission works on: the holder's move record +0x0C
// (MoveRecord.hit_kind) indexes region[].
f32 *hold_region(Wrestler *held, const Wrestler *holder) {
    s32 k = (s8)mem_u8(holder->move + 0x0C);
    if (k < 0 || k > 4)
        NM_UNPORTED("state_49_held: a hold region outside region[0..4]");
    return &held->region[k];
}

// A fall by submission: the bell, and under rule 0x80000 (falls counted) a fall for the opponent with
// the region reset to 5, else end_flags 0x20 (the submission fall).
void submission_fall(Wrestler *w, Wrestler *holder) {
    host_sfx(1);
    if (g_match_rules & 0x80000) {
        fall_count_add(w, w->opponent);
        *hold_region(w, holder) = 5.0f;
    } else {
        w->end_flags |= 0x20;
    }
}

}  // namespace

// N64 0x800E1454: a referee count display call. Only kind 9 does anything (the shown count 0x80154632
// and, for a count of 0 or less, 0x80154634 = -1; else hud_message_refresh); the hold's kind 4 is a
// no-op in No Mercy.
void ref_count_reset(s32 kind, s32 count) {
    if (kind != 9)
        return;
    mem_w16(0x80154632, (u16)count);
    if ((s16)count <= 0)
        mem_w16(0x80154634, 0xFFFF);
    else
        host_hud("hud_message_refresh", 0, 0);
}

// N64 0x80117EB0: a counted fall (rule 0x80000: ironman): one more (99 at most) in the falls tally
// of `slot` (0x80159218); the wrestler who lost it gets 60 health at least and the attitude meter
// reset to 50 (spirit_update 7).
void fall_count_add(Wrestler *w, s32 slot) {
    u32 a = 0x80159218 + 4u * (u32)slot;
    s32 n = mem_s32(a) + 1;
    mem_w32(a, (u32)n);
    if (n >= 100)
        mem_w32(a, 99);
    if (w != nullptr) {
        if (w->health < 60)
            w->health = 60;
        spirit_update(w, 7);
    }
}

// N64 0x80137B6C: the held wrestler crawls for the ropes (a press in the ground hold). With the
// holder in the ring both move 2.5 toward the nearest side of the ring: by the larger of |x| and |z|
// of the held wrestler's position (x on a tie), toward its sign. d-pad right / left (0x100 / 0x200)
// also turn both by +11 / -11 (about 1 degree) and swing the held wrestler round the holder.
void ground_hold_crawl(Wrestler *held, Wrestler *holder, u16 buttons) {
    if (!(holder->location & 1))
        return;
    f32 ax = absf(held->pos.x);
    f32 az = absf(held->pos.z);
    s32 side;
    if (az <= ax)
        side = (held->pos.x < 0.0f) ? 3 : 1;
    else
        side = (held->pos.z < 0.0f) ? 2 : 0;
    Vec3 d = {0.0f, 0.0f, 0.0f};
    switch (side) {
    case 0: d.z = 2.5f; break;
    case 1: d.x = 2.5f; break;
    case 2: d.z = -2.5f; break;
    case 3: d.x = -2.5f; break;
    }
    wrestler_translate(held, &d);
    wrestler_translate(holder, &d);
    s32 turn = (buttons & 0x100) ? 11 : 0;
    if (buttons & 0x200)
        turn = -11;
    if (turn == 0)
        return;
    held->facing = angle_add(held->facing, turn);
    holder->facing = angle_add(holder->facing, turn);
    Vec3 off, p;
    vec_sub(&holder->pos, &held->pos, &off);  // holder -> held
    point_at_offset(&holder->pos, &off, turn, &p);
    p.y = held->pos.y;
    vec_sub(&held->pos, &p, &d);
    wrestler_translate(held, &d);
}

// N64 0x80137044: holding a hold (state 0x48; paired with 0x49). Step 0: flags80 0x140 and
// the hold move, 292 + the follow-up variant of the move just done (0x800A5720 by slot and move: the
// same table the pin reads), its style animation (mode 1), flags80 0x400 for record flag 0x08.
// Step 1 (held): flags80 0x140 (0x540); the stick pushed (in_byte) sets state_flags 0x100, which
// keeps the held wrestler from giving up; the bounds (unless record flag 0x400000); R pressed or a
// pause lets go (0x01 on both); a give-up (0x08, set by the held side) is made the whole word, 0x408
// for a counted submission, and copied to the held wrestler; when the animation runs out or the
// held wrestler slipped the ground grapple or a third wrestler broke it up (0x10: 0x47,
// state_29_break_up) and anything ended it (bits 0x1F:
// 0x01 let go, 0x02 mashed out, 0x04 rope break, 0x08 gave up) the end animation: style + 6 for
// 0x10, + 4 gave up, + 2 the rest (state_flags 0x20, mode 0; the RAM shows + 2 for a mash-out). Step 2: the bounds (with record flag
// 0x400000 only past the player's frame 0x11), the finisher effects; at the end unpaired, 3 spirit
// below 49 health, then lying down (record flag 0x02 or escaped) or 0x30.
void state_48_hold(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *d = ctx->rec_a;
    switch (w->state_step) {
    case 0: {
        w->flags80 |= 0x140;
        s32 slot = (s8)w->move_slot;
        wrestler_start_move(w, slot, mem_u8(A_FOLLOW_VARIANTS + 548u * (u32)slot + (u32)(u16)w->move_num) + 292);
        wrestler_set_anim(w, anim_style(w, 0), 1);
        if (move_flags(w) & MF_HOLD_FLAGS80)
            w->flags80 |= 0x400;
        w->state_step = w->state_step + 1;
        return;
    }
    case 1: {
        u16 f80 = w->flags80;
        w->flags80 = f80 | 0x140;
        if (move_flags(w) & MF_HOLD_FLAGS80)
            w->flags80 = f80 | 0x540;
        if (ctx->in_byte)
            w->state_flags |= 0x100;
        else
            w->state_flags &= 0xFEFF;
        if (!(move_flags(w) & MF_LATE_BOUNDS))
            w->dpad_held = state_bounds(w, d, nullptr, 0);
        if (!(w->state_flags & 0x1F) && ((ctx->pressed & 0x10) || match_can_interrupt())) {
            w->state_flags |= 1;
            d->state_flags |= 1;
        }
        if (w->state_flags & 8) {
            // Assigned, not or-ed: the give-up replaces every other bit on both.
            w->state_flags = (w->state_flags & 0x400) ? 0x408 : 0x008;
            d->state_flags = w->state_flags;
        }
        if (w->anim_frames_left > 0 && !(w->state_flags & 0x10))
            return;
        if (!(w->state_flags & 0x1F))
            return;
        w->state_flags |= 0x20;
        s32 v;
        if (w->state_flags & 0x10)
            v = 6;
        else
            v = (w->state_flags & 8) ? 4 : 2;
        // The state 68 test is the pin handler's (0x44): never true in this one.
        s32 anim = (w->state == state::PinAttacker) ? v + w->anim_id : anim_style(w, v);
        wrestler_set_anim(w, anim, 0);
        w->state_step = w->state_step + 1;
        return;
    }
    case 2:
        if (!(move_flags(w) & MF_LATE_BOUNDS) || w->anim_player[AP_PAIR_FRAME] < w->anim_frame)
            state_bounds(w, d, nullptr, 0);
        finisher_effects(w);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        wrestler_unpair(w);
        if (wrestler_health_eff(w) < 49)
            wrestler_add_spirit(w, 3);
        if ((move_flags(w) & MF_HOLD_ENDS_DOWN) || (w->state_flags & 0x10))
            wrestler_settle_pose(w);
        else
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        return;
    default:
        return;
    }
}

// N64 0x80137364: held in a hold (state 0x49; paired with 0x48). Step 0: flags80 0x180
// (0x580 for record flag 0x08), the holder's style animation + 1 (mode 1), the damage clock cleared,
// escape = escape_time op 3, the mash counter reset; for a submission (record flag 0x80) outside
// guest-referee matches, in the ring (or rule 0x4000) with both in play, ref_count_reset(4) (a
// no-op). Step 1: escape counts down one a frame and one more per four counted presses
// (recover_tick); below 0 (not while the stick is pushed: that keeps it at 0) the held wrestler
// escapes (the holder's state_flags 0x02). Every 31 frames the hold's damage (hit_move_copy,
// apply_damage); then a submission gives up when the region it works on is at 0 or below and spirit
// is 90 or less, unless the holder pushes the stick (its state_flags 0x100): the holder's 0x08, and
// with submissions on (rule 0x10) outside guest-referee matches, in the ring (or rule 0x4000), with
// both in play, 0x408 (the fall). A / B / L / R / C under rule 0x02 crawl for the ropes
// (ground_hold_crawl; not for record flag 0x200000). The bounds (not for 0x400000); under rule 0x02
// in the ring, for record flag 0x100, any of the held wrestler's 19 body points (point_120 and
// joints[0..17]) or its model's 19 joint points (pt_b_world) more than 230 from the ring centre in x
// or z breaks the hold: the holder's 0x04 (0x200 in a guest-referee match, for the referee) and the
// HUD message with both in play. Once the holder's 0x20 is set, the end animation: the holder's
// style + 7 (0x10), + 5 gave up, + 3 the rest (mode 0). Step 2: the bounds as 0x48's; after a
// counted give-up (0x408) the fall at the holder animation's event-2 frame (or at the end without
// one): submission_fall, state_counter 0x01; at the end: under rule 0x80000 a counted give-up stands
// up through 0x30 (flags8a 0x10); else down (on a raised place, record flag 0x04, a lost fall or a
// give-up: flags8c 0x800 for a give-up that was not a fall) or 0x30; then move_aftermath_check
// unless the fall is lost and not yet decided (end_flags & 3 == 1).
void state_49_held(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    u32 tick = A_HOLD_TICK + 4u * (u32)w->slot;
    switch (w->state_step) {
    case 0: {
        u16 f80 = w->flags80;
        w->flags80 = f80 | 0x180;
        if (move_flags(a) & MF_HOLD_FLAGS80)
            w->flags80 = f80 | 0x580;
        wrestler_set_anim(w, anim_style(a, 1), 1);
        mem_w32(tick, 0);
        w->escape = (s16)escape_time(w, a, 3);
        mash_count(w, 0, 0);
        if (!(g_match_type & 0x80000) && (move_flags(a) & MF_SUBMISSION) &&
            ((g_match_rules & 0x4000) || (w->location & 1)) && (w->flags_e9 & a->flags_e9 & 0x10))
            ref_count_reset(4, 0);
        w->state_step = w->state_step + 1;
        return;
    }
    case 1: {
        u16 f80 = w->flags80;
        w->flags80 = f80 | 0x180;
        if (move_flags(a) & MF_HOLD_FLAGS80)
            w->flags80 = f80 | 0x580;
        recover_tick(w, ctx->pressed);
        if (w->escape < 0) {
            if (ctx->in_byte)
                w->escape = 0;
            if (w->escape < 0 && !(a->state_flags & 0x1F))
                a->state_flags |= 2;
        }
        s32 t = mem_s32(tick) + 1;
        mem_w32(tick, (u32)t);
        if (t >= 31) {
            hit_move_copy(a, w);
            apply_damage(a, w);
            mem_w32(tick, 0);
            u16 sf = a->state_flags;
            if (!(sf & 0x100) && !(sf & 0x1F) && (move_flags(a) & MF_SUBMISSION) && w->spirit < 91 &&
                (f64)*hold_region(w, a) <= 0.0) {
                a->state_flags = sf | 8;
                if ((g_match_rules & 0x10) && !(g_match_type & 0x80000) &&
                    ((g_match_rules & 0x4000) || (w->location & 1)) && (w->flags_e9 & a->flags_e9 & 0x10))
                    a->state_flags = sf | 0x408;
            }
        }
        if (!(move_flags(a) & MF_NO_CRAWL) && (g_match_rules & 2) && (ctx->pressed & 0xC03F))
            ground_hold_crawl(w, a, ctx->buttons);
        if (!(move_flags(a) & MF_LATE_BOUNDS)) {
            w->dpad_held = state_bounds(w, a, nullptr, 0);
            if (!(a->state_flags & 0x1F) && (g_match_rules & 2) && (w->location & 1) &&
                (move_flags(a) & MF_ROPE_BREAK)) {
                bool at_ropes = false;
                for (s32 k = 1; k < 20 && !at_ropes; k++) {
                    const Vec3 *j = wrestler_joint(w, k - 1);
                    const Vec3 *m = &g_models[w->slot].joint[k - 1].pt_b_world;
                    at_ropes = (f64)absf(j->x) > 230.0 || (f64)absf(j->z) > 230.0 ||
                               (f64)absf(m->x) > 230.0 || (f64)absf(m->z) > 230.0;
                }
                if (at_ropes) {
                    if (g_match_type & 0x80000) {
                        a->state_flags |= 0x200;
                    } else {
                        a->state_flags |= 4;
                        if (w->flags_e9 & a->flags_e9 & 0x10)
                            host_hud("submission_message", 0, 0);
                    }
                }
            }
        }
        u16 sf = a->state_flags;
        if (!(sf & 0x20))
            return;
        // nullsub_hud_c (0x800E1510) is called here for a submission that was neither broken nor
        // given up, outside guest-referee matches, with both in play: empty in the release build.
        s32 v;
        if (sf & 0x10)
            v = 7;
        else
            v = (sf & 8) ? 5 : 3;
        wrestler_set_anim(w, anim_style(a, v), 0);
        w->state_step = w->state_step + 1;
        return;
    }
    case 2:
        if (!(move_flags(a) & MF_LATE_BOUNDS) || w->anim_player[AP_PAIR_FRAME] < w->anim_frame)
            state_bounds(w, a, nullptr, 0);
        if ((w->state_flags & 8) && (w->state_flags & 0x400)) {
            if (w->anim_frame == anim_event_frame(a->anim_id, 2)) {
                // The state_counter bit is set before the fall either way, so the end-of-animation
                // check below does not count it twice.
                host_sfx(1);
                w->state_counter |= 1;
                if (g_match_rules & 0x80000) {
                    fall_count_add(w, w->opponent);
                    *hold_region(w, a) = 5.0f;
                } else {
                    w->end_flags |= 0x20;
                }
            }
            if (w->anim_frames_left <= 0 && !(w->end_flags & 0x21) && !(w->state_counter & 1))
                submission_fall(w, a);
        }
        finisher_effects(w);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        // The original reads state_flags and end_flags as 32-bit words (the half after each is
        // masked off): (word & 0x04080000) is state_flags & 0x408, (word & 0x30000) end_flags & 3.
        if ((g_match_rules & 0x80000) && (w->state_flags & 0x408) == 0x408) {
            w->flags8a |= 0x10;
            wrestler_set_state_now(w, state::SettleAfterAnimation);
            return;
        }
        if ((w->location & 0x3C4) || (move_flags(a) & MF_HELD_ENDS_DOWN) || (w->end_flags & 1) ||
            (w->state_flags & 8)) {
            if ((w->state_flags & 0x408) == 0x008)
                w->flags8c |= 0x800;
            wrestler_settle_pose(w);
        } else {
            w->flags8a |= 0x10;
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        }
        if ((w->end_flags & 3) != 1)
            move_aftermath_check(a, a, w, 2);
        return;
    default:
        return;
    }
}

}  // namespace nm

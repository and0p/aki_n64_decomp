// Down and getting up: the time down (escape_time), the mash counter, state 0x31 (lying down) and
// state 0x4F (the get-up, the crouch and the get-up attack).
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_MASH_COUNT = 0x80159420,    // s32[4]: presses counted toward the threshold (mash_count op 1)
    A_MASH_RAW = 0x80159430,      // s32[4]: raw presses (ops 2 / 3)
    A_MASH_COOLDOWN = 0x80159440, // s32[4]: frames since the last counted press
    A_HEAT_BANDS = 0x801461E4,    // s16 pairs: finisher_heat bands for the pin's escape time (op 1)
    A_LYING_ANIMS = 0x8014DA98,   // s16[]: the lying animation of each end pose (20-47)
    A_PIN_VARIANTS = 0x800A5720,  // u8[4][548]: each move's pin variant (the cover after it), by slot
    A_PIN_HOLD_TICK = 0x801591D0, // s32[4]: frames in a submission hold since the last damage tick
};

}  // namespace

// N64 0x8011241C: how long a wrestler stays down or pinned, by `op` (0 none in finisher mode):
// op 0 (down): 180 + 3 x finisher_heat (180 after a finisher, flags8c 0x800) - (health + spirit) / 2
// - 80 x down_count (minus 2 under the opponent's flags8c 0x04), x 1.5 on the floor in a tag match
// (type 0x2000), x 0.65 on the apron; op 1 the pin; op 2 / 9: 100 - spirit / 2 - health / 4; op 3
// the ground hold (60 in finisher mode, else op 6); op 4 draped on the ropes; op 5 / 6 the rear lift
// and the carry: (200 or 300 + other.spirit / 2 - (spirit / 4 + health)) / 2; op 7 held upside
// down; op 8 none; op 10 hanging on the cage. Never below 0. The hold ops (3-7, 10) are read from
// the code and not trace-checked.
s32 escape_time(Wrestler *w, Wrestler *other, s32 op) {
    s32 t = 0;
    switch (op) {
    case 0: {
        if (w->flags_ee & 4)
            return 0;
        if (w->flags8c & 0x800)
            w->finisher_heat = 180;
        s32 base = 3 * w->finisher_heat + 180;
        s32 cond = (w->health + w->spirit) / 2;
        s32 downs = (other->flags8c & 4) ? w->down_count - 2 : w->down_count;
        downs = downs * 80;
        t = base - cond - downs;
        if ((g_match_type & 0x2000) && (w->location & 4))
            t = (s32)((f64)t * 1.5);
        if (w->location & 2)
            t = (s32)((f64)t * 0.65);
        break;
    }
    case 1: {
        // The pin: 95 plus the band of finisher_heat (0x801461E4 pairs) plus the pinner's spirit / 5,
        // plus 2 x pin_count, minus spirit / 3 and health / 2, plus the damage history ratio over 15;
        // 90 or more only with 90+ spirit (else 87).
        s32 heat = w->finisher_heat;
        s32 band = 0;
        if (mem_s16(A_HEAT_BANDS) < heat) {
            band = 1;
            u32 i = 4;
            while (mem_s16(A_HEAT_BANDS + i) < heat) {
                i += 4;
                band++;
            }
        }
        s32 bonus = mem_s16(A_HEAT_BANDS + 2 + 4u * (u32)band) + 95;
        s32 a = (s16)(other->spirit / 5) + bonus;
        s32 b = w->pin_count * 2;
        s32 c = (s16)(w->spirit / 3) + (s16)(w->health / 2);
        s32 sum_a, ratio;
        damage_history_ratio(w, &sum_a, &ratio);
        s32 d = ratio - 15;
        if (d < 0)
            d = 0;
        t = a + b - c + d;
        if (!(t < 90) && !(w->spirit < 90))
            t = 87;
        break;
    }
    case 2:
    case 9:
        if (w->flags_ee & 4)
            return 0;
        t = 100 - (w->spirit / 2 + w->health / 4);
        break;
    case 3:
        // The ground hold (0x49): a fixed 60 in finisher mode, else op 6's time.
        if (w->flags_ee & 4) {
            t = 60;
            break;
        }
        t = (300 + other->spirit / 2 - (w->spirit / 4 + w->health)) / 2;
        break;
    case 4:
        // Draped on the ropes (0x15): with count-outs on (g_countout bit 0) only while 6 or more
        // counts are left, else 0.
        if (w->flags_ee & 4)
            return 0;
        t = 180 - (w->spirit / 2 + w->health / 4);
        if ((mem_u8(A_COUNTOUT) & 1) && mem_s32(A_COUNTOUT_LEFT) < 6)
            t = 0;
        break;
    case 5:
        // Escaping the rear lift (0x80).
        if (w->flags_ee & 4)
            return 0;
        t = (200 + other->spirit / 2 - (w->spirit / 4 + w->health)) / 2;
        break;
    case 6:
        // The carry (0x82).
        if (w->flags_ee & 4)
            return 0;
        t = (300 + other->spirit / 2 - (w->spirit / 4 + w->health)) / 2;
        break;
    case 7:
        // Held upside down (0x34): x 8 against a strong hold (its own flags78 0x04), else x 4.
        if (w->flags_ee & 4)
            return 0;
        t = 100 - (w->spirit / 2 + w->health / 4);
        t = (w->flags78 & 4) ? t * 8 : t * 4;
        break;
    case 8:
        t = 0;
        break;
    case 10:
        // Hanging on the cage (0xC6, 0xCA).
        if (w->flags_ee & 4)
            return 0;
        t = (100 - (w->spirit / 2 + w->health / 4)) * 4;
        break;
    default:
        // Ops 11 and up: the original returns whatever was in a3 (0 on every path seen).
        NM_UNPORTED("escape_time: an op above 10");
    }
    if (t < 0)
        t = 0;
    return t;
}

// N64 0x80111FE0: sums the damage history's 30 (hits, damage) pairs: *hits and the damage per hit
// (0 with no hits).
s32 damage_history_ratio(const Wrestler *w, s32 *hits, s32 *ratio) {
    s32 h = 0, d = 0;
    for (s32 i = 0; i < 30; i++) {
        h += mem_u8(w->damage_history + 2u * (u32)i);
        d += mem_u8(w->damage_history + 2u * (u32)i + 1);
    }
    *hits = h;
    if (h != 0) {
        *ratio = d / h;
        return d / h;
    }
    *ratio = 0;
    return 0;
}

// N64 0x80132C30: the mash counter (per slot: presses 0x80159420, raw presses 0x80159430, the
// cooldown 0x80159440). op 0 resets; op 1 counts a press of A / B / L / R once the cooldown is 2
// or more (so presses at least 2 frames apart), toward a threshold by state (8 in 0x47; in the
// pin 0x45 / 0x4B by the referee's count: 2, 1, then every press, and once the count's clock is
// past 15 none), returning 2 below it, else 1 (3 at threshold 0, 4 at -1) with the count reset;
// op 2 counts raw presses (returns 2 on one); op 3 returns them.
s32 mash_count(Wrestler *w, u16 pressed, s32 op) {
    u32 slot = (u32)w->slot;
    if (op == 0) {
        mem_w32(A_MASH_COUNT + 4 * slot, 0);
        mem_w32(A_MASH_RAW + 4 * slot, 0);
        mem_w32(A_MASH_COOLDOWN + 4 * slot, 2);
        return 0;
    }
    if (op == 2) {
        if (!(pressed & 0xC030))
            return 0;
        mem_w32(A_MASH_RAW + 4 * slot, (u32)(mem_s32(A_MASH_RAW + 4 * slot) + 1));
        return 2;
    }
    if (op == 3)
        return mem_s32(A_MASH_RAW + 4 * slot);
    if (op != 1)
        return 0;
    s32 cd = mem_s32(A_MASH_COOLDOWN + 4 * slot);
    if (cd < 2) {
        mem_w32(A_MASH_COOLDOWN + 4 * slot, (u32)(cd + 1));
        return 0;
    }
    if (!(pressed & 0xC030))
        return 0;
    mem_w32(A_MASH_COOLDOWN + 4 * slot, 0);
    s32 count = mem_s32(A_MASH_COUNT + 4 * slot) + 1;
    mem_w32(A_MASH_COUNT + 4 * slot, (u32)count);
    s32 limit;
    s32 st = w->state;
    if (st == state::GroundGrappleDefender) {
        limit = 8;
    } else if (st == state::PinDefender|| st == state::PinFromMoveDefender) {
        s32 c = mem_s32(A_PIN_COUNTS + 12 * slot + 4);
        if (c == 0)
            limit = 2;
        else if (c == 1)
            limit = 1;
        else if (c == 2)
            limit = (mem_s32(A_PIN_COUNTS + 12 * slot + 8) < 16) ? 0 : -1;
        else
            limit = 4;
    } else {
        limit = 4;
    }
    if (count < limit)
        return 2;
    mem_w32(A_MASH_COUNT + 4 * slot, 0);
    if (limit == 0)
        return 3;
    if (limit == -1)
        return 4;
    return 1;
}

// N64 0x80132E24: the time down counts down one, and one more when the mash counter counts a
// press. Returns 1 once it is below 0.
s32 recover_tick(Wrestler *w, u16 pressed) {
    w->escape = (s16)(w->escape - 1);
    if (mash_count(w, pressed, 1) == 1)
        w->escape = (s16)(w->escape - 1);
    return w->escape < 0 ? 1 : 0;
}

// N64 0x800ED954: whether a downed wrestler's stay-down roll may run: not for an eliminated-rules
// wrestler on the floor (type 0x4000, rule 0x400, flags_e9 0x10); with end_flags 0x41 only when
// 0x02 is set.
s32 down_count_allowed(const Wrestler *w) {
    if ((g_match_type & 0x4000) && (g_match_rules & 0x400) && (w->flags_e9 & 0x10) && (w->location & 4))
        return 0;
    if (w->end_flags & 0x41)
        return (w->end_flags & 2) != 0;
    return 1;
}

// N64 0x80138E90: 1 when another wrestler working on this one is approaching it on the ground
// (states 121-123 at step 2-3 with flags_ea 0x02 and not 0x01), which gets flags_ea 0x10.
s32 down_count_others(const Wrestler *w) {
    for (s32 i = 0; i < 4; i++) {
        Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1 || o->opponent != w->slot)
            continue;
        if (o->state >= state::TurnbuckleDiveMove279 || o->state < state::TurnbuckleDiveMoves256And257)
            continue;
        if (!(o->flags_ea & 2))
            continue;
        if ((u32)(o->state_step - 2) >= 2)
            continue;
        if (!(o->flags_ea & 1)) {
            o->flags_ea |= 0x10;
            return 1;
        }
    }
    return 0;
}

// N64 0x8013026C: for a dazed-turn animation flags88 0x01 (run again); else the end bookkeeping
// and, lying, the lying animation of the end pose.
void anim_is_dazed_like(Wrestler *w) {
    if (dazed_turn_target(w, nullptr)) {
        w->flags88 |= 1;
        return;
    }
    anim_end_apply(w);
    if (pose_is_lying(w))
        wrestler_set_anim(w, mem_s16(A_LYING_ANIMS + 2u * (u32)w->anim_player[AP_END_POSE]), 1);
}

// N64 0x8013FEF4: the get-up animation: 12153 face up / 12158 face down (slouched: 12220 / 12245),
// in finisher mode 12166 / 12167 (not slouched); from state 0x4F (79) a variant by finisher_heat
// and health: + 2 (3 unslouched) at heat 30 or more, + 2 at 22-29, below 22: + 1 with heat 15 or
// more and health 64 or more, else + 2 below 64 health, + 1 below 128, else + 0.
s32 get_up_anim(const Wrestler *w) {
    s32 v = 0;
    s32 base;
    if (w->flags80 & 0x10)
        base = (w->flags8a & 1) ? 12245 : 12220;
    else
        base = (w->flags8a & 1) ? 12158 : 12153;
    if (w->flags_ee & 4) {
        if (!(w->flags80 & 0x10))
            base = 0x2F86 | (w->flags8a & 1);
    } else if (w->state == state::GetUp) {
        s32 heat = w->finisher_heat;
        s32 hp = w->health;
        if (!(heat < 30)) {
            v = ((w->flags80 & 0x10) == 0 ? 3 : 0) | 2;
        } else if (!(heat < 22)) {
            v = 2;
        } else if (!(heat < 15) && !(hp < 64)) {
            v = 1;
        } else if (hp < 128) {
            v = (hp < 64) ? 2 : 1;
        } else {
            v = 0;
        }
    }
    return base + v;
}

// N64 0x8013FE48: the end of the get-up: when the wrestler may act (or is on the floor), B held
// becomes the strike 0x22 at step 1 and A held the grapple attempt 0x50 at step 1 (the get-up
// attack); else the settle 0x30.
void get_up_finish(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a &= 0xFF7F;
    anim_end_apply(w);
    if (wrestler_may_act(w) || (w->location & 4)) {
        if (ctx->buttons & 0x4000) {
            wrestler_set_state_now(w, state::Strike);
            w->state_step = 1;
            return;
        }
        if (ctx->buttons & 0x8000) {
            wrestler_set_state_now(w, state::GrappleAttempt);
            w->state_step = 1;
            return;
        }
    }
    wrestler_set_state_now(w, state::SettleAfterAnimation);
}

// N64 0x800E7FC0: lying down (state 0x31). Step 0: counts the knockdown, sets the time down
// (escape_time op 0; 15 after a lost fall without 0x02), flags8c 0x200 turns into a stay-down
// (0x100, state_counter 0x02), resets the mash counter, and unless staying, interrupted, fresh
// (flags_f1 0x01), slouched, or flags8c 0x800, with down_count_allowed and a roll of kind 7
// toggles the stay-down choice (flags8c 0x100, state_counter 0x01). Step 1: flags80 0x09, the
// lying animation of the end pose (46 / 47 by the side when not lying), end_flags 0x02 after 0x01.
// Step 2: a stay-down (state_counter 0x01) goes to step 4; else the long lying animation (12877 /
// 12878, mode 6) and under rule 0x400 on the floor the elimination (end_flags 0x10, flags80 0x01).
// Step 3: bounds; with the match flag 0x40 (the end) end_flags 0x04 stands up at once, else the
// time counts down (recover_tick) and stands (0x4F) unless the stick is pushed or someone is
// approaching on the ground (then the time is reset to 0), or rolls out (153) after a lost fall in
// the ring; the count pauses while the opponent swings a weapon. Steps 4-8: the dazed-turn
// check, the slumped pose 12162 / 12163, bounds and move_event.
void state_31_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s16 downs = w->down_count;
        w->hits_taken = 0;
        w->run_timer = 0;
        w->run_counter = -1;
        w->down_count = (s16)(downs + 1);
        w->escape = (s16)escape_time(w, ctx->rec_a, 0);
        if ((w->end_flags & 0x79) && !(w->end_flags & 2))
            w->escape = 15;
        if (w->flags8c & 0x200) {
            w->flags8c = (w->flags8c & 0xFCFF) | 0x100;
            w->state_counter |= 2;
        }
        mash_count(w, 0, 0);
        if (!(w->state_counter & 2) && !match_can_interrupt() && !(w->flags_f1 & 1) && !(w->flags80 & 0x10) &&
            !(w->flags8c & 0x800) && down_count_allowed(w) && chance_roll(w, nullptr, 7)) {
            if (w->flags8c & 0x100) {
                w->flags8c &= 0xFEFF;
            } else {
                w->flags8c |= 0x100;
                w->state_counter |= 1;
            }
        }
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    }
    case 1: {
        s32 lying = (u32)(w->end_pose - 20) < 28;
        w->flags80 |= 9;
        if (!lying)
            w->end_pose = (w->flags8a & 1) ? 47 : 46;
        wrestler_set_anim(w, mem_s16(A_LYING_ANIMS + 2u * (u32)(s8)w->end_pose), 1);
        if (w->end_flags & 1)
            w->end_flags |= 2;
        w->state_step = w->state_step + 1;
        return;
    }
    case 2: {
        s32 stay = w->state_counter & 1;
        w->flags80 |= 8;
        if (stay) {
            w->state_step = 4;
            return;
        }
        if ((u32)(w->end_pose - 46) < 2)
            wrestler_set_anim(w, (s8)w->end_pose == 46 ? 12877 : 12878, 6);
        if ((g_match_rules & 0x400) && (w->location & 4) && !(w->end_flags & 1)) {
            w->flags80 |= 1;
            w->end_flags |= 0x10;
            return;
        }
        w->state_step = w->state_step + 1;
        return;
    }
    case 3: {
        w->flags80 |= 8;
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (g_match_flags[1] & 0x40) {
            if (w->end_flags & 4)
                wrestler_set_state_now(w, state::GetUp);
            if (!(w->end_flags & 1) && recover_tick(w, ctx->pressed)) {
                if (ctx->in_byte) {
                    w->escape = 0;
                    return;
                }
                if (down_count_others(w)) {
                    w->escape = 0;
                    return;
                }
                wrestler_set_state_now(w, state::GetUp);
            }
            return;
        }
        if (ctx->rec_a->state == state::Taunt && (ctx->rec_a->state_counter & 0x10))
            return;
        if (w->state_counter & 2) {
            if (!(ctx->rec_a->flags80 & 0x20))
                w->state_counter &= 0xFFFD;
            return;
        }
        if (!recover_tick(w, ctx->pressed))
            return;
        if (ctx->in_byte && !(w->flags_f1 & 1)) {
            w->escape = 0;
            return;
        }
        if (down_count_others(w)) {
            w->escape = 0;
            return;
        }
        if ((w->end_flags & 1) && (w->location & 1)) {
            wrestler_set_state_now(w, state::RollToRingSide);
            return;
        }
        wrestler_set_state_now(w, state::GetUp);
        return;
    }
    case 4:
        anim_is_dazed_like(w);
        w->state_step = w->state_step + 1;
        return;
    case 5:
        wrestler_set_anim(w, 0x2F82 | (w->flags8a & 1), 6);
        w->state_step = w->state_step + 1;
        return;
    case 6:
        keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
        move_event(ctx, 512);
        return;
    case 7: {
        s32 anim = 12108;
        if (!(w->flags8a & 1))
            anim = 0x2F4A | ((mem_u32(ctx->rec_a->move + 0x20) & 0x10) ? 0 : 1);
        wrestler_set_anim(w, anim, 6);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    }
    case 8:
        keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
        move_event(ctx, 129);
        return;
    default:
        return;
    }
}

// N64 0x8013FBB8: getting up (state 0x4F). Step 0 starts the get-up animation (mode 6) and a
// 60-frame crouch allowance; step 1 slides (8) with flags8a 0x6000 until the animation ends, then
// R held (and no pause) crouches (12171, step 2) else step 3. Step 2 (the crouch): the opponent
// switch, turning toward the opponent (113 a frame), the stick goes to the weapon state 27 step 2,
// L (not flags_e9 0x01) the backward roll 12172 (step 5); while R is held the allowance counts
// down (past it, step 3), A starts the grapple attempt 0x50 at step 7, B the get-up strike 0x25;
// releasing R goes to step 3. Step 3: standing up (12157) turning toward the opponent over the
// animation's frames; step 4 turns by that; steps 4-5 slide (4) and at the end get_up_finish.
void state_4F_get_up(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, get_up_anim(w), 6);
        w->count_timer = 60;
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1:
        wrestler_slide(w, 8.0f);
        w->flags8a |= 0x6000;
        if (w->anim_frames_left > 0)
            break;
        anim_end_apply(w);
        if (!match_can_interrupt() && (ctx->buttons & 0x10)) {
            wrestler_set_anim(w, 12171, 1);
            w->state_step = w->state_step + 1;
            break;
        }
        wrestler_set_step(w, 3);
        break;
    case 2: {
        opponent_switch_nearest(w, ctx->pressed);
        wrestler_turn_toward(w, w->facing_to_opponent, 113, 512);
        if (ctx->in_byte) {
            wrestler_set_state_now(w, state::Taunt);
            w->state_step = 2;
            return;
        }
        if ((ctx->buttons & 0x20) && !(w->flags_e9 & 1)) {
            wrestler_set_anim(w, 12172, 1);
            w->state_step = 5;
            return;
        }
        s16 left = (s16)(w->count_timer - 1);
        w->count_timer = (u16)left;
        if (ctx->buttons & 0x10) {
            if (left < 0) {
                w->state_step = w->state_step + 1;
                return;
            }
            if (ctx->buttons & 0x8000) {
                wrestler_set_state_now(w, state::GrappleAttempt);
                w->state_step = 7;
                return;
            }
            if (ctx->buttons & 0x4000) {
                wrestler_set_state_now(w, state::GetUpAttack);
                return;
            }
        } else {
            w->state_step = w->state_step + 1;
        }
        break;
    }
    case 3:
        wrestler_set_anim(w, 12157, 1);
        w->turn_step = angle_diff(w->facing_to_opponent, w->facing) / w->anim_player[AP_FRAMES];
        w->state_step = w->state_step + 1;
        break;
    case 4:
        w->facing = angle_add(w->facing, w->turn_step);
        [[fallthrough]];
    case 5:
        wrestler_slide(w, 4.0f);
        if (w->anim_frames_left > 0)
            break;
        get_up_finish(ctx);
        break;
    default:
        break;
    }
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
}

// N64 0x80130898: the frame of the animation's event of kind `kind` (its file 703 record's eight
// (frame, kind) pairs), 0 for none.
s32 anim_event_frame(s16 anim, s32 kind) {
    u32 ev = mem_u32(0x8006A224) + 36 * (u32)(anim - 11737) + 20;
    for (s32 i = 0; i < 8; i++, ev += 2) {
        u8 frame = mem_u8(ev);
        if (frame == 0)
            continue;
        if (mem_u8(ev + 1) == kind)
            return frame;
    }
    return 0;
}

// N64 0x800EB7DC: wrestler_pair_set_state, both run again this frame (flags88 0x01).
void wrestler_pair_set_state_now(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b) {
    wrestler_pair_set_state(w, state_a, other, state_b);
    w->flags88 |= 1;
    other->flags88 |= 1;
}

// N64 0x80113474: holding a pin (states 0x44 and 0x4A). Step 0 starts the cover: the pin move 292
// plus the move's pin variant (0x800A5720 by slot and move) with its style animation (state 68:
// the animation + 2); step 1 holds it (flags80 0x140): after the first frame (state_flags 0x80) a
// lost fall (the defender's end_flags 0x01 without 0x02) ends the count (state_flags 0x08, the
// defender's cleared), R pressed or a pause lets go (0x01; the defender gets 0x40); when the
// animation ends or the count is over (bits 0x13) the end animation: + 6 kicked out (0x10), + 4
// the count done (0x08), + 2 let go, mode 6 (state_flags 0x20, step 2). Step 2: sfx 1 at the
// event-2 frame, the finisher effects, then unpaired into the end pose (kicked out) or 0x30. Step
// 3 (the roll-up from 0x4A): follows the defender's joint 0, then faces back for a move with record
// flag 0x100000, picks the side (dir 3 / 1) and plays 12298 / 12268 into step 1.
void state_44_pin_attacker(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *d = ctx->rec_a;
    state_bounds(w, d, nullptr, 0);
    s32 step = w->state_step;
    if (step == 0) {
        if (w->state == state::PinAttacker) {
            wrestler_set_anim(w, w->anim_id + 2, 0);
        } else {
            s32 slot = (s8)w->move_slot;
            wrestler_start_move(w, slot, mem_u8(A_PIN_VARIANTS + 548u * (u32)slot + (u32)(u16)w->move_num) + 292);
            wrestler_set_anim(w, anim_style(w, 0), 1);
        }
        w->state_step = w->state_step + 1;
        step = 1;
    }
    if (step == 1) {
        w->flags80 |= 0x140;
        if (w->state_flags & 0x80) {
            u16 ef = d->end_flags;
            if ((ef & 1) && !(ef & 2)) {
                w->state_flags = 8;
                d->state_flags = 0;
            }
            if (!(w->state_flags & 0x1F) && ((ctx->pressed & 0x10) || match_can_interrupt())) {
                w->state_flags |= 1;
                d->state_flags |= 0x40;
            }
        }
        w->state_flags |= 0x80;
        if (w->anim_frames_left <= 0 || (w->state_flags & 0x13)) {
            if (w->state_flags & 0x1F) {
                w->state_flags |= 0x20;
                s32 v;
                if (w->state_flags & 0x10)
                    v = 6;
                else
                    v = (w->state_flags & 8) ? 4 : 2;
                s32 anim = (w->state == state::PinAttacker) ? v + w->anim_id : anim_style(w, v);
                wrestler_set_anim(w, anim, 6);
                w->state_step = w->state_step + 1;
            }
        }
        return;
    }
    if (step == 2) {
        if (w->anim_frame == anim_event_frame(w->anim_id, 2))
            host_sfx(1);
        finisher_effects(w);
        if (w->anim_frames_left <= 0) {
            anim_end_apply(w);
            wrestler_unpair(w);
            if (w->state_flags & 0x10)
                wrestler_settle_pose(w);
            else
                wrestler_set_state_now(w, state::SettleAfterAnimation);
        }
        return;
    }
    if (step == 3) {
        Vec3 dlt;
        vec_sub(&w->joints[0], &d->joints[0], &dlt);
        wrestler_translate(w, &dlt);
        finisher_effects(w);
        if (w->anim_frames_left <= 0) {
            d->move_link = (s16)mem_u16(0x8008FA70 + 1096u * (u32)(s8)w->move_slot + 2u * (u32)(u16)w->move_num);
            anim_end_apply(w);
            if (mem_u32(w->move + 0x20) & 0x100000)
                w->facing = angle_add(w->facing, 2048);
            w->dir = (angle_diff(d->facing, w->facing) >= 0) ? 3 : 1;
            wrestler_set_anim(w, w->dir == 1 ? 12268 : 12298, 1);
            wrestler_set_step(w, 1);
        }
    }
}

// N64 0x80113860: being pinned or held (states 0x45 and 0x4B; 69 and 75 the hold forms). Step 0:
// flags80 0x180, the attacker's style animation + 1 (in 69 its animation + 1 and move_link -1
// unless state_counter 0x01), the hold's damage clock cleared, the escape time (escape_time op 1,
// at least 15), pin_count + 1, flags8a 0x400 under rule 0x20 for a legal pin, the mash counter
// reset. Step 1: mashing (mash_count: one, two or three frames off a press) and the count down;
// past it (or the stick pushed: at once) the kick-out: the attacker's state_flags 0x02, the
// generic move 19 copied in with its spirit exchange; in a hold (75) damage every 30 frames; when
// the pair's bounds hit a rope under rule 0x02 in the ring the rope break (the attacker's
// state_flags 0x04, or 0x100 under match type 0x80000, with the HUD message); the attacker's end
// bits (0x20) start the end animation (+ 7 kicked out, + 5 counted, + 3 let go; mode 6). Step 2:
// the finisher effects, then under rule 0x80000 straight to 0x30 (end_flags 0x03 cleared), else
// the end pose when counted, on a raised place, or after a move with record flag 0x04 or let go
// (state_flags 0x40), else 0x30 with flags8a 0x10. Step 3 (the roll-up): 12901, damage from the
// roller (rec_c; x 0.5 with its flags_ea 0x80). Step 4: turns a quarter from the attacker's
// facing, then restarts at step 0 with state_counter 0x01.
void state_45_pin_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    u16 bounds = state_bounds(w, a, nullptr, 0);
    switch (w->state_step) {
    case 0: {
        w->flags80 |= 0x180;
        s32 anim;
        if (w->state == state::PinDefender) {
            anim = a->anim_id + 1;
            if (!(w->state_counter & 1))
                w->move_link = -1;
        } else {
            anim = anim_style(a, 1);
        }
        wrestler_set_anim(w, anim, 1);
        mem_w32(A_PIN_HOLD_TICK + 4u * (u32)ctx->slot, 0);
        w->escape = (s16)escape_time(w, a, 1);
        w->pin_count = (s16)(w->pin_count + 1);
        if ((g_match_rules & 0x20) && ((g_match_rules & 0x4000) || (w->location & 1)) &&
            (w->flags_e9 & a->flags_e9 & 0x10))
            w->flags8a |= 0x400;
        if (w->escape < 15)
            w->escape = 15;
        mash_count(w, 0, 0);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    }
    case 1: {
        w->flags80 |= 0x180;
        s32 m = mash_count(w, ctx->pressed, 1);
        if (m == 1)
            w->escape = (s16)(w->escape - 1);
        if (m == 3)
            w->escape = (s16)(w->escape - 2);
        if (m == 4)
            w->escape = (s16)(w->escape - 3);
        w->escape = (s16)(w->escape - 1);
        if (w->escape < 0) {
            if (ctx->in_byte)
                w->escape = 0;
            if (w->escape < 0 && !(a->state_flags & 0x1F)) {
                a->state_flags |= 2;
                wrestler_set_generic_move(w, 19);
                hit_move_copy(w, w);
                spirit_exchange(a, w, w);
            }
        }
        if (w->state == state::PinFromMoveDefender) {
            u32 tick = A_PIN_HOLD_TICK + 4u * (u32)ctx->slot;
            mem_w32(tick, (u32)(mem_s32(tick) + 1));
            if (mem_s32(tick) > 30) {
                hit_move_copy(a, w);
                apply_damage(a, w);
                mem_w32(tick, 0);
            }
        }
        if (bounds && !(a->state_flags & 0x1F)) {
            if ((g_match_rules & 2) && (w->location & 1)) {
                if (g_match_type & 0x80000) {
                    a->state_flags |= 0x100;
                } else {
                    a->state_flags |= 4;
                    if (w->flags_e9 & a->flags_e9 & 0x10)
                        host_hud("submission_message", 0, 0);
                }
            }
        }
        if (a->state_flags & 0x1F)
            w->flags8a &= 0xFBFF;
        u16 sf = a->state_flags;
        if (sf & 0x20) {
            s32 anim;
            if (w->state == state::PinDefender) {
                anim = a->anim_id + 1;
            } else {
                s32 v;
                if (sf & 0x10)
                    v = 7;
                else
                    v = (sf & 8) ? 5 : 3;
                anim = anim_style(a, v);
            }
            wrestler_set_anim(w, anim, 6);
            w->state_step = w->state_step + 1;
        }
        return;
    }
    case 2:
        finisher_effects(w);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (g_match_rules & 0x80000) {
            w->flags8a |= 0x10;
            wrestler_set_state_now(w, state::SettleAfterAnimation);
            w->end_flags &= 0xFFFC;
            return;
        }
        if (w->state == state::PinDefender) {
            if (w->end_flags & 1) {
                wrestler_settle_pose(w);
                return;
            }
        } else if ((w->location & 0x3C4) || (mem_u32(a->move + 0x20) & 4) || (w->state_flags & 0x40) ||
                   (w->end_flags & 1)) {
            wrestler_settle_pose(w);
            return;
        }
        w->flags8a |= 0x10;
        wrestler_set_state_now(w, state::SettleAfterAnimation);
        return;
    case 3:
        w->flags80 |= 9;
        wrestler_set_anim(w, 12901, 1);
        hit_move_copy(ctx->rec_c, w);
        if (ctx->rec_c->flags_ea & 0x80)
            w->hit_move.damage = (u8)(s32)((f64)(s8)w->hit_move.damage * mem_f64(0x80150A50));
        apply_damage(ctx->rec_c, w);
        w->state_step = w->state_step + 1;
        return;
    case 4: {
        w->flags80 |= 9;
        s32 d = angle_diff(w->facing, a->facing);
        s32 target = angle_add(a->facing, d < 0 ? -1024 : 1024);
        wrestler_turn_toward(w, target, 22, 0);
        if (w->anim_frames_left <= 0 && a->state_step != 3) {
            wrestler_set_step(w, 0);
            w->state_counter |= 1;
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x8013F9DC: the attacker's get-up after a grapple move (state 0x4E): the get-up animation
// (mode 6); at its last frame the crouch 12171 and step 3, a one-frame window: a pause stands at
// once; the stir / pick-up of the opponent (input_stir, flags8a 0x08); the stick goes to the
// weapon state 27 step 2; C-left the referee's call (input_c_left); else standing up (12157,
// turning toward the opponent over the animation) and step 4 (move_event). The slide (4) and
// bounds (2) except on the frame the crouch starts.
void state_4E(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, get_up_anim(w), 6);
        w->state_step = w->state_step + 1;
        break;
    case 1:
        if (w->anim_frames_left != 1)
            break;
        anim_end_apply(w);
        wrestler_set_anim(w, 12171, 1);
        w->state_step = 3;
        break;
    case 2:
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 3:
        if (!match_can_interrupt()) {
            if (input_stir(w, ctx->rec_a, ctx->buttons)) {
                w->flags8a |= 8;
                return;
            }
            if (ctx->in_byte) {
                wrestler_set_state_now(w, state::Taunt);
                w->state_step = 2;
                return;
            }
            if (input_c_left(ctx))
                return;
        }
        wrestler_set_anim(w, 12157, 1);
        w->turn_step = angle_diff(w->facing_to_opponent, w->facing) / w->anim_player[AP_FRAMES];
        w->state_step = w->state_step + 1;
        break;
    case 4:
        w->facing = angle_add(w->facing, w->turn_step);
        move_event(ctx, 0);
        break;
    default:
        break;
    }
    wrestler_slide(w, 4.0f);
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
}

// N64 0x800EA3CC: the L evade (state 0x6B): 11878 (11877 with trait bit 0x01), mode 3, count_timer
// from 0 counting the frames (grapple_attempt_resolve deflects an attempt within 4), bounds (2),
// move_event.
void state_6B_grapple_evade(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 2.0f, 0, 2);
    if (w->state_step == 0) {
        wrestler_set_anim(w, (mem_u32(w->attrs + 0x34) & 1) ? 11877 : 11878, 3);
        w->count_timer = 0;
        w->state_step = w->state_step + 1;
        return;
    }
    if (w->state_step == 1) {
        w->count_timer = (u16)(w->count_timer + 1);
        move_event(ctx, 0);
    }
}

// ---- the cover from standing (L over a lying opponent: 0x42 / 0x43) ----

enum : u32 {
    T_COVER_OFFSETS = 0x80146330,   // f32[4][2]: (x, z) beside the lying opponent per side (cover_side_pick)
    T_DIR_ANGLES = 0x8014D1D8,      // s32[4]: the facing per side (Wrestler.dir)
    T_APPROACH_SPEED = 0x8014DBE0,  // f32[]: the walk's speed factor per 113 units of turn
};

// N64 0x800EBCA8: which side of a lying wrestler `angle` (the approacher's facing to it) comes
// from: 0 head-on (within 512 of its facing), 2 from behind (1537 or more off), else 3 / 1 by the
// sign; the sides swap by two for a wrestler lying face up (flags8a 0x01).
s32 cover_side_pick(const Wrestler *rec, s32 angle) {
    s32 d = angle_diff(rec->facing, angle);
    s32 side;
    if (abs_i(d) < 1537) {
        if (abs_i(d) < 512)
            side = 0;
        else
            side = (((~d) >> 31) & 3) | 1;
    } else {
        side = 2;
    }
    if (rec->flags8a & 1)
        side = (side + 2) & 3;
    return side;
}

// N64 0x8012CA34: keep_in_bounds for a wrestler walking an approach curve: pts3 are its three
// control points (moved with it).
u16 keep_in_bounds_pair_c(Wrestler *w, Wrestler *other, f32 scale, Vec3 *pts3, u32 joints) {
    return keep_in_bounds(w, other, nullptr, scale, nullptr, pts3, nullptr, 0, joints, 1);
}

// N64 0x80132374: the walk to a spot beside a lying opponent as a quadratic curve. Planning
// (`advance` 0): with the opponent at O, the walker at W and the spot at T, the middle control
// point B lies at the mean of |OW| and |OT| from O along the angle half-way between them; the first
// A at |BW| beyond W along the B -> W direction, the last C at |BT| beyond T along the B -> T
// direction (all at the walker's height). The step per frame is 1 / ((|BW| + |BT|) * a factor
// by how far the walker must turn (0x8014DBE0, per 113) / 6); returns the number of frames.
// Advancing (`advance` 1): moves the walker to (1-t)^2/2 * A + ((1-t) t + 1/2) * B + t^2/2 * C
// and steps t; returns 1 once t has reached 1.
s32 ground_approach_plan(WrestlerCtx *ctx, const Vec3 *target, s32 advance) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    Vec3 *p = w->approach_pts;
    if (advance) {
        if (1.0 <= (f64)w->approach_t)
            w->approach_t = 1.0f;
        f64 t = (f64)w->approach_t;
        f32 b0 = (f32)((1.0 - t) * (1.0 - t) * 0.5);
        f32 b1 = (f32)((1.0 - t) * t + 0.5);
        f32 b2 = (f32)((f64)(w->approach_t * w->approach_t) * 0.5);
        w->pos.x = b0 * p[0].x + b1 * p[1].x + b2 * p[2].x;
        w->pos.z = b0 * p[0].z + b1 * p[1].z + b2 * p[2].z;
        if (1.0 <= (f64)w->approach_t)
            return 1;
        w->approach_t = w->approach_t + w->approach_dt;
        return 0;
    }
    f32 out[9];
    Vec3 off = {0.0f, 0.0f, 0.0f};
    f32 len = segment_distance(&o->pos, &w->pos, out);
    s32 ang_w = (s32)out[7];
    segment_distance(&o->pos, target, out);
    off.z = (f32)((f64)(len + out[4]) * 0.5);
    s32 ang_t = (s32)out[7];
    s32 half = angle_diff(ang_w, ang_t) / 2;
    point_at_offset(&o->pos, &off, angle_add(ang_t, half), &p[1]);
    p[1].y = w->pos.y;
    f32 total = segment_distance(&p[1], &w->pos, out);
    s32 back = angle_add((s32)out[7], 2048);
    off.z = out[4];
    point_at_offset(&w->pos, &off, (s32)out[7], &p[0]);
    p[0].y = w->pos.y;
    segment_distance(&p[1], target, out);
    total = total + out[4];
    s32 turn = abs_i(angle_diff(back, (s32)out[7])) / 113;
    off.z = out[4];
    point_at_offset(target, &off, (s32)out[7], &p[2]);
    p[2].y = w->pos.y;
    f32 v = (f32)((f64)(total * mem_f32(T_APPROACH_SPEED + 4u * (u32)turn)) * (1.0 / 6.0));
    f64 step = 1.0 / (f64)v;
    w->approach_t = 0.0f;
    s32 frames = (s32)((f64)v + 1.0);
    w->approach_dt = (f32)step;
    return (s16)frames;
}

// N64 0x80112FE0: the cover from standing (L over a lying opponent; paired with 0x43). Step 0:
// the walk animation (anim_pick 0, or 12173 under flags8a 0x08) with flags80 0x01 and flags8a
// 0x100. Step 1: picks the side (cover_side_pick -> dir), the spot beside the opponent (the
// (x, z) pair at 0x80146330 per side, negated for one lying face up, turned by its facing) and the
// facing to turn to (0x8014D1D8 per side plus the opponent's facing, + 2048 face up), and plans
// the walk (ground_approach_plan). Step 2: walks the curve and turns (113 per frame); when both
// are done, 12168 (speed 2) unless flags8a 0x08. Step 3: at the animation's end, a face-up
// opponent gets the cover 12358 + 2 * dir and state_flags 0x01, else step 5. Step 4: waits for the
// end, then wrestler_set_step -1. Step 5: state_flags 0x02 on the opponent, 12256 + 10 * dir (a
// turn-over). Step 6: bounds and finisher_effects; on the animation's last frame state_flags 0x04
// on the opponent (which then pairs both into 0x44 / 0x45).
void state_42_cover_from_standing(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        if (w->flags8a & 8) {
            wrestler_set_anim(w, 12173, 3);
        } else {
            w->flags80 |= 1;
            w->flags8a |= 0x100;
            wrestler_set_anim(w, anim_pick(ctx, 0, 0), 3);
        }
        w->state_step = w->state_step + 1;
        return;
    case 1: {
        w->dir = (s16)cover_side_pick(o, w->facing_to_opponent);
        Vec3 off;
        off.x = mem_f32(T_COVER_OFFSETS + 8u * (u32)w->dir);
        off.y = 0.0f;
        off.z = mem_f32(T_COVER_OFFSETS + 8u * (u32)w->dir + 4);
        w->lockup_facing = angle_add(mem_s32(T_DIR_ANGLES + 4u * (u32)w->dir), o->facing);
        if (o->flags8a & 1) {
            off.x = -off.x;
            off.z = -off.z;
            w->lockup_facing = angle_add(w->lockup_facing, 2048);
        }
        Vec3 target;
        point_at_offset(&o->pos, &off, o->facing, &target);
        target.y = w->pos.y;
        ground_approach_plan(ctx, &target, 0);
        w->state_step = w->state_step + 1;
    }
        [[fallthrough]];
    case 2: {
        w->flags80 |= 1;
        if (!(w->flags8a & 8))
            w->flags8a |= 0x100;
        keep_in_bounds_pair_c(w, o, 4.0f, w->approach_pts, 0xFFFFE);
        s32 done = ground_approach_plan(ctx, nullptr, 1);
        if (done + wrestler_turn_toward(w, w->lockup_facing, 113, 0) != 2)
            return;
        if (!(w->flags8a & 8))
            wrestler_set_anim(w, 12168, 2);
        w->state_step = w->state_step + 1;
        return;
    }
    case 3:
        if (w->anim_frames_left > 0 && !(w->flags8a & 8))
            return;
        if (o->flags8a & 1) {
            wrestler_set_anim(w, 2 * w->dir + 12358, 0);
            o->state_flags |= 1;
            w->state_step = w->state_step + 1;
        } else {
            wrestler_set_step(w, 5);
        }
        return;
    case 4:
        if (w->anim_frames_left <= 0)
            wrestler_set_step(w, -1);
        return;
    case 5:
        o->state_flags |= 2;
        wrestler_set_anim(w, 10 * w->dir + 12256, 0);
        w->state_step = w->state_step + 1;
        return;
    case 6:
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        finisher_effects(w);
        if (w->anim_frames_left == 1)
            o->state_flags |= 4;
        return;
    default:
        return;
    }
}

// N64 0x80113308: the lying wrestler being covered from standing (paired with 0x42); flags80 0x09
// every frame. Step 0: anim_is_dazed_like. Step 1: state_flags 0x02 (turned over) -> step 3;
// 0x01 (covered) plays the attacker's animation + 1 (speed 6). Step 2: at its end pose_is_lying,
// anim_end_apply and wrestler_set_step -1. Step 3: once turned over (0x02) plays the attacker's
// animation + 1 (speed 1 when also covered, else 6). Step 4: bounds, finisher_effects; state_flags
// 0x04 pairs the attacker into 0x44 and itself into 0x45 (the pin).
void state_43_pin_start(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    w->flags80 |= 9;
    switch (w->state_step) {
    case 0:
        anim_is_dazed_like(w);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1:
        if (w->state_flags & 2) {
            wrestler_set_step(w, 3);
            return;
        }
        if (!(w->state_flags & 1))
            return;
        wrestler_set_anim(w, o->anim_id + 1, 6);
        w->state_step = w->state_step + 1;
        return;
    case 2:
        if (w->anim_frames_left > 0)
            return;
        pose_is_lying(w);
        anim_end_apply(w);
        wrestler_set_step(w, -1);
        return;
    case 3:
        if (!(w->state_flags & 2))
            return;
        wrestler_set_anim(w, o->anim_id + 1, (w->state_flags & 1) ? 1 : 6);
        w->state_step = w->state_step + 1;
        return;
    case 4:
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        finisher_effects(w);
        if (w->state_flags & 4)
            wrestler_pair_set_state_move(o, state::PinAttacker, w, state::PinDefender);
        return;
    default:
        return;
    }
}
// N64 0x800E97B8: state 0x63, hit while lying (a weapon ground attack, a dive): flags80 0x09 every
// frame. Step 0: the damage (apply_damage by rec_c) adds to down_hit_count and down_hit_kind. Step 1:
// a ground attack (flags80 0x10) from an attacker in 0x61 with fewer than 5 hits so far rolls the
// chance for the hit count's band (0x80145048: s16 threshold, s16 chance) to stay hit (state_counter
// 0x01). Unless staying the hit count and kind are cleared. The animation: a ground
// attack 12204 / 12218 staying (12229 / 12243 face down), +1 hit from behind; else by finisher heat
// (below 11, 11-30, above) 12901-12903 (12904-12906 face down). Step 2: kept in bounds; when it ends,
// down again (0x31 at step 1): unless staying or the aftermath took over, the escape time anew with
// flags8c 0x4000, else 15 less.
void state_63_hit_while_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags80 |= 9;
    switch (w->state_step) {
    case 0: {
        s32 d = apply_damage(ctx->rec_c, w);
        w->down_hit_count = (u16)(w->down_hit_count + d);
        w->down_hit_kind = (u8)(w->down_hit_kind + 1);
        anim_is_dazed_like(w);
        w->state_step++;
        return;
    }
    case 1: {
        if ((w->flags80 & 0x10) && ctx->rec_a->state == state::BareHandedGroundAttack && (s8)w->down_hit_kind < 5) {
            s32 i = 0;
            while (!(mem_s16(0x80145048 + 4u * (u32)i) >= (s16)w->down_hit_count))
                i++;
            if (chance_check(mem_s16(0x8014504A + 4u * (u32)i)))
                w->state_counter |= 1;
        }
        if (!(w->state_counter & 1)) {
            w->down_hit_count = 0;
            w->down_hit_kind = 0;
        }
        s32 anim;
        if (w->flags80 & 0x10) {
            if (w->state_counter & 1)
                anim = (w->flags8a & 1) ? 12243 : 12218;
            else
                anim = (w->flags8a & 1) ? 12229 : 12204;
            anim += abs_i(angle_diff(w->facing, w->facing_to_opponent)) >= 1025 ? 1 : 0;
        } else {
            s32 h = w->finisher_heat;
            s32 k = h >= 31 ? 2 : (h >= 11 ? 1 : 0);
            anim = k + ((w->flags8a & 1) ? 12904 : 12901);
        }
        wrestler_set_anim(w, anim, 6);
        pose_is_lying(w);
        w->state_step++;
        return;
    }
    case 2:
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        wrestler_set_state_now(w, state::LyingDown);
        w->state_step = 1;
        if (w->state_counter & 1)
            return;
        if (move_aftermath_check(ctx->rec_c, ctx->rec_c, w, 1))
            return;
        if (w->flags8c & 0x4000)
            w->escape = (s16)escape_time(w, ctx->rec_a, 0);
        else
            w->escape = (s16)(w->escape - 15);
        return;
    default:
        return;
    }
}

// N64 0x800E6638: state 0x17, draped over the ropes. Step 0 the slide onto them (pair_scripted_place)
// with 12147, step 1 until it ends, then step 2. Step 2 the escape time anew (kind 9), fall_chance
// and the mash count cleared; step 3 the hang (12148, fall_chance + 1); step 4 (frame_flags 0x10)
// unless the match has ended (match flag 0x40) with end_flags 0x04: the recovery (recover_tick by the
// presses) to step 5, or, with the stay-down input held (in_byte), the escape time zeroed. Step 5
// getting off (12149) with the events. Step 6 hit while there: the damage (from rec_a) and 12150, then
// step 7 until it ends, back to step 3.
void state_17_on_ropes(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        pair_scripted_place(w, nullptr);
        wrestler_set_anim(w, 12147, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        pair_slide_step(ctx->self, nullptr);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_step(w, 2);
        return;
    case 2:
        w->escape = (s16)escape_time(w, nullptr, 9);
        w->fall_chance = 0;
        mash_count(w, 0, 0);
        w->state_step++;
        [[fallthrough]];
    case 3:
        wrestler_set_anim(w, 12148, 3);
        w->fall_chance = (u8)(w->fall_chance + 1);
        w->state_step++;
        [[fallthrough]];
    case 4:
        w->frame_flags |= 0x10;
        if ((g_match_flags[1] & 0x40) && (w->end_flags & 4)) {
            w->state_step++;
            return;
        }
        if (!recover_tick(w, ctx->pressed))
            return;
        if (ctx->in_byte) {
            w->escape = 0;
            return;
        }
        w->state_step++;
        return;
    case 5:
        wrestler_set_anim(w, 12149, 3);
        move_event(ctx, 0);
        return;
    case 6:
        apply_damage(ctx->rec_a, w);
        wrestler_set_anim(w, 12150, 2);
        w->state_step++;
        return;
    case 7:
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_step(w, 3);
        return;
    default:
        return;
    }
}



// N64 0x8011E2F0: state 0x70, knocked over the top rope. Step 0: a cage escape (state_counter
// 0x01) 12151 (12152 in the cage match); off the apron (location now 0x04, facing the side's angle,
// on the apron line) 12911 with hit_flags 0x1000, 12908 with 0x400 or on side 0 within 140 of the
// middle, else 12909; from the ring, after the slide to the ropes (pair_scripted_place), 20% out to
// the floor (0x04: 12143 / 12144 from behind or in front, hit_flags 0x2000000) or else onto the apron
// (0x02: 12145 / 12146). Step 1: the fall off the apron (12908-12911) slides (8.0, flags8a 0x6000) from
// frame 22; the fall over the ropes (12143-12146) the slide with bounds by the side (768 / 3072,
// location 0x01 for that while). When it ends: on the apron the damage as generic move 18 and the
// stun (0x30, count_timer the escape time, the mash count reset); on the floor generic move 23 / 24 /
// 17 (12908 / 12909 / else) and the damage, then lying (12162 / 12163 face down, step 2) on the floor
// below 25 health when a count may run, else wrestler_settle_pose. Step 2: the slide (8.0), bounds,
// and at the end of the events (0x200) the escape time and mash count.
void state_70_over_ropes(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 anim;
        if (w->state_counter & 1) {
            anim = (g_match_type & 0x10000) ? 12152 : 12151;
        } else if (w->location & 2) {
            w->location = (u16)((w->location & 0xF800) | 4);
            w->facing = mem_s32(0x8014D1E8 + 4u * (u32)w->ring_zone);
            apron_line_pos(w, &w->pos);
            u32 h = w->hit_flags;
            if (h & 0x1000)
                anim = 12911;
            else if (h & 0x400)
                anim = 12908;
            else if (w->ring_zone != 0)
                anim = 12909;
            else if (-140.0 < (f64)w->pos.x && (f64)w->pos.x < 140.0)
                anim = 12908;
            else
                anim = 12909;
        } else {
            pair_scripted_place(w, nullptr);
            bool back = (w->hit_flags & 0x2000000) != 0;
            if (chance_check(200)) {
                w->location = (u16)((w->location & 0xF800) | 4);
                anim = back ? 12143 : 12144;
            } else {
                w->location = (u16)((w->location & 0xF800) | 2);
                anim = back ? 12145 : 12146;
            }
        }
        wrestler_set_anim(w, anim, 2);
        w->state_step++;
        return;
    }
    case 1: {
        s32 a = w->anim_id;
        if (a == 12911 || a == 12908 || a == 12909) {
            if (w->anim_frame >= 22) {
                w->flags8a |= 0x6000;
                wrestler_slide(w, 8.0f);
                keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
            }
        } else if (a >= 12143 && a < 12147) {
            pair_slide_step(w, nullptr);
            u32 mask = (w->dir & 1) ? 3072 : 768;
            w->location |= 1;
            keep_in_bounds_self(w, 2.0f, mask, 0xFFFFE);
            w->location &= 0xFFFE;
        }
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (w->location & 2) {
            wrestler_set_generic_move(w, 18);
            hit_move_copy(w, w);
            apply_damage(ctx->rec_a, w);
            wrestler_set_state_now(w, state::SettleAfterAnimation);
            w->count_timer = (u16)escape_time(w, ctx->rec_a, 0);
            mash_count(w, 0, 0);
            return;
        }
        a = w->anim_id;
        wrestler_set_generic_move(w, a == 12908 ? 23 : (a == 12909 ? 24 : 17));
        hit_move_copy(w, w);
        apply_damage(ctx->rec_a, w);
        if ((w->location & 4) && wrestler_health_eff(w) < 25 && down_count_allowed(w)) {
            pose_is_lying(w);
            wrestler_set_anim(w, (w->flags8a & 1) ? 12163 : 12162, 1);
            w->state_step++;
            return;
        }
        wrestler_settle_pose(w);
        return;
    }
    case 2:
        w->flags8a |= 0x6000;
        wrestler_slide(w, 8.0f);
        keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
        if (move_event(ctx, 0x200)) {
            w->count_timer = (u16)escape_time(w, ctx->rec_a, 0);
            mash_count(w, 0, 0);
        }
        return;
    default:
        return;
    }
}



// N64 0x80133CC0: state 0x38, picking a downed opponent up (paired with 0x39; flags80 0x01, flags8a
// 0x100). Step 0 anim_pick's walk over. Step 1 the plan: to the side of it the wrestler already is on
// (80 out; 9 along, 0 for one sitting, -9 when it lies face down and is not sitting), facing a quarter
// turn in from it; pickup_anims the row of animations (0x8014DC50, or 0x8014DC78 for one sitting, by
// side + 2 when face down). Step 2 walks there (ground_approach_plan, 56 a frame) and starts the first
// animation (state_flags 0x01 tells 0x39; one sitting skips to step 4, state_flags 0x08). Step 3 at its
// end with R still held goes on; otherwise step 7 (state_flags 0x04) with the row's let-go animation.
// Step 4 at its end, if the opponent can reverse (state_flags 0x02) and lies face down state_flags 0x10,
// the row's next animation. Step 5 at its end: state_flags 0x10 into the rear grapple (11909), then step
// 6's wait; state_flags 0x02 reversed: the stun 0x30; else settles. Both unpair. Step 6: the opponent's
// style 1 until 0x39 moves the pair on. Step 7 the events.
void state_38_pickup_attacker(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    auto next_anim = [&](s32 mode) {
        s32 a = mem_s16(w->pickup_anims);
        w->pickup_anims += 2;
        wrestler_set_anim(w, a, mode);
    };
    switch (w->state_step) {
    case 0:
        w->flags80 |= 1;
        w->flags8a |= 0x100;
        wrestler_set_anim(w, anim_pick(ctx, 0, 0), 3);
        w->state_step++;
        return;
    case 1: {
        Vec3 off, target;
        s32 turn, row;
        if (angle_diff(o->facing, w->facing_to_opponent) < 0) {
            off.x = -80.0f;
            turn = 1024;
            row = 0;
        } else {
            off.x = 80.0f;
            turn = -1024;
            row = 1;
        }
        off.y = 0.0f;
        if (o->flags8a & 1) {
            row += 2;
            off.z = (o->flags80 & 0x10) ? 9.0f : -9.0f;
        } else {
            off.z = (o->flags80 & 0x10) ? 0.0f : 9.0f;
        }
        point_at_offset(&o->point_120, &off, o->facing, &target);
        target.y = w->pos.y;
        ground_approach_plan(ctx, &target, 0);
        w->lockup_facing = angle_add(o->facing, turn);
        w->pickup_anims = (o->flags80 & 0x10) ? 0x8014DC78u + 6u * (u32)row : 0x8014DC50u + 10u * (u32)row;
        w->state_step++;
        [[fallthrough]];
    }
    case 2: {
        w->flags80 |= 1;
        w->flags8a |= 0x100;
        keep_in_bounds_pair_c(w, o, 2.0f, ctx->self->approach_pts, 2);
        s32 done = ground_approach_plan(ctx, nullptr, 1);
        done += wrestler_turn_toward(w, w->lockup_facing, 56, 0);
        if (done != 2)
            return;
        if (o->flags80 & 0x10) {
            w->state_step = 4;
            w->state_flags |= 8;
        } else {
            w->state_step++;
        }
        next_anim(2);
        w->state_flags |= 1;
        return;
    }
    case 3:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        if (ctx->buttons & 0x10) {
            w->state_step++;
        } else {
            w->state_step = 7;
            w->pickup_anims += 6;
            w->state_flags |= 4;
        }
        next_anim(0);
        return;
    case 4:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        if (w->state_flags & 2) {
            if (o->flags8a & 1)
                w->state_flags |= 0x10;
            w->pickup_anims += 2;
        }
        next_anim(0);
        w->state_step++;
        return;
    case 5:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (w->state_flags & 0x10) {
            wrestler_set_anim(w, 11909, 1);
            w->state_step++;
            return;
        }
        if (w->state_flags & 2)
            wrestler_settle_pose(w);
        else
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        wrestler_unpair(w);
        return;
    case 6:
        wrestler_set_anim(w, anim_style(o, 1), 1);
        return;
    case 7:
        state_bounds(w, o, nullptr, 0);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80134114: state 0x39, being picked up. Step 0 anim_is_dazed_like; step 1 (flags80 0x01) waits
// for 0x38's state_flags 0x01, then plays the animation one on from the attacker's. Unless in finisher
// mode on either side (flags_ee 0x04) or flags_e9 0x01, with health below 129, chance_roll kind 11 is
// the reversal (state_flags 0x02 on the attacker). One sitting (the attacker's 0x08) goes to step 3.
// Step 2 at its end the attacker's next + 1 (step 6 on its let-go, 0x04). Step 3 at its end the same;
// on the attacker's 0x10 the move 0x54 or, 750 in 1000 not, 0x55. Step 4 at its end: on 0x10 the rear
// grapple (11910, the attacker's facing); reversed the stun 0x30; else settles. Step 5: own style 0,
// the pair goes to 0x5F / 0x60 (flags78 0x880, 0x884 for move 85). Step 6 at its end back to lying
// (0x31 step 1).
void state_39_pickup_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        anim_is_dazed_like(w);
        w->state_step++;
        [[fallthrough]];
    case 1:
        w->flags80 |= 1;
        if (!(o->state_flags & 1))
            return;
        wrestler_set_anim(w, o->anim_id + 1, 6);
        if (((w->flags_ee & 4) || !(o->flags_ee & 4)) && !(w->flags_e9 & 1) && wrestler_health_eff(w) < 129 &&
            chance_roll(w, nullptr, 11))
            o->state_flags |= 2;
        w->state_step = (o->state_flags & 8) ? 3 : w->state_step + 1;
        return;
    case 2:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, o->anim_id + 1, 0);
        w->state_step = (o->state_flags & 4) ? 6 : w->state_step + 1;
        return;
    case 3:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, o->anim_id + 1, 0);
        if (o->state_flags & 0x10)
            wrestler_start_move(w, ctx->slot, chance_check(750) ? 0x54 : 0x55);
        w->state_step++;
        return;
    case 4:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (o->state_flags & 0x10) {
            wrestler_set_anim(w, 11910, 1);
            w->facing = o->facing;
            w->state_step++;
            return;
        }
        if (o->state_flags & 2)
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        else
            wrestler_settle_pose(w);
        return;
    case 5:
        wrestler_set_anim(w, anim_style(w, 0), 1);
        wrestler_pair_set_state_move(w, state::CarryMoveAttacker, o, state::CarryMoveDefender);
        w->flags78 = (w->move_num == 85) ? 2180 : 2176;
        o->flags78 = w->flags78;
        return;
    case 6:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        wrestler_set_state_now(w, state::LyingDown);
        w->state_step = 1;
        return;
    default:
        return;
    }
}

// N64 0x8001A6AC (main segment): a wrestler starts to show blood, under rule 0x800, unless the match has
// it off (match_a +0x458): the blood marks on its two textures (texture_stamp_layer: left to the host),
// the model's flag 0x20 and a mark timer of 7.
static void blood_stamp_apply(s32 slot) {
    if (mem_s32(0x801594D8) != 0 || !(g_match_rules & 0x800))
        return;
    host_hud("blood_stamp", slot, 0);
    u32 m = 0x800A8BD4 + 624u * (u32)slot;
    mem_w8(m, (u8)(mem_u8(m) | 0x20));
    mem_w8(0x800A9458 + (u32)slot, 7);
}

// N64 0x800E7AD4: state 0x2F, a cut: staggering from a blow that made the wrestler bleed
// (move_aftermath_check, tko_flags 0x02). flags80 0x01 and bonus_flags 0x02 (one cut a match, outside
// first blood), the blood marks. The reaction: 12622 at a slump (tko_flags 0x04) or with flags8a 0x02,
// else 12621; from a move's end pose (0x08) its pose animation (0x8014DA98), then the getting-up 12623
// / 12624. Last step: with tko_flags 0x80 (first blood) the bell 2 frames before the end and on the
// last frame end_flags 0xC0: the fall. At the end under a guest referee (match type 0x80000 with rule
// 0x1000) flags8c 0x800, which is what the referee's call waits for. Was state_2F_ropes_step_through.
void state_2F_bleeding(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
    switch (w->state_step) {
    case 0:
        w->flags80 |= 1;
        w->bonus_flags |= 2;
        blood_stamp_apply(ctx->slot);
        if (w->tko_flags & 4) {
            wrestler_set_anim(w, 12622, 3);
        } else if (w->tko_flags & 8) {
            wrestler_set_anim(w, mem_s16(0x8014DA98 + 2u * (u32)(s32)(s8)w->end_pose), 1);
            w->state_step++;
            return;
        } else {
            wrestler_set_anim(w, (w->flags8a & 2) ? 12622 : 12621, 3);
        }
        w->state_step = 2;
        return;
    case 1:
        wrestler_set_anim(w, (w->flags8a & 1) ? 12624 : 12623, 6);
        w->state_step++;
        return;
    case 2:
        if (w->tko_flags & 0x80) {
            if (w->anim_frame == w->anim_player[AP_FRAMES] - 2)
                host_sfx(1);
            if (w->anim_frames_left == 1)
                w->end_flags |= 0xC0;
        }
        if (move_event(ctx, 512) && (g_match_type & 0x80000) && (g_match_rules & 0x1000))
            w->flags8c |= 0x800;
        return;
    default:
        return;
    }
}

// N64 0x80136D90: which ground grapple the attacker's side, the victim's sitting and which way it lies give
// (0x8014DC90 by ground_side 0x01, flags80 0x10, ground_side 0x02). In finisher mode at one lying flat, the
// attacker's finisher for that move when its attrs +60 bit allows (236 / 238 / 240 / 242 to 155-158). On a
// reversal (ground_side 0x10) the victim's bit decides and it is the victim's own move. 1 with the victim's
// answer (the move + 1) when it reversed or escapes (ground_side 0x08, state_flags 0x10): 0x47 goes to 0x46's
// counter.
static s32 ground_grapple_start(Wrestler *w, Wrestler *o) {
    u8 f = w->ground_side;
    s32 i = ((f & 1) << 2) + ((o->flags80 & 0x10) ? 2 : 0) + ((f & 2) ? 1 : 0);
    s32 base = mem_s16(0x8014DC90 + 2u * (u32)i);
    auto finisher = [&](const Wrestler *x) {
        u16 bits = mem_u16(x->attrs + 60);
        switch (base) {
        case 236: return (bits & 0x1000) ? 155 : base;
        case 238: return (bits & 0x2000) ? 156 : base;
        case 240: return (bits & 0x4000) ? 157 : base;
        case 242: return (bits & 0x8000) ? 158 : base;
        default: return base;
        }
    };
    s32 m = base;
    if ((w->flags_ee & 4) && !(o->flags80 & 0x10))
        m = finisher(w);
    if (f & 0x10)
        wrestler_start_move(w, o->slot, finisher(o));
    else
        wrestler_start_move(w, w->slot, m);
    if ((f & 8) || (o->state_flags & 0x10)) {
        wrestler_start_move(o, o->slot, base + 1);
        return 1;
    }
    return 0;
}

// N64 0x80136FBC: a press of A, B, L or R by one being taken (not in finisher mode against a normal
// attacker, nor flags_e9 0x01) gets one try, ground_side 0x04: chance_roll kind 0 is a reversal, 0x08.
static void ground_grapple_defend(Wrestler *w, Wrestler *o, u16 pressed) {
    if (!(w->flags_ee & 4) && (o->flags_ee & 4))
        return;
    if (w->flags_e9 & 1)
        return;
    if ((o->ground_side & 4) || !(pressed & 0xC030))
        return;
    o->ground_side |= 4;
    if (chance_roll(w, nullptr, 0))
        o->ground_side |= 8;
}

// N64 0x80132E7C: recover_tick for one held down: 3 times in 4 the time down gets its frame back. Returns
// 1 once it is out, unless the stay-down input holds it at 0.
static s32 recover_tick_ground(Wrestler *w, u16 pressed, s32 stay) {
    recover_tick(w, pressed);
    if (chance_check(750))
        w->escape = (s16)(w->escape + 1);
    if (w->escape > 0)
        return 0;
    if (stay) {
        w->escape = 0;
        return 0;
    }
    return 1;
}

// N64 0x80136240: state 0x46, a ground grapple on a downed opponent (0x47), or with flags8a 0x08 already at
// it. Step 0 the walk. Step 1 the place: ground_side 0x01 if at its feet side, 0x02 if it lies face down; 80,
// 110, 130, 140 or 150 along it (flags80 0x10 sitting; ground_side 0x20 a drag); step 2 walks there. Step 3
// (ground_side 0x20: a drag): C-left held in the ring or on the floor takes the hold (step 6, the distance and
// angle to keep in match_a +0x3E0, 24 bytes a slot); else 54 / 55 (a stomp). Otherwise step -1, the move.
// Step 4 ground_grapple_start: the answer (state_flags 0x02) or the move (0x01), step 5 follow, events 9344.
// Step 6-7 the drag: the opponent is pulled round by the d-pad, 14 a frame; releasing C-left, or it escaping
// (0x10), ends it (state_flags 0x20) with animation + 4. Step 8 the events.
void state_46_ground_grapple_approach(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    u32 keep = 0x80159460 + 24u * (u32)ctx->slot;
    switch (w->state_step) {
    case 0: {
        s32 a = 12173;
        if (!(w->flags8a & 8)) {
            w->flags80 |= 1;
            w->flags8a |= 0x100;
            a = anim_pick(ctx, 0, 0);
        }
        wrestler_set_anim(w, a, 3);
        w->state_step++;
        return;
    }
    case 1: {
        if (o->flags8a & 1)
            w->ground_side |= 2;
        Vec3 t;
        if (o->flags80 & 0x10) {
            Body b;
            lying_body_axis(o, &b);
            t = b.center;
        } else {
            t = o->point_120;
        }
        s32 ad = abs_i(angle_diff(o->facing, angle_to(&w->point_120, &t)));
        if ((o->flags8a & 1) ? !(ad < 1025) : ad < 1024)
            w->ground_side |= 1;
        Vec3 off = {0.0f, 0.0f, 0.0f};
        u8 f = w->ground_side;
        bool plain = (f & 0x20) || !(o->flags80 & 0x10);
        if (f & 1)
            off.z = (plain || (o->flags8a & 1)) ? -130.0f : -80.0f;
        else
            off.z = plain ? 150.0f : ((o->flags8a & 1) ? 110.0f : 140.0f);
        if (o->flags8a & 1)
            off.z = -off.z;
        Vec3 target;
        point_at_offset(&o->pos, &off, o->facing, &target);
        target.y = w->pos.y;
        ground_approach_plan(ctx, &target, 0);
        w->state_step++;
        [[fallthrough]];
    }
    case 2: {
        w->flags80 |= 1;
        if (!(w->flags8a & 8))
            w->flags8a |= 0x100;
        keep_in_bounds_pair_c(w, o, 4.0f, ctx->self->approach_pts, 0xFFFFE);
        s32 done = ground_approach_plan(ctx, nullptr, 1);
        done += wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        if (done != 2)
            return;
        if (!(w->flags8a & 8))
            wrestler_set_anim(w, 12168, 2);
        w->state_step++;
        return;
    }
    case 3:
        if (w->anim_frames_left > 0 && !(w->flags8a & 8))
            return;
        if (!(w->ground_side & 0x20)) {
            wrestler_set_step(w, -1);
            return;
        }
        if ((ctx->buttons & 2) && (w->location & 5)) {
            f32 out[9];
            segment_distance(&o->pos, &w->pos, out);
            mem_w32(keep, 0);
            mem_w32(keep + 4, 0);
            mem_wf32(keep + 8, out[4]);
            mem_w32(keep + 0x10, (u32)(s32)out[7]);
            wrestler_set_step(w, 6);
            return;
        }
        wrestler_pair_set_state_now(w, state::FlipDownedOpponentAttacker, o, state::FlipDownedOpponentDefender);
        w->state_step = 3;
        o->state_step = 1;
        return;
    case 4:
        pair_follow(w, o, nullptr, 4.0f, 0, 0xFFFFE);
        if (ground_grapple_start(w, o)) {
            w->state_flags |= 2;
            wrestler_set_anim(w, anim_style(o, 1), 2);
            return;
        }
        w->state_flags |= 1;
        wrestler_set_anim(w, anim_style(w, 0), 2);
        w->state_step++;
        return;
    case 5:
        finisher_effects(w);
        pair_follow(w, o, nullptr, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 9344);
        return;
    case 6: {
        w->state_flags |= 4;
        s32 a = (w->ground_side & 1) ? 0x3026 : 0x302E;
        if (w->ground_side & 2)
            a += 16;
        wrestler_set_anim(w, a, 0);
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, w->anim_id + 2, 0);
        w->state_step++;
        return;
    }
    case 7:
        if (!(o->state_flags & 0x10)) {
            s32 moved = 0;
            s32 d8 = dpad_dir8(ctx->buttons);
            if (d8) {
                s32 d = angle_diff(w->facing, mem_s32(0x8014D234 + 4u * (u32)d8));
                if (abs_i(d) < 1536) {
                    s32 s = d >= 0 ? 14 : -14;
                    w->facing = angle_add(w->facing, s);
                    o->facing = angle_add(o->facing, s);
                    mem_w32(keep + 0x10, (u32)angle_add(mem_s32(keep + 0x10), s));
                    moved = 1;
                }
            }
            Vec3 off = {mem_f32(keep), mem_f32(keep + 4), mem_f32(keep + 8)}, t, step;
            point_at_offset(&o->pos, &off, mem_s32(keep + 0x10), &t);
            t.y = w->pos.y;
            vec_sub(&w->pos, &t, &step);
            wrestler_translate(w, &step);
            if (moved) {
                vec_from_angle(w->facing, 2.0f, &step);
                wrestler_translate(w, &step);
                wrestler_translate(o, &step);
            }
        }
        if (!(w->anim_frames_left > 0)) {
            if (!(ctx->buttons & 2))
                o->state_flags |= 8;
            if (o->state_flags & 0x18) {
                w->state_flags |= 0x20;
                wrestler_set_anim(w, w->anim_id + 4, 1);
                w->state_step++;
            } else {
                wrestler_restart_anim(w, w->anim_id, 1);
            }
        }
        w->dpad_held = keep_in_bounds_pair(w, o, nullptr, 1.0f, 0xFFFFE);
        return;
    case 8:
        state_bounds(w, o, nullptr, 0);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80136AA8: state 0x47, taken in a ground grapple (flags80 0x09). Step 1 the one try
// (ground_grapple_defend), then by the attacker's state_flags: 0x01 its animation, step 2; 0x02 reversed, the
// pair goes the other way (70 / 71, step 5 / 2); 0x04 the drag, step 3. Step 2 follows, events 6656. Step 3 the
// attacker's animation + 1. Step 4 being dragged: recover_tick_ground gets it free (state_flags 0x10). Step 5
// at the end: state_flags 0x08 lying (0x31 step 1), else the stun 0x30.
void state_47_ground_grappled(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    w->flags80 |= 9;
    switch (w->state_step) {
    case 0:
        anim_is_dazed_like(w);
        w->state_step++;
        [[fallthrough]];
    case 1:
        ground_grapple_defend(w, o, ctx->pressed);
        if (o->state_flags & 1) {
            wrestler_set_anim(w, anim_style(o, 1), 6);
            w->state_step++;
        } else if (o->state_flags & 2) {
            wrestler_set_anim(w, anim_style(w, 0), 6);
            wrestler_pair_set_state_move(w, state::GroundGrappleAttacker, o, state::GroundGrappleDefender);
            w->state_step = 5;
            o->state_step = 2;
            stats_count(w, o, 1);
        } else if (o->state_flags & 4) {
            wrestler_set_anim(w, o->anim_id + 1, 6);
            w->state_step = 3;
        }
        return;
    case 2:
        finisher_effects(w);
        pair_follow(w, o, nullptr, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 6656);
        return;
    case 3:
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, o->anim_id + 1, 0);
        w->state_step++;
        return;
    case 4:
        if (recover_tick_ground(w, ctx->pressed, ctx->in_byte))
            w->state_flags |= 0x10;
        if (!(w->anim_frames_left > 0)) {
            if (o->state_flags & 0x20) {
                wrestler_set_anim(w, o->anim_id + 1, 1);
                w->state_step++;
            } else {
                wrestler_restart_anim(w, w->anim_id, 1);
            }
        }
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        return;
    case 5:
        state_bounds(w, o, nullptr, 0);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (w->state_flags & 8) {
            wrestler_set_state_now(w, state::LyingDown);
            w->state_step = 1;
        } else {
            w->flags8a |= 0x10;
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        }
        return;
    default:
        return;
    }
}

// N64 0x80111AF0: state 0x36, stepping to the side of a downed opponent to flip it (0x37): the side by
// cover_side_pick (0x80146190: offset x, z and the facing, mirrored if it lies face down). Step 3 the flip, 12366
// + 2 x side (12358 face down), state_flags 0x01. Step 4 the events.
void state_36_cover_approach(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        w->flags80 |= 1;
        w->flags8a |= 0x100;
        wrestler_set_anim(w, anim_pick(ctx, 0, 0), 3);
        w->state_step++;
        return;
    case 1: {
        s32 side = cover_side_pick(o, w->facing_to_opponent);
        w->dir = (s16)side;
        u32 e = 0x80146190 + 12u * (u32)side;
        Vec3 off = {mem_f32(e), 0.0f, mem_f32(e + 4)}, t;
        w->lockup_facing = angle_add(mem_s32(e + 8), o->facing);
        if (o->flags8a & 1) {
            off.x = -off.x;
            off.z = -off.z;
            w->lockup_facing = angle_add(w->lockup_facing, 2048);
        }
        point_at_offset(&o->pos, &off, o->facing, &t);
        t.y = w->pos.y;
        ground_approach_plan(ctx, &t, 0);
        w->state_step++;
        [[fallthrough]];
    }
    case 2: {
        w->flags80 |= 1;
        w->flags8a |= 0x100;
        keep_in_bounds_pair_c(w, o, 2.0f, ctx->self->approach_pts, 2);
        s32 done = ground_approach_plan(ctx, nullptr, 1);
        done += wrestler_turn_toward(w, w->lockup_facing, 113, 0);
        if (done != 2)
            return;
        wrestler_set_anim(w, 12168, 2);
        w->state_step++;
        return;
    }
    case 3: {
        if (w->anim_frames_left > 0)
            return;
        s32 side = cover_side_pick(o, w->facing_to_opponent);
        w->dir = (s16)side;
        wrestler_set_anim(w, 2 * (s32)w->dir + ((o->flags8a & 1) ? 12358 : 12366), 0);
        w->state_flags |= 1;
        w->state_step++;
        return;
    }
    case 4:
        state_bounds(w, o, nullptr, 0);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80111DA8: state 0x37, flipped over by 0x36 (flags80 0x09): on its state_flags 0x01 the attacker's
// animation + 1; 3 frames from the end lying again (0x31 step 1).
void state_37_flipped(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        anim_is_dazed_like(w);
        w->state_step++;
        [[fallthrough]];
    case 1:
        w->flags80 |= 9;
        if (!(o->state_flags & 1))
            return;
        wrestler_set_anim(w, o->anim_id + 1, 6);
        w->state_step++;
        return;
    case 2:
        w->flags80 |= 1;
        state_bounds(w, o, nullptr, 0);
        if (!(w->anim_frames_left < 3))
            return;
        anim_end_apply(w);
        wrestler_set_state_now(w, state::LyingDown);
        w->state_step = 1;
        return;
    default:
        return;
    }
}

// N64 0x80123A78: state 0x25, the get-up attack (move 217): turning to the opponent, 22 a frame for the
// first half of the time before it can hit, then 11; strike_hit_test kind 1, strike_land flags 0x100.
void state_25_getup_attack(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_start_move(w, ctx->slot, 217);
        wrestler_set_anim(w, anim_style(w, 0), 3);
        w->state_step++;
        [[fallthrough]];
    case 1: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 from = w->anim_player[AP_HIT_FROM];
        if (w->anim_frame < from >> 1)
            wrestler_turn_toward(w, w->facing_to_opponent, 22, 0);
        else if (w->anim_frame < from)
            wrestler_turn_toward(w, w->facing_to_opponent, 11, 0);
        s32 t = strike_hit_test(w, 0, 1);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 256);
        }
        break;
    }
    case 2:
        break;
    default:
        return;
    }
    wrestler_slide(w, 2.0f);
    strike_keep_in_bounds(w);
    move_event(ctx, 0);
}

// N64 0x800E848C: state 0x32, dazed on its feet after a get-up, and able to be walked and timed-pressed; on
// the apron hanging on the ropes (12558, facing in, no walking), else 12556 stumbling (walk_step at 4). The
// time down (recover_tick, the stay-down input holds it at 0) or a lost match (end_flags 0x04) ends it:
// 12557 slapping the daze off, 12559 on the apron. Was state_32_ropes_lean, the apron case only.
void state_32_dazed(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    wrestler_slide(w, 4.0f);
    keep_in_bounds_self(w, 2.0f, 0, 2);
    auto wake = [&]() {
        if (w->location & 2)
            wrestler_set_anim(w, 12559, 0);
        else
            wrestler_set_anim(w, 12557, 3);
        w->state_step++;
    };
    switch (w->state_step) {
    case 0:
        w->flags80 |= 1;
        if (w->location & 2) {
            wrestler_set_anim(w, 12558, 0);
            w->facing = mem_s32(0x8014D1E8 + 4u * (u32)w->ring_zone);
        } else {
            wrestler_set_anim(w, 12556, 1);
        }
        w->state_step++;
        return;
    case 1:
        if (!(w->location & 2)) {
            walk_step(ctx, ctx->buttons, 4);
            timed_press_check(w, ctx->buttons, ctx->pressed, 1);
        }
        if ((g_match_flags[1] & 0x40) && (w->end_flags & 4)) {
            wake();
            return;
        }
        if (!recover_tick(w, ctx->pressed))
            return;
        if (ctx->in_byte) {
            w->escape = 0;
            return;
        }
        wake();
        return;
    case 2:
        if (!(w->location & 2))
            timed_press_check(w, ctx->buttons, ctx->pressed, 1);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EAAE4: state 0x97, spinning a dazed opponent (input_c_left_held within 85, L: input_l): it is
// given a facing to end on, to the attacker or away by which is nearer, and the way round picks 12560 / 12561. The
// attacker turns to face it over 6 frames, then the events.
void state_97_spin_dazed(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    switch (w->state_step) {
    case 0:
        o->turn_step = abs_i(angle_diff(w->facing_to_opponent, o->facing)) < 1024 ? angle_add(w->facing_to_opponent, 2048)
                                                                                   : w->facing_to_opponent;
        wrestler_set_anim(w, angle_diff(o->turn_step, o->facing) >= 0 ? 12561 : 12560, 3);
        w->turn_step = angle_diff(w->facing_to_opponent, w->facing) / 6;
        w->count_timer = 0;
        w->state_step++;
        [[fallthrough]];
    case 1:
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 7)
            w->facing = angle_add(w->facing, w->turn_step);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EAC40: state 0x98, the dazed one spun: to the facing 0x97 set over 20 frames; if it was slapping
// the daze off (12557) to its end, anim_pick 1. After 21 frames still dazed (0x32) if it was stumbling (12556),
// else standing.
void state_98_spun(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    state_bounds(w, ctx->rec_a, nullptr, 0);
    switch (w->state_step) {
    case 0:
        w->count_timer = 0;
        w->turn_step = angle_diff(w->turn_step, w->facing) / 20;
        w->state_step++;
        [[fallthrough]];
    case 1:
        w->facing = angle_add(w->facing, w->turn_step);
        if (w->anim_id == 12557 && !(w->anim_frames_left > 0))
            wrestler_set_anim(w, anim_pick(ctx, 1, 0), 2);
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 21)
            return;
        wrestler_set_state_now(w, w->anim_id == 12556 ? state::Dazed : state::Standing);
        return;
    default:
        return;
    }
}

}  // namespace nm



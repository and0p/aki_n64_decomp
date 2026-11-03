// Strikes: the B input, the strike states (0x22 weak, 0x23 strong), the hit test and its
// candidate list, the landing (strike_land: who can be hit, blocks and counters, the reaction),
// damage and spirit, the odds (chance_roll), and the reaction states (0x2D knocked down, 0x33 off
// balance, 0x35 knocked back, 0x4C blocking).
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_MOVESETS = 0x800AA090,      // g_movesets: 548 x 36-byte records a slot
    A_MOVE_ANIMS = 0x8008FA70,    // g_move_anims: u16[4][548]
    A_MOVE_ANIMS2 = 0x8009FF90,   // g_move_anims2
    A_GENERIC_MOVES = 0x800A6040, // the shared move table (wrestler_set_generic_move)
    A_STYLE_ANIMS = 0x8015A700,   // s16 per move animation: anim_style's base
    A_JOINT_LISTS = 0x80146844,   // s8 * per strike test kind: the attacking joints
    A_TARGET_JOINTS = 0x8014689C, // s8[]: the target joints a strike must reach (5, 1, -1)
    A_JOINT_RADIUS = 0x8016B728,  // f32[4][28]: each slot's joint reach radii
    A_HEIGHT_WINDOW = 0x8016B778, // f32 at +0x50 of each slot's 112-byte radius block: the height window above its lowest joint
    A_HEALTH_BANDS = 0x801468A0,  // s16[4]: 128 64 32 0, the health bands of the reaction tables
    A_SPIRIT_BONUS = 0x801465B4,  // s8[]: spirit bonus by attrs +5
    A_REGION_TABLE = 0x801461C0,  // f32[?][3]: extra region damage in state 73 by attrs +22 of both
    A_REACT_TBL = 0x80146898,     // the reaction weight tables (s16 cumulative-of-1000 entries)
    A_ZONE_SOUNDS = 0x801451BC,   // s16[4]: zone_kind's sound ids
    A_SIDE_FACINGS = 0x8014D1E8,  // s32[4]: the facing along each ring side (apron)
    A_CORNER_ANGLES = 0x8014D208, // s32[4]: the angle of each corner (state_35)
    A_STATS_A = 0x8009FDF8,       // u16[4][4] x 3: the hit statistics (stats_count kinds 0, 1, 2)
    A_STATS_B = 0x8009FE18,
    A_STATS_C = 0x8009FE38,
    A_PRESS_COUNT = 0x80159450,   // s16[4]: timed_press_check's press counter
    A_PRESS_BITS = 0x80159458,    // u16[4]: its bits
    A_EVENT_MOVES = 0x8014DB00,   // u8[64]: generic move by move_event code
    A_ROPE_BUTTONS_A = 0x8014D220,// u16[4]: the button toward the ring by side (strike_across_ropes)
    A_ROPE_BUTTONS_B = 0x8014D218,
    A_TRAIT_TBL = 0x8014631C,     // s8[]: chance_roll kind 19
};

// ---- helpers over the record ----

// The move record at an image address, host order.
MoveRecord move_record_load(u32 a) {
    MoveRecord r;
    u8 raw[36];
    std::memcpy(raw, g_ram + ram_off(a), 36);
    std::memcpy(&r, raw, 36);
    swap_runs((u8 *)&r, kMoveRecordSwap, (int)(sizeof(kMoveRecordSwap) / sizeof(SwapRun)));
    return r;
}

s8 attr(const Wrestler *w, u32 off) { return mem_s8(w->attrs + off); }

// The first entry of a table of (threshold, value) s16 pairs whose threshold x reaches.
u32 band_pairs(u32 tbl, s32 x) {
    u32 i = 0;
    if (x < mem_s16(tbl)) {
        i = 1;
        while (x < mem_s16(tbl + 4 * i))
            i++;
    }
    return i;
}

// The ring side (0-3) a world point is nearest to by its diagonal half-planes (strike_land's
// apron and the frame events share the shape).
}  // namespace

// The move record at an image address, host order (for the other files).
MoveRecord move_record_at(u32 a) { return move_record_load(a); }

// N64 0x8012E1B4: the ring side an angle points at.
s32 angle_to_side(s32 angle) {
    s32 v = angle + 4608;
    if (v < 0)
        v += 1023;
    return (v >> 10) & 3;
}

// N64 0x80132AC0: clears the second entry of the slot's button histories (so a press is not read
// twice by input_press_kind).
void input_history_clear(s32 slot) {
    g_input[slot].held_hist[1] = 0;
    g_input[slot].pressed_hist[1] = 0;
}

// N64 0x80132AE8: how A / B was pressed, from the slot's button history: *out = 0x01 held through
// the last 8 entries (a hold; 0x10 with both A and B), or 0x02 pressed recently (a tap), with 0x04
// for A, 0x08 for B and 0x20 when a d-pad direction was held with it.
void input_press_kind(s32 slot, u8 *out) {
    const InputSlot *in = &g_input[slot];
    *out = 0;
    if (in->held & 0xC000) {
        s32 n = 0;
        while (n < 8 && (in->held_hist[n] & 0xC000))
            n++;
        if (n < 8)
            return;
        *out |= 1;
        if ((in->held & 0xC000) == 0xC000)
            *out |= 0x10;
        if (in->held & 0x8000)
            *out |= 4;
        if (in->held & 0x4000)
            *out |= 8;
        if (in->held & 0xF00)
            *out |= 0x20;
        return;
    }
    *out = 2;
    for (s32 n = 1; n < 10; n++) {
        u16 h = in->held_hist[n];
        if (!(h & 0xC000))
            continue;
        if (h & 0x8000)
            *out |= 4;
        if (h & 0x4000)
            *out |= 8;
        if (h & 0xF00)
            *out |= 0x20;
        return;
    }
}

// N64 0x800EC698: makes move `move` of slot `from`'s move set the wrestler's current move: the
// record pointer, its two animations, the repeat count, and flags8c bits 0x01 (record flag 0x8000),
// 0x02 (one of its finishers, attrs +0x1C / +0x20) and 0x04 (moves 70 / 81).
void wrestler_start_move(Wrestler *w, s32 from, s32 move) {
    w->move_anim2_prev = w->move_anim2;
    w->move_repeat = (s16)(w->move_num == move ? w->move_repeat + 1 : 1);
    w->move_slot = (u8)from;
    w->move = A_MOVESETS + 19728u * (u32)from + 36u * (u32)move;
    w->move_num = (s16)move;
    w->move_anim = (s16)mem_u16(A_MOVE_ANIMS + 1096u * (u32)from + 2u * (u32)move);
    w->move_anim2 = (s16)mem_u16(A_MOVE_ANIMS2 + 1096u * (u32)from + 2u * (u32)move);
    if (mem_u32(w->move + 0x20) & 0x8000)
        w->flags8c |= 1;
    else
        w->flags8c &= 0xFFFE;
    if (w->move_num == mem_s16(w->attrs + 0x1C) || w->move_num == mem_s16(w->attrs + 0x20))
        w->flags8c |= 2;
    else
        w->flags8c &= 0xFFFD;
    if (w->move_num == 70 || w->move_num == 81)
        w->flags8c |= 4;
    else
        w->flags8c &= 0xFFFB;
}

// N64 0x800EC880: makes entry `move` of the shared move table the current move (107-111 pick by
// the held object's form).
void wrestler_set_generic_move(Wrestler *w, s32 move) {
    w->move_num = (s16)move;
    w->move_slot = (u8)w->slot;
    s32 idx = move;
    if ((u32)(move - 107) < 5)
        idx = move + 5 * mem_s32(0x8016AE10 + 180 * w->held_obj + 4);
    w->move = A_GENERIC_MOVES + 36u * (u32)idx;
}

// N64 0x80130878: the animation of the current move in the wrestler's style: the table
// 0x8015A700 (by move_anim) plus `variant` plus 13090.
s32 anim_style(const Wrestler *w, s32 variant) {
    return mem_s16(A_STYLE_ANIMS + 2u * (u32)(u16)w->move_anim) + variant + 13090;
}

// N64 0x800EB948: the state step (or the next one with -1) and flags88 bit 0.
void wrestler_set_step(Wrestler *w, s32 step) {
    if (step != -1)
        w->state_step = step;
    else
        w->state_step = w->state_step + 1;
    w->flags88 |= 1;
}

// N64 0x800EC8EC: copies the attacker's current move (slot, repeat, number, animations and the
// 36-byte record) into the defender's hit_move fields.
void hit_move_copy(const Wrestler *src, Wrestler *dst) {
    dst->hit_by = src->move_slot;
    dst->hit_move_repeat = src->move_repeat;
    dst->hit_move_num = src->move_num;
    dst->hit_move_anim = src->move_anim;
    dst->hit_move_anim2 = src->move_anim2;
    dst->hit_move_anim2_prev = src->move_anim2_prev;
    dst->hit_move = move_record_load(src->move);
}

// N64 0x800EB8B0: clears every other loaded wrestler's pair links to w (pair_slot / pair_slot2
// to -1; flags88 0x04 off once both are clear).
void pair_link_clear_others(const Wrestler *w) {
    for (s32 i = 0; i < 4; i++) {
        Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1)
            continue;
        if (o->pair_slot == w->slot)
            o->pair_slot = -1;
        if (o->pair_slot2 == w->slot)
            o->pair_slot2 = -1;
        if (o->pair_slot == -1 && o->pair_slot2 == -1)
            o->flags88 &= 0xFFFB;
    }
}

// N64 0x800EB824: puts two wrestlers into a paired move: states a (w) and b (other), both freed
// of other pairs, linked through pair_slot / pair_other / opponent.
void wrestler_pair_set_state(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b) {
    wrestler_set_state_free(w, state_a);
    wrestler_set_state_free(other, state_b);
    pair_link_clear_others(w);
    pair_link_clear_others(other);
    w->flags88 |= 4;
    w->pair_slot = other->slot;
    w->pair_other = other->slot;
    w->opponent = other->slot;
    other->opponent = w->slot;
    other->pair_other = w->slot;
}

// N64 0x800EB77C: wrestler_pair_set_state for a move: both get flags80 0x01 and flags88 0x02.
void wrestler_pair_set_state_move(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b) {
    wrestler_pair_set_state(w, state_a, other, state_b);
    w->flags80 |= 1;
    other->flags80 |= 1;
    w->flags88 |= 2;
    other->flags88 |= 2;
}

// N64 0x8011CD3C: the health the odds use: health_max in finisher mode, else health.
s32 wrestler_health_eff(const Wrestler *w) { return (w->flags_ee & 4) ? w->health_max : w->health; }

// N64 0x8011D05C: adds to the attitude meter unless it is in a fixed mode (flags_ee 0x05, or
// 0x07 off the cage): 100 and above gives SPECIAL (600 frames; 99 for a CPU-limited wrestler,
// flags_e9 0x01), 0 and below the empty meter (300 frames).
void wrestler_add_spirit(Wrestler *w, s32 n) {
    u8 mask = (w->location & 0x400) ? 5 : 7;
    if (w->flags_ee & mask)
        return;
    w->spirit = (s16)(w->spirit + n);
    if (w->spirit >= 100) {
        if (w->flags_e9 & 1) {
            w->spirit = 99;
        } else {
            w->spirit = 100;
            w->spirit_timer = 600;
            w->flags_ee |= 2;
        }
    }
    if (w->spirit <= 0) {
        w->spirit = 0;
        w->spirit_timer = 300;
        w->flags_ee |= 1;
    }
}

// N64 0x8011CF9C: the spirit each side gets from a landed move (`rec`'s hit_move: +0x07 for the
// attacker, +0x08 for the defender): the attacker gets 3 more below 49 health (not in state 73),
// and each a bonus by attrs +5 with bonus_flags 0x02 (the attacker's not below 0).
void spirit_exchange(Wrestler *att, Wrestler *def, const Wrestler *rec) {
    s32 a = rec->hit_move.spirit_att;
    if (att->state != state::GroundHoldDefender && att->health < 49)
        a += 3;
    if (att->bonus_flags & 2) {
        a += mem_s8(A_SPIRIT_BONUS + (u32)attr(att, 5));
        if (a < 0)
            a = 0;
    }
    wrestler_add_spirit(att, a);
    s32 b = rec->hit_move.spirit_def;
    if (def->bonus_flags & 2)
        b += mem_s8(A_SPIRIT_BONUS + (u32)attr(def, 5));
    wrestler_add_spirit(def, b);
}

// N64 0x80111F90: counts a hit (and its damage) in the current damage history entry; nothing in
// mode 2 (a hold) or for no damage.
void damage_history_add(Wrestler *w, s32 damage, s32 mode) {
    if (mode != 0 || damage == 0)
        return;
    u32 e = w->damage_history + 2u * (u32)w->damage_hist_pos;
    mem_w8(e, (u8)(mem_u8(e) + 1));
    mem_w8(e + 1, (u8)(mem_u8(e + 1) + damage));
}

// N64 0x8011CCAC: adds to a body region's condition, not below 0.
void region_add(Wrestler *w, s32 region, f32 d) {
    f32 v = w->region[region] + d;
    if ((f64)v < 0.0)
        v = 0.0f;
    w->region[region] = v;
}

// N64 0x80112074: the damage of the move that hit `d` (its hit_move), from attacker `w`: the base
// (or the alternate when hit_move_anim2_prev matches move_check) scaled by the attacker's region
// condition + 50 %, plus (strength - defence) x base / 10 and the spirit difference / 20 (each
// part not below 0), x1.2 in finisher mode; off health (and a quarter off health_max, not below
// 64, unless record flag 0x8000000), into the damage history, the five regions (state 73 adds the
// table by both +22 traits), the weakest region, then the spirit exchange. Returns the damage.
s32 apply_damage(Wrestler *w, Wrestler *d) {
    d->flags8c &= 0xBFFF;
    s32 base;
    if (d->hit_move.move_check != 0 && d->hit_move_anim2_prev == d->hit_move.move_check) {
        d->flags8c |= 0x4000;
        base = d->hit_move.alt_damage;
    } else {
        base = d->hit_move.damage;
    }
    s32 region = d->hit_move.region;
    s32 hitkind = d->hit_move.hit_kind;
    f32 cond = wrestler_region_condition(w, region);
    s32 v[3];
    v[0] = (s32)(((f64)cond + 50.0) * (f64)base * 0.01);
    v[1] = ((s32)attr(w, 10 + region) - (s32)attr(d, 15 + hitkind)) * base / 10;
    v[2] = base != 0 ? (w->spirit - d->spirit) / 20 : 0;
    s32 sum = 0;
    for (s32 i = 0; i < 3; i++) {
        if (v[i] < 0)
            v[i] = 0;
        sum += v[i];
    }
    if (w->flags_ee & 4)
        sum = (s32)((f64)sum * 1.2);
    s32 mode = (d->state == state::GroundHoldDefender || d->state == state::PinDefender || d->state == state::PinFromMoveDefender) ? 1 : 0;
    if (d->hit_move.flags & 0x8000000)
        mode = 2;
    wrestler_add_health(d, -sum, mode);
    damage_history_add(d, sum, mode);
    if (!(d->hit_move.flags & 0x8000000)) {
        s32 q = sum / 4;
        if (q <= 0 && base != 0)
            q = 1;
        d->health_max = (s16)(d->health_max - q);
        if (d->health_max < 65)
            d->health_max = 64;
    }
    for (s32 i = 0; i < 5; i++) {
        f32 f = (f32)d->hit_move.region_damage[i];
        if (d->state == state::GroundHoldDefender && (f64)f != 0.0)
            f = f + mem_f32(A_REGION_TABLE + 12u * (u32)attr(w, 22) + 4u * (u32)attr(d, 22));
        f = -f;
        region_add(d, i, f);
    }
    if (hitkind != 4) {
        f32 m = 50.0f;
        for (s32 i = 0; i < 4; i++)
            if (d->region[i] <= m)
                m = d->region[i];
        if (d->region[hitkind] <= m)
            d->weakest_region = (u8)hitkind;
    }
    spirit_exchange(w, d, d);
    spirit_update(d, 3);
    w->flags_ee |= 0x10;
    d->flags_ee |= 0x10;
    return sum;
}

// N64 0x801128C8: rolls a per-mille chance of `kind` for w (against opp): the tables and
// formulas are in the symbol's desc. Kinds 16-18 are fall_chance x 100.
s32 chance_roll(Wrestler *w, Wrestler *opp, s32 kind) {
    s32 h = wrestler_health_eff(w);
    s32 p = 0;
    switch (kind) {
    case 0: {
        u32 i = band_pairs(0x801461F4, w->spirit);
        p = mem_s16(0x801461F6 + 4 * i);
        break;
    }
    case 1: {
        s32 a = (s8)(attr(w, 21) / 3), b = (s8)(attr(opp, 21) / 3);
        s32 diff = a - b;
        u32 i = 0;
        while (w->spirit < mem_s16(0x8014620C + 4 * i))
            i++;
        p = mem_s16(0x8014620E + 4 * i);
        if (diff > 0)
            p <<= 1;
        if (opp->flags78 & 4) {
            s32 he = wrestler_health_eff(w);
            if (!(he < 192))
                p <<= 2;
            else if (!(he < 128))
                p <<= 1;
        }
        if (mem_u32(w->attrs + 0x34) & 0x1000)
            return chance_roll(w, opp, 8);
        break;
    }
    case 2: {
        if (w->flags_ee & 4)
            return 0;
        u32 i = 0;
        if (h < mem_s16(0x80146224)) {
            i = 1;
            while (h < mem_s16(0x80146224 + 8 * i))
                i++;
        }
        p = (s32)mem_f32(0x80146228 + 8 * i);
        break;
    }
    case 3: {
        u32 i = band_pairs(0x8014624C, w->spirit);
        p = mem_s16(0x8014624E + 4 * i);
        break;
    }
    case 4: {
        s32 n = 15 - attr(opp, 2) - mem_s8(w->move + 9) - (s16)(w->spirit / 15);
        p = (s32)(1.0 / (f64)n * 1000.0);
        break;
    }
    case 5:
        if (wrestler_health_eff(opp) >= 25) {
            p = 0;
            break;
        }
        p = (s32)mem_f32(0x80146270 + 4u * (u32)mem_s8(w->move + 10));
        break;
    case 6:
        NM_UNPORTED("chance_roll kind 6 (Wrestler +0x58)");
    case 7: {
        if (w->flags_ee & 4)
            return 0;
        u32 i = 0;
        while (h < mem_s16(0x80146264 + 8 * i))
            i++;
        f32 f = (f32)((100 - w->spirit) / 40);
        f = f / mem_f32(0x80146268 + 8 * i);
        p = (s32)((f64)f * 1000.0);
        break;
    }
    case 8: {
        s32 diff = opp->spirit - w->spirit;
        if (diff < 0)
            diff = 0;
        p = 500 - 10 * diff;
        break;
    }
    case 9:
        p = (s32)(1.0 / (f64)(4 - attr(w, 6)) * 1000.0);
        break;
    case 10: {
        if (w->flags_ee & 4) {
            p = 1000;
            break;
        }
        s32 diff = (s8)(attr(opp, 21) / 3) - (s8)(attr(w, 21) / 3);
        s32 row = diff >= 0 ? diff + 1 : 0;
        s32 he = wrestler_health_eff(opp);
        u32 base = 0x8014628C + 20u * (u32)row;
        u32 i = 0;
        if (he < mem_s16(base)) {
            i = 4;
            while (he < mem_s16(base + i))
                i += 4;
        }
        p = mem_s16(base + i + 2);
        break;
    }
    case 11: {
        u32 i = band_pairs(0x801462F0, w->spirit);
        p = mem_s16(0x801462F2 + 4 * i);
        break;
    }
    case 12: {
        u32 i = band_pairs(0x80146304, w->escape);
        p = mem_s16(0x80146306 + 4 * i);
        break;
    }
    case 13: p = 700; break;
    case 14: p = 500; break;
    case 15: p = 600; break;
    case 16:
    case 17:
    case 18: p = 100 * (s8)w->fall_chance; break;
    case 19: {
        s32 t = mem_s8(A_TRAIT_TBL + (u32)w->ladder_rung);
        s32 d2 = (s16)(opp->spirit / 5) - (s16)(w->spirit / 5);
        p = 10 * (t + d2);
        break;
    }
    default: p = 0; break;
    }
    if (p > 1000)
        p = 1000;
    if (p < 0)
        p = 0;
    return chance_check(p);
}

// N64 0x800EC990: counts a landed move in the per-pair statistics (kind 0, 1 or 2: three
// 4 x 4 u16 tables by attacker and defender slot), saturating.
void stats_count(const Wrestler *w, const Wrestler *d, s32 kind) {
    u32 tbl;
    if (kind == 0)
        tbl = A_STATS_A;
    else if (kind == 1)
        tbl = A_STATS_B;
    else if (kind == 2)
        tbl = A_STATS_C;
    else
        return;
    u32 own = tbl + 8u * (u32)w->slot + 2u * (u32)w->slot;
    if (mem_u16(own) <= 0xFFFE)
        mem_w16(own, (u16)(mem_u16(own) + 1));
    u32 pair = tbl + 8u * (u32)w->slot + 2u * (u32)d->slot;
    if (mem_u16(pair) <= 0xFFFE)
        mem_w16(pair, (u16)(mem_u16(pair) + 1));
}

// N64 0x80122480: an illegal strike under rule 0x2000 (in the ring, on a non-partner's slot that
// shares flags_e9 0x10): under match type 0x80000 marks both (flags8c 0x1000 / 0x2000, dq_victim),
// else disqualifies the striker (end_flags 0x400) with a sound.
void strike_dq_check(Wrestler *w, const Wrestler *d) {
    if (!(g_match_rules & 0x2000))
        return;
    if (!(w->location & 1))
        return;
    if (w->partner == d->slot || w->slot == d->slot)
        return;
    if (!(w->flags_e9 & d->flags_e9 & 0x10))
        return;
    if (g_match_type & 0x80000) {
        w->flags8c |= 0x1000;
        const_cast<Wrestler *>(d)->flags8c |= 0x2000;
        w->dq_victim = d->slot;
        return;
    }
    w->end_flags |= 0x400;
    w->dq_victim = d->slot;
    host_sfx(1);
}

// N64 0x80132FFC: the timed-press tracker (A, B, R; per slot a press counter 0x80159450 and bits
// 0x80159458): op 0 clears bit 1 ("checked this frame"); op 1 records the frame's buttons: a new
// press of A / B / R restarts the counter with its bit (0x04 A, 0x10 R, 0x08 B), a held one counts
// it up, and the state adds a bit (0x100 in 51, 0x20 / 0x40 in 50 by step, 0x80 in 53); ops 2-4
// return 1 when A / B / R has been held less than 3 frames; then, unless the wrestler has flags_e9
// 0x01, a roll of kind 12-15 by the state bit decides.
s32 timed_press_check(Wrestler *w, u16 held, u16 pressed, s32 op) {
    u32 cnt = A_PRESS_COUNT + 2u * (u32)w->slot;
    u32 bits = A_PRESS_BITS + 2u * (u32)w->slot;
    s32 hit = 0;
    switch (op) {
    case 0:
        mem_w16(bits, mem_u16(bits) & 0xFFFE);
        return 0;
    case 1: {
        if (pressed & 0x8030) {
            mem_w16(cnt, 0);
            u16 b = (pressed & 0x10) ? 8 : (pressed & 0x20) ? 16 : 4;
            mem_w16(bits, b);
        }
        if (held & 0x8030) {
            s32 up = 0;
            if ((held & 0x10) && (mem_u16(bits) & 8))
                up = 1;
            else if ((held & 0x20) && (mem_u16(bits) & 0x10))
                up = 1;
            else if ((held & 0x8000) && (mem_u16(bits) & 4))
                up = 1;
            if (up)
                mem_w16(cnt, (u16)(mem_u16(cnt) + 1));
        } else {
            mem_w16(bits, 0);
        }
        mem_w16(bits, mem_u16(bits) | 1);
        u16 st = state::PreBell;
        if (w->state == state::OffBalance)
            st = 256;
        else if (w->state == state::Dazed)
            st = w->state_step == 1 ? 64 : 32;
        else if (w->state == state::KnockedBack)
            st = state::RearLiftDefender;
        mem_w16(bits, mem_u16(bits) | st);
        return 0;
    }
    case 2:
        if ((mem_u16(bits) & 8) && mem_s16(cnt) < 3)
            hit = 2;
        break;
    case 3:
        if ((mem_u16(bits) & 0x10) && mem_s16(cnt) < 3)
            hit = 2;
        break;
    case 4:
        if ((mem_u16(bits) & 4) && mem_s16(cnt) < 3)
            hit = 2;
        break;
    default:
        break;
    }
    if (w->flags_e9 & 1)
        return 0;
    if (hit == 0)
        return 0;
    u16 b = mem_u16(bits);
    if (!(b & 1))
        return 0;
    s32 kind = 12;
    if (b & 0x20)
        kind = 12;
    if (b & 0x40)
        kind = 13;
    if (b & 0x80)
        kind = 14;
    if (b & 0x100)
        kind = 15;
    return chance_roll(w, nullptr, kind) ? 1 : 0;
}

// N64 0x8012265C: which side the strike comes from: the attacker's facing (reversed for the
// moves from behind: 219, 61, 269-271, 278, 207 with record flag 0x100000, or up on the cage)
// halfway toward the direction to the defender; when the defender faces within a quarter turn of
// that it is hit from the front: flags8a 0x02, strike_flags 0x10000, hit_flags 0x2000000.
void relative_facing(const Wrestler *w, Wrestler *d, s32 from_behind) {
    d->flags8a &= 0xFFFD;
    s32 base;
    if (from_behind) {
        base = w->facing;
    } else {
        s32 m = w->move_num;
        s32 rev;
        if (m == 219 || m == 61 || (m >= 269 && m < 272) || m == 278)
            rev = 1;
        else if (m == 207)
            rev = (mem_u32(w->move + 0x20) & 0x100000) != 0;
        else
            rev = 0;
        if (!rev && (w->location & 0x400))
            rev = 1;
        base = rev ? angle_add(w->facing, 2048) : w->facing;
    }
    s32 a = angle_to(&w->point_120, &d->point_120);
    s32 diff = angle_diff(a, base);
    s32 mid = angle_add(base, diff / 2);
    if (abs_i(angle_diff(d->facing, mid)) < 1024) {
        d->flags8a |= 2;
        d->strike_flags |= 0x10000;
        d->hit_flags |= 0x2000000;
    }
}

// N64 0x801250C4: the fall animation for the hit's reaction bits (hit_flags 0x100000-0x1000000)
// in the 12565-12574 pair of the side hit from (0x2000000: + 1), + 14 below 100 health (the
// slouched fall). `fallback` is what the caller had when no bit is set.
s32 strike_reaction_anim(const Wrestler *w, s32 fallback) {
    u32 h = w->hit_flags;
    s32 a = fallback;
    if (h & 0x2000000) {
        if (h & 0x100000) a = 12566;
        if (h & 0x200000) a = 12568;
        if (h & 0x400000) a = 12570;
        if (h & 0x800000) a = 12572;
        if (h & 0x1000000) a = 12574;
    } else {
        if (h & 0x100000) a = 12565;
        if (h & 0x200000) a = 12567;
        if (h & 0x400000) a = 12569;
        if (h & 0x800000) a = 12571;
        if (h & 0x1000000) a = 12573;
    }
    if (w->health < 100)
        a += 14;
    return a;
}

// N64 0x8012201C: a blocking defender d countering attacker w's strike: refused for a partner
// (unless strike_flags 0x100000), from states that cannot be countered (by the attacker's state
// and move), or when d is on a ladder. A kick (flags8a 0x02 on d, from relative_facing) is caught:
// d to 0x68 (104) (ladder: 0x90 (144)). Otherwise d's counter_bits get the attacker's move's bit
// and the pair goes into a counter: 0x66 / 0x67 for bits 0x3800, 0x86 / 0x87 for 0x4400, 0x69 /
// 0x6A for bit 0x01 (the weak strikes), 0x64 / 0x65 otherwise; 0xCD / 0xCE from state 204, 0xBA /
// 0xBB from 183.
s32 strike_counter_check(Wrestler *d, Wrestler *w) {
    u32 beh = d->strike_flags & 0x100000;
    if (!beh && d->partner == w->slot)
        return 0;
    s32 st = w->state;
    // The attacker's state: 0 cannot be countered, 1 can when both are in the same place, 2 can.
    auto counterable = [&](s32 kick) -> s32 {
        if (st < 36) {
            if (st < 34) {
                if (st == state::RunningWeaponStrike)
                    return 1;
                if (st == state::RunningShoulderBlock)
                    return (!kick && w->move_num == 204) ? 0 : 1;
                return st == state::ApronStrikeDownToFloor? 2 : 0;
            }
            return 1;  // 34, 35
        }
        if (st == state::StrikeFromRaisedPlaceAtFloor)
            return 2;
        if (st < 192)
            return (st == state::WeaponSwingLight|| st == state::WeaponSwingHard) ? 1 : 0;
        return st == state::CageWallStrike? 2 : 0;
    };
    if (d->flags8a & 2) {
        if (d->state == state::Blocking)
            return 0;
        if (!beh) {
            s32 c = counterable(1);
            if (c == 0)
                return 0;
            if (c == 1 && !(w->location & d->location & 0x3C5))
                return 0;
        }
        host_sfx_slot(d->slot, 21);
        wrestler_set_state_now(d, state::DiveDodge);
        return 1;
    }
    if (beh) {
        wrestler_set_state_now(d, state::HitHoldingBigWeapon);
        return 1;
    }
    if (st == state::LadderTopStrike) {
        wrestler_pair_set_state_move(d, state::LadderTopShove, w, state::LadderTopYank);
        return 1;
    }
    if (d->location & 8)
        return 0;
    d->counter_bits = 0;
    if (st == state::CageWallStrike) {
        host_sfx_slot(d->slot, 278);
        wrestler_pair_set_state_move(d, state::CageYankAttacker, w, state::CageYankDefender);
        return 1;
    }
    {
        s32 c = counterable(0);
        if (c == 0)
            return 0;
        if (c == 1 && !(w->location & d->location & 0x3C5))
            return 0;
    }
    if ((u32)(w->move_num - 174) < 2)
        d->counter_bits |= 1;
    {
        s32 m = w->move_num;
        u32 bit = 0;
        if (m == 190) bit = 8;
        else if (m == 108) bit = 4096;
        else if (m == 73) bit = 16384;
        else if (m == 107) bit = 2048;
        else if (m == 186) bit = 2;
        else if (m == 109) bit = 8192;
        else if (m == 188) bit = 4;
        else if (m == 198) bit = 128;
        else if (m == 194) bit = 32;
        else if (m == 192) bit = 16;
        else if (m == 196) bit = 64;
        else if (m == 202) bit = 512;
        else if (m == 200) bit = 256;
        else if (m == 219) bit = 1024;
        d->counter_bits |= bit;
    }
    host_sfx_slot(d->slot, 21);
    if (d->counter_bits & 0x3800) {
        wrestler_pair_set_state_move(d, state::WeaponTakeAwayAttacker, w, state::WeaponTakeAwayDefender);
        return 1;
    }
    if (d->counter_bits & 0x4400) {
        wrestler_pair_set_state_move(d, state::GrappleFromFloorAttacker, w, state::GrappleFromFloorDefender);
        d->flags78 = (d->counter_bits & 0x400) ? 0x4020000 : 0x4000000;
        return 1;
    }
    stats_count(d, w, 1);
    if (d->counter_bits & 1)
        wrestler_pair_set_state_move(d, state::CaughtLegHolder, w, state::CaughtLegHeld);
    else
        wrestler_pair_set_state_move(d, state::StrikeCounterAttacker, w, state::StrikeCounterDefender);
    return 1;
}

// N64 0x801227A4: after a landed move, whether the defender bleeds (rule 0x800: the move's bleed
// class, the defender's bleed trait, not a partner, chance_roll kind 4) or staggers toward a TKO
// (the move's TKO class, chance_roll kind 5; not on the apron nor in finisher mode): sets
// tko_flags and the state 0x2E (46) or 0x2F (47) by `how` (0 from a strike, 1 / 2 from a move).
s32 move_aftermath_check(const Wrestler *w, Wrestler *att, Wrestler *d, s32 how) {
    d->move_link = (s16)mem_u16(A_MOVE_ANIMS + 1096u * (u32)w->move_slot + 2u * (u32)(u16)w->move_num);
    u32 f = 0;
    if ((g_match_rules & 0x800) && mem_s8(w->move + 9) != 0 && attr(d, 2) != 0 && att->partner != d->slot &&
        att->slot != d->slot && chance_roll(att, d, 4)) {
        if (g_match_rules & 0x1000) {
            u8 a = att->flags_e9, b = d->flags_e9;
            if ((a & b & 0x10) || ((a & 4) && (b & 0x10)))
                f = ((g_match_type & 0x80000) ? 0 : 0x80) | 2;
            else if (!(b & 0x10) && !(d->bonus_flags & 2))
                f |= 2;
        } else if (!(d->bonus_flags & 2)) {
            f = 2;
        }
    }
    if (mem_s8(w->move + 10) != 0 && !(d->location & 2) && !(d->flags_ee & 4) && chance_roll(att, d, 5)) {
        f |= 1;
        if (att->flags_ee & 4)
            f |= 0x20;
        if (g_match_rules & 1) {
            if (g_match_type & 0x80000)
                f |= 0x40;
            else if ((d->location & 0x3C4) && !(g_match_rules & 0x4000))
                f |= 0x40;
            else if (!(att->flags_e9 & d->flags_e9 & 0x10))
                f |= 0x40;
            else if (att->partner == d->slot)
                f |= 0x40;
            else if (att->slot == d->slot)
                f |= 0x40;
        } else {
            f |= 0x40;
        }
    }
    if (!(f & 3))
        return 0;
    if (how == 0) {
        if (d->state == state::SlumpedInCorner)
            f |= 4;
    } else if (how == 1) {
        f |= 8;
    } else if (how == 2) {
        u8 pose = d->anim_player[AP_END_POSE];
        d->end_pose = pose;
        if ((s8)pose >= 20) {
            f |= 8;
            if (pose & 1)
                d->flags8a |= 1;
            else
                d->flags8a &= 0xFFFE;
        }
    }
    d->tko_flags = (u16)f;
    wrestler_set_state_free_now(d, (f & 2) ? state::BleedingStagger : state::TechnicalKnockoutStagger);
    return 1;
}

// N64 0x80120D98: who strike / grapple kind `kind` (0-31) could reach, into out[] (-1 ended; up
// to 10): by kind an exclude mask over flags80 | location << 16, an include mask and one state
// always accepted; the loaded wrestlers other than itself (kind 0 skips the partner, 24 the
// object's last_hit), then (except kinds 25 / 26) standing ladders near in height as 100 + index.
// The current opponent is moved to the front.
void hit_candidates(const Wrestler *w, s32 obj, s32 kind, s32 *out) {
    for (s32 i = 0; i < 11; i++)
        out[i] = -1;
    u32 ex_loc = 0, ex_f80 = 0;
    u32 inc = 0xFFFFFFFFu;
    s32 accept = -1;
    s32 frame = w->anim_frame, last = w->anim_player[AP_HOLD_FRAME];
    u16 loc = w->location;
    switch (kind) {
    case 0:
        if (w->flags78 & 8) {
            ex_loc = 195; ex_f80 = 26; inc = 32;
        } else {
            ex_loc = 3;
            if (w->flags78 & 0x20) {
                ex_f80 = 6;
            } else {
                accept = 82;
                if (loc & 1)
                    ex_f80 = 4;
                else if (loc & 0x3C0)
                    ex_f80 = 4;
            }
        }
        break;
    case 1: case 3: case 21: case 22: case 25: case 28: case 29: case 30:
        ex_loc = 13;
        if (loc & 1)
            ex_f80 = 4;
        else if (loc & 4)
            ex_f80 = 961;
        accept = 9;
        break;
    case 2: case 4:
        ex_loc = 5; inc = 8;
        break;
    case 5: case 8: case 9: case 12: case 31:
        ex_loc = 5;
        if (frame < last || (mem_u32(w->move + 0x20) & 0x100))
            ex_loc = 13;
        else if (frame == last)
            inc = 8;
        if (loc & 1)
            ex_f80 = 1028;
        else if (loc & 4)
            ex_f80 = 3;
        accept = 130;
        break;
    case 6: case 7: case 11: case 14:
        ex_loc = 5;
        if (frame < last)
            inc = 0x80000;
        else if (frame == last)
            inc = 8;
        if (loc & 1)
            ex_f80 = 4;
        else if (loc & 4)
            ex_f80 = 3;
        break;
    case 10: case 13:
        ex_loc = 13;
        if (loc & 1)
            ex_f80 = 4;
        else if (!(loc & 2))
            ex_f80 = 3;
        break;
    case 15: case 16:
        ex_loc = 5;
        if (loc & 1)
            ex_f80 = 4;
        else if (loc & 4)
            ex_f80 = 3;
        accept = 130;
        break;
    case 17: ex_loc = 13; ex_f80 = 6; break;
    case 18: ex_loc = 5; ex_f80 = 5; break;
    case 19: ex_loc = 13; ex_f80 = 3; break;
    case 20: ex_loc = 517; inc = 256; break;
    case 23: ex_loc = 13; break;
    case 24: ex_loc = 13; accept = 9; break;
    case 26: ex_loc = 13; inc = 0x80000; break;
    case 27: ex_loc = 13; ex_f80 = 963; break;
    default: break;
    }
    u32 ex = ex_loc | (ex_f80 << 16);
    s32 n = 0;
    for (s32 s = 0; s < 4; s++) {
        const Wrestler *o = &g_wrestlers[s];
        if (s == w->slot || o->roster_id == -1)
            continue;
        if (kind == 24 && s == mem_s32(0x8016AE10 + 180 * obj + 0x10))
            continue;
        u32 word = (u32)o->flags80 | ((u32)o->location << 16);
        if (o->state != accept) {
            if (word & ex)
                continue;
            if (!(word & inc))
                continue;
        }
        if (kind == 0 && s == w->partner)
            continue;
        out[n++] = s;
    }
    if ((u32)(kind - 25) >= 2) {
        for (s32 j = 0; j < 7; j++) {
            u32 ob = 0x8016AE10 + 180u * (u32)j;
            if (mem_s32(ob + 4) != 25)
                continue;
            if ((mem_u32(ob + 0x20) & 0x41040000u) != 0x1040000u)
                continue;
            f32 dy = absf(mem_f32(ob + 0x48) - w->pos.y);
            if ((f64)dy < 150.0 || kind == 24)
                out[n++] = 100 + j;
        }
    }
    if (out[0] != -1) {
        for (s32 i = 0; out[i] != -1; i++) {
            if (out[i] != w->opponent)
                continue;
            if (i == 0)
                break;
            s32 t = out[0];
            out[0] = out[i];
            out[i] = t;
            break;
        }
    }
}

// N64 0x8012069C: the target a strike of kind `kind` reaches this frame: a slot, 100 + an object,
// or -1. Only inside the move's active window (anim_player +8 to +9; not for a thrown object, kind
// 24). For each candidate: kinds 2 / 4 take the current opponent in the same place, 20 / 23 any
// wrestler; the others need the two bodies within 460 and an attacking joint (the move's joint
// lists, kinds 5-16 / 28 / 31; the move's joint bytes 13-15 for the rest; the weapon tip for 3 /
// 24) within the summed joint radii of the target's joints 5 and 1 (an object: 60) and inside the
// target's height window.
s32 strike_hit_test(Wrestler *w, s32 obj, s32 kind) {
    if (kind != 24) {
        if (w->anim_frame < w->anim_player[8] || w->anim_player[9] < w->anim_frame)
            return -1;
    }
    s32 cand[11];
    hit_candidates(w, obj, kind, cand);
    Vec3 p0;
    f32 tip_radius = 0.0f;
    if (kind == 24)
        tip_radius = weapon_tip_point(w, obj, &p0);
    else
        p0 = w->point_120;
    for (s32 ci = 0; cand[ci] != -1; ci++) {
        s32 t = cand[ci];
        const Wrestler *o = nullptr;
        Vec3 tp;
        f32 low, high;
        f32 obj_reach = 60.0f;
        if (t < 100) {
            o = &g_wrestlers[t];
            tp = o->point_120;
            f32 m = 10000.0f;
            for (s32 j = 0; j < 19; j++) {
                f32 y = wrestler_joint(o, j)->y;
                if (y < m)
                    m = y;
            }
            low = m;
            high = m + mem_f32(A_HEIGHT_WINDOW + 112u * (u32)t);
        } else {
            u32 ob = 0x8016AE10 + 180u * (u32)(t - 100);
            tp.x = mem_f32(ob + 0x44);
            tp.y = mem_f32(ob + 0x48);
            tp.z = mem_f32(ob + 0x4C);
            high = (f32)((f64)tp.y + 126.0);
            low = (f32)((f64)tp.y - 126.0);
        }
        if ((u32)(kind - 1) >= 31)
            continue;
        s32 path;  // 0: opponent in place, 1: any wrestler, 2: joint lists, 3: move joints
        switch (kind) {
        case 2: case 4: path = 0; break;
        case 20: case 23: path = 1; break;
        case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
        case 28: case 31: path = 2; break;
        default: path = 3; break;
        }
        if (path == 0) {
            if (t < 100 && t == w->opponent && (w->location & o->location & 0x3C5))
                return t;
            continue;
        }
        if (path == 1) {
            if (t < 100)
                return t;
            continue;
        }
        if (!((f64)dist_to(&p0, &tp) <= 460.0))
            continue;
        u32 tgt_radius = A_JOINT_RADIUS + 112u * (u32)t;
        s32 rows = 3;
        if (path == 3 && (kind == 3 || kind == 24))
            rows = 1;
        for (s32 r = 0; r < rows; r++) {
            s32 jk = mem_s8(w->move + 13 + r);
            // The attacking joints of this row.
            s8 single[2] = {(s8)jk, -1};
            const s8 *list = single;
            u32 lst = 0;
            if (path == 2) {
                lst = mem_u32(A_JOINT_LISTS + 4u * (u32)jk);
                if (mem_s8(lst) == -1)
                    continue;
            }
            for (s32 k = 0;; k++) {
                s32 j;
                if (path == 2) {
                    j = mem_s8(lst + (u32)k);
                    if (j == -1)
                        break;
                } else {
                    if (k > 0)
                        break;
                    j = list[0];
                }
                Vec3 jp;
                f32 reach;
                if (path == 3 && kind == 3) {
                    reach = weapon_tip_point(w, w->held_obj, &jp);
                } else if (path == 3 && kind == 24) {
                    jp = p0;
                    reach = tip_radius;
                } else {
                    jp = *wrestler_joint(w, j - 1);
                    reach = mem_f32(A_JOINT_RADIUS + 112u * (u32)w->slot + 4u * (u32)j);
                }
                if (t < 100) {
                    for (s32 ti = 0;; ti++) {
                        s32 tj = mem_s8(A_TARGET_JOINTS + (u32)ti);
                        if (tj == -1)
                            break;
                        f32 d = dist_to(&jp, wrestler_joint(o, tj - 1));
                        f32 lim = reach + mem_f32(tgt_radius + 4u * (u32)tj);
                        if (d <= lim && jp.y <= high && low <= jp.y)
                            return t;
                    }
                } else {
                    f32 d = dist_to(&jp, &tp);
                    if (d <= reach + obj_reach && jp.y <= high && low <= jp.y)
                        return t;
                }
            }
        }
    }
    return -1;
}

// N64 0x80121330: strike `flags` from w lands on `target` (a slot, or 100 + an object, which is
// knocked away). A wrestler is hit unless in a hold without flags80 0x200 or staying down; it
// takes the strike (opponent, strike_flags, weapon dropped, the side it was hit from, the move
// copied), the reaction bit by the move's reaction kind; a blocking defender may counter (within
// 3 frames: strike_counter_check) or hold the block (0x4D) unless the strike is unblockable;
// states 50 / 51 / 53 may counter with the timed R press; then the statistics, the DQ check and
// where it goes: a ladder (178), the cage wall, the apron (45 with a direction bit), or the ring:
// the aftermath (bleeding / TKO), hits_taken, the hit bits, and a weighted reaction (stagger 45
// or knockdown 53) from the tables by health band, place and strike flags.
void strike_land(Wrestler *w, s32 target, u32 flags) {
    if (target >= 100) {
        s32 kind = (flags & 0x4418) ? 4 : 5;
        if (flags & 0x106)
            kind = 6;
        if (flags & 0x200)
            kind = w->state == state::WeaponSwingHard ? 8 : 7;
        u32 op = 0x8016AE10 + 180u * (u32)(target - 100) + 0x44;
        Vec3 pos = {mem_f32(op), mem_f32(op + 4), mem_f32(op + 8)}, dir;
        vec_from_angle(angle_to(&w->point_120, &pos), 1.0f, &dir);
        object_set_motion(target - 100, kind, &dir);
        return;
    }
    Wrestler *d = &g_wrestlers[target];
    if ((d->flags80 & 0xC0) && !(d->flags80 & 0x200))
        return;
    if (d->stay_flags & 5)
        return;
    w->hits_taken = 0;
    d->pair_other = w->slot;
    if (w->partner != d->slot)
        d->opponent = w->slot;
    d->strike_flags = flags;
    d->hit_flags = 0;
    weapon_release(d, 0);
    if (d->strike_flags & 0x100000) {
        relative_facing(w, d, 1);
        d->hit_flags |= 0x4000000;
    } else {
        relative_facing(w, d, 0);
        hit_move_copy(w, d);
    }
    switch (d->hit_move.reaction) {
    case 0: d->hit_flags |= 0x100000; break;
    case 1: d->hit_flags |= 0x200000; break;
    case 2: case 5: d->hit_flags |= 0x400000; break;
    case 6: d->hit_flags |= 0x1000000; break;
    default: d->hit_flags |= 0x800000; break;
    }
    s32 st = d->state;
    s32 sfx_kind = 0;
    if (st == state::Blocking) {
        if (!(d->flags_e9 & 1)) {
            if (d->count_timer < 3 && strike_counter_check(d, w))
                return;
            if (!(d->strike_flags & 0x100600)) {
                if (!(d->flags8a & 2)) {
                    if (!((d->strike_flags & 0x40) && (d->location & 0x10))) {
                        wrestler_set_state_now(d, state::BlockHit);
                        if (d->location & 8)
                            d->state_step = 4;
                        return;
                    }
                }
            }
        }
    } else if (st == state::BlockHit) {
        if (!(d->flags_e9 & 1) && !(d->strike_flags & 0x100600) && !(d->flags8a & 2)) {
            if (!(d->strike_flags & 0x40))
                return;
            if (!(d->location & 0x10))
                return;
        }
    } else if (st == state::Dazed|| st == state::OffBalance|| st == state::KnockedBack) {
        if (timed_press_check(d, 0, 0, 2) && strike_counter_check(d, w))
            return;
    } else if (st == state::DrapedOverRopes) {
        d->strike_flags |= 0x80000;
    } else if (st == state::HeldUpsideDown) {
        d->strike_flags |= 0x40000;
    } else if (st == state::HitCageWall) {
        d->strike_flags |= 0x400000;
    } else if (st == state::SlumpedInCorner) {
        if (d->frame_flags & 1)
            d->strike_flags |= 0x20000;
    } else if (st == state::Taunt) {
        wrestler_add_spirit(d, -10);
    } else if (st == state::ApronDodge) {
        if ((d->strike_flags & 0x360B) && (w->location & 1))
            return;
    }
    if (d->strike_flags & 0x100600) {
        if (d->strike_flags & 0x600)
            object_sfx(w->slot, w->held_obj, 0);
        sfx_kind = 2;
    }
    stats_count(w, d, sfx_kind);
    w->flags8e &= 0xFFBF;
    if (d->strike_flags & 0x100600)
        strike_dq_check(w, d);
    if (d->location & 8) {
        wrestler_set_state_free_now(d, state::LadderHit);
        if (!(d->strike_flags & 0x61))
            d->state_counter |= 1;
        if (d->strike_flags & 0x4418)
            d->state_counter |= 2;
        if ((d->strike_flags & 0x200) && w->state == state::WeaponSwingHard)
            d->state_counter |= 2;
        return;
    }
    if (d->location & 0x400) {
        if (d->strike_flags & 0x800000)
            w->flags_f7 |= 2;
        return;
    }
    if (d->location & 2) {
        u32 f = d->strike_flags;
        u32 dir = 0;
        if (f & 1)
            dir = chance_check(700) ? 512 : 1024;
        if (f & 0xE0)
            dir = chance_check(800) ? 512 : 1024;
        if (f & 0x100306)
            dir = 1024;
        if (f & 0x4418) {
            dir = 2048;
            if (w->location & 4)
                dir = 1024;
        }
        if (dir == 0)
            dir = 1024;
        d->hit_flags |= dir;
        wrestler_set_state_free_now(d, state::KnockedDown);
        return;
    }
    if (!(d->strike_flags & 0x140000)) {
        if (move_aftermath_check(w, w, d, 0))
            return;
    }
    if (!(d->strike_flags & 0x100000)) {
        d->hits_taken = (u8)(d->hits_taken + 1);
        if ((s8)d->hits_taken >= 5)
            d->hits_taken = 4;
    }
    hits_taken_decay(d, 0);
    u32 sf = d->strike_flags;
    if ((sf & 0x20010) == 0x20010)
        d->hit_flags |= 0x10020;
    if ((sf & 0x10010) == 0x10 && !(d->hit_flags & 0x20))
        d->hit_flags |= 0x80;
    if ((sf & 0x44000) == 0x44000)
        d->hit_flags |= 2;
    if ((sf & 0x10008) == 8)
        d->hit_flags |= 0x80;
    if ((sf & 2) && d->hit_move.fall != 0)
        d->hit_flags |= 0x8000;
    if (sf & 0x8000) {
        d->hit_flags |= 0x40;
        if (chance_check(500))
            d->hit_flags |= 0x10000;
    }
    if ((sf & 0x70400) == 0x400)
        d->hit_flags |= 0x100;
    if (sf & 0x1000) {
        if (sf & 0x20000)
            d->hit_flags |= 0x10;
        else
            d->strike_flags = (sf & ~0x1000u) | 1;
    }
    sf = d->strike_flags;
    if (sf & 0x2000) {
        if (sf & 0x40000)
            d->hit_flags |= 1;
        else
            d->strike_flags = (sf & ~0x2000u) | 1;
    }
    sf = d->strike_flags;
    if (sf & 0x80000) {
        if (sf & 0x20) {
            d->strike_flags &= 0xFFF7FFFFu;
        } else {
            if ((sf & 1) && !chance_roll(d, nullptr, 18)) {
                wrestler_set_state_now(d, state::DrapedOverRopes);
                d->state_step = 6;
                return;
            }
            wrestler_set_state_now(d, state::KnockedOverTopRope);
            d->state_counter |= 1;
            return;
        }
    }
    if (d->hit_flags & 0x10000) {
        wrestler_set_state_free_now(d, state::KnockedBack);
        return;
    }
    sf = d->strike_flags;
    if ((sf & 0x20000) && (sf & 0x20))
        d->hit_flags |= 0xC;
    if ((sf & 0x40000) && !(d->hit_flags & 1))
        d->hit_flags |= 2;
    if ((sf & 0x400000) && (mem_u16(d->grab_zone_info + 40) & 4)) {
        d->hit_flags |= (sf & 1) ? 0x2000 : 0x4000;
        d->state_step = 4;
        return;
    }
    if (d->hit_flags & 0x81F7) {
        wrestler_set_state_free_now(d, state::KnockedDown);
        return;
    }
    if (sf & 0x4418) {
        if ((sf & 0x100) && d->hit_move.reaction == 4)
            d->hit_flags |= 0x40000;
        else
            d->hit_flags |= 0x20000;
        wrestler_set_state_free_now(d, state::KnockedDown);
        return;
    }
    if (d->hit_move.reaction == 4) {
        d->hit_flags |= 0x40000;
        wrestler_set_state_free_now(d, state::KnockedDown);
        return;
    }
    if (sf & 0x1000000) {
        if (w->flags_ee & 4) {
            if (chance_check(300)) {
                d->hit_flags |= 0x40000;
                wrestler_set_state_free_now(d, state::KnockedDown);
            } else if (chance_check(850)) {
                d->hit_flags |= 0x10000;
                wrestler_set_state_free_now(d, state::KnockedBack);
            } else {
                d->hit_flags |= 0x20000;
                wrestler_set_state_free_now(d, state::KnockedDown);
            }
        } else if (chance_check(800)) {
            d->hit_flags |= 0x40000;
            wrestler_set_state_free_now(d, state::KnockedDown);
        } else {
            d->hit_flags |= 0x10000;
            wrestler_set_state_free_now(d, state::KnockedBack);
        }
        return;
    }
    s32 h = wrestler_health_eff(d);
    u32 band = 0;
    while (band < 4 && h < mem_s16(A_HEALTH_BANDS + 2 * band))
        band++;
    u32 row;
    if (sf & 0x40) {
        row = 0x80146980 + 4 * band;
    } else if (d->flags80 & 0x20) {
        if (sf & 0x100202) row = 0x80146910 + 4 * band;
        else if (sf & 4) row = 0x80146920;
        else if (sf & 1) row = 0x80146900 + 4 * band;
        else row = 0x80146924;
    } else if (d->state == state::OffBalance) {
        if (sf & 0x100202) row = 0x80146938 + 4 * band;
        else if (sf & 4) row = 0x80146948;
        else if (sf & 1) row = 0x80146928 + 4 * band;
        else row = 0x8014694C;
    } else if (d->state == state::Dazed) {
        if (sf & 0x100202) row = 0x80146968 + 4 * band;
        else if (sf & 4) row = 0x80146978;
        else if (sf & 1) row = 0x80146950 + 6 * band;
        else row = 0x8014697C;
    } else {
        if (sf & 0x100202) row = 0x801468E8 + 4 * band;
        else if (sf & 4) row = 0x801468F8;
        else if (sf & 1) row = A_REACT_TBL + 16u * (u32)(s8)d->hits_taken + 4 * band;
        else row = 0x801468FC;
    }
    s32 r = rand_pool_next();
    s32 pick = 0;
    for (;;) {
        s32 e = mem_s16(row + 2u * (u32)pick);
        if (!(e < r))
            break;
        r -= e;
        pick++;
        if (pick >= 4)
            break;
    }
    if (sf & 0x100306)
        pick++;
    if (pick == 0) {
        d->hit_flags |= 0x40000;
        wrestler_set_state_free_now(d, state::KnockedDown);
    } else if (pick == 1) {
        d->hit_flags |= 0x10000;
        wrestler_set_state_free_now(d, state::KnockedBack);
    } else if (pick == 2) {
        d->hit_flags |= 0x20000;
        wrestler_set_state_free_now(d, state::KnockedDown);
    } else {
        wrestler_set_state_free_now(d, state::KnockedDown);
    }
}

// N64 0x80122B18: keeps a striker inside its place with scale 2 (joints 24782; in state 26 past
// the player's hold frame joints 14).
void strike_keep_in_bounds(Wrestler *w) {
    if (w->state == state::CornerReboundStrike) {
        if (!(w->anim_player[AP_HOLD_FRAME] < w->anim_frame))
            return;
        keep_in_bounds_self(w, 2.0f, 0, 14);
        return;
    }
    keep_in_bounds_self(w, 2.0f, 0, 24782);
}

// N64 0x8012C0D8: the strong strike's push against the other bodies: scale 2 up to the player's
// hold frame; past it scale 4, with flags8a 0x6000 while the end pose is 20-47 or 2.
void strike_slide(Wrestler *w) {
    f32 scale;
    s32 exclude = -1;
    if (!(w->anim_player[AP_HOLD_FRAME] < w->anim_frame)) {
        scale = 2.0f;
    } else {
        scale = 4.0f;
        u8 pose = w->anim_player[AP_END_POSE];
        if ((u32)(pose - 20) < 28 || pose == 2)
            w->flags8a |= 0x6000;
    }
    wrestler_move_step(w, nullptr, scale, 0, exclude, 1);
}

// N64 0x800EB970: whether `opp` is in reach `reach` of w's step vector (point_120 + step_vec), for
// a running / standing ground attack: the opponent's body as a lying body when it is down (or
// mode 1 with flags8a 0x2000), else a 33-unit disc at its facing; *angle gets the direction to it.
s32 running_ground_attack_check(Wrestler *w, Wrestler *opp, f32 reach, s32 *angle, s32 mode) {
    Body b;
    if ((opp->flags80 & 8) || ((opp->flags8a >> 13) & 1) || mode == 1) {
        lying_body_axis(opp, &b);
    } else {
        b.rx = 33.0f;
        b.ry = 0.0f;
        b.rz = 33.0f;
        b.angle = opp->facing;
        b.center = opp->point_120;
    }
    f32 d = lying_body_dist(&b, &w->point_120);
    f32 out[9];
    segment_distance(&w->point_120, &b.center, out);
    if (angle)
        *angle = (s32)out[7];
    return (out[4] < d + reach) ? 1 : 0;
}

// N64 0x801301F4: whether `w`'s animation is one of the dazed-turn list (0x8014DAC0, 0 ended):
// *angle gets its facing plus the player's turn when it is, else its facing. Returns 1 when found.
s32 dazed_turn_target(const Wrestler *w, s32 *angle) {
    for (u32 a = 0x8014DAC0;; a += 2) {
        s16 v = mem_s16(a);
        if (v == 0) {
            if (angle)
                *angle = w->facing;
            return 0;
        }
        if (v == w->anim_id) {
            if (angle) {
                u32 turn = (u32)w->anim_player[0] << 24 | (u32)w->anim_player[1] << 16 | (u32)w->anim_player[2] << 8 |
                           w->anim_player[3];
                *angle = angle_add(w->facing, (s32)turn);
            }
            return 1;
        }
    }
}

// N64 0x8013E6C0: B on the apron: the d-pad toward the ring (0x8014D220 by zone) gives 31, away
// (0x8014D218) 32; else at an opponent in the ring 31, elsewhere 32; against an opponent on the
// apron the strike over the ropes 33 (flags8a 0x04 by the side).
void strike_across_ropes(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u32 z = (u32)w->ring_zone;
    s32 state;
    if (mem_u16(A_ROPE_BUTTONS_A + 2 * z) & ctx->buttons) {
        state = 31;
    } else if (mem_u16(A_ROPE_BUTTONS_B + 2 * z) & ctx->buttons) {
        state = 32;
    } else {
        u16 ol = ctx->rec_a->location;
        if (ol & 1) {
            state = 31;
        } else if (ol & 0x3C4) {
            state = 32;
        } else if (ol & 2) {
            w->flags8a &= 0xFFFB;
            if (angle_diff(w->facing, w->facing_to_opponent) <= 0)
                w->flags8a |= 4;
            state = 33;
        } else {
            return;
        }
    }
    wrestler_set_state_now(w, state);
}

// N64 0x8013E4C4: B held from standing: in a place shared with the opponent, a downed opponent
// (flags80 0x08) within 120 and facing it within a sixth of a turn gives 40 (with a weapon) or 97;
// a dazed or lying one (stay_flags 0x05, or flags80 0x40 without 0x200) within 120 and facing
// within a quarter turn gives 41; else the strike 34 (flags8a 0x80 from state 27). On the apron
// the strike across the ropes; up on the cage 204 at an opponent in the ring.
s32 input_cup_opponent(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a &= 0xFF7F;
    if (!(ctx->buttons & 0x4000))
        return 0;
    Wrestler *a = ctx->rec_a;
    u16 loc = w->location;
    if (loc & 0x3C5) {
        s32 state;
        if (loc & a->location & 0x3C5) {
            s32 angle;
            if (a->flags80 & 8) {
                if (running_ground_attack_check(w, a, 120.0f, &angle, 0)) {
                    dazed_turn_target(a, nullptr);
                    angle = abs_i(angle_diff(w->facing, angle));
                    if (angle < 341) {
                        state = (w->flags_ef & 1) ? 40 : 97;
                        wrestler_set_state_now(w, state);
                        return 1;
                    }
                }
            }
            if (w->location & a->location & 0x3C5) {
                if (running_ground_attack_check(w, a, 120.0f, &angle, 1)) {
                    angle = abs_i(angle_diff(angle, w->facing));
                    if (angle < 512) {
                        if ((a->stay_flags & 5) || (a->flags80 & 0x240) == 0x40) {
                            wrestler_set_state_now(w, state::BreakUpPinOrHold);
                            return 1;
                        }
                    }
                }
            }
        }
        if (w->state == state::Taunt)
            w->flags8a |= 0x80;
        wrestler_set_state_now(w, state::Strike);
        return 1;
    }
    if (loc & 2) {
        strike_across_ropes(ctx);
        return 1;
    }
    if (loc & 0x400) {
        if (a->location & 1) {
            wrestler_set_state_now(w, state::CageWallStrike);
            return 1;
        }
        return 0;
    }
    return 0;
}

// N64 0x800EE4D4: the sound of landing against the arena segment `zone` (its +0x24 kind, +0x26
// bits, +0x28 flags): a table of four by the segment's class.
s32 zone_kind(u32 zone) {
    u16 bits = mem_u16(zone + 0x26);
    s32 k;
    if (bits & 8) {
        u16 t = mem_u16(zone + 0x24);
        if ((u32)(t - 25) < 3) k = 1;
        else if ((u32)(t - 46) < 3) k = 2;
        else if ((u32)(t - 51) < 3) k = 2;
        else k = 0;
    } else if (bits & 0x10) {
        k = 1;
    } else {
        u16 t = mem_u16(zone + 0x24);
        if ((u32)(t - 196) < 4 || (u32)(t - 370) < 3 || (u32)(t - 151) < 2 || (u32)(t - 156) < 4)
            k = 1;
        else
            k = (mem_u16(zone + 0x28) & 0x2000) ? 0 : 3;
    }
    return mem_s16(A_ZONE_SOUNDS + 2u * (u32)k);
}

// N64 0x800EBBF4: the point on the apron line of the wrestler's zone nearest its body.
void apron_line_pos(const Wrestler *w, Vec3 *out) {
    out->x = w->point_120.x;
    out->z = w->point_120.z;
    switch (w->ring_zone) {
    case 0: out->z = 265.0f; break;
    case 1: out->x = 265.0f; break;
    case 2: out->z = -265.0f; break;
    case 3: out->x = -265.0f; break;
    default: break;
    }
}

// N64 0x801202F4: whether a knockdown against the wall goes along it: from the wall's normal
// (zone_info +0x18 / +0x20) the angle away from it; with hit_flags 0x2000000 (hit from the front)
// within 227 of the facing, or without it beyond 1821, dir gets the side of that angle. Returns 1
// then.
s32 knockdown_dir_from_zone(Wrestler *w) {
    s32 away = angle_add(vec_xz_angle(mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 32)), 2048);
    s32 diff = abs_i(angle_diff(away, w->facing));
    s32 front = (w->hit_flags >> 25) & 1;
    if ((front && diff < 227) || (!front && !(diff < 1821))) {
        w->dir = (s16)angle_to_side(away);
        return 1;
    }
    return 0;
}

// N64 0x800ED6E8: the end of a move: the pose class the player ends in (anim_player +7) decides
// the state: 0-2, 13, 49 stand 0x30 with flags8a 0x10; 3 -> 51; 4 -> 50; 7 / 8 -> 24 step 3 (with
// flags8a bit 0 by the pose's low bit); 10 -> 52 step 1; 20-47 lying -> 49 (flags8a bit 0 by the
// low bit, flags80 0x10 for 44 / 45; down_hit_count and down_hit_kind cleared); else stand 1.
void wrestler_settle_pose(Wrestler *w) {
    u8 pose = w->anim_player[AP_END_POSE];
    w->end_pose = pose;
    s32 p = (s8)pose;
    if ((u32)p < 50) {
        switch (p) {
        case 0: case 1: case 2: case 13: case 49:
            w->flags8a |= 0x10;
            wrestler_set_state_now(w, state::SettleAfterAnimation);
            return;
        case 3: wrestler_set_state_now(w, state::OffBalance); return;
        case 4: wrestler_set_state_now(w, state::Dazed); return;
        case 7: case 8:
            if (w->end_pose & 1)
                w->flags8a &= 0xFFFE;
            else
                w->flags8a |= 1;
            wrestler_set_state_now(w, state::SlumpedInCorner);
            w->state_step = 3;
            return;
        case 10:
            wrestler_set_state_now(w, state::HeldUpsideDown);
            w->state_step = 1;
            return;
        default: break;
        }
    }
    if ((u32)(w->end_pose - 20) < 28) {
        if (w->end_pose & 1)
            w->flags8a |= 1;
        else
            w->flags8a &= 0xFFFE;
        if ((u32)(w->end_pose - 44) < 2)
            w->flags80 |= 0x10;
        else
            w->flags80 &= 0xFFEF;
        w->down_hit_count = 0;
        w->down_hit_kind = 0;
        wrestler_set_state_now(w, state::LyingDown);
        return;
    }
    wrestler_set_state_now(w, state::Standing);
}

// ---- the states ----

// N64 0x80123288: the A / B action start (state 0x22). Step 0 clears the input history; step 1
// walks and turns toward the opponent unless flags8a 0x80 (held over from state 27 while the
// countdown runs) or on the apron, then reads the press kind: 0x0C held becomes the grapple
// attempt 0x50 (step 1); a hold the strong strike 0x23 (or 0x27 with a weapon; across the ropes
// on the apron); a tap picks by place: across the ropes on the apron, 0xC9 at an opponent up on
// the cage (close, facing), 0x26 with a weapon, 0xBF from a raised place at one on the floor,
// 0x24 at one facing away, 0x2B / 0x2C at one slumped (frame_flags 0x01 / 0x08) in the ring; else
// the weak strike (move 172 / 174 by distance, + 1 with a direction) with its style animation,
// mode 3 (step 2). Step 2: turns toward the opponent before the active window (22 a frame in its
// first half, then 11), the hit test (strike_land on a hit, step 3), the slide, bounds and move_event.
void state_22_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        input_history_clear(ctx->slot);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1: {
        if (w->flags8a & 0x80) {
            if (!(w->anim_frames_left > 0))
                w->flags8a &= 0xFF7F;
        }
        if (!(w->flags8a & 0x80) && !(w->location & 2)) {
            walk_step(ctx, ctx->buttons, 2);
            wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        }
        input_press_kind(ctx->slot, &w->press_kind);
        u8 pk = w->press_kind;
        if (!(pk & 3))
            return;
        if ((pk & 0xC) == 0x4) {
            wrestler_set_state_now(w, state::GrappleAttempt);
            w->state_step = 1;
            return;
        }
        if (pk & 1) {
            if (w->location & 2) {
                strike_across_ropes(ctx);
                return;
            }
            wrestler_set_state_now(w, (w->flags_ef & 1) ? state::WeaponSwingHard : state::StrongStrike);
            return;
        }
        u16 loc = w->location;
        if (loc & 2) {
            strike_across_ropes(ctx);
            return;
        }
        Wrestler *a = ctx->rec_a;
        if ((loc & 1) && (a->location & 0x400) && (f64)w->opp_distance < 100.0 &&
            abs_i(angle_diff(mem_s32(A_SIDE_FACINGS + 4u * (u32)a->ring_zone), w->facing_to_opponent)) < 1024) {
            wrestler_set_state_now(w, state::StrikeAtOpponentOnCageWall);
            return;
        }
        if (w->flags_ef & 1) {
            wrestler_set_state_now(w, state::WeaponSwingLight);
            return;
        }
        if ((loc & 0x3C0) && (a->location & 4) && (f64)w->opp_distance < 160.0 &&
            abs_i(angle_diff(w->facing_to_opponent, w->facing)) < 1024) {
            wrestler_set_state_now(w, state::StrikeFromRaisedPlaceAtFloor);
            return;
        }
        if ((a->flags80 & 0x20) && (w->location & a->location & 0x3C5) &&
            !(abs_i(angle_diff(w->facing, a->facing)) < 1537)) {
            wrestler_set_state_now(w, state::StrikeAtReturningWhippedRunner);
            return;
        }
        if ((a->frame_flags & 1) && (w->location & a->location & 1) && (f64)w->opp_distance < 180.0) {
            wrestler_set_state_now(w, state::StrikeAtOpponentSlumpedInCorner);
            return;
        }
        if ((a->frame_flags & 8) && (w->location & a->location & 1) && (f64)w->opp_distance < 180.0) {
            wrestler_set_state_now(w, state::StrikeAtOpponentHeldUpsideDown);
            return;
        }
        s32 move = ((f64)w->opp_distance < 100.0) ? 172 : 174;
        wrestler_start_move(w, ctx->slot, move + ((w->press_kind & 0x20) ? 1 : 0));
        wrestler_set_anim(w, anim_style(w, 0), 3);
        w->state_step = w->state_step + 1;
        return;
    }
    case 2: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        u8 active = w->anim_player[AP_HIT_FROM];
        if (w->anim_frame < (active >> 1))
            wrestler_turn_toward(w, w->facing_to_opponent, 22, 0);
        else if (w->anim_frame < active)
            wrestler_turn_toward(w, w->facing_to_opponent, 11, 0);
        s32 t = strike_hit_test(w, 0, 1);
        if (t != -1) {
            w->state_step = w->state_step + 1;
            strike_land(w, t, 1);
        }
        [[fallthrough]];
    }
    case 3:
        wrestler_slide(ctx->self, 2.0f);
        strike_keep_in_bounds(ctx->self);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x801237A4: the strong strike (state 0x23): step 0 picks the move (190 with A+B held, 188
// with a direction, else 186) and goes to step 2 (run again this frame); step 1 (the counter-strike)
// uses the move's follow-up; step 2 starts the style animation (mode 3) and runs on into step 3:
// the turn toward the opponent before the hit window, the hit test (strike_land with flags 2),
// then (and step 4) the slide, bounds and move_event.
void state_23_strong_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 move;
        if (w->press_kind & 0x10)
            move = 190;
        else if (w->press_kind & 0x20)
            move = 188;
        else
            move = 186;
        wrestler_start_move(w, ctx->slot, move);
        wrestler_set_step(w, 2);
        return;
    }
    case 1:
        wrestler_start_move(w, ctx->slot, mem_s16(w->move + 4));
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 2:
        wrestler_set_anim(w, anim_style(w, 0), 3);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 3: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        u8 active = w->anim_player[AP_HIT_FROM];
        if (w->anim_frame < (active >> 1))
            wrestler_turn_toward(w, w->facing_to_opponent, 22, 0);
        else if (w->anim_frame < active)
            wrestler_turn_toward(w, w->facing_to_opponent, 11, 0);
        s32 t = strike_hit_test(w, 0, 1);
        if (t != -1) {
            w->state_step = w->state_step + 1;
            strike_land(w, t, 2);
        }
        [[fallthrough]];
    }
    case 4:
        strike_slide(ctx->self);
        strike_keep_in_bounds(ctx->self);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80124AE4: knocked down by a strike (state 0x2D). Step 0: the damage (unless hit_flags
// 0x80000), then the fall animation: a weapon hit (strike_flags 0x800) by the attacker's move
// number (12626 / 12565 and hit bits, or a 10 % chance in the ring of 53 with bits 0x190000);
// else hit_flags 0x40000 picks the stagger by the move's reaction (12531-12575), 0x20000 / 0x40
// the strike_reaction_anim fall, 0x8000 the fall by hit_move +0x17, 0x80 its 12628 variant, 0x20
// 12064, 0x4 + 0x8 12064 with bit 0x2000000 flipped, 0x10 the knockdown roll (kind 16: 12062),
// 0x1 the roll of kind 17 (12079 / 12080), 0x2 12080, 0x100 12629, 0x8000000 the wall fall 12868
// with the zone's sound, 0x200 the apron fall 12907 (facing along the side, placed on the line);
// mode 2 with the direction bits 0x13 or 0x201C3, else 3 with + 1 for a front hit. Step 1: past
// 3/4 of the player's frames when lying, flags8a 0x2000; animations 12079 / 12062-12063 set
// frame_flags 0x08 / 0x01, 12575-12576 and 12598-12599 flags8c 0x20; without direction bits the
// slide, the bounds (in the ring with hit bits 0x281C0 in the first 5 frames, not in the cage, a
// wall knockdown goes to 112), then move_event with the end pose.
void state_2D_knocked_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        if (!(w->hit_flags & 0x80000))
            apply_damage(ctx->rec_c, w);
        s32 anim = 0;
        s32 mode = 3;
        if (w->strike_flags & 0x800) {
            // The attacker's (pair_other, rec_c) weapon move: 0 the fall by the record's flag
            // 0x40, 1 the heavy fall, 3 the knockdown 53, 2 the fall chosen before.
            s32 m = ctx->rec_c->move_num;
            s32 kind = 2;
            if (m == 61 || m == 75 || m == 256 || m == 257 || m == 263 || m == 268 || (m >= 271 && m <= 273) ||
                m == 275 || (m >= 278 && m <= 281))
                kind = 0;
            else if (m == 265 || m == 266)
                kind = 1;
            else if (m == 261 || m == 262 || m == 264 || m == 269 || m == 274)
                kind = 3;
            if (kind == 0) {
                w->hit_flags |= 0x20000;
                if (mem_u32(ctx->rec_c->move + 0x20) & 0x40) {
                    anim = 12626;
                    w->hit_flags |= 0x400000;
                } else {
                    anim = 12565;
                    w->hit_flags |= 0x100000;
                }
            } else if (kind == 1) {
                anim = 12626;
                w->hit_flags |= 0x420000;
            }
            if (kind == 3 || ((w->location & 1) && chance_check(100))) {
                w->hit_flags |= 0x190000;
                wrestler_set_state_now(w, state::KnockedBack);
                return;
            }
            anim += (w->hit_flags >> 25) & 1;
            wrestler_set_anim(w, anim, 3);
            w->state_step = w->state_step + 1;
            return;
        }
        if (w->hit_flags & 0x40000) {
            switch (w->hit_move.reaction) {
            case 1: anim = 12533; break;
            case 2: anim = 12535; break;
            case 3: anim = 12537; break;
            case 4: anim = 12575; break;
            case 5: anim = 12539; break;
            default: anim = 12531; break;
            }
        }
        if (w->hit_flags & 0x20040)
            anim = strike_reaction_anim(w, anim);
        u32 h = w->hit_flags;
        if (h & 0x8000)
            anim = 2 * w->hit_move.fall + 12596;
        if (h & 0x80)
            anim = w->hit_move.fall + 12628;
        if (h & 0x20)
            anim = 12064;
        if ((h & 4) && (h & 8)) {
            anim = 12064;
            if (h & 0x2000000)
                w->hit_flags = h & 0xFDFFFFFFu;
            else
                w->hit_flags = h | 0x2000000;
        }
        if (w->hit_flags & 0x10)
            anim = chance_roll(w, nullptr, 16) ? 12064 : 12062;
        if (w->hit_flags & 1)
            anim = chance_roll(w, nullptr, 17) ? 12080 : 12079;
        h = w->hit_flags;
        if (h & 2)
            anim = 12080;
        if (h & 0x100)
            anim = 12629;
        if (h & 0x8000000) {
            anim = 12868;
            host_sfx_slot(w->slot, (s16)zone_kind(w->zone_info));
        }
        h = w->hit_flags;
        if (h & 0x200) {
            w->facing = mem_s32(A_SIDE_FACINGS + 4u * (u32)w->ring_zone);
            apron_line_pos(w, &w->pos);
            wrestler_set_anim(w, 12907, 2);
            w->state_step = w->state_step + 1;
            return;
        }
        if (h & 0x1C00) {
            wrestler_set_state_now(w, state::KnockedOverTopRope);
            return;
        }
        if (!(h & 0x201C3))
            anim += (h >> 25) & 1;
        mode = (h & 0x13) ? 2 : 3;
        wrestler_set_anim(w, anim, mode);
        w->state_step = w->state_step + 1;
        return;
    }
    case 1: {
        if (pose_lying(w)) {
            s32 lim = (s32)((f64)w->anim_player[AP_FRAMES] * 0.75);
            if (!(w->anim_frame < lim))
                w->flags8a |= 0x2000;
        }
        if (w->anim_id == 12079)
            w->frame_flags |= 8;
        if ((u32)(w->anim_id - 12062) < 2)
            w->frame_flags |= 1;
        if ((u32)(w->anim_id - 12575) < 2 || (u32)(w->anim_id - 12598) < 2)
            w->flags8c |= 0x20;
        if (!(w->hit_flags & 0x13)) {
            wrestler_slide(w, 4.0f);
            if (!(w->hit_flags & 0x13)) {
                if (keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE) != 0) {
                    if ((w->location & 1) && (w->hit_flags & 0x281C0) && w->anim_frame < 5 &&
                        !(g_match_type & 0x10000) && knockdown_dir_from_zone(w)) {
                        wrestler_set_state_now(w, state::KnockedOverTopRope);
                        return;
                    }
                }
            }
        }
        move_event(ctx, 512);
        return;
    }
    default:
        return;
    }
}

// N64 0x800E866C: off balance after being countered (state 0x33): the slide (4), bounds, the
// timed press, then step 0 plays 12562 (mode 1), step 1 waits for the countdown then plays 12563,
// step 2 waits for the countdown then stands.
void state_33_off_balance(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    wrestler_slide(w, 4.0f);
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
    timed_press_check(w, ctx->buttons, ctx->pressed, 1);
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12562, 1);
        w->state_step = w->state_step + 1;
        return;
    case 1:
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, 12563, 1);
        w->state_step = w->state_step + 1;
        return;
    case 2:
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_state_now(w, state::Standing);
        return;
    default:
        return;
    }
}

// N64 0x800E873C: knocked back (state 0x35): step 0 the damage (unless hit_flags 0x80000) and the
// stagger animation (12060; 12867 into a wall with its sound; 12541 for 0x1300000 bits; 12545 /
// 12543 for 0x400000), + 1 for a front hit, mode 3. Step 1: the timed press, the slide (4), and
// unless strike_flags 0x20000 the bounds (joints 16514): against a wall, in the ring within 10
// frames an 80 % chance past 5 frames of a counter-strike (23) when the wall is in front, else the
// wall knockdown 112 (not in a cage), a free corner the slump 24 (flags8a bit 0 by the corner's
// side), or (segment flag 0x400) 193 when the wall is mostly behind; then move_event with the end
// pose.
void state_35_knocked_back(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        if (!(w->hit_flags & 0x80000))
            apply_damage(ctx->rec_c, w);
        u32 h = w->hit_flags;
        s32 anim = 12060;
        if (!(h & 0x20)) {
            if (h & 0x8000000) {
                anim = 12867;
                host_sfx_slot(w->slot, (s16)zone_kind(w->zone_info));
            } else if (h & 0x1300000) {
                anim = 12541;
            } else if (h & 0x400000) {
                anim = 12543;
            } else {
                anim = 12545;
            }
        }
        wrestler_set_anim(w, anim + ((w->hit_flags >> 25) & 1), 3);
        w->state_step = w->state_step + 1;
        return;
    }
    case 1: {
        timed_press_check(w, ctx->buttons, ctx->pressed, 1);
        wrestler_slide(w, 4.0f);
        if (w->strike_flags & 0x20000) {
            move_event(ctx, 512);
            return;
        }
        if (keep_in_bounds_self(w, 2.0f, 0, 16514) == 0) {
            move_event(ctx, 512);
            return;
        }
        Vec3 fwd;
        vec_from_angle(w->facing, 1.0f, &fwd);
        Vec3 normal = {mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 28), mem_f32(w->zone_info + 32)};
        f32 dot = vec_normalize_two(&normal, &fwd);
        if ((w->location & 1) && w->anim_frame < 11) {
            if (!(w->anim_frame < 5) && chance_check(800) && 0.93969262 < (f64)dot) {
                wrestler_set_state_now(w, state::DrapedOverRopes);
                return;
            }
            if (!(g_match_type & 0x10000)) {
                if (knockdown_dir_from_zone(w)) {
                    wrestler_set_state_now(w, state::KnockedOverTopRope);
                    return;
                }
            }
            s32 corner = nearest_corner(&w->point_120, 0);
            w->dir = (s16)corner;
            if (corner != -1 && corner_free(w)) {
                s32 d = abs_i(angle_diff(mem_s32(A_CORNER_ANGLES + 4u * (u32)corner), w->facing));
                if (!((u32)(d - 768) < 513)) {
                    if (d < 768)
                        w->flags8a &= 0xFFFE;
                    else
                        w->flags8a |= 1;
                    w->flags86 = 0;
                    wrestler_set_state_now(w, state::SlumpedInCorner);
                    return;
                }
            }
        }
        if (mem_u16(w->zone_info + 40) & 0x400) {
            if (0.707106781 < (f64)absf(dot)) {
                wrestler_set_state_now(w, state::HitCageWall);
                if ((f64)dot < 0.0)
                    w->state_counter |= 1;
                return;
            }
        }
        move_event(ctx, 512);
        return;
    }
    default:
        return;
    }
}

// N64 0x800E8A70: blocking with R (state 0x4C; steps 5-7 on a ladder). Steps 0-4: flags8a 0x100,
// the slide (4) and bounds (scale 1, joints 2). Step 0 starts the block pose (anim_pick 2) and
// clears count_timer; step 1 turns toward the opponent and counts it up (cap 100), holds 5 frames,
// then releasing R (or a pause) plays the release pose (anim_pick 1, step 2, count_timer 100);
// step 2 turns and after 106 stands; step 3 is the kick caught (12105, flags8a 0x100 off); step
// 4 the hit taken through the block: move_event 71 (the damage of generic move 7). Steps 5-7 guard on
// the ladder's top (ladder_pose; 12990, R or a falling ladder ends it, back to 0xB1).
void state_4C_block(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->state_step < 5) {
        w->flags8a |= 0x100;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 1.0f, 0, 2);
    } else {
        if (ladder_pose(w))
            return;
    }
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, anim_pick(ctx, 2, 0), 3);
        w->count_timer = 0;
        w->state_step = w->state_step + 1;
        return;
    case 1:
        wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 5)
            return;
        if (!((s16)w->count_timer < 100))
            w->count_timer = 100;
        if ((ctx->buttons & 0x10) && match_can_interrupt() == 0)
            return;
        wrestler_set_anim(w, anim_pick(ctx, 1, 0), 3);
        w->count_timer = 100;
        w->state_step = w->state_step + 1;
        return;
    case 2:
        wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 106)
            return;
        wrestler_set_state_now(w, state::Standing);
        return;
    case 3:
        w->flags8a &= 0xFEFF;
        wrestler_set_anim(w, 12105, 3);
        w->state_step = w->state_step + 1;
        return;
    case 4:
        w->flags8a &= 0xFEFF;
        move_event(ctx, 71);
        return;
    case 5:
        wrestler_set_anim(w, 12990, 6);
        w->count_timer = 0;
        w->state_step++;
        return;
    case 6:
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 5)
            return;
        if (!((s16)w->count_timer < 100))
            w->count_timer = 100;
        if ((ctx->buttons & 0x10) && !match_can_interrupt() &&
            !(mem_u16(0x8016AE10 + 180u * (u32)w->held_obj + 0x20) & 0x4000))
            return;
        wrestler_set_anim(w, 12989, 6);
        w->count_timer = 100;
        w->state_step++;
        return;
    case 7:
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 106)
            return;
        wrestler_set_state_now(w, state::OnLadderTop);
        return;
    default:
        return;
    }
}

// N64 0x80123058: states 0x1F / 0x20 / 0x21, a strike from the apron (into the ring, down to the
// floor, along the apron; strike_across_ropes picks one). Step 0: the move (218 / 219, 0x21 220 or
// 221 the other way round, flags8a 0x04) and its animation. Step 1: the hit test (kinds 17 / 19 /
// 18), on a hit step 2 and strike_land (flags 0x20 / 0x40 / 0x80). Every step: the 2.0 slide, kept in
// bounds, and the animation's events.
void state_1F_apron_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    s32 st = w->state;
    switch (w->state_step) {
    case 0: {
        s32 move = st == state::ApronStrikeIntoRing? 218 : st == state::ApronStrikeDownToFloor? 219 : ((w->flags8a & 4) ? 221 : 220);
        wrestler_start_move(w, ctx->slot, move);
        wrestler_set_anim(w, anim_style(w, 0), 2);
        w->state_step++;
        [[fallthrough]];
    }
    case 1: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 kind = st == state::ApronStrikeIntoRing? 17 : st == state::ApronStrikeDownToFloor? 19 : 18;
        s32 t = strike_hit_test(w, 0, kind);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, st == state::ApronStrikeIntoRing? 0x20u : st == state::ApronStrikeDownToFloor? 0x40u : 0x80u);
        }
        [[fallthrough]];
    }
    case 2:
        wrestler_slide(w, 2.0f);
        keep_in_bounds_self(w, 2.0f, 0, 14);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80122D24: state 0x07, the running strike (shoulder block and kin). Step 0 the move: both in
// the ring at an opponent draped (flags80 0x20 with flags86 0x28) or in state 24, 200; at one with
// frame_flags 0x08, 204; else 196 (192 with flags86 0x04); +2 with A in the input entry's +0x0A
// (not for 204); then step 2. Step 1: the move's follow-up (record +4), facing the opponent. Step 2:
// its animation; when nobody has flags8c 0x10 yet, both legal, the opponent not down and within 200
// in the same place (ring and ring or apron, or both on the floor) and faced within 114, flags8c
// 0x10. Step 3 the hit test (kind 1): strike_land with 0x10 for 200 / 202, 0x4000 for 204, else 0x08.
// Then the strike's slide, bounds and events.
void state_07_running_shoulder_block(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0: {
        s32 move;
        if ((w->location & o->location & 1) &&
            (((o->flags80 & 0x20) && (o->flags86 & 0x28) == 0x28) || o->state == state::SlumpedInCorner))
            move = 200;
        else if ((w->location & o->location & 1) && (o->frame_flags & 8))
            move = 204;
        else
            move = (w->flags86 & 4) ? 192 : 196;
        if (move != 204 && (ctx->in_0a & 0x8000))
            move += 2;
        wrestler_start_move(w, ctx->slot, move);
        wrestler_set_step(w, 2);
        return;
    }
    case 1:
        wrestler_start_move(w, ctx->slot, mem_s16(w->move + 4));
        w->facing = w->facing_to_opponent;
        w->state_step++;
        [[fallthrough]];
    case 2: {
        wrestler_set_anim(w, anim_style(w, 0), 1);
        bool taken = false;
        for (s32 i = 0; i < 4; i++)
            if (g_wrestlers[i].roster_id != -1 && (g_wrestlers[i].flags8c & 0x10))
                taken = true;
        if (!taken) {
            bool place = ((w->location & 1) && (o->location & 3)) || ((w->location & 4) && (o->location & 4));
            if (place && (w->flags_e9 & o->flags_e9 & 0x10) && !(o->flags80 & 8) && (f64)w->opp_distance <= 200.0 &&
                abs_i(angle_diff(w->facing, w->facing_to_opponent)) < 114)
                w->flags8c |= 0x10;
        }
        w->state_step++;
        [[fallthrough]];
    }
    case 3: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 t = strike_hit_test(w, 0, 1);
        if (t != -1) {
            w->state_step++;
            s32 m = w->move_num;
            u32 f = (m == 200 || m == 202) ? 0x10u : (m == 204 ? 0x4000u : 0x8u);
            strike_land(w, t, f);
        }
        [[fallthrough]];
    }
    case 4:
        strike_slide(w);
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80123BD4: states 0x28 / 0x61 / 0x62, the ground attack at a downed opponent (0x28 with a
// weapon, 0x61 bare, 0x62 from the run). Step 0 the move: 0x62 with a weapon the weapon's animation
// 12494 + 5 * family and generic move 104; 0x28 12493 + 5 * family and generic move 110; else the
// move from 0x801469B0 (0x801469B8 for 0x62; s16 x4: +4 at a face-down opponent, flags80 0x10, which
// also sets state_counter 0x01; +2 with its flags8a 0x01). The opponent's escape grows by the
// player's hold frame. Step 1 the hit test (kind 4 with a weapon, else 2): the hit wrestler takes the
// move (hit_move_copy; a weapon's sound and the DQ check, stats kind 2), pairs with the attacker
// and goes to 0x63; the opponent's escape shrinks by 30 less the frames left (at least 0) and
// hits_taken clears. Steps 1-2 the slide: with nobody in reach 8.0 past the hold frame; at a lying
// wrestler 8.0 from the hold frame on, or 4.0 before it when state_counter 0x01; anything else 4.0 with
// flags8a 0x6000. Then flags8a 0x6000, bounds and events.
void state_28_weapon_ground_attack(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 st = w->state;
        if (st == state::RunningGroundAttack&& (w->flags_ef & 1)) {
            wrestler_set_anim(w, 5 * wrestler_move_anim(w, st) + 12494, 3);
            wrestler_set_generic_move(w, 104);
        } else if (st == state::WeaponGroundAttack) {
            wrestler_set_anim(w, 5 * wrestler_move_anim(w, st) + 12493, 3);
            wrestler_set_generic_move(w, 110);
        } else {
            u32 e = st == state::RunningGroundAttack? 0x801469B8u : 0x801469B0u;
            if (ctx->rec_a->flags80 & 0x10) {
                e += 4;
                w->state_counter |= 1;
            }
            if (ctx->rec_a->flags8a & 1)
                e += 2;
            wrestler_start_move(w, ctx->slot, mem_s16(e));
            wrestler_set_anim(w, anim_style(w, 0), 3);
        }
        ctx->rec_a->escape = (s16)(ctx->rec_a->escape + w->anim_player[AP_HOLD_FRAME]);
        w->state_step++;
        return;
    }
    case 1: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 t = strike_hit_test(w, 0, w->state == state::WeaponGroundAttack ? 4 : 2);
        if (t != -1) {
            Wrestler *d = &g_wrestlers[t];
            hit_move_copy(w, d);
            s32 kind = 0;
            if (w->state == state::WeaponGroundAttack) {
                object_sfx(w->slot, w->held_obj, 0);
                kind = 2;
                strike_dq_check(w, d);
            }
            stats_count(w, ctx->rec_a, kind);
            d->pair_other = ctx->slot;
            d->opponent = ctx->slot;
            wrestler_set_state_now(d, state::HitWhileDown);
            s32 left = (s32)w->anim_player[4] - w->anim_frame;
            ctx->rec_a->escape = (s16)(ctx->rec_a->escape - (left < 30 ? 30 - left : 0));
            w->hits_taken = 0;
            w->state_step++;
        }
        [[fallthrough]];
    }
    case 2: {
        s32 hit = wrestler_translate_pushed(w, nullptr, -1);
        bool past = w->anim_player[AP_HOLD_FRAME] < w->anim_frame;
        if (hit == -1) {
            if (past)
                wrestler_slide(w, 8.0f);
        } else if (hit < 4 && (g_wrestlers[hit].flags80 & 8)) {
            if (!(w->anim_frame < w->anim_player[AP_HOLD_FRAME]))
                wrestler_slide(w, 8.0f);
            else if (w->state_counter & 1)
                wrestler_slide(w, 4.0f);
        } else {
            w->flags8a |= 0x6000;
            wrestler_slide(w, 4.0f);
        }
        w->flags8a |= 0x6000;
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    }
    default:
        return;
    }
}

// N64 0x80123FDC: states 0x26 / 0x27, a swing of the held weapon (0x26 light, 0x27 hard). Step 0
// 12490 / 12491 + 5 x the weapon's family, generic move 107 / 108. Step 1 the hit test (kind 3, the
// weapon tip): a hit lands with strike flags 0x200 (step 2). Steps 1-2 bounds and the events.
void state_26_weapon_swing(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 k = 5 * wrestler_move_anim(w, 0);
        bool hard = w->state == state::WeaponSwingHard;
        wrestler_set_anim(w, k + (hard ? 12491 : 12490), 3);
        wrestler_set_generic_move(w, hard ? 108 : 107);
        w->state_step++;
        return;
    }
    case 1: {
        s32 t = strike_hit_test(w, 0, 3);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 0x200);
        }
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    }
    case 2:
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012394C: state 0x24, the strike at a whipped opponent coming back (move 205), flags8a 0x6000
// throughout: step 0 the move; step 1 the hit test (kind 1; a hit lands with strike flags 0x04, step
// 2); steps 1-2 the slide (4.0), bounds and the events.
void state_24(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a |= 0x6000;
    switch (w->state_step) {
    case 0:
        wrestler_start_move(w, w->slot, 205);
        wrestler_set_anim(w, anim_style(w, 0), 3);
        w->state_step++;
        [[fallthrough]];
    case 1: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 t = strike_hit_test(w, 0, 1);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 4);
        }
        [[fallthrough]];
    }
    case 2:
        wrestler_slide(w, 4.0f);
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}


// N64 0x80124974: state 0xCC, a strike from the cage wall: step 0 by where the opponent is against
// the wall's side angle (0x8014D1D8 by ring zone): 12852 / 12854 to either side, 12853 beyond 1821
// (behind), with generic move anim - 12787; step 1 the hit test (kind 30; strike flags 0x1000000,
// step 2); steps 1-2 at the end back to the hang (12804, state 1).
void state_CC_cage_climb_idle(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 d = angle_diff(mem_s32(0x8014D1D8 + 4u * (u32)w->ring_zone), w->facing_to_opponent);
        s32 anim = abs_i(d) >= 1821 ? 12853 : (d >= 0 ? 12854 : 12852);
        wrestler_set_generic_move(w, anim - 12787);
        wrestler_set_anim(w, anim, 14);
        w->state_step++;
        return;
    }
    case 1: {
        g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        s32 t = strike_hit_test(w, 0, 30);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 0x1000000);
        }
        [[fallthrough]];
    }
    case 2:
        if (w->anim_frames_left == 1) {
            wrestler_set_anim(w, 12804, 14);
            wrestler_set_state_now(w, state::Standing);
        }
        return;
    default:
        return;
    }
}

// N64 0x800E8D28: state 0x4D, a hit taken through the block (anim_pick 3). Step 1 near the end: the
// damage of generic move 3 goes to spirit (spirit_exchange), and back to the block (0x4C step 1, at 100). Steps
// 2-3 the same with no spirit, from a blocked run. Step 4 on a ladder's top (ladder_pose, 12991): generic move
// 3 hurts it (apply_damage from itself), back to the ladder's guard (12990, 0x4C step 6).
void state_4D_block_hit(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    auto back_to_block = [&](s32 step) {
        wrestler_set_state_now(w, state::Blocking);
        w->count_timer = 100;
        w->state_step = step;
    };
    switch (w->state_step) {
    case 0:
    case 2:
        wrestler_set_anim(w, anim_pick(ctx, 3, 0), 1);
        w->state_step++;
        return;
    case 1:
        keep_in_bounds_self(w, 1.0f, 0, 2);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_generic_move(w, 3);
        hit_move_copy(w, w);
        spirit_exchange(w, ctx->rec_a, w);
        wrestler_set_anim(w, anim_pick(ctx, 2, 0), 3);
        back_to_block(1);
        return;
    case 3:
        keep_in_bounds_self(w, 1.0f, 0, 2);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, anim_pick(ctx, 2, 0), 3);
        back_to_block(1);
        return;
    case 4:
        if (ladder_pose(w))
            return;
        wrestler_set_anim(w, 12991, 6);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_generic_move(w, 3);
        hit_move_copy(w, w);
        apply_damage(w, w);
        wrestler_set_anim(w, 12990, 6);
        back_to_block(6);
        return;
    default:
        return;
    }
}

}  // namespace nm

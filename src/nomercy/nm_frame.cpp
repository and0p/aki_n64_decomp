// The match frame: match_tick and the per-frame match rules around the wrestler updates (run-ins,
// the pin count, the clocks, the count-out, the fall causes).
#include "nm_funcs.h"

namespace nm {

// N64 0x80138E80
void match_flags_clear(s32 *p, s32 a, s32 b) {
    p[0] = a;
    p[1] = b;
    p[2] = 0;
}

// N64 0x80138E30: one frame on a (minutes, seconds, frames) clock; 1 when a second passed.
s32 clock_tick(s32 *c, s32 frames_per_second) {
    if (++c[2] < frames_per_second)
        return 0;
    c[2] = 0;
    if (++c[1] < 60)
        return 1;
    c[1] = 0;
    c[0]++;
    return 1;
}

// N64 0x800EE064: some wrestler in the match has flags8c 0x8000.
s32 any_wrestler_flag8c_8000() {
    for (s32 i = 0; i < 4; i++) {
        if (g_wrestlers[i].roster_id != -1 && (g_wrestlers[i].flags8c & 0x8000))
            return 1;
    }
    return 0;
}

// N64 0x8011D5D0: brings slot `slot` in as side `side`'s ally when the side's run-in is armed.
s32 run_in_try(s32 slot, s32 side) {
    if (any_wrestler_flag8c_8000())
        return 0;
    if (g_match_flags[1] & 0x40)
        return 0;
    Wrestler *w = &g_wrestlers[slot];
    if (w->roster_id != -1)
        return 0;
    if (!(w->flags_e9 & 2))
        return 0;
    u32 cond = (g_match_type & 0x20000) ? mem_u32(0x800AA07C) : mem_u32(0x800AA078 + 4 * side);
    u16 arm_bit = mem_u16(0x801465C0 + 2 * side);
    s32 go = 0;
    if (mem_u16(A_RUNIN_FLAGS) & arm_bit) {
        if (cond == 0)
            go = 1;
        else if (cond & 0x100)
            go = !((g_match_clock[0] * 60 + g_match_clock[1]) < (s32)(cond & 0xFF));
        else if (cond & 0x200)
            go = mem_s32(A_ARENA) == (s32)(cond & 0xFF);
    }
    if (!go)
        return 0;
    mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) & ~arm_bit);
    Wrestler *ally_of = &g_wrestlers[side];
    w->roster_id = ally_of->ally_id;
    ally_of->flags_f1 |= 0x08;
    wrestler_set_state_now(w, state::RunIn);
    w->state_step = 13;
    return 1;
}

// N64 0x8011D400: the run-in rolls, then the elimination re-entry of wrestlers on the floor.
void run_in_check() {
    if ((g_match_type & 0x1000) && (g_match_rules & 0x200)) {
        s32 a, b;
        if (chance_check(500)) {
            if (run_in_try(2, 0))
                return;
            a = 3;
            b = 1;
        } else {
            if (run_in_try(3, 1))
                return;
            a = 2;
            b = 0;
        }
        if (run_in_try(a, b))
            return;
    }
    if ((g_match_type & 0x20000) && run_in_try(3, 2))
        return;
    if (!(g_match_type & 0x4000))
        return;
    if (any_wrestler_flag8c_8000())
        return;
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        if (!(w->flags_f1 & 1) || !(w->location & 4))
            continue;
        if (!(w->state == state::Standing || w->state == state::LyingDown))
            continue;
        w->flags_f1 &= 0xFE;
        w->end_flags &= 0xFFFE;
        wrestler_set_state_now(w, state::RunIn);
        if (!(g_match_rules & 0x40000))
            w->state_step = 4;
    }
}

// N64 0x80116378
void pin_count_reset(s32 slot) {
    mem_w32(A_PIN_SLOT, (u32)-1);
    mem_w32(A_PIN_SHOWN, 0);
    s32 c[3];
    match_flags_clear(c, 0, 0);
    for (s32 i = 0; i < 3; i++)
        mem_w32(A_PIN_COUNTS + 12 * slot + 4 * i, (u32)c[i]);
}

// N64 0x801163C4: the referee's pin count, one count a second while the pinned wrestler has
// flags8a 0x400; three counts give end_flags 0x08.
void referee_count_tick() {
    s32 slot = mem_s32(A_PIN_SLOT);
    if (slot != -1) {
        Wrestler *w = &g_wrestlers[slot];
        if (g_match_type & 0x80000) {
            if (w->flags8a & 0x400)
                goto find;
            host_hud("hud_count_hide", 0, 0);
            pin_count_reset(slot);
            goto find;
        }
        if (w->flags8a & 0x400) {
            s32 c[3];
            for (s32 i = 0; i < 3; i++)
                c[i] = mem_s32(A_PIN_COUNTS + 12 * slot + 4 * i);
            clock_tick(c, 30);
            for (s32 i = 0; i < 3; i++)
                mem_w32(A_PIN_COUNTS + 12 * slot + 4 * i, (u32)c[i]);
            if (mem_s32(A_PIN_SHOWN) == c[1])
                return;
            mem_w32(A_PIN_SHOWN, (u32)c[1]);
            host_hud("hud_count_show", (s16)c[1], 0);
            if (w->health < 50)
                host_sfx_slot(slot, mem_s16(0x8014631C + 4 * mem_s32(A_PIN_SHOWN) + 66));
            if (mem_s32(A_PIN_SHOWN) == 3) {
                w->end_flags |= 8;
                pin_count_reset(slot);
            }
            return;
        }
        if (mem_s32(A_PIN_COUNTS + 12 * slot + 4) == 2 && mem_s32(A_PIN_COUNTS + 12 * slot + 8) >= 20) {
            host_hud("hud_near_fall_message", 0, 0);
            if (w->health < 50) {
                host_sfx_slot(slot, 73);
                host_sfx_slot(slot, -1);
            }
        }
        host_hud("hud_count_hide", 0, 0);
        pin_count_reset(mem_s32(A_PIN_SLOT));
        return;
    }
find:
    for (s32 i = 0; i < 4; i++) {
        if (!(g_wrestlers[i].flags8a & 0x400))
            continue;
        if ((g_match_type & 0x80000) || !(mem_u8(A_COUNTOUT) & 2)) {
            mem_w32(A_PIN_SLOT, (u32)i);
            return;
        }
    }
}

// N64 0x80111F18: moves each wrestler's damage history on one entry (once a second).
void damage_history_tick() {
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        w->damage_hist_pos = w->damage_hist_pos + 1;
        if (w->damage_hist_pos >= 30)
            w->damage_hist_pos = 0;
        mem_w8(w->damage_history + 2 * w->damage_hist_pos, 0);
        mem_w8(w->damage_history + 2 * w->damage_hist_pos + 1, 0);
    }
}

// N64 0x80116AC4: the clocks, the minute run-in roll, the time limit. Non-zero ends play.
s32 match_clock_tick() {
    clock_tick(g_run_clock, 30);
    if (clock_tick(g_match_clock, 30)) {
        damage_history_tick();
        if ((g_match_rules & 0x200) && g_match_clock[0] >= 10) {
            s32 chance = 500;
            bool roll = true;
            if (g_match_type & 0x400000) {
                if (mem_u16(A_RUNIN_FLAGS) & 0x400)
                    roll = false;
                else {
                    mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) | 0x400);
                    chance = 250;
                }
            }
            if (roll && chance_check(chance))
                mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) | 0x30);
        }
    }
    s8 limit = mem_s8(A_TIME_LIMIT);
    if (limit == 0) {
        if (g_match_clock[0] == 10000)
            match_flags_clear(g_match_clock, 10000, 0);
        if (g_run_clock[0] == 60)
            match_flags_clear(g_run_clock, 0, 0);
        host_hud("hud_clock_draw", (s16)g_run_clock[1], (s16)g_run_clock[0]);
        return 0;
    }
    // A time limit (option 1-5: 5 / 10 / 15 / 30 / 60 minutes, 0x80146574): the HUD shows the match
    // clock, counting down when falls are counted (rule 0x80000: ironman). At the limit a match type
    // 0x2000000 only flags the end (match flag 0x10); any other match is decided by match_result
    // (0x8000: a draw, a decision or the falls), whose result ends play. Read from the asm
    // 2026-11-03, not trace-checked.
    s32 minutes = mem_s8(A_TIME_LIMITS + (u32)limit);
    s16 secs, mins;
    if (g_match_rules & 0x80000) {
        s32 left = minutes - g_match_clock[0];
        s32 sec = 60 - g_match_clock[1];
        if (sec == 60) {
            secs = 0;
            mins = (s16)left;
        } else {
            secs = (s16)sec;
            mins = (s16)(left - 1);
        }
    } else {
        secs = (s16)g_match_clock[1];
        mins = (s16)g_match_clock[0];
    }
    host_hud("hud_clock_draw", secs, mins);
    if (g_match_clock[0] != minutes)
        return 0;
    if (g_match_type & 0x2000000) {
        g_match_flags[1] |= 0x10;
        return 1;
    }
    host_sfx(1);
    return match_result(0x8000, 0);
}

namespace {

// The count-out clock (minutes, seconds, frames) in the RAM image, copied for clock_tick.
void countout_clock_get(s32 c[3]) {
    for (s32 i = 0; i < 3; i++)
        c[i] = mem_s32(A_COUNTOUT_CLOCK + 4u * (u32)i);
}
void countout_clock_put(const s32 c[3]) {
    for (s32 i = 0; i < 3; i++)
        mem_w32(A_COUNTOUT_CLOCK + 4u * (u32)i, (u32)c[i]);
}

}  // namespace

// N64 0x801166B8: the count-out. Op 0 (set-up): count-outs on (g_countout bit 0) when the option
// (0x800A9462) is not 0, then op 1. Op 1 (restart): the count's clock cleared, counts left from the
// option (0x80146568: 0 / 10 / 20), the number shown 0, g_countout bit 1 off, the HUD. Op 3 (one
// count): the clock cleared, shown + 1, left - 1, the HUD number, bit 1 on once something was
// counted. Op 2 (every frame from match_tick): with count-outs on, in a ring arena (g_arena < 10),
// while a legal man (flags_e9 0x10) is off the ring and apron (location & 3 == 0) and play is not
// paused (match_can_interrupt): with counts left, outside guest-referee matches (the referee counts
// there) and while no pin is being counted, the clock runs and a count comes every 2 seconds (6 or
// more left), 3 (3-5 left) or 4 (fewer); with none left it is the count-out: under rule 0x80000
// (falls counted) the bell and a fall for every legal man still inside whose opponent side has a
// legal man outside, then the count restarts; otherwise match_result(0x80000), with the bell when it
// returns 1 (its result is returned). Nobody outside: op 1 every frame. Read from the asm
// 2026-11-03, not trace-checked.
s32 countout_tick(s32 op) {
    switch (op) {
    case 0:
        mem_w8(A_COUNTOUT, mem_s8(A_COUNTOUT_OPTION) != 0 ? 1 : 0);
        countout_tick(1);
        return 0;
    case 1: {
        s32 c[3];
        match_flags_clear(c, 0, 0);
        countout_clock_put(c);
        s32 left = mem_s32(A_COUNTOUT_COUNTS + 4u * (u32)mem_s8(A_COUNTOUT_OPTION));
        mem_w32(A_COUNTOUT_SHOWN, 0);
        mem_w8(A_COUNTOUT, mem_u8(A_COUNTOUT) & 0xFD);
        mem_w32(A_COUNTOUT_LEFT, (u32)left);
        host_hud("hud_countout_show", 0, 0);
        return 0;
    }
    case 3: {
        s32 c[3];
        match_flags_clear(c, 0, 0);
        countout_clock_put(c);
        s32 shown = mem_s32(A_COUNTOUT_SHOWN) + 1;
        mem_w32(A_COUNTOUT_SHOWN, (u32)shown);
        mem_w32(A_COUNTOUT_LEFT, (u32)(mem_s32(A_COUNTOUT_LEFT) - 1));
        host_hud("hud_countout_number", (s16)shown, 0);
        if (shown > 0)
            mem_w8(A_COUNTOUT, mem_u8(A_COUNTOUT) | 2);
        return 0;
    }
    case 2:
        break;
    default:
        return 0;
    }
    if (!(mem_u8(A_COUNTOUT) & 1))
        return 0;
    bool outside = false;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || !(w->flags_e9 & 0x10) || (w->location & 3))
            continue;
        if (mem_s32(A_ARENA) < 10) {
            outside = true;
            break;
        }
    }
    if (!outside) {
        countout_tick(1);
        return 0;
    }
    if (match_can_interrupt())
        return 0;
    if (mem_s32(A_COUNTOUT_LEFT) != 0) {
        if (g_match_type & 0x80000)
            return 0;
        if (mem_s32(A_PIN_SLOT) != -1)
            return 0;
        s32 c[3];
        countout_clock_get(c);
        clock_tick(c, 30);
        countout_clock_put(c);
        s32 left = mem_s32(A_COUNTOUT_LEFT);
        s32 every = (left < 3) ? 4 : ((left < 6) ? 3 : 2);
        if (c[1] == every)
            countout_tick(3);
        return 0;
    }
    if (g_match_rules & 0x80000) {
        host_sfx(1);
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->roster_id == -1 || !(w->flags_e9 & 0x10) || !(w->location & 3))
                continue;
            bool scores;
            if (!(g_match_type & 0x22000)) {
                scores = true;
            } else {
                // Slots 0 / 1 against 2 / 3: a legal man of the other pair outside (the second slot of
                // the pair 0 / 1 is not checked for roster_id, the original's quirk).
                const Wrestler *a = &g_wrestlers[i < 2 ? 2 : 0];
                const Wrestler *b = &g_wrestlers[i < 2 ? 3 : 1];
                scores = ((a->flags_e9 & 0x10) && !(a->location & 3)) ||
                         ((b->flags_e9 & 0x10) && !(b->location & 3) && (i >= 2 || b->roster_id != -1));
            }
            if (scores)
                fall_count_add(nullptr, i);
        }
        countout_tick(1);
        return 0;
    }
    s32 r = match_result(0x80000, 0);
    if (r == 1)
        host_sfx(1);
    return r;
}

namespace {

// match_result's per-slot statistic: table 0 of stats_count, the slot's own cell (attacks landed).
u16 result_stat(s32 slot) { return mem_u16(0x8009FDF8 + 10u * (u32)slot); }
// The falls each slot has won (fall_count_add).
s32 result_falls(s32 slot) { return mem_s32(0x80159218 + 4u * (u32)slot); }

}  // namespace

// N64 0x80117438: decides the result of a time limit (0x8000), a count-out (0x80000) or the guest
// referee's call (0x800000) and records it (match_record_fall). Returns 1 when a result was
// recorded that ends the decision, 0 when play goes on. Lists of up to four slots, -1 ended: A and
// B (the two sides) and `none` (always empty: a draw records it as both winners and losers).
// Read from the asm 2026-11-03 (Hex-Rays loses the lists and the return), not trace-checked.
//
// The guest referee's call: when every legal man in play other than the referee (flags_e9 0x04) has
// end_flags 0x200 (none unflagged), the referee is recorded over them (0x810400); otherwise in a
// singles-style match (type 0x1000) slot 0 beats slot 1 unless slot 0 is flagged; then in types
// 0x28000 the first flagged slot loses to its opponent.
//
// Otherwise each legal man in play is counted:
// - one-on-one / elimination (types 0xC000): inside the ring or apron to A, outside to B; an
//   outside wrestler just out of the fall (flags_f1 0x01) or entering (0xA3) stops it (0) unless
//   `arg` is 1. Under type 0x8000 also: with rule 0x80000 each slot's falls (A lists every slot);
//   without, the healthiest slot (ties to the higher statistic) and the least healthy;
// - type 0x1000: the first slot is side A, the second B, with health, statistic, falls and whether
//   outside;
// - types 0x22000 (teams): by the side bit 0x10 << slot (0x8014636C), summed.
// Count-out (0x80000): type 0x8000 (and 0x4000 unless arg 1, which records A over B): nobody inside a
// draw (0x81400), one inside wins (0x80400), more inside only the outside ones lose (0x80000, play
// goes on: 0); type 0x1000: equal numbers outside a draw (0x101400), else the side with fewer
// outside wins (0x80400); types 0x22000: both sides with someone outside a draw (0x101400), else the
// side with nobody outside wins. Time limit (0x8000): elimination (0x4000), or neither type 0x200000
// nor rule 0x80000, a draw (0x9400); rule 0x80000 the higher falls total wins (0x1008400, the
// one-on-one scores sorted first), equal a draw (0x9400); type 0x200000 without the rule the
// healthier side wins (0x8C00), equal health the higher statistic, equal again chance_check(500).
s32 match_result(u32 code, s32 arg) {
    s32 A[4], B[4], none[4], score[4];
    for (s32 i = 0; i < 4; i++) {
        A[i] = -1;
        B[i] = -1;
        none[i] = -1;
        score[i] = 0;
    }
    if (code == 0x800000) {
        s32 t = 0;
        for (; t < 4; t++) {
            const Wrestler *w = &g_wrestlers[t];
            if (w->roster_id == -1 || (w->flags_e9 & 4) || !(w->flags_e9 & 0x10))
                continue;
            if (!(w->end_flags & 0x200))
                break;
        }
        if (t == 4) {
            s32 nb = 0;
            for (s32 i = 0; i < 4; i++) {
                const Wrestler *w = &g_wrestlers[i];
                if (w->roster_id == -1)
                    continue;
                if (w->flags_e9 & 4)
                    A[0] = i;
                else if (w->flags_e9 & 0x10)
                    B[nb++] = i;
            }
            match_record_fall(A, B, code | 0x10400, 0);
            return 1;
        }
        if (g_match_type & 0x1000) {
            A[0] = 0;
            B[0] = 1;
            if (g_wrestlers[0].end_flags & 0x200)
                match_record_fall(B, A, code | 0x10400, 0);
            else
                match_record_fall(A, B, code | 0x10400, 0);
        }
        if (!(g_match_type & 0x28000))
            return 1;
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->roster_id == -1 || !(w->end_flags & 0x200))
                continue;
            B[0] = i;
            A[0] = w->opponent;
            match_record_fall(A, B, code | 0x10400, 0);
            return 1;
        }
        return 1;
    }

    u32 type = g_match_type;
    s32 na = 0, nb = 0;          // A / B counts: inside / outside (0xC000), outside per side (others)
    s32 health_a = 0, health_b = 0;
    unsigned long long stat_a = 0, stat_b = 0;  // a 64-bit sum in the original
    s32 falls_a = 0, falls_b = 0;
    s32 max_health = g_wrestlers[0].health, min_health = g_wrestlers[1].health;
    s32 max_slot = 0, min_slot = 1;
    s32 best_stat = 0;
    s32 fill_a = 0, fill_b = 0;  // the team lists' fill (types 0x22000)
    for (s32 t = 0; t < 4; t++) {
        const Wrestler *w = &g_wrestlers[t];
        if (w->roster_id == -1 || !(w->flags_e9 & 0x10))
            continue;
        bool outside = !(w->location & 3);
        if (type & 0xC000) {
            if (!outside) {
                A[na++] = t;
            } else if ((w->flags_f1 & 1) || w->state == state::RunIn) {
                if (arg != 1)
                    return 0;
            } else {
                B[nb++] = t;
            }
            if (type & 0x8000) {
                if (g_match_rules & 0x80000) {
                    A[t] = t;
                    score[t] = result_falls(t);
                } else {
                    if (w->health == max_health && best_stat < (s32)result_stat(t)) {
                        best_stat = result_stat(t);
                        max_slot = t;
                    }
                    if (max_health < w->health) {
                        max_health = w->health;
                        max_slot = t;
                    }
                    if (w->health < min_health) {
                        min_health = w->health;
                        min_slot = t;
                    }
                }
            }
        }
        if (type & 0x1000) {
            if (A[0] == -1) {
                A[0] = t;
                health_a = w->health;
                stat_a = result_stat(t);
                falls_a = result_falls(t);
                na += outside;
            } else {
                B[0] = t;
                health_b = w->health;
                stat_b = result_stat(t);
                falls_b = result_falls(t);
                nb += outside;
            }
        }
        if (type & 0x22000) {
            if (type & mem_u32(0x8014636C + 4u * (u32)t)) {
                A[fill_a++] = t;
                health_a += w->health;
                stat_a += result_stat(t);
                falls_a += result_falls(t);
                na += outside;
            } else {
                B[fill_b++] = t;
                health_b += w->health;
                stat_b += result_stat(t);
                falls_b += result_falls(t);
                nb += outside;
            }
        }
    }

    if (code == 0x80000) {
        if ((type & 0x8000) || (type & 0x4000)) {
            if (!(type & 0x8000) && arg == 1) {
                match_record_fall(A, B, 0x80400, 0);
                return 1;
            }
            if (na == 0) {
                match_record_fall(none, none, 0x81400, 0);
                return 1;
            }
            if (na == 1) {
                match_record_fall(A, B, 0x80400, 0);
                return 1;
            }
            match_record_fall(none, B, code, 0);
            return 0;
        }
        if (type & 0x1000) {
            if (na == nb)
                match_record_fall(none, none, 0x101400, 1);
            else if (na < nb)
                match_record_fall(A, B, 0x80400, 1);
            else
                match_record_fall(B, A, 0x80400, 1);
            return 1;
        }
        if (type & 0x22000) {
            if (na != 0 && nb != 0) {
                match_record_fall(none, none, 0x101400, 1);
                return 1;
            }
            if (na == 0)
                match_record_fall(A, B, 0x80400, 1);
            if (nb != 0)
                return 1;
            match_record_fall(B, A, 0x80400, 1);
            return 1;
        }
        return 1;
    }
    if (code != 0x8000)
        return 1;

    // The time limit.
    const s32 *win = none, *lose = none;
    u32 result = 0x9400;
    bool by_falls;
    if (type & 0x4000)
        by_falls = false;  // a draw
    else if (type & 0x200000)
        by_falls = (g_match_rules & 0x80000) != 0;
    else if (g_match_rules & 0x80000)
        by_falls = true;
    else
        by_falls = false;
    bool by_health = !(type & 0x4000) && (type & 0x200000) && !(g_match_rules & 0x80000);
    if (by_falls) {
        if (type & 0x8000) {
            // Sort the first three by falls (highest first), then the top two face each other.
            for (s32 i = 0; i < 2; i++) {
                for (s32 j = i + 1; j < 3; j++) {
                    if (score[i] < score[j]) {
                        s32 v = score[j];
                        score[j] = score[i];
                        score[i] = v;
                        v = A[j];
                        A[j] = A[i];
                        A[i] = v;
                    }
                }
            }
            falls_a = score[0];
            falls_b = score[1];
            B[0] = A[1];
            A[1] = -1;
        }
        if (falls_a == falls_b) {
            result = 0x8000 | 0x1400;
        } else {
            result = 0x8000 | 0x1000400;
            win = A;
            lose = B;
            if (falls_a < falls_b) {
                win = B;
                lose = A;
            }
        }
    } else if (by_health) {
        if (type & 0x8000) {
            A[0] = max_slot;
            A[1] = -1;
            B[0] = min_slot;
            B[1] = -1;
            health_a = g_wrestlers[max_slot].health;
            health_b = g_wrestlers[min_slot].health;
            stat_a = result_stat(max_slot);
            stat_b = result_stat(min_slot);
        }
        result = 0x8C00;
        bool b_wins;
        if (health_a != health_b)
            b_wins = health_a < health_b;
        else if (stat_a != stat_b)
            b_wins = stat_a < stat_b;
        else
            b_wins = chance_check(500) != 0;
        win = b_wins ? B : A;
        lose = b_wins ? A : B;
    }
    match_record_fall(win, lose, result, 1);
    return 1;
}

// ---- the end of a fall: the results bookkeeping ----

enum : u32 {
    A_FALL_TIMES = 0x8009FE58,      // s32[4]: each loser's fall time in seconds
    A_RESULT_FLAGS = 0x8009FE68,    // u32: result bits (bits 0 / 1 for flags_f1 0x08 in a 0x1000 match; bonus bits)
    A_FALL_COUNT = 0x800A8784,      // s16: falls recorded so far
    A_FALL_MINUTES = 0x800A84C0,    // s16 minutes, +2 s16 seconds, +4 s16 the loser's move_link (pins / submissions)
    A_FALL_CAUSES = 0x800A84C8,     // u32: the cause word per fall, 12 bytes apart
    A_RESULT_CAUSE = 0x800A10EC,    // u32: the last fall's cause word (code | winner / loser bits)
    A_RESULT_WINNER = 0x80162A84,   // s32: the last fall's first winner
    A_WIN_BITS = 0x8014637C,        // u16[4]: the cause word's bit per winning slot
    A_LOSE_BITS = 0x80146384,       // u16[4]: the cause word's bit per losing slot
    A_HEALTH_GAP = 0x80159208,      // s32: the winners' minus the losers' average health, x 100 / 255
    A_NO_RESULTS = 0x8015920C,      // s32: set when the results copy at the end is skipped
    A_ENTRANT_COUNT = 0x80159210,   // s32: entrants in the list
    A_ENTRANT_NEXT = 0x80159214,    // s32: the next entrant
    A_EVENT_SLOT = 0x801591E8,      // s32: match_events' slot
    A_END_WORDS = 0x80159228,       // u32[4]: the elimination masks (copied out on a DQ)
    A_DQ_COPY = 0x800AA068,         // u32[4]: their copy; 0x800A10E0 the entrant index; 0x8009FDF1.. arena, minutes, seconds, health x4
    A_BONUS_BITS = 0x80146580,      // u32[4]: result bits per slot with bonus_flags 0x02
    A_RESULTS_OUT = 0x80097FB8,     // u8[4][8]: health, the five regions (1, 4, 2, 3, 5), bonus, the weakest region
    A_END_ARENA_WORD = 0x801594D2,  // u16: from the table 0x80146350 by the event slot, in arenas under 10
    A_END_REPLAY = 0x801593F0,      // s32 winner, +4 loser, +8 the record frame, +0xC 9999, +0x10 camera delay, +0x14 start frame, +0x1C start copy, +0x24 3
};

// N64 0x80119328: the per-wrestler tallies of a fall (0x800A3DC0, 28 bytes a slot, for the results
// screen) in elimination matches (type 0x4000); nothing otherwise. Returns -1. Each tally has two
// columns, by flags_e9 0x08 (A) or not (B): a winner still in the fall gets a win (+0x02 / +0x16);
// then, by the result, the number of losers is added to its pins (+0x10 / +0x0C), submissions
// (+0x11 / +0x0D), count-outs (+0x12 / +0x0E) or over-the-top eliminations (+0x13 / +0x0F; and when
// the winner is out of the fall too, +0x15 / +0x14); a winner still in also counts each loser by
// slot (+0x08.. / +0x04.. + slot); each loser gets a loss (+0x03 / +0x17). Read from the code
// 2026-11-03 (the singles path trace-checked), the elimination path not trace-checked.
s32 result_stats_record(const s32 *winners, const s32 *losers, u32 code) {
    if (!(g_match_type & 0x4000))
        return -1;
    s32 nl = 0;
    while (losers[nl] != -1)
        nl++;
    auto bump = [](u32 a, s32 n) { mem_w8(a, (u8)(mem_u8(a) + n)); };
    for (const s32 *wp = winners; *wp != -1; wp++) {
        s32 ws = *wp;
        const Wrestler *w = &g_wrestlers[ws];
        u32 t = 0x800A3DC0 + 28u * (u32)ws;
        bool a = (w->flags_e9 & 8) != 0;
        if (!(w->end_flags & 1))
            bump(t + (a ? 0x02 : 0x16), 1);
        if (code & 0x4000) {
            bump(t + (a ? 0x10 : 0x0C), nl);
        } else if (code & 0x2000) {
            bump(t + (a ? 0x11 : 0x0D), nl);
        } else if (code & 0x10000) {
            bump(t + (a ? 0x12 : 0x0E), nl);
        } else if (code & 0x80000) {
            bump(t + (a ? 0x13 : 0x0F), nl);
            if (w->end_flags & 1)
                bump(t + (a ? 0x15 : 0x14), nl);
        }
        if (w->end_flags & 1)
            continue;
        for (const s32 *lp = losers; *lp != -1; lp++) {
            if ((u32)*lp < 4)
                bump(t + (a ? 0x08u : 0x04u) + (u32)*lp, 1);
        }
    }
    for (const s32 *lp = losers; *lp != -1; lp++) {
        const Wrestler *l = &g_wrestlers[*lp];
        bump(0x800A3DC0 + 28u * (u32)*lp + ((l->flags_e9 & 8) ? 0x03 : 0x17), 1);
    }
    return -1;
}

// N64 0x801199F0: after a fall: outside tag / elimination matches (0x4000) clears the loser's
// team_member, ally_id and run_in_e8 (0); the entrant and team bookkeeping is not ported.
s32 elimination_record(s32 slot) {
    Wrestler *w = &g_wrestlers[slot];
    if (!(g_match_type & 0x4000)) {
        w->team_member = 0;
        w->ally_id = 0;
        w->run_in_e8 = 0;
        return 0;
    }
    NM_UNPORTED("elimination_record: tag / elimination matches");
}

// N64 0x8011723C: marks `slot` out (end_flags 0x01) and says whether the fall ends the match: 1 in
// the 0x3B000 match types; 0 outside tag / handicap (0x4000); in a tag match 1 when slot 0 loses
// under rules 0x100000 or type 0x400000; with type 0x4000000 (teams) 0 unless three slots have
// flags_f1 0x04 and this one too, then 1 when fewer than two slots are still in (out but fresh or
// run-ins count as in); otherwise 0 while entrants are left, else 1 when fewer than two are in.
s32 tag_partner_takes_over(s32 slot) {
    g_wrestlers[slot].end_flags |= 1;
    if (g_match_type & 0x3B000)
        return 1;
    if (!(g_match_type & 0x4000))
        return 0;
    s32 first = 0;
    if ((g_match_rules & 0x100000) || (g_match_type & 0x400000))
        first = 1;
    if (first && slot == 0)
        return 1;
    if (g_match_type & 0x4000000) {
        s32 last = 0;
        for (s32 i = 0; i < 4; i++)
            last += (g_wrestlers[i].flags_f1 & 4) != 0;
        if (last < 3)
            return 0;
        s32 in = 0;
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (!(w->end_flags & 1))
                in++;
            else if (w->flags_f1 & 1)
                in++;
            else if (w->state == state::RunIn)
                in++;
        }
        if (!(g_wrestlers[slot].flags_f1 & 4))
            return 0;
        return in < 2 ? 1 : 0;
    }
    if (mem_s32(A_ENTRANT_NEXT) < mem_s32(A_ENTRANT_COUNT))
        return 0;
    s32 in = 0;
    for (s32 i = 0; i < 4; i++)
        in += (g_wrestlers[i].end_flags & 1) == 0;
    return in < 2 ? 1 : 0;
}

// N64 0x80118A20: records a fall: the tallies (record_stats), each loser's fall time, the fall's
// clock and move, the cause word (0x800A10EC: code, + 0x100 / 0x200 by the first winner's team in
// tag / handicap, the winners' and losers' bits from 0x8014637C / 0x80146384), end_flags 0x04 on
// the winners and 0x01 on the losers (flags_e9 0x08 under type 0x204000; elimination_record sets
// flags_f1 0x01 or clears flags_e9 0x10), the cause per fall, the health gap (average winner minus
// average loser, x 100 / 255, floored at 0), the 0x1000 result bits, the DQ record (code 0x400),
// the fall count and the HUD message.
void match_record_fall(const s32 *winners, const s32 *losers, u32 code, s32 record_stats) {
    if (record_stats)
        result_stats_record(winners, losers, code);
    for (s32 k = 0; k < 3; k++) {
        if (losers[k] != -1)
            mem_w32(A_FALL_TIMES + 4u * (u32)losers[k], (u32)(60 * g_match_clock[0] + g_match_clock[1]));
    }
    mem_w16(A_FALL_COUNT, 0);
    mem_w16(A_FALL_MINUTES, (u16)g_match_clock[0]);
    mem_w16(A_FALL_MINUTES + 2, (u16)g_match_clock[1]);
    s32 mv = -1;
    if (losers[0] != -1 && (code & 0x6000))
        mv = g_wrestlers[losers[0]].move_link;
    mem_w16(A_FALL_MINUTES + 4, (u16)mv);
    mem_w32(A_RESULT_CAUSE, code);
    mem_w32(A_RESULT_WINNER, (u32)winners[0]);
    if ((g_match_type & 0x22000) && winners[0] != -1 && losers[0] != -1)
        mem_w32(A_RESULT_CAUSE, (g_wrestlers[winners[0]].flags_e9 & 0x20) ? (code | 0x100) : (code | 0x200));
    for (s32 i = 0; i < 4; i++)
        g_wrestlers[i].end_flags &= 0xFFFB;
    s32 n_win = 0, sum_win = 0;
    const s32 *p = winners;
    for (s32 k = 0; k < 3; k++) {
        if (*p == -1)
            continue;
        mem_w32(A_RESULT_CAUSE, mem_u32(A_RESULT_CAUSE) | mem_u16(A_WIN_BITS + 2u * (u32)*p));
        g_wrestlers[*p].end_flags |= 4;
        sum_win += g_wrestlers[*p].health;
        n_win++;
        p++;
    }
    if (n_win)
        sum_win /= n_win;
    s32 n_lose = 0, sum_lose = 0;
    p = losers;
    for (s32 k = 0; k < 3; k++) {
        if (*p == -1)
            continue;
        Wrestler *w = &g_wrestlers[*p];
        mem_w32(A_RESULT_CAUSE, mem_u32(A_RESULT_CAUSE) | mem_u16(A_LOSE_BITS + 2u * (u32)*p));
        w->end_flags |= 1;
        if ((g_match_type & 0x204000) == 0x204000)
            w->flags_e9 |= 8;
        if ((g_match_type & 0x4000) && (g_match_rules & 0x100000) && (g_wrestlers[0].end_flags & 4))
            NM_UNPORTED("match_record_fall: the elimination masks (0x80159228)");
        if (elimination_record(*p))
            w->flags_f1 |= 1;
        else
            w->flags_e9 &= 0xEF;
        sum_lose += w->health;
        n_lose++;
        p++;
    }
    if (n_lose)
        sum_lose /= n_lose;
    mem_w32(A_FALL_CAUSES + 12u * (u32)mem_s16(A_FALL_COUNT), mem_u32(A_RESULT_CAUSE));
    s32 gap = sum_win - sum_lose;
    mem_w32(A_HEALTH_GAP, (u32)gap);
    if (gap < 0)
        mem_w32(A_HEALTH_GAP, 0);
    mem_w32(A_HEALTH_GAP, (u32)(s32)((f64)mem_s32(A_HEALTH_GAP) * 0.39215686274509803));
    if (g_match_type & 0x1000) {
        if (g_wrestlers[0].flags_f1 & 8)
            mem_w32(A_RESULT_FLAGS, mem_u32(A_RESULT_FLAGS) | 1);
        if (g_wrestlers[1].flags_f1 & 8)
            mem_w32(A_RESULT_FLAGS, mem_u32(A_RESULT_FLAGS) | 2);
    }
    if (code & 0x400) {
        for (s32 k = 0; k < 4; k++)
            mem_w32(A_DQ_COPY + 4u * (u32)k, mem_u32(A_END_WORDS + 4u * (u32)k));
        mem_w32(0x800A10E0, (u32)(mem_s32(A_ENTRANT_NEXT) - 1));
        mem_w8(0x8009FDF2, (u8)g_match_clock[0]);
        mem_w8(0x8009FDF3, (u8)g_match_clock[1]);
        mem_w8(0x8009FDF1, (u8)mem_s32(A_ARENA));
        for (s32 k = 0; k < 4; k++) {
            const Wrestler *w = &g_wrestlers[k];
            if (w->roster_id == -1)
                continue;
            mem_w8(0x8009FDF4 + (u32)k, (u8)w->health);
            if (w->bonus_flags & 2)
                mem_w32(A_RESULT_FLAGS, mem_u32(A_RESULT_FLAGS) | mem_u32(A_BONUS_BITS + 4u * (u32)k));
        }
    }
    mem_w16(A_FALL_COUNT, (u16)(mem_u16(A_FALL_COUNT) + 1));
    host_hud("hud_result_message", (s32)mem_u32(A_RESULT_CAUSE), 0);
}

// N64 0x8013111C: starts the end-of-match replay (not in ironman 0x2000000: 9999): the winner and
// loser (0x801593F0 / F4), frame_bits 0x100 plus 0x200 (TKO 0x10000), 0x400 (submission 0x2000),
// 0x800 (pin 0x4000), 0x1000 (DQ 0x20000) or 0x2000 (ladder 0x40000), the camera delay
// (0x80159400: -135, -60, -150, -180) and the replay's start frame (0x80159404: the record frame
// 0x801593F8 plus the winner's frames left + 3 / + 6, or minus 60 / 90), wrapped into 0..599 (a
// negative start wraps under frame_bits 0x80000, else 0); returns the start frame.
s32 match_end_start(s32 winner, s32 loser, u32 code) {
    if (g_match_type & 0x2000000)
        return 9999;
    u32 bits = g_frame_bits;
    mem_w32(A_END_REPLAY + 0xC, 9999);
    mem_w32(A_END_REPLAY, (u32)winner);
    mem_w32(A_END_REPLAY + 4, (u32)loser);
    mem_w32(A_END_REPLAY + 0x24, 3);
    g_frame_bits |= 0x100;
    s32 rec = mem_s32(A_END_REPLAY + 8);
    switch (code) {
    case 0x10000:
        g_frame_bits = bits | 0x300;
        mem_w32(A_END_REPLAY + 0x10, (u32)-135);
        if (g_wrestlers[loser].end_flags & 0x80)
            mem_w32(A_END_REPLAY + 0x14, (u32)(rec + 3));
        else
            mem_w32(A_END_REPLAY + 0x14, (u32)(rec - 90));
        break;
    case 0x20000:
        g_frame_bits = bits | 0x1100;
        mem_w32(A_END_REPLAY + 0x10, (u32)-60);
        mem_w32(A_END_REPLAY + 0x14, (u32)(rec + g_wrestlers[winner].anim_frames_left + 3));
        break;
    case 0x40000:
        g_frame_bits = bits | 0x2100;
        mem_w32(A_END_REPLAY + 0x10, (u32)-150);
        mem_w32(A_END_REPLAY + 0x14, (u32)(rec + g_wrestlers[winner].anim_frames_left + 6));
        break;
    case 0x2000:
        g_frame_bits = bits | 0x500;
        mem_w32(A_END_REPLAY + 0x10, (u32)-135);
        mem_w32(A_END_REPLAY + 0x14, (u32)(rec + g_wrestlers[winner].anim_frames_left + 3));
        break;
    case 0x4000:
        g_frame_bits = bits | 0x900;
        if (!(g_match_type & 0x2000)) {
            mem_w32(A_END_REPLAY + 0x10, (u32)-150);
            mem_w32(A_END_REPLAY + 0x14, (u32)(rec - 60));
        } else {
            mem_w32(A_END_REPLAY + 0x10, (u32)-180);
            mem_w32(A_END_REPLAY + 0x14, (u32)(rec + g_wrestlers[winner].anim_frames_left + 3));
        }
        break;
    default:
        break;
    }
    s32 start = mem_s32(A_END_REPLAY + 0x14);
    if (start >= 600)
        start -= 600;
    if (start < 0)
        start = (g_frame_bits & 0x80000) ? start + 600 : 0;
    mem_w32(A_END_REPLAY + 0x14, (u32)start);
    mem_w32(A_END_REPLAY + 0x1C, (u32)start);
    return start;
}

// N64 0x80116CDC: acts on fall cause `cause` (an end_flags bit) when some wrestler has it; returns
// 1 when it did. Cause to result code: 0x08 pin 0x4000, 0x40 TKO 0x10000, 0x20 submission 0x2000,
// 0x1000 cage 0x2000000, 0x10 over the top 0x80000, 0x100 last standing 0x800, 0x200 the guest
// referee 0x800000, 0x400 DQ 0x20000, 0x800 ladder 0x40000. 0x800: the first slot with the flag
// is the winner alone (match_record_fall, 0x800 | 0x400). 0x80000 (over the top) and 0x800000 (the
// guest referee) go to match_result (below). The rest: each flagged loser keeps the flags in saved_end_flags, loses the bit, and
// falls to its pair_other (the dq_victim for a DQ); under rules 0x80000 the fall is only counted
// (fall_count_add); else tag_partner_takes_over decides: the match ends (the fall is
// recorded with 0x400, match_end_start outside the cage, 1) or only the fall is recorded.
s32 match_end(u32 cause) {
    u32 code = 0;
    switch (cause & 0xFFFF) {
    case 0x08: code = 0x4000; break;
    case 0x40: code = 0x10000; break;
    case 0x20: code = 0x2000; break;
    case 0x1000: code = 0x2000000; break;
    case 0x10: code = 0x80000; break;
    case 0x100: code = 0x800; break;
    case 0x200: code = 0x800000; break;
    case 0x400: code = 0x20000; break;
    case 0x800: code = 0x40000; break;
    default: break;
    }
    if (code == 0x800) {
        for (s32 i = 0; i < 4; i++) {
            u16 f = g_wrestlers[i].end_flags;
            if (!(f & cause))
                continue;
            g_wrestlers[i].end_flags = f & (u16)~cause;
            s32 rec[5] = {i, -1, -1, -1, -1};
            match_record_fall(rec, rec + 2, code | 0x400, 0);
            return 1;
        }
        return 0;
    }
    if (code == 0x80000) {
        // Over the top (cause 0x10; read from the asm 2026-11-03, not trace-checked). Waits until
        // every legal man still in the fall is settled: one in the ring or on the apron must not be
        // on its way to the floor (its animation's end place 2), one outside must be just out
        // (flags_f1 0x01), entering (0xA3) or flagged itself. Then each flagged wrestler's fall is
        // tallied (result_stats_record) and recorded against its pair_other; under elimination with
        // rule 0x100000, or match type 0x400000, slot 0 going out decides at once (match_result's
        // arg 1); otherwise tag_partner_takes_over for the last one decides whether match_result
        // runs. The bell when it returns 1.
        bool any = false;
        for (s32 i = 0; i < 4; i++)
            any = any || (g_wrestlers[i].end_flags & cause);
        if (!any)
            return 0;
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (!(w->flags_e9 & 0x10) || (w->end_flags & 1))
                continue;
            if (w->location & 3) {
                if (w->anim_player[AP_END_PLACE] == 2)
                    return 0;
                continue;
            }
            if ((w->flags_f1 & 1) || w->state == state::RunIn || (w->end_flags & 0x10))
                continue;
            return 0;
        }
        for (s32 i = 0; i < 4; i++) {
            if (!(g_wrestlers[i].end_flags & cause))
                continue;
            s32 rec[5] = {g_wrestlers[i].pair_other, -1, i, -1, -1};
            result_stats_record(rec, rec + 2, code);
        }
        s32 last = -1;
        s32 at_once = 0;
        for (s32 i = 0; i < 4; i++) {
            Wrestler *w = &g_wrestlers[i];
            u16 f = w->end_flags;
            if (!(f & cause))
                continue;
            w->saved_end_flags = f;
            w->end_flags = f & (u16)~cause;
            s32 rec[5] = {w->pair_other, -1, i, -1, -1};
            match_record_fall(rec, rec + 2, code, 0);
            last = i;
            if (i == 0 && (((g_match_type & 0x4000) && (g_match_rules & 0x100000)) || (g_match_type & 0x400000)))
                at_once = 1;
        }
        if (last == -1)
            return 0;
        if (at_once != 1 && !tag_partner_takes_over(last))
            return 0;
        s32 r = match_result(0x80000, at_once);
        if (r == 1)
            host_sfx(1);
        return r;
    }
    if (code == 0x800000) {
        // The guest referee's call: match_result decides, with the bell (the flag stays).
        for (s32 i = 0; i < 4; i++) {
            if (g_wrestlers[i].end_flags & cause) {
                match_result(0x800000, 0);
                host_sfx(1);
                return 1;
            }
        }
        return 0;
    }
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        u16 f = w->end_flags;
        if (!(f & cause))
            continue;
        w->saved_end_flags = f;
        w->end_flags = f & (u16)~cause;
        s32 victim = (code == 0x20000) ? w->dq_victim : w->pair_other;
        if (g_match_rules & 0x80000) {
            // Falls are counted (ironman): the winner scores and play goes on; the loser is marked
            // out of the fall (end_flags 0x01) and recovers (fall_count_add's health and meter),
            // except after a DQ.
            if (code == 0x20000) {
                fall_count_add(nullptr, victim);
            } else {
                w->end_flags |= 1;
                fall_count_add(w, victim);
            }
            continue;
        }
        s32 rec[5] = {victim, -1, i, -1, -1};
        if (tag_partner_takes_over(i)) {
            match_record_fall(rec, rec + 2, code | 0x400, 1);
            if (code != 0x2000000)
                match_end_start(rec[0], rec[2], code);
            return 1;
        }
        match_record_fall(rec, rec + 2, code, 1);
    }
    return 0;
}

// N64 0x80119C28: after a fall: the slots whose bits are in the cause word (winners by 0x8014637C,
// losers by 0x80146384); -1 without a winner (or four); else the first winner, or the first loser
// when the winner has flags_e9 0x08 and the loser not; in tag / handicap (0x22000) the winner's
// partner (slot ^ 1) gets end_flags 0x04 when neither has flags_e9 0x04.
s32 match_events() {
    s32 win[6], lose[5];
    for (s32 i = 0; i < 5; i++)
        win[i] = lose[i] = -1;
    u32 cause = mem_u32(A_RESULT_CAUSE);
    s32 n = 0, nw = 0, nl = 0;
    for (s32 i = 0; i < 4; i++) {
        if (cause & mem_u16(A_WIN_BITS + 2u * (u32)i)) {
            win[nw++] = i;
            n++;
        }
        if (cause & mem_u16(A_LOSE_BITS + 2u * (u32)i))
            lose[nl++] = i;
    }
    if (n == 0 || n < 0 || n >= 4)
        return -1;
    s32 slot = win[0];
    if (g_wrestlers[slot].flags_e9 & 8) {
        s32 l = lose[0];
        if (l >= 0 && !(g_wrestlers[l].flags_e9 & 8))
            slot = l;
    }
    if (!(g_match_type & 0x22000))
        return slot;
    s32 w0 = win[0];
    if (g_wrestlers[w0].flags_e9 & 4)
        return slot;
    s32 partner = w0 ^ 1;
    if (g_wrestlers[partner].flags_e9 & 4)
        return slot;
    g_wrestlers[partner].end_flags |= 4;
    return slot;
}

// The N64's f32 -> u32 conversion (values from 2^31 up go through a subtraction and the top bit).
static u32 f32_to_u32(f32 f) {
    if (2147483648.0f <= f)
        return (u32)(s32)(f - 2147483648.0f) | 0x80000000u;
    return (u32)(s32)f;
}

// N64 0x80114514: the match's frame, phase 2 (play).
void match_tick() {
    // What the draw after the last tick left for the logic: the objects' probe points.
    objects_probe_points();
    switch (g_match_phase) {
    case 2:
        if (g_frame_bits & 2)
            NM_UNPORTED("match_tick: match_frame_extras (paused play)");
        run_in_check();
        wrestlers_update_all(1);
        referee_count_tick();
        if (match_clock_tick() || countout_tick(2) || match_end(0x08) || match_end(0x40) || match_end(0x20) ||
            match_end(0x10) || match_end(0x100) || match_end(0x200) || match_end(0x400) || match_end(0x800) ||
            match_end(0x1000))
            g_match_phase++;
        return;
    case 3:
        // The fall's frame: the event slot, the end timer (150 under match flag 0x10, else 0 with flag 0x40).
        mem_w32(A_EVENT_SLOT, (u32)match_events());
        if (g_match_flags[1] & 0x10) {
            g_phase_timer = 150;
        } else {
            g_match_flags[1] |= 0x40;
            g_phase_timer = 0;
        }
        wrestlers_update_all(1);
        g_match_phase++;
        return;
    case 4: {
        // The end: play goes on while the timer runs; from 91 a button (0xD00F) ends it (or starts the
        // replay under frame_bits 0x100), 271 ends it without the replay (181 in ironman); at the end
        // match flag 0x80 and the results copy (0x80097FB8) unless 0x8015920C says not to.
        if (g_match_flags[1] & 0x10) {
            wrestlers_update_all(1);
        } else {
            if ((g_match_flags[1] & 0x20) && mem_s32(A_ARENA) < 10)
                mem_w16(A_END_ARENA_WORD, (u16)(mem_u32(0x80146350 + 4u * (u32)mem_s32(A_EVENT_SLOT)) | 0x20));
            if (g_frame_bits & 2)
                NM_UNPORTED("match_tick: match_frame_extras (the end replay)");
            wrestlers_update_all(1);
        }
        g_phase_timer++;
        // g_phase_timer == 90: match_tick_stub (empty)
        if (g_phase_timer >= 91) {
            if (g_match_type & 0x2000000) {
                if (g_phase_timer >= 181)
                    g_phase_timer = 30000;
            } else {
                if (g_phase_timer >= 271)
                    g_phase_timer = (g_frame_bits & 0x100) ? 271 : 30000;
                for (s32 k = 0; k < 4; k++) {
                    if (!(g_pads[k].pressed & 0xD00F))
                        continue;
                    if (!(g_frame_bits & 2) && (g_frame_bits & 0x100)) {
                        g_frame_bits |= 1;
                        g_match_flags[1] &= 0xDF;
                    } else {
                        g_phase_timer = 30000;
                    }
                }
            }
        }
        if (g_phase_timer < 30000)
            return;
        g_match_flags[1] |= 0x80;
        if (mem_s32(A_NO_RESULTS) != 0)
            return;
        for (s32 k = 0; k < 4; k++) {
            const Wrestler *w = &g_wrestlers[k];
            u32 o = A_RESULTS_OUT + 8u * (u32)k;
            mem_w8(o + 0, (u8)w->health);
            mem_w8(o + 1, (u8)f32_to_u32(w->region[0]));
            mem_w8(o + 4, (u8)f32_to_u32(w->region[1]));
            mem_w8(o + 2, (u8)f32_to_u32(w->region[2]));
            mem_w8(o + 3, (u8)f32_to_u32(w->region[3]));
            mem_w8(o + 5, (u8)f32_to_u32(w->region[4]));
            mem_w8(o + 6, 0);
            mem_w8(o + 7, w->weakest_region);
            if (w->bonus_flags & 2)
                mem_w8(o + 6, 1);
        }
        return;
    }
    default:
        NM_UNPORTED("match_tick: phases 0, 1");
    }
}

}  // namespace nm

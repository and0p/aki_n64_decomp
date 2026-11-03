// Counters and reversals: strikes countered with a timed R (0x64 / 0x65), the second kind that takes a
// held weapon (0x66 / 0x67), dodges, caught kicks (0x69 / 0x6A), deflected grapples (0x6C / 0x6D), and flyers caught
// in the air (0x88 / 0x89). Drafted from IDA's pseudocode (tools/decomp/hr_draft.py), checked against the asm.
#include "nm_funcs.h"

namespace nm {

// N64 0x8012253C: whether a countering wrestler's reversal is the strong one: chance_check by spirit from
// the table 0x80146990 (s16 limit, s16 chance; -1 ends), 100 only counting in finisher mode.
s32 finisher_full_spirit_roll(const Wrestler *w) {
    s32 s = w->spirit;
    if (s == 100 && !(w->flags_ee & 4))
        s = 99;
    for (u32 p = 0x80146990; mem_s16(p) != -1; p += 4) {
        if (mem_s16(p) >= s)
            return chance_check(mem_s16(p + 2));
    }
    return -1;
}

// N64 0x801426CC: w takes the object `from` holds (holder, owner, its flags_ef).
void object_hand_over(Wrestler *w, Wrestler *from) {
    u32 o = 0x8016AE10 + 180u * (u32)from->held_obj;
    w->held_obj = from->held_obj;
    mem_w32(o + 8, (u32)(s32)w->slot);
    mem_w32(o + 0xC, (u32)(s32)w->slot);
    w->flags_ef = from->flags_ef;
    from->flags_ef = 0;
}

// N64 0x800E9A54: state 0x64, countering a strike with a timed R: by counter_bits from the move list (2..0x200
// give 187..203 at the opponent), 0x01 183 or 184 at random, else 180 or the strong 181 (finisher_full_spirit_roll).
// Moves with record flag 0x1000000 slide on the spot; the others run the paired move step. Events 0x2480.
void state_64_counter_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        u32 c = w->counter_bits;
        s32 slot, m = 0;
        if (c & 0x3FE) {
            slot = ctx->slot_a;
            for (s32 k = 1; k <= 9; k++) {
                if (c & (1u << k))
                    m = 185 + 2 * k;
            }
        } else {
            slot = ctx->slot;
            if (c & 1)
                m = rand_pool_next() % 2 + 183;
            else
                m = (finisher_full_spirit_roll(w) != 0) | 0xB4;
        }
        wrestler_start_move(w, slot, m);
        wrestler_move_pos_to_pelvis(w);
        wrestler_set_anim(w, anim_style(w, 0), 2);
        w->state_step++;
        return;
    }
    case 1:
        if (mem_u32(w->move + 0x20) & 0x1000000) {
            wrestler_slide(w, 4.0f);
            keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        } else {
            state_move_step(ctx, 101, 3, 2);
            state_bounds(w, ctx->rec_a, nullptr, 0);
        }
        move_event(ctx, 0x2480);
        return;
    default:
        return;
    }
}

// N64 0x800E9C44: state 0x65, its strike countered: the counterer's move; flag 0x1000000 moves step 2
// (flags8a 0x6000, sliding, events 512), the others paired, the slide to it, events 6656 while the moves match.
void state_65_strike_countered(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        w->move_num = o->move_num;
        if (mem_u32(o->move + 0x20) & 0x1000000) {
            wrestler_set_step(w, 2);
            return;
        }
        wrestler_set_anim(w, anim_style(o, 1), 2);
        pair_scripted_place(w, o);
        w->state_step++;
        [[fallthrough]];
    case 1:
        pair_slide_step(w, nullptr);
        state_bounds(w, o, nullptr, 0);
        move_event(ctx, w->move_num == o->move_num ? 6656 : 512);
        return;
    case 2:
        w->flags8a |= 0x6000;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 512);
        return;
    default:
        return;
    }
}

// N64 0x800E9DA8: state 0x66, taking a held weapon off a swing (12427 + 8 x its kind, 12431 + with counter
// bit 0x1000): at the slide's and animation's end it changes hands (object_hand_over), animation + 2, the events.
void state_66_counter_b(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    switch (w->state_step) {
    case 0: {
        s32 k = 8 * wrestler_move_anim(o, 0);
        wrestler_set_anim(w, k + ((w->counter_bits & 0x1000) ? 12431 : 12427), 2);
        pair_scripted_place(w, o);
        stats_count(w, o, 1);
        w->state_step++;
        [[fallthrough]];
    }
    case 1:
        pair_slide_step(w, nullptr);
        if (w->anim_frames_left > 0)
            return;
        object_hand_over(w, o);
        wrestler_set_anim(w, w->anim_id + 2, 0);
        w->state_step++;
        return;
    case 2:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800E9EC8: state 0x67, the weapon taken: the counterer's animation - 1, then + 2, events 0x248.
void state_67_counter_b_taken(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    state_bounds(w, ctx->rec_a, nullptr, 0);
    switch (w->state_step) {
    case 0:
        wrestler_move_pos_to_pelvis(w);
        wrestler_set_anim(w, ctx->rec_a->anim_id - 1, 2);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, w->anim_id + 2, 0);
        w->state_step++;
        return;
    case 2:
        move_event(ctx, 0x248);
        return;
    default:
        return;
    }
}

// N64 0x800E9F90: state 0x68, the roll dodging a dive (11887).
void state_68_dive_dodge(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags80 |= 1;
    if (w->state_step == 0) {
        wrestler_set_anim(w, 11887, 3);
        w->state_step++;
    } else if (w->state_step == 1) {
        keep_in_bounds_self(w, 1.0f, 0, 0xFFFFE);
        move_event(ctx, 0);
    }
}

// N64 0x800EA01C: state 0x69, holding a caught leg (11888, the slide). The strong roll is move 185; otherwise
// count_timer 90 (30 against finisher mode) for A (183) or B (184), state_flags 0x02; running out 0x04.
void state_69_catch_holder(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    switch (w->state_step) {
    case 0:
        pair_scripted_place(w, o);
        wrestler_set_anim(w, 11888, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (!pair_slide_step(w, nullptr))
            return;
        if (finisher_full_spirit_roll(w))
            w->state_flags |= 0x10;
        else
            w->count_timer = (o->flags_ee & 4) ? 30 : 90;
        w->state_step++;
        return;
    case 2:
        if (w->state_flags & 0x10) {
            w->state_flags |= 2;
            wrestler_start_move(w, w->slot, 185);
            w->state_step++;
            return;
        }
        if (ctx->pressed & 0xC000) {
            w->state_flags |= 2;
            wrestler_start_move(w, w->slot, (ctx->pressed & 0x8000) ? 183 : 184);
            w->state_step++;
            return;
        }
        w->count_timer = (u16)(w->count_timer - 1);
        if ((s16)w->count_timer < 0)
            w->state_flags |= 4;
        return;
    case 3:
        wrestler_set_anim(w, anim_style(w, 0), 2);
        w->state_step++;
        return;
    case 4:
        move_event(ctx, 0x2480);
        return;
    default:
        return;
    }
}

// N64 0x800EA1E4: state 0x6A, its leg caught (11889). When the holder picks A or B, the same button alone
// (and not the strong move) breaks out, state_flags 0x08; that or the holder's time running out reverses the pair
// (move 182, 0x69 / 0x6A swapped at step 3). Otherwise the holder's animation, events 0x1A00.
void state_6A_catch_held(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    switch (w->state_step) {
    case 0:
        pair_scripted_place(w, o);
        wrestler_set_anim(w, 11889, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        return;
    case 2: {
        u16 sf = o->state_flags;
        if (sf & 2) {
            u16 b = ctx->buttons & 0xC000;
            if (!(sf & 0x10) && b != 0 && b != 0xC000) {
                if ((b & 0x8000) && o->move_num == 183)
                    w->state_flags |= 8;
                if ((b & 0x4000) && o->move_num == 184)
                    w->state_flags |= 8;
            }
            if (!(w->state_flags & 8)) {
                w->state_step++;
                return;
            }
        }
        if ((o->state_flags & 4) || (w->state_flags & 8)) {
            wrestler_start_move(w, w->slot, 182);
            wrestler_pair_set_state_move(w, state::CaughtLegHolder, o, state::CaughtLegHeld);
            w->state_step = 3;
            o->state_step = 3;
        }
        return;
    }
    case 3:
        wrestler_set_anim(w, anim_style(o, 1), 2);
        w->state_step++;
        return;
    case 4:
        move_event(ctx, 0x1A00);
        return;
    default:
        return;
    }
}

// N64 0x800EA474: state 0x6C, deflecting a grapple with L (11881).
void state_6C_grapple_deflect(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 1.0f, 0, 2);
    if (w->state_step == 0) {
        wrestler_set_anim(w, 11881, 3);
        w->state_step++;
    } else if (w->state_step == 1) {
        move_event(ctx, 0);
    }
}

// N64 0x800EA4F0: state 0x6D, the grapple deflected: the deflector's animation + 1, events 0x200.
void state_6D_grapple_deflected(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 1.0f, 0, 2);
    if (w->state_step == 0) {
        wrestler_set_anim(w, ctx->rec_a->anim_id + 1, 3);
        w->state_step++;
    } else if (w->state_step == 1) {
        move_event(ctx, 0x200);
    }
}

// N64 0x800EA8FC: state 0x88, catching a flyer off the corner `dir`: move 141, or 142 (flags78 0x80) when it
// faces away from the corner's line; the slide to the corner, events 0x2400.
void state_88_catch_flyer(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (w->state_step == 0) {
        w->dir = o->dir;
        s32 m = 141;
        if (abs_i(angle_diff(o->facing, mem_s32(0x8014D208 + 4u * (u32)w->dir))) >= 1024) {
            m = 142;
            w->flags78 |= 0x80;
        }
        wrestler_start_move(w, ctx->slot, m);
        wrestler_set_anim(w, anim_style(w, 0), 2);
        stats_count(w, o, 0);
        pair_scripted_place(w, nullptr);
        w->state_step++;
    } else if (w->state_step != 1) {
        return;
    }
    pair_slide_step(w, nullptr);
    move_event(ctx, 0x2400);
}

// N64 0x800EAA08: state 0x89, caught flying: the catcher's animation, sliding in unless flags78 0x8000000 (a catch
// where it was); events 0x1A00.
void state_89_caught_flying(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (w->state_step == 0) {
        if (o->flags78 & 0x8000000) {
            wrestler_set_anim(w, anim_style(o, 1), 3);
        } else {
            wrestler_set_anim(w, anim_style(o, 1), 2);
            pair_scripted_place(w, o);
        }
        w->state_step++;
    } else if (w->state_step == 1) {
        if (!(o->flags78 & 0x8000000))
            pair_slide_step(w, nullptr);
        move_event(ctx, 0x1A00);
    }
}

// N64 0x801439C0: state 0x90, hit while holding a two-handed weapon, or catching one thrown: 12458, 12459 or
// 12460 by the hit's reaction (0, 2, others); the events.
void state_90_hit_holding_weapon(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    s32 r = (s8)w->hit_move.reaction;
    wrestler_set_anim(w, r == 0 ? 12458 : r == 2 ? 12459 : 12460, 3);
    move_event(ctx, 0);
}

}  // namespace nm

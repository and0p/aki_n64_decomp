// Running: the run (state 0x03), the rope rebound (0x04), the settle state after an animation
// (0x30), and the run's helpers (start, step, collisions, exits, buttons).
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_RUN_DIR = 0x801590D0,   // s32[4]: each slot's run direction
    A_SIDE_ANGLE = 0x8014D1D8,  // s32[4]: the angle toward ring side 0-3
    A_SIDE_BACK = 0x8014D1E8,   // s32[4]: the angle back from side 0-3
};

s32 side_angle(s32 side) { return mem_s32(A_SIDE_ANGLE + 4 * side); }
s32 side_back(s32 side) { return mem_s32(A_SIDE_BACK + 4 * side); }
s32 anim_turn(const Wrestler *w) {
    const u8 *p = w->anim_player;
    return (s32)((u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]);
}

}  // namespace

// ---- d-pad helpers ----

// N64 0x80132F90: the diagonal as a corner (0 up-right, 1 down-right, 2 down-left, 3 up-left), or -1.
s32 dpad_diagonal_quadrant(u16 buttons) {
    u16 d = buttons & 0xF00;
    s32 q = d != 0x900 ? -1 : 0;
    if (d == 0x500) q = 1;
    if (d == 0x600) q = 2;
    if (d == 0xA00) q = 3;
    return q;
}

// N64 0x80132EEC: the d-pad as a ring side (0 up, 1 right, 2 down, 3 left), or -1.
s32 dpad_to_side(u16 buttons) {
    if (buttons & 0x800) return 0;
    if (buttons & 0x100) return 1;
    if (buttons & 0x400) return 2;
    return (buttons & 0x200) ? 3 : -1;
}

// ---- probes ----

// N64 0x8012BF50: the slot the wrestler would overlap after moving by d (it is moved back).
s32 wrestler_translate_pushed(Wrestler *w, const Vec3 *d, s32 exclude) {
    Vec3 v = {0.0f, 0.0f, 0.0f};
    if (d)
        v = *d;
    wrestler_translate(w, &v);
    s32 hit = wrestler_move_step(w, nullptr, 0.0f, 0, exclude, 0);
    v.x = -v.x;
    v.y = -v.y;
    v.z = -v.z;
    wrestler_translate(w, &v);
    return hit;
}

// N64 0x8012C884: the walls the wrestler would touch after moving by d (it is moved back).
u16 wrestler_translate_bounded(Wrestler *w, u32 joints, const Vec3 *d) {
    wrestler_translate(w, d);
    u16 dirs = keep_in_bounds(w, nullptr, nullptr, 1.0f, nullptr, nullptr, nullptr, 0, joints, 0);
    Vec3 back = {-d->x, -d->y, -d->z};
    wrestler_translate(w, &back);
    return dirs;
}

// N64 0x80132700: where a line from p at `angle` meets the rope square (x / z +-230, y 90);
// returns that point's ring zone.
s32 side_point(const Vec3 *p, s32 angle, Vec3 *out) {
    Vec3 hit;
    if (angle == 0) {
        hit.x = p->x;
        hit.z = 230.0f;
    } else if (angle == 1024) {
        hit.x = 230.0f;
        hit.z = p->z;
    } else if (angle == 2048 || angle == -2048) {
        hit.x = p->x;
        hit.z = -230.0f;
    } else if (angle == -1024) {
        hit.x = -230.0f;
        hit.z = p->z;
    } else {
        f32 zline = (u32)(angle + 1023) < 2047 ? 230.0f : -230.0f;
        f32 t = absf(zline - p->z);
        t = absf(t / cosf_n64(angle_to_float(angle)));
        f32 x = t * sinf_n64(angle_to_float(angle)) + p->x;
        if (-230.0 <= (f64)x && (f64)x <= 230.0) {
            hit.x = x;
            hit.z = zline;
        } else {
            f32 xline = angle < 0 ? -230.0f : 230.0f;
            t = absf(xline - p->x);
            t = absf(t / sinf_n64(angle_to_float(angle)));
            f32 z = t * cosf_n64(angle_to_float(angle)) + p->z;
            hit.x = xline;
            hit.z = z;
        }
    }
    hit.y = 90.0f;
    if (out)
        *out = hit;
    return ring_zone_of(&hit);
}

// N64 0x80110308: running at side w->dir from close to it (past 150), facing away from it.
s32 run_toward_ropes_check(const Wrestler *w) {
    if (!(w->location & 1))
        return 0;
    s32 near = 0;
    switch (w->dir) {
    case 0: near = 150.0 < (f64)w->pos.z; break;
    case 1: near = 150.0 < (f64)w->pos.x; break;
    case 2: near = (f64)w->pos.z < -150.0; break;
    case 3: near = (f64)w->pos.x < -150.0; break;
    default: break;
    }
    if (!near)
        return 0;
    return abs_i(angle_diff(side_back(w->dir), w->facing)) < 967;
}

// ---- the not-yet-decompiled helpers the run reaches ----

// N64 0x80110880: the whipped runner's reversal try (returns 0): in the ring, not flags_e9 0x01, with
// the trait attrs +0x34 0x2000, while whipped (flags86 0x20), in finisher mode or against a whipper
// who is not, on the way back (flags86 0x08 without 0x80) and not tried yet (state_counter 0x01):
// a press of A / B / L / R 75.27-175.27 from the ring side point it runs toward (0x8014D1A8 by dir)
// uses the try, and a chance_roll of kind 3 sets flags86 0x800 (A) or 0x400 (the others).
s32 whip_run_inputs(Wrestler *w, Wrestler *opp, u16 pressed) {
    if (!(w->location & 1) || (w->flags_e9 & 1) || !(mem_u32(w->attrs + 0x34) & 0x2000))
        return 0;
    u32 c = 0x8014D1A8 + 12u * (u32)w->dir;
    Vec3 side = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
    f32 d = dist_to(&w->point_120, &side);
    if (!(w->flags86 & 0x20))
        return 0;
    if (!(w->flags_ee & 4) && (opp->flags_ee & 4))
        return 0;
    if ((w->state_counter & 1) || (w->flags86 & 0x88) != 8 || !(pressed & 0xC030))
        return 0;
    if (!(75.26911943162372 <= (f64)d) || !((f64)d <= 175.26911943162372))
        return 0;
    w->state_counter |= 1;
    if (chance_roll(w, nullptr, 3))
        w->flags86 |= (pressed & 0x8000) ? 0x800 : 0x400;
    return 0;
}
s32 run_corner_stop(Wrestler *w, s32 mode) {
    (void)w; (void)mode;
    NM_UNPORTED("run_corner_stop");
}
// N64 0x80120398: 1 when the wrestler may leave the ring: the legal man of a tag team (flags_e9
// 0x03), one whose end flags have 0x01, or anyone unless match rule 0x400 (over the top).
s32 may_leave_ring(Wrestler *w) {
    if (w->flags_e9 & 3)
        return 1;
    if (w->end_flags & 1)
        return 1;
    return (g_match_rules & 0x400) ? 0 : 1;
}

// ---- collisions while running ----

// N64 0x801211F8: knocks wrestlers standing on the apron off it (state 0x70) when the rebounding
// runner passes within 66.
void run_collide_check(Wrestler *w, u32 bits) {
    for (s32 i = 0; i < 4; i++) {
        if (i == w->slot)
            continue;
        Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1)
            continue;
        u16 loc = o->location;
        if (!(loc & 2) || (loc & 1) || (o->flags80 & 1) || (loc & 0x800))
            continue;
        if (!((f64)dist_to(&w->point_120, &o->point_120) <= 66.0))
            continue;
        wrestler_set_state_free(o, state::KnockedOverTopRope);
        o->hit_flags = bits | 0x80000;
        o->pair_other = w->slot;
    }
}

// N64 0x8012E2D8: 1 / 2 whether b is in front of / behind a, | 4 / 8 whether a is in front of /
// behind b.
u16 relative_position(const Wrestler *a, const Wrestler *b) {
    s32 to = angle_to(&a->point_120, &b->point_120);
    u16 r = abs_i(angle_diff(a->facing, to)) < 1025 ? 1 : 2;
    if (abs_i(angle_diff(b->facing, angle_add(to, 2048))) < 1025)
        r |= 4;
    else
        r |= 8;
    return r;
}

// N64 0x801117E4: with C-down held, a downed wrestler ahead of the runner: 1 the dodge over it
// (it becomes the runner_slot), 2 when it already is.
s32 run_target_downed(Wrestler *w, Wrestler *o, u16 held) {
    if (w->flags86 & 0x20)
        return 0;
    if (!(held & 4))
        return 0;
    if (!(w->location & o->location & 0x3C5))
        return 0;
    if (!(o->flags80 & 8))
        return 0;
    if (w->runner_slot == o->slot)
        return 2;
    s32 heading = (w->flags86 & 0x2000) ? w->run_dir_copy : w->facing;
    if (!(abs_i(angle_diff(angle_to(&w->point_120, &o->point_120), heading)) < 682))
        return 0;
    w->runner_slot = o->slot;
    if (w->flags86 & 0x2000)
        w->facing = w->run_dir_copy;
    return 1;
}

// N64 0x801112F0: what the runner runs into (another wrestler, a ladder). 1 when it took over.
s32 run_collide(Wrestler *w, s32 mode, u16 held) {
    s32 hit = wrestler_translate_pushed(w, nullptr, -1);
    w->_unk068 = hit;
    if (hit == -1) {
        w->runner_slot = hit;
        return 0;
    }
    if (!(hit < 100)) {
        // An object: a whipped runner (flags86 0x20) into a ladder hurts itself (generic move 92, the
        // knockdown 53, or 45 one time in ten) and knocks it (object_set_motion 10, or 11 with 0x40). A
        // run into one otherwise only rocks a ladder; either way the run goes on (0x03 step 3).
        s32 j = w->contact - 100;
        u32 o = 0x8016AE10 + 180u * (u32)j;
        if (!(w->flags86 & 0x20)) {
            object_set_motion(j, 0, nullptr);
        } else if (mem_s32(o + 4) == 25) {
            wrestler_set_generic_move(w, 92);
            hit_move_copy(w, w);
            w->strike_flags = 0;
            w->hit_flags = 0x8000000;
            wrestler_set_state_now(w, chance_check(100) ? state::KnockedDown : state::KnockedBack);
            s32 kind = (w->flags86 & 0x40) ? 11 : 10;
            Vec3 pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)}, d;
            vec_from_angle(angle_to(&w->point_120, &pos), 1.0f, &d);
            object_set_motion(j, kind, &d);
            return 1;
        }
        wrestler_set_state_now(w, state::Running);
        w->state_step = 3;
        return 1;
    }
    Wrestler *o = &g_wrestlers[hit];
    w->flags8a &= 0xFFFD;
    o->flags8a &= 0xFFFD;
    u16 rp = relative_position(w, o);
    if (rp & 2)
        w->flags8a |= 2;
    if (rp & 8)
        o->flags8a |= 2;
    if (o->flags80 & 0x20) {
        // Two runners.
        if (w->location & 2) {
            wrestler_set_state_free(w, state::KnockedDown);
            wrestler_set_state_free(o, state::KnockedDown);
            w->strike_flags = 0;
            w->hit_flags = 0x80400;
            w->pair_other = o->slot;
            o->strike_flags = 0;
            o->hit_flags = 0x80400;
            o->pair_other = w->slot;
            return 1;
        }
        weapon_release(w, 0);
        weapon_release(o, 0);
        wrestler_set_state(w, state::HitByRunner);
        wrestler_set_state(o, state::HitByRunner);
        return 1;
    }
    if ((u32)(o->state - state::Blocking) < 2 && !(o->flags8a & 2)) {
        // Into a blocker facing it.
        weapon_release(w, 0);
        wrestler_set_state(w, state::HitByRunner);
        wrestler_set_state(o, state::BlockHit);
        o->state_step = 2;
        return 1;
    }
    if (w->flags86 & 0x20) {
        // A whipped runner: an opponent slumped in the corner is knocked (hit_flags 0x04); one flagged
        // frame_flags 0x10 gets 0x70, knocked over the ropes; one on a ladder is knocked off (0xB2) and the
        // runner is hurt (generic move 92, the knockdown 0x35, or 0x2D one time in ten).
        if (o->state == state::SlumpedInCorner) {
            wrestler_set_state(w, state::HitByRunner);
            wrestler_set_state_free(o, state::KnockedDown);
            o->strike_flags = 0;
            o->hit_flags = 4;
            return 1;
        }
        if (o->frame_flags & 0x10) {
            wrestler_set_state(w, state::HitByRunner);
            wrestler_set_state_now(o, state::KnockedOverTopRope);
            o->state_counter |= 1;
            return 1;
        }
        if (o->location & 8) {
            wrestler_set_generic_move(w, 92);
            hit_move_copy(w, w);
            w->strike_flags = 0;
            w->hit_flags = 0x8000000;
            wrestler_set_state_now(w, chance_check(100) ? state::KnockedDown : state::KnockedBack);
            wrestler_set_state_free_now(o, state::LadderHit);
            o->state_counter |= (w->flags86 & 0x40) ? 7 : 5;
            return 1;
        }
    }
    s32 down = run_target_downed(w, o, held);
    if (down == 1) {
        weapon_release(w, 0);
        wrestler_set_state_now(w, state::SideStepRunner);
        return 1;
    }
    if (down != 0)
        return 0;
    if ((w->flags86 & 0x80) && hit == w->pair_other && g_wrestlers[hit].state == state::DuckingRunner)
        return 0;
    if (o->state == state::DuckingRunner && (o->state_counter & 1))
        return 0;
    if (!(w->location & 2)) {
        wrestler_set_state_now(w, state::Running);
        w->state_step = 3;
        return 1;
    }
    if (!(o->flags80 & 1) && !(o->location & 0x800)) {
        // From the apron into a standing wrestler: both go down.
        wrestler_set_state_free(w, state::KnockedDown);
        w->strike_flags = 0;
        w->hit_flags = 0x80200;
        wrestler_set_state_free(o, state::KnockedDown);
        o->strike_flags = 0;
        o->hit_flags = 0x80400;
        o->pair_other = w->slot;
        return 1;
    }
    if (mode != 0) {
        wrestler_set_state_now(w, state::Running);
        w->state_step = 3;
        return 1;
    }
    w->flags_f0 |= 2;
    w->step_vec.x = 0.0f;
    w->step_vec.z = 0.0f;
    return 0;
}

// N64 0x80110A90: leaving the ring (C-up / C-down at an exit wall) or climbing the cage (C-right)
// while running.
s32 run_exit_check(WrestlerCtx *ctx, s32 angle) {
    Wrestler *w = ctx->self;
    Vec3 probe;
    if ((g_match_type & 0x10000) && (w->location & 1) && !(w->flags86 & 0x20) && (ctx->buttons & 1)) {
        vec_from_angle(angle, 45.0f, &probe);
        if (wrestler_translate_bounded(w, 2, &probe)) {
            wrestler_set_state_now(w, state::CageClimbStart);
            return 1;
        }
    }
    if (w->location & 2)
        return 0;
    if (w->flags86 & 0x28)
        return 0;
    vec_from_angle(angle, 50.0f, &probe);
    if (!wrestler_translate_bounded(w, 2, &probe))
        return 0;
    u32 wall = w->zone_info;
    if (!(mem_u16(wall + 0x28) & 0x10))
        return 0;
    Vec3 n = {mem_f32(wall + 0x18), mem_f32(wall + 0x1C), mem_f32(wall + 0x20)};
    if ((ctx->buttons & 8) && (w->flags86 & 2)) {
        if (g_match_type & 0x10000) {
            wrestler_set_state_now(w, state::DropDown);
            return 1;
        }
        u16 t = mem_u16(wall + 0x26);
        bool exit = (t & 2) != 0;
        if (!exit && (t & 0x10))
            exit = illegal_check(w) || (g_match_type & 0x2000);
        if (exit && (f64)vec_normalize_two(&probe, &n) < -0.999390827) {
            weapon_release(w, 0);
            wrestler_set_state_now(w, (mem_u16(w->zone_info + 0x26) & 2) ? state::RunOffEdge : state::RunOntoRaisedPlace);
            w->state_counter |= 1;
            return 1;
        }
    }
    if (g_match_type & 0x10000)
        return 0;
    if (!(ctx->buttons & 4))
        return 0;
    if (!((f64)vec_normalize_two(&probe, &n) < -0.642787609))
        return 0;
    u16 t = mem_u16(w->zone_info + 0x26);
    bool go;
    if ((t & 2) && may_leave_ring(w) && (ctx->rec_a->location & 0x66))
        go = true;
    else if ((t & 0x10) && illegal_check(w))
        go = true;
    else
        go = (mem_u16(w->zone_info + 0x26) & 0x30E8) != 0;
    if (!go)
        return 0;
    wrestler_set_state_now(w, (mem_u16(w->zone_info + 0x26) & 0x10E2) ? state::RunOffEdge : state::RunOntoRaisedPlace);
    return 1;
}

// N64 0x801109D8: B at a downed opponent ahead: a running ground attack (0x62).
s32 run_attack_input(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 0x4000))
        return 0;
    Wrestler *w = ctx->self, *o = ctx->rec_a;
    if (!(w->location & o->location & 0x3C5))
        return 0;
    if (!(o->flags80 & 8))
        return 0;
    s32 angle;
    if (!running_ground_attack_check(w, o, 130.0f, &angle, 0))
        return 0;
    if (!(abs_i(angle_diff(angle, ctx->self->facing)) < 512))
        return 0;
    wrestler_set_state_now(ctx->self, state::RunningGroundAttack);
    return 1;
}

// N64 0x80110410: the buttons while running.
s32 run_inputs(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->flags86 & 0x20) {
        whip_run_inputs(w, ctx->rec_a, ctx->pressed);
        return 0;
    }
    u32 traits38 = mem_u32(w->attrs + 0x38);
    if (!(g_match_rules & 0x400)) {
        if ((w->location & 2) && (ctx->rec_a->location & 0x44) && (ctx->buttons & 0x8000) && (traits38 & 0x10000)) {
            wrestler_set_state_now(w, state::ApronDiveMove272);
            return 1;
        }
        if ((w->location & 1) && (ctx->rec_a->location & 0x44)) {
            s32 move = 0;
            if ((traits38 & 0x800) && ((ctx->buttons & 0x8000) || ctx->in_byte) && (w->flags86 & 0xA) == 2)
                move = 267;
            if ((traits38 & 0x200) && (ctx->buttons & 0x8000) && !(w->flags86 & 2))
                move = 265;
            if ((traits38 & 0x400) && (ctx->buttons & 0x8000) && (w->flags86 & 2))
                move = 266;
            if (move)
                NM_UNPORTED("run_inputs: a running dive at an opponent outside");
        }
    }
    if ((w->location & ctx->rec_a->location & 1) && (traits38 & 0x80000) && (w->flags86 & 0xA) == 2 &&
        (ctx->pressed & 0x8000) && run_corner_stop(w, 0)) {
        wrestler_set_state(w, state::RunningFlyingMove);
        return 1;
    }
    if ((w->location & ctx->rec_a->location & 0x3C5) && (ctx->buttons & 0x4000)) {
        s32 angle;
        // B at a downed opponent ahead (within 512 of the facing): the running stomp / drop (0x2A),
        // at one with stay_flags 0x05, or flags80 0x40 without 0x200.
        Wrestler *o = ctx->rec_a;
        if (running_ground_attack_check(w, o, 180.0f, &angle, 1) && abs_i(angle_diff(angle, w->facing)) < 512 &&
            ((o->stay_flags & 5) || (o->flags80 & 0x240) == 0x40)) {
            wrestler_set_state_now(w, state::RunningBreakUpPinOrHold);
            return 1;
        }
    }
    if (run_attack_input(ctx))
        return 1;
    if (w->location & 0x3C5) {
        u16 held = ctx->buttons;
        if (held & 0x10) {
            wrestler_set_state_now(w, state::RunningRButtonMove);
            return 1;
        }
        if (held & 0x20) {
            w->state_step = 4;
            return 1;
        }
        if (held & 0x4000) {
            if ((g_match_type & 0x10000) && (ctx->rec_a->location & 0x400))
                wrestler_set_state_now(w, state::RunAtCageClimber);
            else
                wrestler_set_state_now(w, (w->flags_ef & 1) ? state::RunningWeaponStrike : state::RunningShoulderBlock);
            return 1;
        }
        if ((held & 0x8000) && (ctx->in_0e & 0x8000) && (mem_u32(ctx->self->attrs + 0x38) & 0x1800000)) {
            wrestler_set_state_now(w, state::GrappleAttempt);
            w->state_step = 8;
            return 1;
        }
    }
    if (ctx->pressed & 0xF00)
        w->state_step = 3;
    return 0;
}

// N64 0x8010FF78: sets the run's direction from the d-pad (a side, a corner, along the apron, or
// with no d-pad toward the opponent, facing_to_opponent) and its step at walk_speed(8). 0 when the
// run cannot start.
s32 run_start(WrestlerCtx *ctx, s32 *dir) {
    Wrestler *w = ctx->self;
    w->flags86 = 0;
    w->flags8a &= 0xFFFB;
    w->flags_f0 &= 0xFD;
    u16 held = ctx->buttons;
    u16 f86;
    if (held & 0xF00) {
        w->dir = (s16)dpad_diagonal_quadrant(held);
        s32 corner = w->dir;
        if (corner == -1) {
            f86 = w->flags86 | 3;
        } else if (w->location & 1) {
            if (corner == nearest_corner(&w->point_120, 2))
                return 0;
            f86 = w->flags86 | 0xA;
        } else {
            f86 = w->flags86 | 0x13;
        }
    } else {
        f86 = 4;
    }
    w->flags86 = f86;
    if (w->flags86 & 1) {
        if (w->location & 2) {
            s32 along = side_input_check(w, ctx->buttons);
            if (!along)
                return 0;
            s32 side = w->ring_zone;
            if (along == 1) {
                side--;
                w->flags8a |= 4;
            } else {
                side++;
            }
            w->dir = (s16)(side & 3);
            *dir = side_angle(w->dir);
        } else if (w->flags86 & 0x10) {
            *dir = mem_s32(0x8014D1F8 + 4 * w->dir);
        } else {
            w->dir = (s16)dpad_to_side(ctx->buttons);
            *dir = mem_s32(A_SIDE_ANGLE + 4 * (s32)w->dir);
            if (run_toward_ropes_check(w))
                w->flags86 |= 0x1000;
        }
    }
    if (w->flags86 & 4) {
        u16 loc = w->location;
        if (loc & 1) {
            Vec3 p;
            side_point(&w->point_120, w->facing_to_opponent, &p);
            w->dir = (s16)nearest_corner(&p, 0);
            if (w->dir == -1) {
                w->dir = (s16)ring_zone_of(&p);
                *dir = w->facing_to_opponent;
                w->flags86 |= 1;
            } else {
                w->flags86 |= 8;
            }
        } else if (loc & 2) {
            s32 side = w->ring_zone;
            if (angle_diff(side_back(side), w->facing_to_opponent) >= 0) {
                side++;
            } else {
                side--;
                w->flags8a |= 4;
            }
            w->dir = (s16)(side & 3);
            *dir = side_angle(w->dir);
            w->flags86 |= 1;
        } else {
            w->flags86 |= 1;
            *dir = w->facing_to_opponent;
        }
    }
    if (w->flags86 & 8) {
        Vec3 c = {mem_f32(0x8014D1A8 + 12 * w->dir), mem_f32(0x8014D1AC + 12 * w->dir),
                  mem_f32(0x8014D1B0 + 12 * w->dir)};
        *dir = angle_to(&w->point_120, &c);
    }
    vec_from_angle(*dir, walk_speed(w, 8), &w->step_vec);
    if (wrestler_translate_pushed(w, &w->step_vec, -1) != -1)
        return 0;
    if (!(w->flags86 & 1))
        return 1;
    if (w->flags86 & 0x1000)
        return 1;
    if (!wrestler_translate_bounded(w, 2, &w->step_vec))
        return 1;
    u32 wall = w->zone_info;
    if (!(mem_u16(wall + 0x28) & 0x10))
        return 0;
    Vec3 n = {mem_f32(wall + 0x18), mem_f32(wall + 0x1C), mem_f32(wall + 0x20)};
    if ((f64)vec_normalize_two(&w->step_vec, &n) < -0.642787609)
        return 1;
    return 0;
}

// N64 0x80110D7C: one frame of the run: the step, what it meets, the ropes (rebound 0x04), the
// corners and walls. 1 when the state changed.
s32 run_step(WrestlerCtx *ctx, s32 mode) {
    Wrestler *w = ctx->self;
    wrestler_translate(w, &w->step_vec);
    if (match_can_interrupt())
        goto back_to_run;
    if (run_collide(w, mode, ctx->buttons))
        return 1;
    {
        u16 dirs = keep_in_bounds_self(w, 1.0f, 0, 2);
        if (dirs && mode == 1 && (w->flags86 & 1)) {
            u32 wall = w->zone_info;
            Vec3 n = {mem_f32(wall + 0x18), mem_f32(wall + 0x1C), mem_f32(wall + 0x20)};
            if (w->flags86 & 0x20) {
                // A whipped runner driven into a wall head on.
                if (!((f64)vec_normalize_two(&w->step_vec, &n) < -0.034899496))
                    goto corner;
                if ((mem_u16(wall + 0x28) & 0x200) && (w->flags_e9 & 0x10))
                    NM_UNPORTED("run_step: a whipped runner through a cage door (area_change)");
                u16 type = mem_u16(wall + 0x26);
                u16 fl = mem_u16(wall + 0x28);
                if (type & 0x30F8) {
                    if (fl & 1) {
                        // Into the steps: 0x16 near one of the four steps' corners (0x80146160),
                        // else 0x15.
                        if (type & 0x10)
                            for (s32 i = 0; i < 4; i++) {
                                Vec3 c = {mem_f32(0x80146160 + 12u * (u32)i), mem_f32(0x80146164 + 12u * (u32)i),
                                          mem_f32(0x80146168 + 12u * (u32)i)};
                                if ((f64)dist_to(&w->point_120, &c) < 55.0) {
                                    wrestler_set_state_now(w, state::RunKnockoutFall);
                                    return 1;
                                }
                            }
                        wrestler_set_state_now(w, state::RunIntoCorner);
                        return 1;
                    }
                    if (fl & 2) {
                        // Into the barrier or a wall: generic move 92 and a stagger or knockdown.
                        wrestler_set_generic_move(w, 92);
                        hit_move_copy(w, w);
                        w->strike_flags = 0;
                        w->hit_flags = 0x8000000;
                        wrestler_set_state_now(w, chance_check(500) ? state::KnockedDown : state::KnockedBack);
                        return 1;
                    }
                    if (fl & 0x400) {
                        wrestler_set_state_now(w, state::HitCageWall);
                        w->state_counter |= 1;
                        return 1;
                    }
                    wrestler_set_state_now(w, state::Running);
                    w->state_step = 3;
                    return 1;
                }
                if (!(type & 2))
                    goto corner;
                // Into the ropes: the rebound, but a charged whip (flags86 0x180) before frame 20 at a
                // side the knockdown can go over (knockdown_dir_from_zone) sends it over the top
                // (0x70), or in the cage into the cage wall as above.
                if ((w->flags86 & 0x180) == 0x180) {
                    w->hit_flags = 0x2000000;
                    if (w->anim_frame < 20 && knockdown_dir_from_zone(w)) {
                        if (g_match_type & 0x10000) {
                            wrestler_set_generic_move(w, 92);
                            hit_move_copy(w, w);
                            w->strike_flags = 0;
                            w->hit_flags = 0x8000000;
                            wrestler_set_state_now(w, chance_check(500) ? state::KnockedDown : state::KnockedBack);
                        } else {
                            wrestler_set_state_now(w, state::KnockedOverTopRope);
                        }
                        return 1;
                    }
                }
                wrestler_set_state_now(w, state::RunningRopeRebound);
                return 1;
            }
            if ((f64)vec_normalize_two(&w->step_vec, &n) < -0.034899496) {
                u16 type = mem_u16(wall + 0x26);
                if (type & 2) {
                    // Head on into the ropes: the rebound.
                    wrestler_set_state_now(w, state::RunningRopeRebound);
                    return 1;
                }
                if (type & 4) {
                    // Along the apron into its corner with C-down held: up onto the turnbuckle (state
                    // 0x0A) for the dive climb_check allows (move_num), facing the corner ahead.
                    s32 move = climb_check(w, ctx->rec_a);
                    if (move && (ctx->buttons & 4)) {
                        s32 rz = w->ring_zone;
                        w->flags8a &= 0xFFFB;
                        if (side_input_check(w, dirs) != mode) {
                            w->dir = (s16)rz;
                        } else {
                            w->flags8a |= 4;
                            w->dir = (s16)((rz - 1) & 3);
                        }
                        if (corner_free(w)) {
                            w->move_num = (s16)move;
                            w->flags8a |= 0x1000;
                            wrestler_set_state_now(w, state::TurnbucklePerch);
                            return 1;
                        }
                    }
                }
                if (input_weapon_grab(ctx))
                    return 1;
                goto back_to_run;
            }
        }
    }
corner:
    // Running at a corner (flags86 0x08).
    if (!(w->flags86 & 8))
        return 0;
    if (!(w->flags86 & 0x20)) {
        s32 nc = nearest_corner(&w->point_120, 2);
        if (nc != -1 && nc == w->dir && corner_free(w)) {
            if (ctx->in_byte) {
                if (w->flags_ef & 1)
                    goto back_to_run;
                wrestler_set_state_now(w, state::CornerTaunt);
                return 1;
            }
            if (ctx->buttons & 4)
                NM_UNPORTED("run_step: C-down at the corner (climb_check)");
        }
    }
    {
        s32 nc = nearest_corner(&w->point_120, 0);
        if (nc == -1)
            return 0;
        if (!(w->flags86 & 0xA0))
            return nc == w->dir ? (wrestler_set_state_now(w, state::Running), w->state_step = 3, 1) : 0;
        w->dir = (s16)nc;
        if (w->flags86 & 0x400) {
            wrestler_set_state_now(w, state::TurningInCorner);
            return 1;
        }
        if (w->flags86 & 0x800) {
            wrestler_set_state_now(w, state::CornerReboundStrike);
            return 1;
        }
        if (!corner_free(w))
            goto back_to_run;
        w->flags8a &= 0xFFFE;
        wrestler_set_state_now(w, state::SlumpedInCorner);
        return 1;
    }

back_to_run:
    wrestler_set_state_now(w, state::Running);
    w->state_step = 3;
    return 1;
}

// ---- states ----

// N64 0x8010EFB0: state 0x03, the run. Steps: 0 start, 1 the start-up turn, 2 running, 3 the
// turn after a new direction, 4 / 5 the L side-step, 6 the end.
void state_03_run(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags80 |= 0x20;
    illegal_man_check(w);
    u32 run_dir = A_RUN_DIR + 4 * ctx->slot;
    switch (w->state_step) {
    case 0: {
        s32 dir = mem_s32(run_dir);
        s32 ok = run_start(ctx, &dir);
        mem_w32(run_dir, (u32)dir);
        if (!ok) {
            if ((w->location & 2) && w->anim_id != 11794 && w->anim_id != 11745)
                wrestler_set_anim(w, 11745, 2);
            wrestler_set_state(w, state::Standing);
            w->flags80 &= 0xFFFE;
            w->flags8a |= 0x100;
            return;
        }
        w->run_counter = -1;
        if (w->flags86 & 0x1000) {
            wrestler_set_state_now(w, state::RunningRopeRebound);
            w->state_step = 5;
            return;
        }
        s32 mode = (w->location & 2) ? 2 : 3;
        w->run_dir_copy = mem_s32(run_dir);
        wrestler_set_anim(w, anim_pick(ctx, 4, 0), mode);
        s32 turn = angle_diff(mem_s32(run_dir), w->facing);
        if (w->anim_frames_left == 0)
            NM_UNPORTED("state_03_run: a start-up animation of no frames (division by zero)");
        turn = turn / w->anim_frames_left;
        w->state_step++;
        w->turn_step = turn;
    }
        [[fallthrough]];  // into the start-up
    case 1:
        w->flags86 |= 0x2000;
        if (w->anim_frames_left > 0) {
            if (w->location & 0x3C5)
                w->facing = angle_add(w->facing, w->turn_step);
            if (run_step(ctx, 0))
                return;
            if (run_exit_check(ctx, mem_s32(run_dir)))
                w->turn_step = mem_s32(run_dir);
            return;
        }
        w->facing = mem_s32(run_dir);
        if (w->location & 2) {
            wrestler_set_anim(w, anim_pick(ctx, 5, 0), 0);
            wrestler_translate(w, &w->step_vec);
            keep_in_bounds_self(w, 1.0f, 0, 2);
        } else {
            w->flags88 |= 1;
        }
        w->state_step++;
        return;
    case 2:
        if (stand_flags_update(w)) {
            w->state_step++;
            return;
        }
        if (w->location & 2)
            wrestler_set_anim(w, anim_pick(ctx, 5, 0), 0);
        else
            wrestler_set_anim(w, anim_pick(ctx, 5, 0), 1);
        if (w->flags86 & 8) {
            Vec3 c = {mem_f32(0x8014D1A8 + 12 * w->dir), mem_f32(0x8014D1AC + 12 * w->dir),
                      mem_f32(0x8014D1B0 + 12 * w->dir)};
            w->facing = angle_to(&w->point_120, &c);
            vec_from_angle(w->facing, walk_speed(w, 8), &w->step_vec);
        }
        if (run_step(ctx, 1))
            return;
        if (!(w->flags8e & 1) || w->anim_frame == 1) {
            object_underfoot(w);
            w->flags8e |= 1;
        }
        if (run_exit_check(ctx, w->facing)) {
            w->turn_step = w->facing;
            return;
        }
        if (run_inputs(ctx)) {
            s32 st = w->state;
            if (st != state::RunningWeaponStrike) {
                if ((st != state::RunningGroundAttack && st != state::RunningBreakUpPinOrHold) || !(w->flags_ef & 1))
                    weapon_release(w, 0);
            }
        }
        if (w->flags_f0 & 2)
            w->state_step++;
        return;
    case 3:
        wrestler_set_anim(w, anim_pick(ctx, 6, 0), (w->location & 2) ? 0 : 1);
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 1.0f, 0, 2);
        if (run_attack_input(ctx))
            return;
        move_event(ctx, 0);
        return;
    case 4:
        wrestler_set_anim(w, 11850, 3);
        w->state_step++;
        [[fallthrough]];  // into the side-step
    case 5: {
        // The L side-step: carried on by the run's step and the slide. Past the active window's start
        // a strike coming at it from the opponent (0x07, 0x08, 0x22 from step 2, 0x23-0x27) in its
        // hit window, within 120, from the front (facings 683+ apart) and to the side (the facing to
        // the opponent -1535..-228 off) is dodged (the opponent's flags8e 0x40). At frame 11 without
        // R the dash out of it (11851, step 6); at the end back to running (step 2).
        wrestler_translate(w, &w->step_vec);
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 1.0f, 0, 34);
        Wrestler *o = ctx->rec_a;
        if (!(w->anim_frame < w->anim_player[AP_ACTIVE_FROM]) && (w->location & o->location & 5) &&
            o->opponent == w->slot) {
            bool strike = false;
            switch (o->state) {
            case state::Strike: strike = o->state_step >= 2; break;
            case state::RunningShoulderBlock: case state::RunningWeaponStrike: case state::StrongStrike: case state::StrikeAtReturningWhippedRunner: case state::GetUpAttack: case state::WeaponSwingLight: case state::WeaponSwingHard: strike = true; break;
            default: break;
            }
            if (strike && !(o->anim_frame < o->anim_player[AP_HIT_FROM]) && !(o->anim_player[AP_HOLD_FRAME] < o->anim_frame) &&
                (f64)w->opp_distance < 120.0 && abs_i(angle_diff(w->facing, o->facing)) >= 683 &&
                (u32)(angle_diff(w->facing, w->facing_to_opponent) + 1535) < 1308)
                o->flags8e |= 0x40;
        }
        if (w->anim_frame == 11 && !(ctx->buttons & 0x20)) {
            wrestler_set_anim(w, 11851, 1);
            w->state_step++;
        }
        if (w->anim_frames_left <= 0)
            w->state_step = 2;
        return;
    }
    case 6:
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 1.0f, 0, 34);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EC2B0: the rebound animations' slide off the ropes (per-frame tables), along w->dir.
void rope_rebound_slide(Wrestler *w) {
    u32 table;
    switch (w->anim_id) {
    case 11953: case 11955: table = 0x80145060; break;
    case 11954: case 11956: table = 0x80145078; break;
    default: return;
    }
    f32 v = mem_f32(table + 4 * w->anim_frame);
    Vec3 d = {0.0f, 0.0f, 0.0f};
    switch (w->dir) {
    case 0: d.z = v; break;
    case 1: d.x = v; break;
    case 2: d.z = -v; break;
    case 3: d.x = -v; break;
    default: NM_UNPORTED("rope_rebound_slide: a direction out of range");
    }
    wrestler_translate(w, &d);
}

// N64 0x8010F624: state 0x04, the rope rebound (steps: 0 into the ropes, 1 off them, 2 the
// relaunch, 3 / 4 the grab of the ropes, 5-7 the rebound turned round off a corner bounce).
void state_04_run_rebound(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->runner_slot = -1;
    w->flags80 |= 0x20;
    switch (w->state_step) {
    case 0: {
        s32 anim = (w->flags_ef & 1) ? 2 * wrestler_move_anim(w, -1) + 11959 : 11953;
        wrestler_set_anim(w, anim, 1);
        w->facing = side_angle(w->dir);
        w->run_counter--;
        w->state_step++;
        w->rebound_count++;
    }
        [[fallthrough]];  // into the slide off the ropes
    case 1:
        run_collide_check(w, 2048);
        rope_rebound_slide(w);
        if (!(w->flags_e9 & 1) && !(w->flags_ef & 1) && (mem_u32(w->attrs + 0x34) & 0x2000) &&
            (w->flags86 & 0x20) && !(w->flags86 & 0x280) && w->rebound_count == 1 &&
            ((w->flags_ee & 4) || !(ctx->rec_a->flags_ee & 4)) && w->anim_frame < 3 && (ctx->pressed & 0xC030))
        {
            if (chance_roll(w, nullptr, 3)) {
                if (!(ctx->pressed & 0x8000)) {
                    wrestler_set_anim(w, 11955, 1);
                    w->state_step = 3;
                    return;
                }
                w->flags86 |= 0x200;
            }
        }
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, w->anim_id + 1, 1);
        w->facing = side_back(w->dir);
        w->state_step++;
        return;
    case 2: {
        rope_rebound_slide(w);
        if (w->anim_frames_left > 0)
            return;
        w->dir ^= 2;
        vec_from_angle(w->facing, walk_speed(w, 8), &w->step_vec);
        u16 f86 = w->flags86;
        w->flags86 = f86 & 0xFF7F;
        if ((w->flags86 & 0x220) == 0x20) {
            if (!(w->rebound_count < 2)) {
                wrestler_set_state_now(w, state::Standing);
                return;
            }
        } else {
            w->flags86 = (f86 & 0xFF5B) | 3;
            if (w->run_counter < 0)
                w->run_counter = -1;
            if (w->run_counter == 0) {
                wrestler_set_state_now(w, state::Running);
                w->state_step = 3;
                return;
            }
        }
        wrestler_set_state_now(w, state::Running);
        w->state_step = 2;
        w->flags_f0 &= 0xFD;
        return;
    }
    case 3:
        rope_rebound_slide(w);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, 11956, 1);
        w->facing = side_back(w->dir);
        w->state_step++;
        return;
    case 4:
        rope_rebound_slide(w);
        move_event(ctx, 0x44);
        return;
    default:
        NM_UNPORTED("state_04: the rebound off a corner bounce (steps 5-7)");
    }
}

// N64 0x80130038: puts the wrestler at the height of the animation's end place, and there.
void anim_end_height(Wrestler *w) {
    switch (w->anim_player[AP_END_PLACE]) {
    case 2: w->pos.y = 0.0f; break;
    case 1: case 3: case 7: w->pos.y = 90.0f; break;
    default: break;
    }
    anim_end_location(w);
    if (w->anim_player[AP_END_PLACE] == 0)
        w->pos.y = (w->location & 4) ? 0.0f : 90.0f;
}

// N64 0x8012FFF0: the animation's end: its pose, its turn, the lying side, the end place.
void anim_end_apply(Wrestler *w) {
    w->end_pose = w->anim_player[AP_END_POSE];
    w->facing = angle_add(w->facing, anim_turn(w));
    pose_is_lying(w);
    anim_end_height(w);
}

// N64 0x8012F660: the common end of a move's animation (once anim_frames_left < 2): by `bits`
// its damage, a chain, a statistic, then the settle (state 0x30) or the end pose.
s32 move_event(WrestlerCtx *ctx, u32 bits) {
    Wrestler *w = ctx->self;
    if (!(w->anim_frames_left < 2))
        return 0;
    if (bits & 0x40) {
        wrestler_set_generic_move(w, mem_u8(0x8014DB00 + (bits & 0x3F)));
        hit_move_copy(w, w);
        apply_damage(ctx->rec_a, w);
    }
    if (bits & 0x2000) {
        if (bits & 0x4000) {
            hit_move_copy(ctx->rec_a, w);
            apply_damage(ctx->rec_a, w);
        } else {
            hit_move_copy(w, ctx->rec_a);
            apply_damage(w, ctx->rec_a);
        }
    }
    if ((bits & 0x400) && move_chain_check(ctx))
        return 1;
    if (bits & 0x80) {
        Wrestler *who = (bits & 0x100) ? ctx->rec_a : w;
        stats_count(who, &g_wrestlers[who->opponent], (s32)(bits & 0x3F));
    }
    anim_end_apply(ctx->self);
    wrestler_unpair(ctx->self);
    if (bits & 0x200) {
        wrestler_settle_pose(ctx->self);
        if (bits & 0x800)
            move_aftermath_check((bits & 0x1000) ? ctx->rec_a : ctx->self, ctx->rec_a, ctx->self, 2);
        return 1;
    }
    w = ctx->self;
    if (w->flags8e & 0x40) {
        wrestler_set_state_now(w, state::SettleAfterAnimation);
        ctx->self->flags8e |= 0x40;
        return 1;
    }
    wrestler_set_state_now(w, state::SettleAfterAnimation);
    return 1;
}

// N64 0x8013F8E4: on the apron: back on the apron line of its zone.
void settle_zone(Wrestler *w) {
    w->flags88 &= 0xFFFE;
    w->ring_zone = ring_zone_of(&w->point_120);
    w->facing = side_back(w->ring_zone);
    w->pos.x = w->point_120.x;
    w->pos.z = w->point_120.z;
    switch (w->ring_zone) {
    case 0: w->pos.z = 265.0f; break;
    case 1: w->pos.x = 265.0f; break;
    case 2: w->pos.z = -265.0f; break;
    case 3: w->pos.x = -265.0f; break;
    default: break;
    }
}

// N64 0x800E7CA0: state 0x30: after an animation, step 0 plays the end pose's idle, step 1 picks
// the next state from it.
void state_30_settle(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->location & 8) {
        wrestler_set_state_now(w, w->ladder_rung == 5 ? state::OnLadderTop : state::OnLadder);
        return;
    }
    w->flags80 |= 1;
    if (w->state_step == 0) {
        u8 pose = w->anim_player[AP_END_POSE];
        w->end_pose = pose;
        s32 anim;
        s8 sp = (s8)pose;
        switch ((u32)(s32)sp < 50 ? sp : -1) {
        case 0: case 1: case 13:
            if (w->flags_ef & 1) {
                anim = anim_pick(ctx, 1, 0);
            } else if (w->location & 2) {
                settle_zone(w);
                anim = 11745;
            } else if (w->location & 0x400) {
                anim = 12804;
            } else {
                anim = (s8)w->end_pose == 1 ? 12519 : 12745;
            }
            break;
        case 2:
            anim = 12171;
            break;
        case 3:
            wrestler_set_state_now(w, state::OffBalance);
            return;
        case 4:
            if (w->location & 2) {
                settle_zone(w);
                anim = 12558;
            } else {
                anim = (s8)w->end_pose == 1 ? 12519 : 12745;
            }
            break;
        case 9:
            anim = 12092;
            break;
        case 12:
            ctx->rec_a->flags8c |= 0x200;
            anim = anim_pick(ctx, 5, 0);
            break;
        default: {
            u8 p = w->end_pose;
            if ((u32)(p - 20) < 28) {
                if (p & 1)
                    w->flags8a |= 1;
                else
                    w->flags8a &= 0xFFFE;
                anim = mem_s16(0x8014DA98 + 2 * (s8)w->end_pose);
            } else {
                anim = 12519;
            }
            break;
        }
        }
        s32 mode = (w->location & 2) ? 0 : ((w->location & 0x400) ? 9 : 1);
        wrestler_set_anim(w, anim, mode);
        w->state_step++;
        return;
    }
    if (w->state_step != 1)
        return;
    s8 sp = (s8)w->end_pose;
    switch ((u32)(s32)sp < 50 ? sp : -1) {
    case 0: case 1: case 13:
        if (w->flags8e & 0x40) {
            wrestler_set_anim(ctx->self, 12564, 3);
            wrestler_set_state_now(w, state::GrappleAttempt);
            w->state_step = 5;
        } else {
            wrestler_set_state_now(w, state::Standing);
        }
        break;
    case 2:
        if (w->flags8a & 0x10) {
            wrestler_set_state_now(w, state::GetUp);
            w->count_timer = 60;
        } else {
            wrestler_set_state_now(w, state::CrouchAfterGetUp);
        }
        w->state_step = 2;
        break;
    case 3:
        wrestler_set_state_now(w, state::OffBalance);
        break;
    case 4:
        wrestler_set_state_now(w, state::Dazed);
        break;
    case 9:
        wrestler_set_state_now(w, state::TurnbucklePerch);
        w->state_step = 3;
        w->count_timer = 0;
        w->aux_timer = 0;
        break;
    case 12:
        wrestler_set_state_now(w, state::RunToSidePoint);
        break;
    default:
        if ((u32)(w->end_pose - 20) < 28)
            wrestler_set_state_now(w, (w->flags8a & 0x10) ? state::GetUp : state::CrouchAfterGetUp);
        else
            wrestler_set_state_now(w, state::Standing);
        break;
    }
    w->flags8a &= 0xFFEF;
}

// N64 0x8010FB2C: state 0x05, hit by a running wrestler (run_collide): kept in bounds every frame.
// Step 0: on the apron facing its side's angle (0x8014D1E8) with animation 12907; with flags8a 0x02
// 11861; 70% the stagger 11860 (11861 with attrs +0x34 0x400); else 50 / 50 a fall (hit_flags
// 0x100000, strike_reaction_anim, step 2) or knocked back (hit_flags 0x190000, state 0x35). Step 1:
// the 4.0 slide until the animation ends, then standing. Step 2: flags8a 0x2000, the slide and the
// events (0x200).
void state_05_run_collide_hit(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
    switch (w->state_step) {
    case 0: {
        s32 anim;
        if (w->location & 2) {
            w->facing = mem_s32(0x8014D1E8 + 4u * (u32)w->ring_zone);
            anim = 12907;
        } else if (w->flags8a & 2) {
            anim = 11861;
        } else if (!chance_check(300)) {
            anim = (mem_u32(w->attrs + 0x34) & 0x400) ? 11861 : 11860;
        } else if (chance_check(500)) {
            w->hit_flags = 0x100000;
            wrestler_set_anim(w, strike_reaction_anim(w, 0), 3);
            w->state_step = 2;
            return;
        } else {
            w->hit_flags = 0x190000;
            wrestler_set_state_now(w, state::KnockedBack);
            return;
        }
        wrestler_set_anim(w, anim, 3);
        w->state_step++;
        return;
    }
    case 1:
        wrestler_slide(w, 4.0f);
        if (w->anim_frames_left <= 0)
            wrestler_set_state_now(w, state::Standing);
        return;
    case 2:
        w->flags8a |= 0x2000;
        wrestler_slide(w, 4.0f);
        move_event(ctx, 0x200);
        return;
    default:
        return;
    }
}

// N64 0x8011FDE8: states 0x9A / 0x9B, a run off the edge: out over the ropes or off a raised place
// (0x9A), or up onto a table / raised place (0x9B). Step 0 with state_counter 0x01 (from the apron)
// onto the apron (location 0x02): the slide into place, the ropes' bounds, 11855 / 11857. Otherwise a
// 3-frame turn (aux_timer) toward turn_step, and by the wall (+0x26 0x30E8 a raised place): 0x9A off
// it (13019, state_counter 0x02, location 0x04; bounds 0x80147B7C from the steps); 0x9B onto the
// announce table (id 166, 0x80), the stage parts (196-199, 0x200; 156-159, 0x100) or the steps (0x40,
// dropping a ladder), 13018 with their bounds; at the ropes 0x9B 11853 (location 0x03, no 0x04)
// and 0x9A 11852 (0x06, no 0x01) with the ring's bounds. Step 1: the slide or the turn (bounds 6.0
// unless state_counter 0x02). Step 2: the slide along (4.0) and bounds 2.0 without set bounds (none
// before frame 13 off a raised place), else bounds 2.0 / 6.0; in over the ropes (11853) from frame 17
// the ring (0x01), out (11852) from frame 21 the floor (0x04); the events, and at their end with
// flags8a 0x8000 the floor (pos.y 0).
void state_9A_climb(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        s32 anim, mode = 3;
        Vec3 n = {mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 28), mem_f32(w->zone_info + 32)};
        if (w->state_counter & 1) {
            w->location |= 2;
            pair_scripted_place(w, nullptr);
            w->climb_target = dist_to_ring_point(&n);
            if (w->climb_target & 0x300)
                w->bounds_override = 0x801470E8;
            if (w->climb_target & 0xC00)
                w->bounds_override = 0x801470F0;
            anim = w->state == state::RunOntoRaisedPlace ? 11857 : 11855;
            mode = 2;
        } else {
            w->aux_timer = 3;
            w->climb_target = dist_to_ring_point(&n);
            u32 z = w->zone_info;
            if (mem_u16(z + 0x26) & 0x30E8) {
                if (w->state != state::RunOntoRaisedPlace) {
                    w->state_counter |= 2;
                    if (w->location & 0x40)
                        w->bounds_override = 0x80147B7C;
                    w->location = 4;
                    anim = 13019;
                } else {
                    s16 id = mem_s16(z + 0x24);
                    anim = 13018;
                    if (id == 166) {
                        w->location = 0x80;
                        w->bounds_override = 0x8014BD0C;
                    } else if ((u16)(id - 196) < 4) {
                        w->location = 0x200;
                        s16 id2 = mem_s16(w->zone_info + 0x24);
                        w->bounds_override = (id2 == 0xC4 || id2 == 0xC6) ? 0x8014C6D8 : 0x8014C658;
                    } else if ((u16)(id - 156) < 4) {
                        w->location = 0x100;
                        if (w->climb_target & 0x300)
                            w->bounds_override = 0x8014B784;
                        if (w->climb_target & 0xC00)
                            w->bounds_override = 0x8014B804;
                    } else {
                        w->location = 0x40;
                        if (w->climb_target & 0x300)
                            w->bounds_override = 0x80147E28;
                        if (w->climb_target & 0xC00)
                            w->bounds_override = 0x80147E30;
                        if ((w->flags_ef & 1) && mem_s32(0x8016AE10 + 180u * (u32)w->held_obj + 4) == 25)
                            weapon_release(w, 0);
                    }
                }
            } else {
                if (w->climb_target & 0x300)
                    w->bounds_override = 0x80146C28;
                if (w->climb_target & 0xC00)
                    w->bounds_override = 0x80146C30;
                if (w->state == state::RunOntoRaisedPlace) {
                    anim = 11853;
                    w->location = (u16)((w->location & 0xFFFB) | 3);
                } else {
                    anim = 11852;
                    w->location = (u16)((w->location & 0xFFFE) | 6);
                }
            }
        }
        wrestler_set_anim(w, anim, mode);
        w->state_step++;
    }
        [[fallthrough]];
    case 1:
        if (w->state_counter & 1) {
            if (pair_slide_step(w, nullptr))
                w->state_step++;
            return;
        }
        w->facing = angle_add(w->facing, angle_diff(w->turn_step, w->facing) / (s16)w->aux_timer);
        w->aux_timer = (u16)(w->aux_timer - 1);
        if ((s16)w->aux_timer <= 0)
            w->state_step++;
        if (w->bounds_override && !(w->state_counter & 2))
            keep_in_bounds_self(w, 6.0f, 0, 46);
        return;
    case 2:
        if (w->bounds_override == 0) {
            if (!(w->state_counter & 2) || w->anim_frame >= 13) {
                wrestler_move_step_b(w, w->climb_target, 4.0f);
                keep_in_bounds_self(w, 2.0f, 0, 46);
            }
        } else {
            keep_in_bounds_self(w, (w->state_counter & 2) ? 2.0f : 6.0f, 0, 46);
        }
        if (w->anim_id == 11853 && w->anim_frame >= 17)
            w->location = 1;
        if (w->anim_id == 11852 && w->anim_frame >= 21)
            w->location = 4;
        if (move_event(ctx, 0) && (w->flags8a & 0x8000)) {
            w->flags8a &= 0x7FFF;
            w->location = 4;
            w->pos.y = 0.0f;
        }
        return;
    default:
        return;
    }
}



// N64 0x8010FCDC: state 0x06, R while running: step 0 the running move 208 (its animation by the
// move's style, speed 1); step 1 bounds (1.0) and the move's events, which do any hitting.
void state_06_run_r_move(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_start_move(w, ctx->slot, 208);
        wrestler_set_anim(w, anim_style(w, 0), 1);
        w->state_step++;
        return;
    case 1:
        keep_in_bounds_self(w, 1.0f, 0, 0xFFFFE);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

namespace {

// Whether the opponent is down for a dive: in a hold or carry state (flags80 0xC0) 0x44 / 0x45, or
// 0x48-0x4B when its move (its own with flags80 0x40, else its opponent's) lacks record flag
// 0x800000; otherwise lying (flags80 0x08).
bool dive_target_down(const Wrestler *o) {
    if (!(o->flags80 & 0xC0))
        return (o->flags80 & 8) != 0;
    s32 st = o->state;
    if (st >= 68 && st < 70)
        return true;
    if (st < 72 || st >= 76)
        return false;
    u32 move = (o->flags80 & 0x40) ? o->move : g_wrestlers[o->opponent].move;
    return !(mem_u32(move + 0x20) & 0x800000);
}

}  // namespace

// N64 0x8013A738: the dive w may make from the turnbuckle at `o` (0 for none), by w's trait attrs
// +0x38: at a wrestler starting 0x82 (step below 3) 279 / 280 (traits 0x100000, 0x200000 when it is
// on the floor or a raised place); on the apron or the cage wall 263 (traits 0x80); in the ring at a
// downed opponent 262 if allowed, else 261 (traits 0x40), at a standing one 257 if allowed, else 256
// (traits 0x20); on the floor or a raised place 264 at a downed one (traits 0x100), else 263 (0x80).
s32 climb_check(const Wrestler *w, const Wrestler *o) {
    u32 traits = mem_u32(w->attrs + 0x38);
    if (o->state == state::CarryDefender && o->state_step < 3) {
        u32 bit = (o->location & 0x44) ? 0x200000 : 0x100000;
        if (traits & bit)
            return bit == 0x200000 ? 280 : 279;
    }
    if ((o->location & 0x402) && (traits & 0x80))
        return 263;
    if (o->location & 1) {
        if (dive_target_down(o)) {
            if (move_allowed(w, o, 262))
                return 262;
            if (mem_u32(w->attrs + 0x38) & 0x40)
                return 261;
        } else {
            if (move_allowed(w, o, 257))
                return 257;
            if (mem_u32(w->attrs + 0x38) & 0x20)
                return 256;
        }
    }
    if (!(o->location & 0x44))
        return 0;
    if (dive_target_down(o))
        return (mem_u32(w->attrs + 0x38) & 0x100) ? 264 : 0;
    return (mem_u32(w->attrs + 0x38) & 0x80) ? 263 : 0;
}

// N64 0x8010EEE8: at corner `dir`: pos from 0x80146130 face down, 0x80146100 face up; with `set_facing` also
// the corner's facing (0x8014D1F8, 0x8014D208), which it returns either way.
static s32 corner_place(Wrestler *w, s32 dir, s32 set_facing) {
    u32 p = ((w->flags8a & 1) ? 0x80146130 : 0x80146100) + 12u * (u32)dir;
    w->pos = {mem_f32(p), mem_f32(p + 4), mem_f32(p + 8)};
    s32 angle = mem_s32(((w->flags8a & 1) ? 0x8014D1F8 : 0x8014D208) + 4u * (u32)dir);
    if (set_facing)
        w->facing = angle;
    return angle;
}

// N64 0x801118D8: run_step, and when it ends run_corner_stop (state 0x78).
static s32 corner_run_step(WrestlerCtx *ctx) {
    s32 r = run_step(ctx, 1);
    if (!r)
        r = run_corner_stop(ctx->self, 1);
    return r;
}

// N64 0x8011B5B0: a shove at a wall, by state_counter which: 0x03 over the ropes (11995 / 11997, the
// side's bounds), 0x3FC into the steps, a table or the barrier (13036 or 13038, by the wall's bounds set). The other's
// counter, flags78 and bounds follow; it is held hold_dist ahead, turned to face. 12 scale at places, else 4.
void state_0D_shove(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0: {
        w->hold_dist = dist_to(&w->pos, &o->pos);
        o->state_counter = w->state_counter;
        o->flags78 = w->flags78;
        pair_scripted_place(w, nullptr);
        u32 z = w->zone_info;
        Vec3 n = {mem_f32(z + 24), mem_f32(z + 28), mem_f32(z + 32)};
        w->climb_target = dist_to_ring_point(&n);
        u16 c = w->state_counter;
        s32 a;
        auto by_side = [&](u32 a300, u32 ac00) {
            if (w->climb_target & 0x300)
                w->bounds_override = a300;
            if (w->climb_target & 0xC00)
                w->bounds_override = ac00;
        };
        if (c & 3) {
            by_side(0x80146C28, 0x80146C30);
            a = (c & 1) ? 11995 : 11997;
        } else if (c & 0x3FC) {
            if (c & 0x28) {
                w->bounds_override = 0x8014BD0C;
            } else if (c & 0xC0) {
                by_side(0x8014B784, 0x8014B804);
            } else if (!(c & 0x300)) {
                by_side(0x80147E28, 0x80147E30);
            } else {
                s16 id = mem_s16(z + 0x24);
                bool first = (c & 0x100) ? (id == 0xC4 || id == 0xC6) : (id != 201 && id != 203);
                w->bounds_override = first ? 0x8014C6D8 : 0x8014C658;
            }
            a = ((c & 0x14C) ? 2 : 0) | 0x32EC;
        } else {
            NM_UNPORTED("state_0D_shove: no place in state_counter");
        }
        o->bounds_override = w->bounds_override;
        wrestler_set_anim(w, a, 2);
        w->state_step++;
        [[fallthrough]];
    }
    case 1: {
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        o->facing = angle_add(w->facing, 2048);
        Vec3 off = {0.0f, 0.0f, w->hold_dist}, t, d;
        point_at_offset(&w->pos, &off, w->facing, &t);
        t.y = o->pos.y;
        vec_sub(&o->pos, &t, &d);
        wrestler_translate(o, &d);
        [[fallthrough]];
    }
    case 2:
        pair_follow(w, o, nullptr, (w->state_counter & 0x3FC) ? 12.0f : 4.0f, 0, 0xFFFFE);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8011B934: shoved: over to the place the counter says (0x2B1 floor, 0x02 ring, 0x04 0x40, 0x08 0x80, 0x40
// 0x100, 0x100 0x200). Location 0x1000. Events by place and flags78 0x04; at the end on the floor if flags8a 0x8000.
void state_0E_shoved(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0: {
        u16 c = w->state_counter;
        if (c & 0x2B1) w->location = 4;
        else if (c & 2) w->location = 1;
        else if (c & 4) w->location = 64;
        else if (c & 8) w->location = 128;
        else if (c & 0x40) w->location = 256;
        else if (c & 0x100) w->location = 512;
        wrestler_set_anim(w, o->anim_id + 1, 2);
        w->state_step++;
        return;
    }
    case 1: {
        w->location |= 0x1000;
        pair_follow(w, o, nullptr, (w->state_counter & 0x3FC) ? 12.0f : 4.0f, 0, 0xFFFFE);
        u32 ev;
        if (w->state_counter & 0x2B1)
            ev = ((w->flags78 & 4) ? 0 : 0xC) | 0xA53;
        else
            ev = (w->flags78 & 4) ? 2644 : 2656;
        if (move_event(ctx, ev) && (w->flags8a & 0x8000)) {
            w->flags8a &= 0x7FFF;
            w->location = 4;
            w->pos.y = 0.0f;
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x801240F4: a running weapon strike (generic move 109, 12492 + 5 x kind; kind 3, strike_land 0x400).
void state_08_run_weapon_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 5 * wrestler_move_anim(w, 0) + 12492, 1);
        wrestler_set_generic_move(w, 109);
        w->state_step++;
        [[fallthrough]];
    case 1: {
        s32 t = strike_hit_test(w, 0, 3);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 0x400);
        }
        [[fallthrough]];
    }
    case 2:
        strike_keep_in_bounds(w);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800E6424: a run into the corner: turning off the wall over 6 frames. A mash (A, B, L, R; one try, not
// against a finisher mode) rolls chance_roll kind 3 to stay up (12004) rather than slump (12003, the time down).
void state_15_run_into_corner(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        wrestler_set_anim(w, 12005, 3);
        u32 z = w->zone_info;
        s32 a = vec_xz_angle(mem_f32(z + 0x18), mem_f32(z + 0x20));
        w->count_timer = 0;
        w->state_step++;
        w->turn_step = angle_diff(a, w->facing) / 6;
        [[fallthrough]];
    }
    case 1:
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer < 7)
            w->facing = angle_add(w->facing, w->turn_step);
        if (!(w->flags86 & 0x80) && ((w->flags_ee & 4) || !(ctx->rec_a->flags_ee & 4))) {
            if (!(w->state_counter & 1) && (ctx->pressed & 0xC030)) {
                w->state_counter |= 1;
                if (chance_roll(w, nullptr, 3))
                    w->state_counter |= 2;
            }
        }
        if (!(w->anim_frames_left > 0)) {
            s32 a = 12003;
            if (w->state_counter & 2) {
                a = 12004;
                w->state_step = 3;
            } else {
                w->state_step++;
            }
            wrestler_set_anim(w, a, 3);
            host_sfx_slot(w->slot, zone_kind(w->zone_info));
        }
        return;
    case 2:
        if (move_event(ctx, 0x24C)) {
            w->escape = (s16)escape_time(w, nullptr, 4);
            mash_count(w, 0, 0);
        }
        return;
    case 3:
        move_event(ctx, 0x45);
        return;
    default:
        return;
    }
}

// N64 0x8010E9AC: knocked out by the run, 12625, events 0xA4D.
void state_16_run_knockout_fall(WrestlerCtx *ctx) {
    wrestler_set_anim(ctx->self, 12625, 3);
    move_event(ctx, 0xA4D);
}

// N64 0x8010EC90: at corner `dir` turning to it (12055), events 0x46.
void state_19_run_stop_turn(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->state_step == 0) {
        corner_place(w, w->dir, 0);
        wrestler_set_anim(w, 12055, 0);
        if (wrestler_turn_toward(w, mem_s32(0x8014D1F8 + 4u * (u32)w->dir), 341, 0))
            w->state_step++;
    } else if (w->state_step == 1) {
        move_event(ctx, 0x46);
    }
}

// N64 0x800E74B8 (0x1D): L at a runner (11975, or 11976 by the wrestler's attrs +52 0x100000, or 0x400000 with
// a d-pad held): within the strike window it counts as ducking (state_counter 0x01), and a runner in 0x03 step 2 it
// touches goes over (0x1E). After the window the slide; bounds; events.
void state_1D_l_vs_runner(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->state_counter &= 0xFFFE;
    if (w->state_step == 0) {
        u32 at = mem_u32(w->attrs + 52);
        u32 v = (at & 0x400000) ? (u32)(ctx->buttons & 0xF00) : (at & 0x100000);
        wrestler_set_anim(w, v ? 11976 : 11975, 3);
        w->state_step++;
        return;
    }
    if (w->state_step > 2)
        return;
    if (w->state_step == 1 && !(w->anim_frame < w->anim_player[AP_HIT_FROM]) && !(w->anim_player[AP_HOLD_FRAME] < w->anim_frame)) {
        w->state_counter |= 1;
        s32 t = wrestler_translate_pushed(w, nullptr, -1);
        if (t != -1 && t < 4) {
            Wrestler *x = &g_wrestlers[t];
            if (x->state == state::Running && x->state_step == 2) {
                x->opponent = w->slot;
                x->pair_other = w->slot;
                wrestler_set_state_now(x, state::RunnerHopOver);
                w->state_step++;
            }
        }
    }
    if (w->anim_player[AP_HOLD_FRAME] < w->anim_frame)
        wrestler_slide(w, 4.0f);
    keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
    move_event(ctx, 0);
}

// N64 0x800E7658: state 0x1E, the runner hopped over someone ducking (11978; generic move 31, the hit, flags86
// 0x1A0), running on; at its end back to the run (0x03 step 2), the rebound count kept if it had a run going.
void state_1E_run_hop_over(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        weapon_release(w, 0);
        wrestler_set_anim(w, 11978, 2);
        w->flags86 |= 0x1A0;
        wrestler_set_generic_move(w, 31);
        hit_move_copy(w, w);
        apply_damage(ctx->rec_a, w);
        w->state_step++;
        [[fallthrough]];
    case 1:
        run_step(ctx, 1);
        if (w->anim_frames_left > 0)
            return;
        w->flags86 &= 0xFEFF;
        if (w->run_timer > 0)
            w->flags86 &= 0xFF7F;
        else
            w->rebound_count = 0;
        w->run_timer = (s16)(w->run_timer + 1);
        wrestler_set_state_now(w, state::Running);
        w->state_step = 2;
        w->flags_f0 &= 0xFD;
        return;
    default:
        return;
    }
}

// N64 0x8013BB78: a running move at a target off the side (move_num; 267 straight to step 4, else the take-off, the
// flight by special_move_kind 1): the side's bounds and direction by run_target. Flags8a 0x40 until past the hit
// window, which also hits the others (run_collide_check). At the end the stun 0x30 with a hit, else special_move_land.
void state_71_running_target_move(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        w->flags_ea = 0;
        wrestler_start_move(w, ctx->slot, w->move_num);
        wrestler_set_anim(w, anim_style(w, 0), 1);
        if (w->run_target & 1) {
            w->climb_target = 3072;
            w->bounds_override = 0x80146C30;
        } else {
            w->climb_target = 768;
            w->bounds_override = 0x80146C28;
        }
        if (w->move_num == 267)
            w->state_step = 4;
        else
            w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, anim_style(w, 1), 1);
        w->state_step++;
        return;
    case 2:
        if (w->anim_frame >= w->anim_player[AP_HIT_FROM])
            w->location = (u16)((w->location & 0xFFFA) | 4);
        if (special_move_kind(w, ctx->rec_a, 1)) {
            if (w->flags_ea & 8)
                stats_count(w, ctx->rec_a, 0);
            w->state_step++;
        }
        if (w->anim_player[AP_END_PLACE] != 2)
            w->location = (u16)((w->location & 0xFFFA) | 1);
        [[fallthrough]];
    case 3:
        w->flags8a |= 0x40;
        if (!(w->flags_ea & 8))
            g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        run_collide_check(w, 0x400);
        if (w->anim_frame >= w->anim_player[AP_HOLD_FRAME]) {
            w->flags8a &= 0xFFBF;
            if (w->anim_player[AP_END_PLACE] == 2)
                aerial_height(w, 0);
            else
                wrestler_move_step_b(w, w->climb_target, 4.0f);
            keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        }
        if (!(w->anim_frames_left > 0)) {
            anim_end_apply(w);
            if (w->flags_ea & 8)
                wrestler_set_state_now(w, state::SettleAfterAnimation);
            else
                special_move_land(w);
        }
        return;
    case 4:
        if (w->anim_player[AP_END_PLACE] != 2) {
            wrestler_move_step_b(w, w->climb_target, 4.0f);
            keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        }
        move_event(ctx, 0x2000);
        return;
    default:
        return;
    }
}

// N64 0x8013B9E0: the running flying move (278) off the run to a corner: run_step until it takes off, then the flight
// (aerial_step, special_move_kind 1). At the end the stun 0x30 with a hit, else special_move_land.
void state_78_running_move(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        if (corner_run_step(ctx)) {
            w->flags_ea = 0;
            wrestler_start_move(w, ctx->slot, 278);
            wrestler_set_anim(w, anim_style(w, 0), 1);
            w->state_step++;
        }
        return;
    case 1:
        if (special_move_kind(w, o, 1)) {
            if (w->flags_ea & 8)
                stats_count(w, o, 0);
            w->state_step++;
        }
        [[fallthrough]];
    case 2:
        if (!(w->flags_ea & 8))
            g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        aerial_height(w, 0);
        aerial_step(w, o, 1);
        if (!(w->anim_frames_left > 0)) {
            anim_end_apply(w);
            if (w->flags_ea & 8)
                wrestler_set_state_now(w, state::SettleAfterAnimation);
            else
                special_move_land(w);
        }
        return;
    default:
        return;
    }
}

// N64 0x8011F590: rolling out of the ring or back: to the ring side `dir` (ring_zone_of). Step 0 12135 / 12137 (12136 /
// 12138 face down) by the angle; step 1 at its end the roll 12139 / 12140 toward the next side, its step to the
// 185 line and turn spread over its length. Step 2 the roll; step 3 the events (0x200).
void state_99_roll(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        w->dir = (s16)ring_zone_of(&w->pos);
        s32 d = angle_diff(w->facing, mem_s32(0x8014D1D8 + 4u * (u32)w->dir));
        s32 a;
        if (w->flags8a & 1)
            a = (d < 1 ? 2 : 0) | 0x2F68;
        else
            a = d > 0 ? 12137 : 12135;
        wrestler_set_anim(w, a, 6);
        w->state_step++;
        return;
    }
    case 1: {
        keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        if (!(w->flags8a & 1))
            w->facing = angle_add(w->facing, 2048);
        anim_end_apply(w);
        s32 id = w->anim_id, a, to;
        if (id >= 12135 && id < 12137) {
            a = 12139;
            to = mem_s32(0x8014D1D8 + 4u * (u32)((w->dir + 1) & 3));
        } else if (id >= 12137 && id < 12139) {
            a = 12140;
            to = mem_s32(0x8014D1D8 + 4u * (u32)((w->dir - 1) & 3));
        } else {
            NM_UNPORTED("state_99_roll: an animation out of range");
        }
        wrestler_set_anim(w, a, 1);
        s32 n = w->anim_player[AP_FRAMES];
        w->turn_step = angle_diff(to, w->facing) / n;
        switch (w->dir) {
        case 0:
            w->step_vec.x = 0.0f;
            w->step_vec.z = (f32)((185.0 - (f64)w->point_120.z) / (f64)n);
            break;
        case 1:
            w->step_vec.x = (f32)((185.0 - (f64)w->point_120.x) / (f64)n);
            w->step_vec.z = 0.0f;
            break;
        case 2:
            w->step_vec.x = 0.0f;
            w->step_vec.z = (f32)((-185.0 - (f64)w->point_120.z) / (f64)n);
            break;
        case 3:
            w->step_vec.x = (f32)((-185.0 - (f64)w->point_120.x) / (f64)n);
            w->step_vec.z = 0.0f;
            break;
        default:
            break;
        }
        w->step_vec.y = 0.0f;
        w->state_step++;
        return;
    }
    case 2:
        w->facing = angle_add(w->facing, w->turn_step);
        wrestler_translate(w, &w->step_vec);
        keep_in_bounds_self(w, 2.0f, 0, 0xFFFFE);
        if (!(w->anim_frames_left > 0)) {
            wrestler_set_anim(w, w->anim_id + 2, 1);
            w->state_step++;
        }
        return;
    case 3:
        move_event(ctx, 0x200);
        return;
    default:
        return;
    }
}

// N64 0x800EAD70: leapfrogging a runner (11979; its time down gets the jump's length unless it reversed). Sliding 4,
// or 8 over someone down from frame 14; the events.
void state_9E_leapfrog(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->state_step == 0) {
        wrestler_set_anim(w, 11979, 3);
        Wrestler *r = &g_wrestlers[w->runner_slot];
        if (!(r->state_counter & 2))
            r->escape = (s16)(r->escape + w->anim_player[AP_FRAMES]);
        w->facing = w->turn_step;
        w->state_step++;
    }
    if (w->state_step == 1) {
        s32 t = wrestler_translate_pushed(w, nullptr, -1);
        if (t != -1 && t < 4) {
            if (!(g_wrestlers[t].flags80 & 8))
                wrestler_slide(w, 4.0f);
            else if (w->anim_frame >= 14)
                wrestler_slide(w, 8.0f);
        }
        move_event(ctx, 0);
    }
    keep_in_bounds_self(w, 1.0f, 0, 2);
}

// N64 0x800EAEDC: side-stepping a runner (11980, flags80 0x20; its time down as 0x9E). At the end with C-down held the
// run on (anim_pick 5, 0x03 step 2), else anim_pick 6, sliding 12, the events.
void state_9F_run_dodge(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags80 |= 0x20;
    if (w->state_step == 0) {
        wrestler_set_anim(w, 11980, 3);
        Wrestler *r = &g_wrestlers[w->runner_slot];
        if (!(r->state_counter & 2))
            r->escape = (s16)(r->escape + w->anim_player[AP_FRAMES]);
        w->state_step++;
    } else if (w->state_step == 1) {
        s32 t = wrestler_translate_pushed(w, nullptr, -1);
        if (t != -1 && (t >= 4 || !(g_wrestlers[t].flags80 & 8)))
            wrestler_slide(w, 4.0f);
        if (!(w->anim_frames_left > 0)) {
            anim_end_apply(w);
            if (ctx->buttons & 4) {
                wrestler_set_anim(w, anim_pick(ctx, 5, 0), 3);
                wrestler_set_state_now(w, state::Running);
                w->state_step = 2;
                w->flags_f0 &= 0xFD;
            } else {
                wrestler_set_anim(w, anim_pick(ctx, 6, 0), 1);
                w->state_step++;
            }
        }
    } else if (w->state_step == 2) {
        wrestler_slide(w, 12.0f);
        move_event(ctx, 0);
    }
    keep_in_bounds_self(w, 1.0f, 0, 2);
}

// N64 0x80129F04: a running grapple: style 1 or 3 by rear (flags78 0x80) against the move (104 the other way); the move's
// damage entry +4, if any, set to 73 / 30 for its rear / front variant. Events 0x480.
void state_A0_running_grapple_attacker(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    state_bounds(w, ctx->rec_a, nullptr, 0);
    if (w->state_step == 0) {
        bool rear = (w->flags78 & 0x80) != 0;
        s32 style = (w->move_num == 104 ? rear : !rear) ? 3 : 1;
        if (mem_u16(w->move + 4)) {
            if (w->move_num == 104) {
                if (rear)
                    mem_w16(w->move + 4, 73);
            } else if (!rear) {
                mem_w16(w->move + 4, 30);
            }
        }
        wrestler_move_pos_to_pelvis(w);
        wrestler_set_anim(w, anim_style(w, style), 2);
        w->state_step++;
    } else if (w->state_step == 1) {
        finisher_effects(w);
        move_event(ctx, 0x480);
    }
}

// N64 0x8012A034: taken by a running grapple: the attacker's animation + 1, the slide; events 0x7A00.
void state_A1_running_grapple_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    switch (w->state_step) {
    case 0:
        pair_scripted_place(w, o);
        wrestler_set_anim(w, o->anim_id + 1, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        return;
    case 2:
        finisher_effects(w);
        move_event(ctx, 0x7A00);
        return;
    default:
        return;
    }
}

// N64 0x8010E9E8: slumped in a corner after being run into it: turned to the corner's facing (a move 13 hit once the
// fall anim ends), then held by the escape count (frame_flags 0x05) until recover_tick lets it stand; a finished
// elimination falls instead. flags86 0x40 face-up takes the short slide (12060) with step 6.
void state_18_corner_slump(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u16 up = w->flags8a & 1;
    switch (w->state_step) {
    case 0: {
        s32 angle = corner_place(w, w->dir, 0);
        wrestler_set_anim(w, up | 0x2F12, 2);
        if (wrestler_turn_toward(w, angle, 341, 0)) {
            if (up || !(w->flags86 & 0x40)) {
                wrestler_set_anim(w, up | 0x2F14, 2);
                w->state_step++;
            } else {
                wrestler_set_anim(w, 12060, 2);
                w->state_step = 6;
            }
        }
        return;
    }
    case 1:
        w->frame_flags |= 1;
        if (w->anim_frames_left <= 0) {
            wrestler_set_generic_move(w, 13);
            hit_move_copy(w, w);
            apply_damage(ctx->rec_a, w);
            wrestler_set_step(w, 2);
        }
        return;
    case 2:
        w->escape = escape_time(w, nullptr, 2);
        w->fall_chance = 0;
        mash_count(w, 0, 0);
        w->state_step++;
        // fall through
    case 3:
        corner_place(w, w->dir, 1);
        wrestler_set_anim(w, up | 0x2F18, 0);
        w->fall_chance++;
        w->state_step++;
        // fall through
    case 4:
        w->frame_flags |= 5;
        if ((g_match_flags[1] & 0x40) && (w->end_flags & 4)) {
            wrestler_set_anim(w, up | 0x2F1A, 2);
            w->state_step++;
        } else if (recover_tick(w, ctx->pressed)) {
            if (ctx->in_byte) {
                w->escape = 0;
            } else {
                wrestler_set_anim(w, up | 0x2F1A, 0);
                w->state_step++;
            }
        }
        return;
    case 5:
        w->frame_flags |= 3;
        keep_in_bounds_self(w, 2.0f, 0, 2);
        move_event(ctx, 0);
        return;
    case 6:
        w->flags8c |= 0x40;
        wrestler_slide(w, 4.0f);
        move_event(ctx, 512);
        return;
    default:
        return;
    }
}

}  // namespace nm

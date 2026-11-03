// Standing (state 0x01) and the input checks it runs in priority order. Each check returns 1 when it
// took over (changed the state); the paths that lead into systems not decompiled yet stop a
// replay where they start.
#include "nm_funcs.h"

namespace nm {

// ---- tag rules and the like (no effect outside tag and survival matches) ----

// N64 0x80137D90: whether a wrestler may act (in tag matches, a partner only while the legal
// wrestlers are outside).
s32 wrestler_may_act(const Wrestler *w) {
    if (!(g_match_type & 0x2000))
        return 1;
    if (g_match_rules & 0x40)
        return 1;
    if (!(mem_s32(A_ARENA) < 10))
        return 1;
    if (w->flags_e9 & 0x10)
        return 1;
    u16 pf = w->partner_flags80;
    if ((pf & 0xC0) && !(pf & 0x400))
        return 1;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1 || (o->end_flags & 1) || !(o->flags_e9 & 0x10))
            continue;
        if (!(o->location & 3))
            return 1;
    }
    return 0;
}

// N64 0x80137E74: the idle flags of a wrestler who may not act; non-zero sends it to its corner.
s32 stand_flags_update(Wrestler *w) {
    if (wrestler_may_act(w)) {
        w->idle_flags &= 0xFA;
        return 0;
    }
    if (!(w->act_flags & 0xE) && (w->location & 2)) {
        s32 k = w->slot & 2;
        if (w->ring_zone == mem_s8(0x8014DCA0 + k) || w->ring_zone == mem_s8(0x8014DCA1 + k)) {
            w->idle_flags &= 0xFA;
            return 0;
        }
    }
    w->idle_flags |= 1;
    return (w->idle_flags & 2) != 0;
}

// N64 0x801203D0: whether a spare slot (flags_e9 & 3) is allowed in the ring now.
s32 illegal_check(Wrestler *w) {
    u32 type = g_match_type;
    if (type & 0xC000)
        return (w->flags_e9 & 0x14) != 0;
    if (type & 0x21000) {
        s32 in_play = 0;
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *o = &g_wrestlers[i];
            if (o->roster_id == -1)
                continue;
            if (o->flags_e9 & 0x10)
                in_play += (o->location & 4) != 0;
            else if (o->flags_e9 & 2)
                in_play += (o->flags_f1 & 2) != 0;
        }
        if ((w->flags_e9 & 1) && ((w->partner_flags80 & 0xC0) || in_play))
            return 1;
        if (w->flags_e9 & 2) {
            if (w->flags_f1 & 2)
                return 1;
            if ((w->partner_flags80 & 0xC0) || in_play)
                return 1;
        }
        if (w->flags_e9 & 0x14)
            return 1;
    }
    if (!(g_match_type & 0x2000))
        return 0;
    if (w->flags_e9 & 0x10)
        return 1;
    if ((w->location & 4) && w->state != state::Running)
        return 1;
    return idle_check(w) ? 1 : 0;
}

// N64 0x80137FD0: in tag matches, the illegal partner's idle bits by state.
void illegal_man_check(Wrestler *w) {
    if (!(g_match_type & 0x2000) || (g_match_rules & 0x40))
        return;
    if (w->partner == w->slot)
        return;
    u8 bits = 0;
    switch (w->state) {
    case state::Standing: if (w->location & 2) bits = 8; break;
    case state::Running: if ((w->location & 2) && w->state_step < 4) bits = 8; break;
    case state::ApronDodge: bits = 8; break;
    case state::TurnbucklePerch: if (w->act_flags & 0xA) bits = 16; break;
    case state::Taunt: if (w->state_step == 1 && (w->location & 2)) bits = 8; break;
    case state::ApronPartnerWaiting: bits = w->state_step < 2 ? 8 : 16; break;
    default: break;
    }
    w->idle_flags = (w->idle_flags & 0xE7) | bits;
}

// N64 0x8011CD60: adds to health (capped by health_max); at 0 the run-in roll for its side. Mode 0
// and 2 also remember the amount (+0xBE) and flash on a loss; mode 2 changes nothing else.
void wrestler_add_health(Wrestler *w, s32 amount, s32 mode) {
    if (mode != 2) {
        w->health = (s16)(w->health + amount);
        if (w->health_max < w->health)
            w->health = w->health_max;
        if (!(w->health > 0)) {
            w->health = 0;
            if ((g_match_type & 0x1000) && w->slot < 2) {
                u16 armed = mem_u16(0x801465C0 + 2 * w->slot);
                if (g_match_type & 0x400000) {
                    u16 once = mem_u16(0x801465B0 + 2 * w->slot);
                    if (!(mem_u16(A_RUNIN_FLAGS) & once)) {
                        if (chance_check(250))
                            mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) | armed);
                        mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) | once);
                    }
                } else if (chance_check(500)) {
                    mem_w16(A_RUNIN_FLAGS, mem_u16(A_RUNIN_FLAGS) | armed);
                }
            }
        }
    }
    if (mode == 0 || mode == 2) {
        w->finisher_heat = (s16)abs_i(amount);
        if (amount < 0) {
            w->hit_flash_timer = 30;
            w->flags_ee |= 0x20;
        }
    }
}

// N64 0x8011CF14: op 0 restarts, op 1 the regen tick: every 60 frames (90 with bonus_flags 0x02)
// adds the attribute record's regen (+3, or +4 with the bit).
void health_regen(Wrestler *w, s32 op) {
    if (op == 0) {
        w->regen_timer = 0;
        return;
    }
    if (op != 1)
        return;
    s32 every, amount;
    if (w->bonus_flags & 2) {
        every = 90;
        amount = mem_s8(w->attrs + 4);
    } else {
        every = 60;
        amount = mem_s8(w->attrs + 3);
    }
    w->regen_timer++;
    if (w->regen_timer < every)
        return;
    w->regen_timer = 0;
    wrestler_add_health(w, amount, 1);
}

// N64 0x800EC484: C-right: the nearest other wrestler (not the partner or the opponent) becomes
// the opponent.
s32 opponent_switch_nearest(Wrestler *w, u16 pressed) {
    if (!(pressed & 1))
        return 0;
    struct Near {
        s32 slot;
        s32 pad;
        f32 dist;
    } list[4];
    for (s32 i = 3; i >= 0; i--)
        list[i].slot = -1;
    s32 n = 0;
    for (s32 i = 0; i < 4; i++) {
        Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1 || i == w->slot)
            continue;
        list[n].slot = i;
        list[n].dist = dist_to(&w->point_120, &o->point_120);
        n++;
    }
    for (s32 i = 0; i < n - 1; i++)
        for (s32 k = i + 1; k < n; k++)
            if (list[k].dist < list[i].dist) {
                Near t = list[i];
                list[i] = list[k];
                list[k] = t;
            }
    for (s32 i = 0; i < 4; i++) {
        if (list[i].slot == w->partner)
            list[i].slot = -1;
        if (list[i].slot == w->opponent)
            list[i].slot = -1;
    }
    for (s32 i = 0; i < 4; i++) {
        if (list[i].slot != -1) {
            w->opponent = list[i].slot;
            host_hud("nullsub_hud_a", (s8)w->slot, (s8)list[i].slot);
            return 1;
        }
    }
    return 0;
}

// N64 0x8012BE50: the ring corner within reach (kind 0-3: 43.84, 56.57, 73.54, 127.28) of p, or -1.
s32 nearest_corner(const Vec3 *p, s32 kind) {
    f32 reach = 0.0f;
    switch (kind) {
    case 0: reach = 43.840622f; break;
    case 1: reach = 56.5685425f; break;
    case 2: reach = 73.5391083f; break;
    case 3: reach = 127.279221f; break;
    default: NM_UNPORTED("nearest_corner: kind out of range");
    }
    for (s32 i = 0; i < 4; i++) {
        Vec3 c = {mem_f32(0x8014D1A8 + 12 * i), mem_f32(0x8014D1AC + 12 * i), mem_f32(0x8014D1B0 + 12 * i)};
        if (dist_to(&c, p) <= reach)
            return i;
    }
    return -1;
}

// N64 0x8013F820: nobody else is using the corner w->dir names.
s32 corner_free(const Wrestler *w) {
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *o = &g_wrestlers[i];
        if (o->roster_id == -1 || o->dir != w->dir)
            continue;
        if (o->state == state::SlumpedInCorner && o->state_step < 5)
            return 0;
        if (o->state == state::TurnbucklePerch || o->state == state::CornerTaunt || o->state == state::AerialAttackAtDownedOpponent)
            return 0;
        if (o->frame_flags & 8)
            return 0;
    }
    return 1;
}

// ---- the input checks ----

// N64 0x8013D8BC: on the apron, walking along it into the next corner (state 0x02): the d-pad held
// along the apron (side_input_check: 1 or 2) for 9 frames. The corner is the ring zone (one back,
// mod 4, for side 1, which sets flags8a 0x04); nobody may be walking to the same corner already.
s32 input_apron_dpad(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (!(w->location & 2)) {
        w->apron_hold = 0;
        return 0;
    }
    s32 along = side_input_check(w, w->dpad_held);
    if (!along || !(w->dpad_held & ctx->buttons) || !may_act_b(w)) {
        w->apron_hold = 0;
        return 0;
    }
    if (++w->apron_hold < 9)
        return 0;
    s32 corner = w->ring_zone;
    if (along == 1)
        corner = (corner - 1) & 3;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *o = &g_wrestlers[i];
        if (o->roster_id != -1 && o->state == state::ApronCornerWalk && o->dir == corner)
            return 0;
    }
    w->flags8a &= 0xFFFB;
    if (along == 1)
        w->flags8a |= 4;
    w->dir = (s16)corner;
    w->apron_hold = 0;
    wrestler_set_state_now(w, state::ApronCornerWalk);
    return 1;
}

// N64 0x8013E280: at a free corner the d-pad points at (0x8014D228 by corner). In the ring: C-down
// with a dive available (climb_check) climbs up for it (0x0A, move_num); A with trait 0x4000 at a downed
// opponent in the ring, 0x74 (weapon dropped); the stay-down input (in_byte) sits up on the corner
// (0x1C, or 0x1B with a weapon) unless flags_e9 0x01. On the apron, C-down with a dive available and
// the d-pad along it (0x8014DF6C by side) climbs the corner ahead when it is free.
s32 input_climb_turnbuckle(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->location & 1) {
        w->dir = (s16)nearest_corner(&w->point_120, 1);
        if (w->dir != -1 && (ctx->buttons & 0xF00) == mem_u16(0x8014D228 + 2u * (u32)w->dir) && corner_free(w)) {
            if (ctx->pressed & 4) {
                s32 m = climb_check(w, ctx->rec_a);
                if (m) {
                    w->move_num = (s16)m;
                    wrestler_set_state_now(w, state::TurnbucklePerch);
                    return 1;
                }
            }
            const Wrestler *o = ctx->rec_a;
            if ((mem_u32(w->attrs + 0x38) & 0x4000) && (ctx->pressed & 0x8000) && (o->location & 1) && (o->flags80 & 8)) {
                weapon_release(w, 0);
                wrestler_set_state_now(w, state::AerialAttackAtDownedOpponent);
                return 1;
            }
            if (ctx->in_byte && !(w->flags_e9 & 1)) {
                wrestler_set_state_now(w, (w->flags_ef & 1) ? state::Taunt : state::CornerTaunt);
                return 1;
            }
        }
    }
    if (!(w->location & 2) || !(ctx->pressed & 4))
        return 0;
    s32 m = climb_check(w, ctx->rec_a);
    if (!m)
        return 0;
    if (!(ctx->buttons & w->dpad_held & mem_u16(0x8014DF6C + 2u * (u32)w->ring_zone)))
        return 0;
    w->flags8a &= 0xFFFB;
    if (side_input_check(w, w->dpad_held) == 1) {
        w->flags8a |= 4;
        w->dir = (s16)((w->ring_zone - 1) & 3);
    } else {
        w->dir = (s16)w->ring_zone;
    }
    if (!corner_free(w))
        return 0;
    w->move_num = (s16)m;
    wrestler_set_state_now(w, state::TurnbucklePerch);
    return 1;
}

// N64 0x8013B884: the apron grapple / springboard move at `o` (0 for none) by w's trait attrs +0x38:
// at an opponent in a hold or carry (flags80 0xC0) 274 for 0x44 / 0x45, 273 / 274 for 0x48-0x4B (by
// the move's record flag 0x800000), 273 for 0x7F / 0x80, 281 for one in the ring starting 0x82, else
// 273; otherwise 275 when allowed, else 274 at a downed one and 273 at a standing one. 273 needs
// traits 0x20000, 274 0x40000, 281 0x400000.
s32 apron_grapple_start(const Wrestler *w, const Wrestler *o) {
    s32 m = 273;
    if (o->flags80 & 0xC0) {
        switch (o->state) {
        case state::PinAttacker: case state::PinDefender:
            m = 274;
            break;
        case state::GroundHoldAttacker: case state::GroundHoldDefender: case state::PinFromMoveAttacker: case state::PinFromMoveDefender: {
            u32 move = (o->flags80 & 0x40) ? o->move : g_wrestlers[o->opponent].move;
            m = (mem_u32(move + 0x20) & 0x800000) ? 273 : 274;
            break;
        }
        case state::CarryDefender:
            if (o->state_step < 3 && (o->location & 1))
                m = 281;
            break;
        default:
            break;
        }
    } else {
        if (move_allowed(w, o, 275))
            return 275;
        m = (o->flags80 & 8) ? 274 : 273;
    }
    u32 need = m == 273 ? 0x20000 : (m == 274 ? 0x40000 : 0x400000);
    return (mem_u32(w->attrs + 0x38) & need) ? m : 0;
}

// N64 0x8013D630: A on the apron. Unless over-the-top rules (0x400): at an opponent on the floor or a
// raised place (0x44) from the ropes' side (+0x26 0x02) the weapon drops and the dive over the ropes
// (0x72); with trait 0x8000 from the apron's edge (0x04 with a way down) and the d-pad toward it the
// jump down onto it (0x75); at one on the floor from a raised place's edge (0x10E0 with a way down,
// the d-pad toward it, facing it within 80 degrees of the wall's normal) the dive from there (0xC0).
// Then the springboard / apron grapple apron_grapple_start allows at an opponent in the ring, from
// the apron's way in with the d-pad toward it, when w may act (0x77, move_num). Last, with trait
// 0x2000, a straight d-pad at the ropes at a downed opponent in the ring: the slingshot (0x73,
// dir the joint zone, weapon dropped).
s32 input_apron_moves(WrestlerCtx *ctx) {
    if (!(ctx->pressed & 0x8000))
        return 0;
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    u16 b = ctx->buttons;
    if (!(g_match_rules & 0x400)) {
        if (o->location & 0x44) {
            u32 z = w->zone_info;
            u16 d = mem_u16(z + 0x26);
            if (d & 2) {
                weapon_release(w, 0);
                wrestler_set_state_now(w, state::RopeTaunt);
                return 1;
            }
            if ((mem_u32(w->attrs + 0x38) & 0x8000) && (d & 4) && (mem_u16(z + 0x28) & 4) && (d & b & 0xF00)) {
                wrestler_set_state_now(w, state::ApronDiveMove271);
                return 1;
            }
        }
        if (o->location & 4) {
            u32 z = w->zone_info;
            u16 d = mem_u16(z + 0x26);
            if ((d & 0x10E0) && (mem_u16(z + 0x28) & 4) && (d & b & 0xF00)) {
                Vec3 v, n = {mem_f32(z + 24), mem_f32(z + 28), mem_f32(z + 32)};
                vec_from_angle(w->facing_to_opponent, 1.0f, &v);
                if ((f64)vec_normalize_two(&n, &v) < -0.173648177) {
                    wrestler_set_state_now(w, state::ApronDiveGenericMove75);
                    return 1;
                }
            }
        }
    }
    s32 m = apron_grapple_start(w, o);
    if (m) {
        u32 z = w->zone_info;
        u16 d = mem_u16(z + 0x26);
        if ((d & 4) && (mem_u16(z + 0x28) & 8) && (d & b & 0xF00) && (o->location & 1) && wrestler_may_act(w)) {
            w->move_num = (s16)m;
            wrestler_set_state_now(w, state::ApronToRingMove);
            return 1;
        }
    }
    if (!(mem_u32(w->attrs + 0x38) & 0x2000))
        return 0;
    u16 d = mem_u16(w->zone_info + 0x26);
    if (!(d & 2) || !(d & b & 0xF00))
        return 0;
    if (dpad_diagonal_quadrant(b) != -1)
        return 0;
    if (!(o->location & 1) || !(o->flags80 & 8))
        return 0;
    w->dir = (s16)w->joint_zone;
    weapon_release(w, 0);
    wrestler_set_state_now(w, state::ApronMove);
    return 1;
}

// N64 0x8013DA8C: C-up pressed with the d-pad toward the wall the wrestler stands at (zone_info:
// +0x24 id, +0x26 the sides it faces and what it is, +0x28 4 a way down / out, 8 a way up / in).
// In order: a ring-side wall (0x02) with a way out: dir by the d-pad, the tag (tag_try_partner) or
// out (0x6E). An apron wall (0x04): one who may leave, on ring side 0 / 1 beyond 175 (2 / 3 beyond
// -175) with that table standing and free (climb_place_free), the d-pad of 0x8014DF60 / 0x8014DF66
// drops onto the table (0x6E, state_counter 0x04 / 0x08); a way in in a tag match tags or sends the
// illegal man back (illegal_man_leave; the legal man first re-targets, partner_in_move), and the
// illegal man (or anyone outside a tag match) climbs in (0x6F); a way out with the side's d-pad
// (0x8014D218) gets down (0x6E). Then a way up: from the floor onto the apron (0x10, illegal man),
// the steps / tables (0x08: id below 45 the apron, state_counter 0x01; 47 / 52 the tables 0x04 /
// 0x08 when standing and free), out of a raised place (0x10E0 with a way down, 0x6E), onto the
// raised places (0x2000: id 166 0x02, 156-159 0x10, 196-199 0x20; 0x6F). On the cage wall: climbing
// on up (0x800 with a way up: state 196) or down (0x400 with a way down: 0x6F), at once on the press
// or, without C-up, by holding the d-pad into the top or bottom for 21 frames (climb_counter).
s32 input_c_up(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u16 b = ctx->buttons;
    u32 z = w->zone_info;
    u16 d = mem_u16(z + 0x26);
    if (!(ctx->pressed & 8))
        goto cage_wall;
    if ((d & 2) && (mem_u16(z + 0x28) & 4) && (d & b & 0xF00)) {
        w->dir = (s16)dpad_to_side((u16)(d & b & 0xF00));
        if (tag_try_partner(ctx))
            return 1;
        wrestler_set_state_now(w, state::DropDown);
        return 1;
    }
    d = mem_u16(w->zone_info + 0x26);
    if ((d & 4) && (d & b & 0xF00)) {
        if (may_leave_ring(w) && (mem_u16(w->zone_info + 0x28) & 4) && w->ring_zone >= 0) {
            s32 rz = w->ring_zone;
            u32 table = 0;
            if (rz < 2) {
                if (climb_place_free(0) && (mem_u8(0x8016B928) & 1) &&
                    ((rz == 0 && 175.0 < (f64)w->pos.x) || (rz == 1 && 175.0 < (f64)w->pos.z)))
                    table = 0x8014DF60;
            } else if (rz < 4) {
                if (climb_place_free(1) && (mem_u8(0x8016B928) & 2) &&
                    ((rz == 2 && (f64)w->pos.x < -175.0) || (rz == 3 && (f64)w->pos.z < -175.0)))
                    table = 0x8014DF66;
            }
            if (table) {
                for (s32 i = 0; i < 3; i++) {
                    if ((b & 0xF00) == mem_u16(table + 2u * (u32)i)) {
                        wrestler_set_state_now(w, state::DropDown);
                        w->state_counter |= table == 0x8014DF60 ? 4 : 8;
                        return 1;
                    }
                }
            }
        }
        if (mem_u16(w->zone_info + 0x28) & 8) {
            if (g_match_type & 0x2000) {
                if (tag_try_partner(ctx))
                    return 1;
                if (illegal_check(w)) {
                    partner_in_move(w);
                } else if (illegal_man_leave(w)) {
                    return 1;
                }
            }
            if (illegal_check(w)) {
                wrestler_set_state_now(w, state::ClimbOnto);
                return 1;
            }
        }
        if ((mem_u16(w->zone_info + 0x28) & 4) && may_leave_ring(w) &&
            (mem_u16(0x8014D218 + 2u * (u32)w->ring_zone) & b)) {
            w->dir = (s16)w->ring_zone;
            wrestler_set_state_now(w, state::DropDown);
            return 1;
        }
    }
    z = w->zone_info;
    d = mem_u16(z + 0x26);
    if ((d & 0x10) && (mem_u16(z + 0x28) & 8) && (d & b & 0xF00)) {
        if (illegal_check(w)) {
            wrestler_set_state_now(w, state::ClimbOnto);
            return 1;
        }
        z = w->zone_info;
        d = mem_u16(z + 0x26);
    }
    if ((d & 8) && (d & b & 0xF00) && (mem_u16(z + 0x28) & 8)) {
        s16 id = mem_s16(z + 0x24);
        if (id < 45) {
            wrestler_set_state_now(w, state::ClimbOnto);
            w->state_counter |= 1;
            return 1;
        }
        if (id == 47 && (mem_u8(0x8016B928) & 1) && climb_place_free(0)) {
            wrestler_set_state_now(w, state::ClimbOnto);
            w->state_counter |= 4;
            return 1;
        }
        if (mem_s16(w->zone_info + 0x24) == 52 && (mem_u8(0x8016B928) & 2) && climb_place_free(1)) {
            wrestler_set_state_now(w, state::ClimbOnto);
            w->state_counter |= 8;
            return 1;
        }
    }
    z = w->zone_info;
    d = mem_u16(z + 0x26);
    if ((d & 0x10E0) && (d & b & 0xF00) && (mem_u16(z + 0x28) & 4)) {
        wrestler_set_state_now(w, state::DropDown);
        return 1;
    }
    z = w->zone_info;
    d = mem_u16(z + 0x26);
    if ((d & 0x2000) && (d & b & 0xF00) && (mem_u16(z + 0x28) & 8)) {
        s16 id = mem_s16(z + 0x24);
        u16 bit = 0;
        if (id == 166)
            bit = 2;
        else if ((u16)(id - 156) < 4)
            bit = 0x10;
        else if ((u16)(id - 196) < 4)
            bit = 0x20;
        if (bit) {
            wrestler_set_state_now(w, state::ClimbOnto);
            w->state_counter |= bit;
            return 1;
        }
    }
cage_wall:
    if (!(w->location & 0x400))
        return 0;
    z = w->zone_info;
    if ((mem_u16(z + 0x26) & b & 0x800) && (mem_u16(z + 0x28) & 8)) {
        w->climb_counter = (u16)(w->climb_counter + 1);
        if ((ctx->pressed & 8) || (s16)w->climb_counter >= 21) {
            wrestler_set_state_now(w, state::CageTopClimbOver);
            return 1;
        }
        z = w->zone_info;
    }
    if ((mem_u16(z + 0x26) & b & 0x400) && (mem_u16(z + 0x28) & 4)) {
        w->climb_counter = (u16)(w->climb_counter + 1);
        if ((ctx->pressed & 8) || (s16)w->climb_counter >= 21) {
            wrestler_set_state_now(w, state::ClimbOnto);
            return 1;
        }
    }
    return 0;
}

// N64 0x80142E80: C-left at the steps, the crowd or a table edge (by the wall's zone id).
s32 input_weapon_grab(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u32 zone = w->zone_info;
    if (!(mem_u16(zone + 0x28) & 0x20))
        return 0;
    u16 id = mem_u16(zone + 0x24);
    bool grab = w->state == state::Standing && (ctx->pressed & 2) && (w->dpad_held & ctx->buttons);
    if ((u32)(id - 46) < 8 || (u32)(id - 188) < 8) {
        if (!grab)
            return 0;
        // The ring steps (0x8C): the two halves are objects 4 and 5, by the wall's id.
        weapon_release(w, 0);
        wrestler_set_state_now(w, state::LiftObject);
        bool far = (u32)(id - 46) < 8 ? mem_s16(w->zone_info + 0x24) >= 51 : mem_s16(w->zone_info + 0x24) >= 192;
        if ((u32)(id - 46) < 8)
            w->state_counter |= far ? 4 : 2;
        else
            w->state_counter |= far ? 0x10 : 8;
        w->held_obj = far ? 5 : 4;
        return 1;
    }
    if (!grab && !(w->state == state::Running && (ctx->buttons & 2)))
        return 0;
    // A crowd weapon (0x8A) from this wall.
    w->grab_zone_info = w->zone_info;
    wrestler_set_state_now(w, state::CrowdWeaponGrab);
    return 1;
}

// N64 0x80143060: B at a wall with weapons (+0x28 0x40), the d-pad toward it: empty-handed the pickup
// 0x91; holding something 0x8E, or a carried ladder away from a 0x10 wall, stood up (0x8D).
s32 input_weapon_grab_zone(WrestlerCtx *ctx) {
    if (!(ctx->pressed & 2))
        return 0;
    u32 zone = ctx->self->zone_info;
    if (!(mem_u16(zone + 0x28) & 0x40))
        return 0;
    if (!(mem_u16(zone + 0x26) & ctx->buttons & 0xF00))
        return 0;
    Wrestler *w = ctx->self;
    s32 st = state::WeaponPickup;
    if (w->flags_ef & 1) {
        st = state::ObjectPairedMove;
        if (wrestler_move_anim(w, 145) == 3 && !(mem_u16(w->zone_info + 0x26) & 0x10))
            st = state::LadderSetUp;
    }
    wrestler_set_state_now(w, st);
    return 1;
}

// N64 0x800EEC9C: in the cage match, from the ring: C-right, or A / B at an opponent up the cage wall
// (location 0x400) more than 100 away or above 350 with the d-pad toward the wall, starts the climb up
// the cage (0xC8).
s32 cage_climb_check(WrestlerCtx *ctx) {
    if (!(g_match_type & 0x10000))
        return 0;
    Wrestler *w = ctx->self;
    if (!(w->location & 1))
        return 0;
    u16 p = ctx->pressed;
    if (!(p & 1)) {
        if (!(p & 0xC000))
            return 0;
        const Wrestler *o = ctx->rec_a;
        if (!(o->location & 0x400))
            return 0;
        if (!(100.0 < (f64)w->opp_distance) && !(350.0 < (f64)o->pos.y))
            return 0;
        if (!(mem_u16(w->zone_info + 0x26) & ctx->buttons & 0xF00))
            return 0;
    }
    wrestler_set_state_now(w, state::CageClimbStart);
    return 1;
}

// N64 0x8013E108: C-down held: the leapfrog over a downed wrestler ahead, otherwise the run.
s32 input_leapfrog(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 4))
        return 0;
    Wrestler *w = ctx->self;
    if (w->location & 0x400)
        return 0;
    s32 dir = dpad_dir8(ctx->buttons);
    if (dir == 0)
        w->turn_step = w->facing;
    else
        w->turn_step = mem_s32(0x8014D234 + 4 * dir);
    for (s32 i = 0; i < 4; i++) {
        Wrestler *self = ctx->self;
        if (i == self->slot || i == self->runner_slot)
            continue;
        Wrestler *o = &g_wrestlers[i];
        if (!(self->location & o->location & 0x3C5))
            continue;
        if (!(o->flags80 & 8))
            continue;
        NM_UNPORTED("input_leapfrog: a downed wrestler near the run (wrestler_translate_pushed)");
    }
    wrestler_set_state_now(ctx->self, state::Running);
    return 1;
}

// N64 0x8013E9C4: R held: the block (and its special cases).
// R held from standing (not up on the cage): on the apron 9; with a weapon 139; at a downed
// opponent (flags80 0x08 without 0x01) in the same place within 100 the pick-up pair 56 / 57; in
// the ring at a corner-slumped opponent (frame_flags 0x01, not 0x02) within 120 the pair 58 / 59,
// at one with frame_flags 0x08 within 120 the pair 64 / 65; else the block 76.
s32 input_r(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 0x10))
        return 0;
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    u16 loc = w->location;
    if (loc & 0x400)
        return 0;
    if (loc & 2) {
        wrestler_set_state_now(w, state::ApronDodge);
        return 1;
    }
    if (w->flags_ef & 1) {
        wrestler_set_state_now(w, state::BigWeaponDrop);
        return 1;
    }
    if ((a->flags80 & 8) && !(a->flags80 & 1) && (loc & a->location & 0x3C5)) {
        s32 angle;
        if (running_ground_attack_check(w, a, 100.0f, &angle, 0)) {
            wrestler_pair_set_state_move(w, state::PickUpDownedOpponentAttacker, a, state::PickUpDownedOpponentDefender);
            return 1;
        }
    }
    if (!(w->location & a->location & 1)) {
        wrestler_set_state_now(w, state::Blocking);
        return 1;
    }
    if ((a->frame_flags & 1) && !(a->frame_flags & 2) && (f64)w->opp_distance < 120.0) {
        wrestler_pair_set_state_move(w, state::CornerPullDownAttacker, a, state::CornerPullDownDefender);
        return 1;
    }
    if ((a->frame_flags & 8) && (f64)w->opp_distance < 120.0) {
        wrestler_pair_set_state_move(w, state::YankDownAttacker, a, state::YankDownDefender);
        return 1;
    }
    wrestler_set_state_now(w, state::Blocking);
    return 1;
}

// N64 0x8013F2B4: C-left from standing at a held opponent: a referee slot (flags_e9 0x04) in the
// same place as an opponent in a ground hold within reach breaks it; nothing without C-left.
s32 input_c_left(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 2))
        return 0;
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (!(w->location & o->location & 0x3C5) || !(w->flags_e9 & 4))
        return 0;
    if (!(o->flags80 & 0xC0) || (o->flags80 & 0x600))
        return 0;
    Wrestler *x = &g_wrestlers[o->opponent];
    if (!running_ground_attack_check(w, o, 200.0f, nullptr, 0) && !running_ground_attack_check(w, x, 200.0f, nullptr, 0))
        return 0;
    if (!(g_match_rules & 0x4000) && !(w->location & 1))
        return 0;
    if (!(o->flags_e9 & x->flags_e9 & 0x10))
        return 0;
    const Wrestler *a = (o->state == state::GroundHoldAttacker || o->state == state::PinAttacker || o->state == state::PinFromMoveAttacker) ? o : x;
    s32 st = state::PreBell;
    if (o->state == state::GroundHoldAttacker || o->state == state::GroundHoldDefender) {
        if ((g_match_rules & 0x10) && (mem_u32(a->move + 0x20) & 0x80))
            st = state::DuckBackBreakingGrab;
        if (a->state_flags & 0x200)
            st = state::DuckBackBreakingGrab;
    } else {
        if (g_match_rules & 0x20)
            st = state::RefereeCount;
        if (a->state_flags & 0x100)
            st = state::RefereeCount;
    }
    if (!st)
        return 0;
    weapon_release(w, 0);
    wrestler_set_state(w, st);
    return 1;
}

// N64 0x8013E870: the stir / pick-up of a downed opponent (A, or C-left with a direction).
s32 input_stir(Wrestler *w, Wrestler *opp, u16 held) {
    if (!(held & 0x8000)) {
        if (!(held & 2) || !(held & 0xF00) || !(w->location & 0x85))
            return 0;
    }
    if ((opp->flags80 & 9) != 8)
        return 0;
    if (!(w->location & opp->location & 0x3C5))
        return 0;
    if (!running_ground_attack_check(w, opp, 100.0f, nullptr, 0))
        return 0;
    u16 h = held;
    if (w->flags_e9 & 1) {
        if (!(held & 2))
            return 0;
        h = held & 0x7FFF;
    }
    w->flags8a &= 0xFFF7;
    weapon_release(w, 0);
    wrestler_pair_set_state_move(w, state::GroundGrappleAttacker, opp, state::GroundGrappleDefender);
    w->ground_side = (h & 0x8000) ? 0 : 0x20;
    if ((h & 0xC000) == 0xC000 && (w->flags_ee & 4))
        w->ground_side |= 0x10;
    return 1;
}

// N64 0x8013E798: A held (the grapple attempt 0x50, after the stir check).
s32 input_a(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a &= 0xFF7F;
    w = ctx->self;
    if (w->location & 0x400) {
        if (ctx->buttons & 0x8000) {
            wrestler_set_state_now(w, state::CageTopDive);
            return 1;
        }
    } else if ((ctx->buttons & 0x8000) && (w->flags_ef & 1)) {
        wrestler_set_state_now(w, state::BigWeaponDrop);
        return 1;
    }
    if (input_stir(ctx->self, ctx->rec_a, ctx->buttons))
        return 1;
    if (!(ctx->buttons & 0x8000))
        return 0;
    w = ctx->self;
    if (w->state == state::Taunt)
        w->flags8a |= 0x80;
    wrestler_set_state_now(ctx->self, state::GrappleAttempt);
    return 1;
}

// N64 0x8013EB58: L held from standing: with a weapon 139; a wrestler with the trait bits
// 0x700000 at a slumped opponent (flags80 0x20, flags86 0x20) in the ring, roughly facing it,
// 29; at an opponent in the grapple attempt (50) in the same place within 85 the pair 151 / 152;
// a human at a downed opponent (flags80 0x08 without 0x01) in the same place in reach (100) the
// cover 66 / 67; in the ring at a corner-slumped opponent (frame_flags 0x01, not 0x02) within 120
// the pair 60 / 61, or one with frame_flags 0x08 the pair 64 / 65; else the L evade 107 (not for a
// CPU-limited wrestler), or on the apron 9.
s32 input_l(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 0x20))
        return 0;
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (w->flags_ef & 1) {
        wrestler_set_state_now(w, state::BigWeaponDrop);
        return 1;
    }
    if ((mem_u32(w->attrs + 0x34) & 0x700000) && (w->location & o->location & 1) && (o->flags80 & 0x20) &&
        (o->flags86 & 0x20)) {
        s32 d = abs_i(angle_diff(o->facing, w->facing_to_opponent));
        if ((u32)(d - 113) >= 0x71F) {
            wrestler_set_state_now(w, state::DuckingRunner);
            return 1;
        }
    }
    if (o->state == state::Dazed && (w->location & o->location & 0x3C5) && (f64)w->opp_distance < 85.0) {
        wrestler_pair_set_state_move(w, state::SpinDazedOpponentAttacker, o, state::SpinDazedOpponentDefender);
        return 1;
    }
    if (!(w->flags_e9 & 3) && (w->location & o->location & 0x3C5) && (o->flags80 & 8) && !(o->flags80 & 1)) {
        if (running_ground_attack_check(w, o, 100.0f, nullptr, 0)) {
            w->flags8a &= 0xFFF7;
            wrestler_pair_set_state_move(w, state::CoverFromStanding, o, state::PinStartDefender);
            return 1;
        }
    }
    if (w->location & o->location & 1) {
        if ((o->frame_flags & 1) && !(o->frame_flags & 2) && (f64)w->opp_distance < 120.0) {
            wrestler_pair_set_state_move(w, state::CornerPlaceOnTopAttacker, o, state::CornerPlaceOnTopDefender);
            return 1;
        }
        if ((o->frame_flags & 8) && (f64)w->opp_distance < 120.0) {
            wrestler_pair_set_state_move(w, state::YankDownAttacker, o, state::YankDownDefender);
            return 1;
        }
    }
    if (w->location & 0x3C5) {
        if (!(w->flags_e9 & 1)) {
            wrestler_set_state_now(w, state::GrappleEvade);
            return 1;
        }
    }
    if (w->location & 2) {
        wrestler_set_state_now(w, state::ApronDodge);
        return 1;
    }
    return 0;
}

void objects_near(const Vec3 *p, s32 idx[8], f32 dist[8]);

// N64 0x80142B28: C-left with something: not on the apron. Holding it, a carried ladder is stood up
// (0x8D), anything else thrown (0x8F). Empty-handed, the nearest loose object within 100 and from 15 below
// to 80 above (a set-up ladder measured 124 lower), with nobody on it, is lifted (0x8C, held_obj).
s32 input_weapon_use(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->location & 2)
        return 0;
    if (w->flags_ef & 1) {
        wrestler_set_state(w, wrestler_move_anim(w, 0) == 3 ? state::LadderSetUp : state::WeaponThrow);
        return 1;
    }
    s32 idx[8];
    f32 dist[8];
    objects_near(&w->pos, idx, dist);
    for (s32 k = 0; k < 8 && idx[k] != -1; k++) {
        if (!((f64)dist[k] < 100.0))
            continue;
        u32 o = 0x8016AE10 + 180u * (u32)idx[k];
        f32 y = mem_f32(o + 0x48);
        if (mem_u16(o + 0x20) & 0x100)
            y = (f32)((f64)y - 124.0);
        f64 dy = (f64)(f32)(y - w->pos.y);
        if (!(-15.0 < dy) || !(dy < 80.0))
            continue;
        if (mem_s32(o + 0x14) != -1 || mem_s32(o + 0x18) != -1)
            continue;
        w->held_obj = idx[k];
        wrestler_set_state(w, state::LiftObject);
        return 1;
    }
    return 0;
}

// N64 0x8013EE00: C-left held. A guest referee (flags_e9 0x04) calls the bout when every other fighter is
// bleeding under its rule (flags8c 0x800: end_flags 0x200 on them all, 0xA9). Then the weapon use. At an
// opponent in the same place the guest referee may end it on a bleeder (within 200) or use C-left's
// moves; anyone at one lying down (within 100) starts 0x36 / 0x37, at one in 0x32 within 85 0x97 / 0x98,
// in the ring at one slumped (flags_ec 0x01 without 0x02) within 120 0x3C / 0x3D. A guest referee
// otherwise: a foul (flags8c 0x1000 on the one that did it, opponent or its target, 0x2000 on the other)
// disqualifies (end_flags 0x400, the bell, 0xA9); a press with the count on and its fighter outside
// starts the count (0xA8). Everyone left, once a match (flags_f5 0x01), taunts (0x1B).
s32 input_c_left_held(WrestlerCtx *ctx) {
    if (!(ctx->buttons & 2))
        return 0;
    Wrestler *w = ctx->self;
    if (w->flags_e9 & 4) {
        bool all = true;
        for (s32 i = 0; i < 4 && all; i++) {
            const Wrestler *x = &g_wrestlers[i];
            if (x->roster_id != -1 && i != w->slot && (x->flags_e9 & 0x10) && !(x->flags8c & 0x800))
                all = false;
        }
        if (all) {
            for (s32 i = 0; i < 4; i++) {
                Wrestler *x = &g_wrestlers[i];
                if (x->roster_id != -1 && i != w->slot && (x->flags_e9 & 0x10))
                    x->end_flags |= 0x200;
            }
            weapon_release(w, 0);
            wrestler_set_state(w, state::GuestRefereeCall);
            return 1;
        }
    }
    if (input_weapon_use(ctx))
        return 1;
    Wrestler *o = ctx->rec_a;
    if (w->location & o->location & 0x3C5) {
        if (w->flags_e9 & 4) {
            if ((o->flags8c & 0x800) && (o->flags_e9 & 0x10) && running_ground_attack_check(w, o, 200.0f, nullptr, 0)) {
                o->end_flags |= 0x200;
                weapon_release(w, 0);
                wrestler_set_state(w, state::GuestRefereeCall);
                return 1;
            }
            if (input_c_left(ctx))
                return 1;
        }
        if ((o->flags80 & 9) == 8 && running_ground_attack_check(w, o, 100.0f, nullptr, 0)) {
            weapon_release(w, 0);
            wrestler_pair_set_state_move(w, state::FlipDownedOpponentAttacker, o, state::FlipDownedOpponentDefender);
            return 1;
        }
        if (o->state == state::Dazed && (f64)w->opp_distance < 85.0) {
            weapon_release(w, 0);
            wrestler_pair_set_state_move(w, state::SpinDazedOpponentAttacker, o, state::SpinDazedOpponentDefender);
            return 1;
        }
    }
    if ((w->location & o->location & 1) && (o->frame_flags & 1) && !(o->frame_flags & 2) && (f64)w->opp_distance < 120.0) {
        weapon_release(w, 0);
        wrestler_pair_set_state_move(w, state::CornerPlaceOnTopAttacker, o, state::CornerPlaceOnTopDefender);
        return 1;
    }
    if (!(w->flags_e9 & 4))
        return 0;
    Wrestler *x = &g_wrestlers[o->opponent];
    if ((o->flags8c | x->flags8c) & 0x3000) {
        Wrestler *d;
        if (o->flags8c & 0x3000)
            d = (o->flags8c & 0x1000) ? o : x;
        else
            d = (x->flags8c & 0x1000) ? x : o;
        d->end_flags |= 0x400;
        weapon_release(w, 0);
        wrestler_set_state(w, state::GuestRefereeCall);
        host_sfx(1);
        return 1;
    }
    if ((ctx->pressed & 2) && (mem_u8(0x80162A80) & 1) && (w->location & 1) && (o->location & 0x3C4) && (o->flags_e9 & 0x10)) {
        weapon_release(w, 0);
        wrestler_set_state(w, state::GuestRefereeCountOut);
        return 1;
    }
    if (w->call_flags & 1)
        return 0;
    if (!(w->location & 0x3C5))
        return 0;
    weapon_release(w, 0);
    wrestler_set_state(w, state::Taunt);
    w->call_flags = 1;
    return 1;
}

// N64 0x8013F60C: C-up pressed at a ladder: in the ring or on the floor (location 0x05), the object
// last touched (contact, 100 + the object; the record is read as such whatever contact holds) a
// standing ladder (type2 25, not flags 0x4000). The side it stands on, more than 140 degrees from the
// wrestler's direction from it (the free side, climber0 / climber1), starts the climb (0xAA): whatever
// it holds it drops.
s32 input_ladder_climb(WrestlerCtx *ctx) {
    if (!(ctx->pressed & 8))
        return 0;
    Wrestler *w = ctx->self;
    if (!(w->location & 5))
        return 0;
    u32 o = 0x8016AE10 + 180u * (u32)(w->contact - 100);
    if (mem_s32(o + 4) != 25)
        return 0;
    if (mem_u16(o + 0x20) & 0x4000)
        return 0;
    s32 j = w->contact - 100;
    object_contact_faces(j, 0x807FFE00);
    Vec3 pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)}, d;
    vec_sub(&w->point_120, &pos, &d);
    d.y = 0.0f;
    for (u32 side = 0; side < 2; side++) {
        u32 n = 0x807FFE00 + 48u * side + 24;
        Vec3 nrm = {mem_f32(n), mem_f32(n + 4), mem_f32(n + 8)};
        f32 dot = vec_normalize_two(&nrm, &d);
        if (!(dot < cosf_n64(0x1.38C354p+1f)))
            continue;
        if (mem_s32(o + 0x14 + 4 * side) != -1) {
            if (side == 1)
                return 0;
            continue;
        }
        weapon_release(w, 0);
        w->held_obj = j;
        w->ladder_side = (s32)side;
        wrestler_set_state_now(w, state::LadderGetOn);
        return 1;
    }
    return 0;
}

// N64 0x8013F538: the control stick pushed (1 when it acted), not up on the cage wall: with move 94
// allowed (move_allowed) the weapon is dropped and 0x50 starts at step 3 with flags78 0x400000 (the
// taunt as a move), otherwise the taunt state 0x1B.
s32 input_stick(WrestlerCtx *ctx) {
    if (ctx->in_byte == 0)
        return 0;
    Wrestler *w = ctx->self;
    if (w->location & 0x400)
        return 0;
    if (move_allowed(w, ctx->rec_a, 94)) {
        weapon_release(w, 0);
        wrestler_set_state_now(w, state::GrappleAttempt);
        w->state_step = 3;
        w->flags78 = 0x400000;
        return 1;
    }
    wrestler_set_state_now(w, state::Taunt);
    return 1;
}

// N64 0x8013D9FC: 1 when the wrestler may act (wrestler_may_act), or else when the d-pad it holds
// (dpad_held) has no bit pointing toward its own team's side: ring zone 0 / 1 for slots 2 / 3 (0x200 /
// 0x400), 2 / 3 for slots 0 / 1 (0x100 / 0x800); other zones count as clear.
s32 may_act_b(const Wrestler *w) {
    if (wrestler_may_act(w))
        return 1;
    u16 mask = 0;
    if (w->slot & 2) {
        if (w->ring_zone == 0)
            mask = 0x200;
        else if (w->ring_zone == 1)
            mask = 0x400;
    } else {
        if (w->ring_zone == 2)
            mask = 0x100;
        else if (w->ring_zone == 3)
            mask = 0x800;
    }
    return (w->dpad_held & mask) ? 0 : 1;
}

// ---- the state ----

// N64 0x800E5EB8
void state_01_stand(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a |= 0x100;
    if (g_match_flags[1] & 0x40) {
        // The match is over: off the apron, or face the opponent.
        w = ctx->self;
        if (w->end_flags & 4) {
            if (w->location & 2) {
                w->zone_info = mem_u32(0x8014D3B0 + 4 * w->ring_zone);
                wrestler_set_state_now(ctx->self, state::ClimbOnto);
            } else {
                wrestler_set_state_now(w, state::Taunt);
            }
            return;
        }
        u16 loc = w->location;
        if (!(loc & 0x3C7)) {
            wrestler_set_state_now(w, state::CageFallOff);
            return;
        }
        if (loc & 0x3C5)
            wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        walk_step(ctx, 0, 2);
        return;
    }
    w = ctx->self;
    if (w->end_flags & 1)
        w->end_flags |= 2;
    if (opponent_switch_nearest(ctx->self, ctx->pressed))
        return;
    w = ctx->self;
    if (w->end_flags & 1) {
        if (w->location & 1) {
            weapon_release(w, 0);
            wrestler_set_state_now(ctx->self, state::LeaveAfterFall);
            return;
        }
        if (w->location & 2)
            goto off_apron;
    }
    w = ctx->self;
    if (w->flags_f1 & 0x10) {
        w->flags_f1 &= 0xEF;
        s32 i;
        for (i = 0; i < 4; i++) {
            if (i == ctx->self->slot)
                continue;
            const Wrestler *o = &g_wrestlers[i];
            if (!(o->location & 4) || !(o->end_flags & 1) || (o->flags_f1 & 1) || o->state == state::RunIn)
                break;
        }
        if (!(i < 4)) {
            ctx->self->end_flags |= 0x100;
            host_sfx(1);
            return;
        }
    }
    if (stand_flags_update(ctx->self)) {
        wrestler_set_state_now(ctx->self, state::IllegalManReturn);
        return;
    }
    w = ctx->self;
    if ((w->flags_e9 & 3) && !illegal_check(w)) {
        w = ctx->self;
        if (w->location & 1) {
            wrestler_set_state_now(w, state::IllegalManLeave);
            return;
        }
        if (w->location & 2)
            goto off_apron;
    }
    if (ctx->self->flags_f1 & 1) {
        ctx->buttons = 0;
        ctx->pressed = 0;
        ctx->in_byte = 0;
    }
    if ((g_match_rules & 0x400) && (ctx->self->location & 4) && !(ctx->self->end_flags & 1)) {
        w = ctx->self;
        w->flags80 |= 1;
        w->end_flags |= 0x10;
        ctx->buttons = 0;
        ctx->pressed = 0;
        ctx->in_byte = 0;
    }
    w = ctx->self;
    w->down_count = 0;
    w->run_timer = 0;
    w->runner_slot = -1;
    w->run_counter = -1;
    if (w->location & 0x3E5)
        wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
    health_regen(ctx->self, 1);
    spirit_update(ctx->self, 1);
    illegal_man_check(ctx->self);
    ctx->self->dpad_held = walk_step(ctx, ctx->buttons, 2);
    if (input_apron_dpad(ctx))
        return;
    if (ctx->self->dpad_held) {
        if (input_climb_turnbuckle(ctx) || input_apron_moves(ctx) || input_c_up(ctx) || input_weapon_grab(ctx) ||
            input_weapon_grab_zone(ctx) || cage_climb_check(ctx))
            return;
    }
    if (input_leapfrog(ctx) || input_r(ctx) || input_a(ctx) || input_cup_opponent(ctx) || input_l(ctx) ||
        input_c_left_held(ctx) || input_tag(ctx) || input_ladder_climb(ctx))
        return;
    input_stick(ctx);
    return;

off_apron:
    w = ctx->self;
    w->zone_info = mem_u32(0x8014D3B0 + 4 * w->ring_zone);
    wrestler_set_state_now(ctx->self, state::DropDown);
}



// ---- the taunt state 0x1B (also the apron wait and the post-match pose) ----

enum : u32 {
    A_TAUNT_ANGLE = 0x801590C0,   // s32[4]: the stick angle each slot started its taunt with
    A_FALL_WINNER = 0x80162A84,   // s32: the last fall's first winner (match_record_fall)
};

// N64 0x80132FCC: the d-pad as a side: up 1, down 2, left / right 3, none 0.
s32 dpad_side(u16 buttons) {
    if (buttons & 0x800)
        return 1;
    if (buttons & 0x400)
        return 2;
    return (buttons & 0x300) ? 3 : 0;
}

// N64 0x800EDA98: a taunt over an opponent that is down (0x31) or draped on the ropes (0x32) adds
// the taunt's length (anim_player[4]) to the opponent's escape time, so it gives no free time.
void taunt_hold_opponent(WrestlerCtx *ctx) {
    Wrestler *o = ctx->rec_a;
    if ((u32)(o->state - state::LyingDown) < 2)
        o->escape = (s16)(o->escape + ctx->self->anim_player[4]);
}

// N64 0x800ED924: a wrestler on the apron line (flags8a 0x800) snaps to it (apron_line_pos); 1 when it did.
s32 apron_line_snap(Wrestler *w) {
    if (!(w->flags8a & 0x800))
        return 0;
    apron_line_pos(w, &w->pos);
    return 1;
}

// N64 0x800EC238: after a taunt, an opponent in the same place that is down (flags80 0x08), not in
// a move (0x01) and not sitting (0x10) is stirred with chance 300 into 0x31 step 4 (getting up).
void taunt_stir_check(Wrestler *w, Wrestler *o) {
    if (!(w->location & o->location & 0x3C5))
        return;
    u16 f = o->flags80;
    if (!(f & 1) && (f & 8) && !(f & 0x10) && chance_check(300)) {
        wrestler_set_state_now(o, state::LyingDown);
        o->state_step = 4;
    }
}

// N64 0x800E67F0: taunts, the apron wait and the post-match pose (renamed from state_1B_apron_wait,
// the tag partners seen in it). Step 0: with call_flags 0x01 the d-pad taunt 12958 + dpad_side;
// else the stick angle is kept per slot (0x801590C0) and turned into a side; after the match
// (match flag 0x40) weapon_release and step 4 (flag 0x20); with a weapon (flags_ef 0x01) the
// generic move 105 and 12510 + wrestler_move_anim, step 1 with state_counter 0x24; in the ring
// with the stick down (side 2) step 7 (a crouch); else the taunt move: 234 on the apron (location
// 0x02), 230 (counter 0x10) with spirit_update 5 (counter 0x03, taunt_hold_opponent, the
// opponent's down_count 0) or flags_ee 0x04, else 229 / 228 / 227 by the side (up / left-right /
// none); a tag partner (rec_b) in this state on the same move within 10 frames makes it a double
// taunt from the partner. The move's animation (anim_style) at speed 3; counter 0x04 unless the
// move has +0x20 0x1000000; aux_timer takes move_anim2. Step 1: on the apron flags8a 0x800; else
// pushing the stick 1537 or more off the start angle in the first 7 frames (not while 0x29, nor
// with call_flags 0x01 or an opponent with flags_e9 0x01) switches to 230 or a random 227-229 /
// 231; then the inputs (leapfrog, R, input_a, cup, L, the guest referee's call, the
// grapple attempt under call_flags 0x01), the slide and bounds (scale 1 on the apron), move_anim2
// restored at 90% of the animation; at its end: without counter 0x04 the animation + 1 (speed 1)
// and counter 0x0C; else flags8c 0x80, hit_move_copy / spirit_exchange on itself (not across the
// ropes from the opponent, nor under call_flags 0x01), taunt_stir_check (not 0x10), and 0x01 with
// 11745 (speed 2) from the apron line, else 0x01 at once. Steps 2 / 3: move 231 (speed 1) the
// same way, ending in 0x30. Steps 4-6: the post-match pose (move 235), count_timer 30 sets
// frame_bits 0x04 for the winner; at its end a 195 - length (or 15) countdown with anim_pick 7,
// reaching 0 with frame_bits 0x100 starts the replay (frame_bits 0x01, flag 0x20 off), floored at
// -30. Steps 7 / 8: the crouch 12168, then back to step 2.
void state_1B_taunt(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    illegal_man_check(w);
    switch (w->state_step) {
    case 0: {
        s32 anim;
        w->flags8c &= 0xFF7F;
        if (w->call_flags & 1) {
            anim = dpad_side(ctx->buttons) + 12958;
        } else {
            mem_w32(A_TAUNT_ANGLE + 4u * (u32)ctx->slot, (u32)vec_xz_angle((f32)ctx->in_s16_a, (f32)ctx->in_s16_b));
            s32 side = angle_to_side(mem_s32(A_TAUNT_ANGLE + 4u * (u32)ctx->slot));
            if (g_match_flags[1] & 0x40) {
                weapon_release(w, 0);
                w->state_step = 4;
                g_match_flags[1] |= 0x20;
                return;
            }
            if (w->flags_ef & 1) {
                if (spirit_update(w, 5)) {
                    w->state_counter |= 3;
                    taunt_hold_opponent(ctx);
                }
                wrestler_set_generic_move(w, 105);
                // The carry argument is whatever a1 held (105 from the call above): only read
                // with a weapon that sets none of flags_ef 0x02-0x20.
                wrestler_set_anim(w, wrestler_move_anim(w, 105) + 12510, 3);
                w->move_anim2 = 0;
                w->aux_timer = 0;
                w->state_step = 1;
                w->state_counter |= 0x24;
                return;
            }
            if (!(w->location & 2) && side == 2) {
                w->state_step = 7;
                return;
            }
            if (spirit_update(w, 5)) {
                w->state_counter |= 3;
                if (w->location & 2) {
                    wrestler_start_move(w, ctx->slot, 234);
                    anim = anim_style(w, 0);
                } else {
                    wrestler_start_move(w, ctx->slot, 230);
                    anim = anim_style(w, 0);
                    w->state_counter |= 0x10;
                }
                taunt_hold_opponent(ctx);
                o->down_count = 0;
            } else {
                s32 mv;
                if (w->flags_ee & 4) {
                    if (w->location & 2) {
                        mv = 234;
                    } else {
                        mv = 230;
                        w->state_counter |= 0x10;
                    }
                } else if (w->location & 2) {
                    mv = 234;
                } else if (side == 1) {
                    mv = 229;
                } else if (side == 3) {
                    mv = 228;
                } else {
                    mv = 227;
                }
                wrestler_start_move(w, ctx->slot, mv);
                anim = anim_style(w, 0);
                if (!(w->location & 2)) {
                    const Wrestler *b = ctx->rec_b;
                    if (w->slot != b->slot && !(b->flags_e9 & 1) && b->state == state::Taunt && b->anim_frame < 10 &&
                        b->move_num == mv) {
                        wrestler_start_move(w, w->partner, mv);
                        anim = anim_style(w, 0);
                    }
                }
            }
        }
        wrestler_set_anim(w, anim, 3);
        if (!(mem_u32(w->move + 0x20) & 0x1000000))
            w->state_counter |= 4;
        w->aux_timer = w->move_anim2;
        w->move_anim2 = 0;
        w->state_step = w->state_step + 1;
        return;
    }
    case 1:
        if (w->location & 2) {
            w->flags8a |= 0x800;
        } else if (!(o->flags_e9 & 1) && !(w->call_flags & 1) && !(w->state_counter & 0x29) && w->anim_frame < 7 &&
                   ctx->in_byte) {
            s32 a = vec_xz_angle((f32)ctx->in_s16_a, (f32)ctx->in_s16_b);
            if (abs_i(angle_diff(mem_s32(A_TAUNT_ANGLE + 4u * (u32)ctx->slot), a)) >= 1537) {
                s32 mv = 230;
                if (!(o->flags_ee & 4)) {
                    s32 r = rand_pool_next() & 3;
                    if (r == 3)
                        r = 4;
                    mv = r + 227;
                }
                wrestler_start_move(w, o->slot, mv);
                wrestler_set_anim(w, anim_style(w, 0), 3);
                u16 c = (u16)((w->state_counter | 1) & 0xFFFB);
                w->state_counter = c;
                if (!(mem_u32(w->move + 0x20) & 0x1000000))
                    w->state_counter = c | 4;
                w->aux_timer = w->move_anim2;
                w->move_anim2 = 0;
            }
        }
        if (input_leapfrog(ctx) || input_r(ctx)) {
            apron_line_snap(w);
            return;
        }
        if (input_a(ctx))
            return;
        if (input_cup_opponent(ctx) || input_l(ctx) || input_c_left_held(ctx)) {
            apron_line_snap(w);
            return;
        }
        if ((w->call_flags & 1) && input_stick(ctx))
            return;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, (w->location & 2) ? 1.0f : 4.0f, 0, 0xFFFFE);
        if ((w->state_counter & 4) && (f64)w->anim_player[4] * 0.9 <= (f64)w->anim_frame)
            w->move_anim2 = w->aux_timer;
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (!(w->state_counter & 4)) {
            wrestler_set_anim(w, w->anim_id + 1, 1);
            w->state_counter |= 0xC;
            return;
        }
        w->flags8c |= 0x80;
        if (!((w->location & 0x3C4) && (o->location & 3)) && !(w->call_flags & 1)) {
            hit_move_copy(w, w);
            spirit_exchange(w, o, w);
        }
        if (!(w->call_flags & 1) && !(w->state_counter & 0x10))
            taunt_stir_check(w, o);
        if (apron_line_snap(w)) {
            wrestler_set_state(w, state::Standing);
            wrestler_set_anim(w, 11745, 2);
        } else {
            wrestler_set_state_now(w, state::Standing);
        }
        return;
    case 2:
        wrestler_start_move(w, ctx->slot, 231);
        wrestler_set_anim(w, anim_style(w, 0), 1);
        if (!(mem_u32(w->move + 0x20) & 0x1000000))
            w->state_counter |= 4;
        if (spirit_update(w, 5)) {
            w->state_counter |= 3;
            taunt_hold_opponent(ctx);
        }
        w->aux_timer = w->move_anim2;
        w->move_anim2 = 0;
        w->state_step = w->state_step + 1;
        return;
    case 3:
        if (input_leapfrog(ctx))
            return;
        if ((ctx->pressed & 0x10) && input_r(ctx))
            return;
        if (input_a(ctx) || input_cup_opponent(ctx) || input_l(ctx) || input_c_left_held(ctx))
            return;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if ((w->state_counter & 4) && (f64)w->anim_player[4] * 0.9 <= (f64)w->anim_frame)
            w->move_anim2 = w->aux_timer;
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (!(w->state_counter & 4)) {
            wrestler_set_anim(w, w->anim_id + 1, 1);
            w->state_counter |= 4;
            return;
        }
        w->flags8c |= 0x80;
        if (!((w->location & 0x3C4) && (o->location & 3))) {
            hit_move_copy(w, w);
            spirit_exchange(w, o, w);
        }
        taunt_stir_check(w, o);
        wrestler_set_state_now(w, state::SettleAfterAnimation);
        return;
    case 4:
        wrestler_start_move(w, w->slot, 235);
        wrestler_set_anim(w, anim_style(w, 0), 3);
        if (!(mem_u32(w->move + 0x20) & 0x1000000))
            w->state_counter |= 4;
        w->count_timer = 0;
        w->state_step = w->state_step + 1;
        return;
    case 5: {
        w->count_timer = (u16)(w->count_timer + 1);
        if ((s16)w->count_timer == 30 && w->slot == mem_s32(A_FALL_WINNER))
            g_frame_bits |= 4;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (!(w->state_counter & 4)) {
            wrestler_set_anim(w, w->anim_id + 1, 1);
            w->state_counter |= 4;
            return;
        }
        s32 t = (w->anim_player[4] < 181) ? 195 - w->anim_player[4] : 15;
        w->count_timer = (u16)t;
        if (w->slot == mem_s32(A_FALL_WINNER))
            g_frame_bits |= 4;
        wrestler_set_anim(w, anim_pick(ctx, 7, 0), 3);
        w->state_step = w->state_step + 1;
        return;
    }
    case 6:
        w->count_timer = (u16)(w->count_timer - 1);
        if (w->count_timer == 0 && w->slot == mem_s32(A_FALL_WINNER) && (g_frame_bits & 0x100)) {
            g_frame_bits |= 1;
            g_match_flags[1] &= 0xDF;
        }
        if ((s16)w->count_timer < -29)
            w->count_timer = (u16)-30;
        return;
    case 7:
        w->flags80 |= 1;
        wrestler_set_anim(w, 12168, 3);
        w->state_step = w->state_step + 1;
        return;
    case 8:
        w->flags80 |= 1;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        w->state_step = 2;
        return;
    default:
        return;
    }
}

// N64 0x8011E748: state 0x6E, getting down: off the apron, out of the ring, or off a raised place
// (location 0x1000, "between", while it runs, except on the cage wall). Step 0 on the apron: with
// state_counter 0x0C a jump to the floor (11827 / 11828 by the side, location 0x04, step 4), else the
// climb back in (12128, step 3, dir the ring side). Elsewhere: one who may leave the ring keeps the
// weapon (state_counter 0x01), others drop it; the animation (in the cage match 12803 on the cage
// wall, else the stand set's 1), and the slide into place (pair_scripted_place, then step 1). Step
// 1 (flags80 0x01) when the slide is done: on the cage wall the bounds for its side (0x8014CFE4 /
// 0x8014D064, step 3); from a raised place 13017 with its bounds (0x80147E28 / 0x80147E30 or
// 0x8014BD0C, step 3); from the ring the ropes' bounds (0x801470E8 / 0x801470F0) and, holding a
// weapon it keeps, 12474 + its family (step 3), else the way through the ropes by build and health
// (12115, 12129, 12112 or 12109; step 2). Step 2 (bounds 8.0) when it ends: + 2 when it may go on
// (end_flags 0x01, C-up held by one who may leave, or a legal tag partner not the illegal man), else
// + 1 (step 3). Step 3: the slide along the side (4.0 unless location 0x200), bounds and the events,
// 10 spirit off for one coming down the cage wall. Step 4: the events.
void state_6E_drop_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (!(w->location & 0x400))
        w->location |= 0x1000;
    switch (w->state_step) {
    case 0:
        if (w->location & 2) {
            if (w->state_counter & 0xC) {
                w->location = 4;
                wrestler_set_anim(w, 11827 + ((w->ring_zone & 1) == 0 ? 1 : 0), 2);
                w->state_step = 4;
            } else {
                wrestler_set_anim(w, 12128, 2);
                w->state_step = 3;
                w->dir = (s16)w->ring_zone;
            }
            return;
        }
        if (may_leave_ring(w))
            w->state_counter |= 1;
        else
            weapon_release(w, 0);
        {
            s32 anim;
            if (g_match_type & 0x10000) {
                w->location = 1024;
                anim = 12803;
            } else {
                anim = anim_pick(ctx, 1, 0);
            }
            wrestler_set_anim(w, anim, 2);
        }
        pair_scripted_place(w, ctx->rec_a);
        w->state_step++;
        [[fallthrough]];
    case 1: {
        w->flags80 |= 1;
        if (!pair_slide_step(w, nullptr))
            return;
        u16 loc = w->location;
        Vec3 n = {mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 28), mem_f32(w->zone_info + 32)};
        if (loc & 0x400) {
            w->climb_target = dist_to_ring_point(&n);
            if (w->climb_target & 0x300)
                w->bounds_override = 0x8014CFE4;
            if (w->climb_target & 0xC00)
                w->bounds_override = 0x8014D064;
            w->state_step = 3;
            return;
        }
        if (loc & 0x3C0) {
            wrestler_set_anim(w, 13017, 2);
            w->climb_target = dist_to_ring_point(&n);
            if (w->location & 0x40) {
                if (w->climb_target & 0x300)
                    w->bounds_override = 0x80147E28;
                if (w->climb_target & 0xC00)
                    w->bounds_override = 0x80147E30;
            } else if (w->location & 0x80) {
                w->bounds_override = 0x8014BD0C;
            }
            w->state_step = 3;
            return;
        }
        w->climb_target = dist_to_ring_point(&n);
        if (w->climb_target & 0x300)
            w->bounds_override = 0x801470E8;
        if (w->climb_target & 0xC00)
            w->bounds_override = 0x801470F0;
        if ((w->flags_ef & 1) && (w->state_counter & 1)) {
            wrestler_set_anim(w, wrestler_move_anim(w, 0) + 12474, 2);
            w->state_step = 3;
            return;
        }
        u32 t = mem_u32(w->attrs + 0x34);
        s32 anim;
        if (t & 0x10000)
            anim = 12115;
        else if (t & 0x80000)
            anim = 12129;
        else if (wrestler_health_eff(w) >= 128 && (mem_u32(w->attrs + 0x34) & 0x38000))
            anim = 12112;
        else
            anim = 12109;
        wrestler_set_anim(w, anim, 2);
        w->state_step++;
        return;
    }
    case 2: {
        keep_in_bounds_self(w, 8.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        s32 add;
        if (w->end_flags & 1)
            add = 2;
        else if (may_leave_ring(w) && (ctx->buttons & 8))
            add = 2;
        else if (illegal_check(w))
            add = 1;
        else
            add = (w->flags_e9 & 3) ? 2 : 1;
        wrestler_set_anim(w, w->anim_id + add, 0);
        w->state_step++;
        return;
    }
    case 3:
        if (!(w->location & 0x200))
            wrestler_move_step_b(w, w->climb_target, 4.0f);
        if (w->location & 0x4C7)
            keep_in_bounds_self(w, 4.0f, 0, 2);
        if (move_event(ctx, 0) && (w->location & 0x400))
            wrestler_add_spirit(w, -10);
        return;
    case 4:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}



// N64 0x8011EBC0: state 0x6F, climbing in or up. Step 0 from the apron: into the ring (location
// 0x01) through the ropes by build (12124 for attrs +0x34 0x10000, 12127 for 0x80000, else 12122
// below 128 health or without 0x78000, else 12123 / 12125 / 12126 by 0x8000 / 0x20000 / 0x40000),
// the ropes' bounds, step 3. From the cage wall: over into the ring (12805, state_counter 0x40), the
// side's bounds, step 3. Otherwise (the floor, up onto the apron or a raised place by state_counter)
// a ladder in hand is dropped, and onto the apron (location 0x02) any weapon unless the illegal man
// keeps it; the stand set's 1 and the slide into place (pair_scripted_place), step 1. Step 1 (flags80
// 0x01) when there: onto the steps (0x01), the announce table (0x02), the raised places (0x10, 0x20)
// with 13016 and their bounds (step 3; back to standing, location 0x04, when that place is gone);
// onto a ringside table (0x0C) step 4; onto the apron the ropes' bounds and the climb (12470 + the
// weapon's family for the illegal man with a weapon, location 0x01, step 3; else 12119 / 12132 by
// attrs 0x80000, step 2). Step 2: the slide (4.0) and bounds; when it ends + 1, or with C-up held by
// the illegal man in through the ropes (location 0x01; - 1 with attrs 0x10000, else + 2); step 3.
// Step 3: the slide along the wall and bounds (8.0, 4.0 from the cage) while on the apron, the floor
// or a raised place, and the events; when they end with flags8a 0x8000, location 0x04 and pos.y
// 0. Step 4 (a table): 11824, then 11826 when the d-pad holds the
// table's direction (0x80146790 by side) or 11825; step 5 the events.
void state_6F_climb_onto(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        u16 loc = w->location;
        Vec3 n = {mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 28), mem_f32(w->zone_info + 32)};
        if (loc & 2) {
            w->location = 1;
            u32 t = mem_u32(w->attrs + 0x34);
            s32 anim;
            if (t & 0x10000) {
                anim = 12124;
            } else if (t & 0x80000) {
                anim = 12127;
            } else if (!(t & 0x78000) || wrestler_health_eff(w) < 128) {
                anim = 12122;
            } else {
                t = mem_u32(w->attrs + 0x34);
                anim = 0;
                if (t & 0x8000)
                    anim = 12123;
                if (t & 0x20000)
                    anim = 12125;
                if (t & 0x40000)
                    anim = 12126;
            }
            wrestler_set_anim(w, anim, 2);
            w->climb_target = dist_to_ring_point(&n);
            if (w->climb_target & 0x300)
                w->bounds_override = 0x801470E8;
            if (w->climb_target & 0xC00)
                w->bounds_override = 0x801470F0;
            w->state_step = 3;
            return;
        }
        if (loc & 0x400) {
            w->location = 1;
            w->state_counter |= 0x40;
            wrestler_set_anim(w, 12805, 14);
            if (w->ring_zone & 1) {
                w->climb_target = 3072;
                w->bounds_override = 0x80146C30;
            } else {
                w->climb_target = 768;
                w->bounds_override = 0x80146C28;
            }
            w->state_step = 3;
            return;
        }
        if (w->state_counter & 0x33) {
            if ((w->flags_ef & 1) && mem_s32(0x8016AE10 + 180u * (u32)w->held_obj + 4) == 25)
                weapon_release(w, 0);
        } else {
            w->location = 2;
            if ((w->state_counter & 0xC) || !illegal_check(w))
                weapon_release(w, 0);
        }
        wrestler_set_anim(w, anim_pick(ctx, 1, 0), 0);
        pair_scripted_place(w, nullptr);
        w->state_step++;
    }
        [[fallthrough]];
    case 1: {
        w->flags80 |= 1;
        if (!pair_slide_step(w, nullptr))
            return;
        u16 c = w->state_counter;
        Vec3 n = {mem_f32(w->zone_info + 24), mem_f32(w->zone_info + 28), mem_f32(w->zone_info + 32)};
        u8 tables = mem_u8(0x8016B928);
        if ((c & 1) || (c & 2) || (c & 0x10) || (c & 0x20)) {
            u16 place;
            u8 need;
            if (c & 1) { place = 0x40; need = 4; }
            else if (c & 2) { place = 0x80; need = 0; }
            else if (c & 0x10) { place = 0x100; need = 8; }
            else { place = 0x200; need = 0x10; }
            if (need && !(tables & need)) {
                w->location = 4;
                wrestler_set_state(w, state::Standing);
                return;
            }
            w->location = place;
            wrestler_set_anim(ctx->self, 13016, 2);
            if (place == 0x40) {
                w->climb_target = dist_to_ring_point(&n);
                if (w->climb_target & 0x300)
                    w->bounds_override = 0x80147E28;
                if (w->climb_target & 0xC00)
                    w->bounds_override = 0x80147E30;
            } else if (place == 0x80) {
                w->climb_target = dist_to_ring_point(&n);
                w->bounds_override = 0x8014BD0C;
            } else if (place == 0x100) {
                w->climb_target = dist_to_ring_point(&n);
                if (w->climb_target & 0x300)
                    w->bounds_override = 0x8014B784;
                if (w->climb_target & 0xC00)
                    w->bounds_override = 0x8014B804;
            } else {
                s16 id = mem_s16(w->zone_info + 0x24);
                w->bounds_override = (id == 0xC4 || id == 0xC6) ? 0x8014C6D8 : 0x8014C658;
            }
            w->state_step = 3;
            return;
        }
        if (c & 0xC) {
            wrestler_set_step(w, 4);
            return;
        }
        w->climb_target = dist_to_ring_point(&n);
        if (w->climb_target & 0x300)
            w->bounds_override = 0x801470E8;
        if (w->climb_target & 0xC00)
            w->bounds_override = 0x801470F0;
        if ((w->flags_ef & 1) && illegal_check(w)) {
            w->location = 1;
            wrestler_set_anim(w, wrestler_move_anim(w, 0) + 12470, 2);
            w->state_step = 3;
            return;
        }
        wrestler_set_anim(w, (mem_u32(w->attrs + 0x34) & 0x80000) ? 12132 : 12119, 2);
        w->state_step++;
        return;
    }
    case 2: {
        wrestler_move_step_b(w, w->climb_target, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (w->anim_frames_left > 0)
            return;
        s32 add = 1;
        if ((ctx->buttons & 8) && illegal_check(w)) {
            w->location = 1;
            add = (mem_u16(w->attrs + 0x34) & 1) ? -1 : 2;
        }
        wrestler_set_anim(w, w->anim_id + add, 0);
        w->state_step++;
        return;
    }
    case 3:
        if (w->location & 0x3C3) {
            if (!(w->location & 0x200))
                wrestler_move_step_b(w, w->climb_target, 4.0f);
            keep_in_bounds_self(w, (w->state_counter & 0x40) ? 4.0f : 8.0f, 0, 0xFFFFE);
        }
        if (move_event(ctx, 0) && (w->flags8a & 0x8000)) {
            w->flags8a &= 0x7FFF;
            w->location = 4;
            w->pos.y = 0.0f;
        }
        return;
    case 4: {
        wrestler_set_anim(w, 11824, 2);
        if (w->anim_frames_left > 0)
            return;
        s32 add = 0;
        u32 t = 0x80146790 + 6u * ((w->state_counter & 4) ? 0u : 1u);
        for (s32 i = 0; i < 3; i++)
            if ((ctx->buttons & 0xF00) == mem_u16(t + 2u * (u32)i))
                add = 1;
        wrestler_set_anim(w, 11825 + add, 1);
        w->state_step++;
        return;
    }
    case 5:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}



}  // namespace nm

// Tag matches (g_match_type 0x2000, without rule 0x40 "no tag"): who may tag, the tag itself (0x93 /
// 0x94), the partner on the apron (0x95), and the illegal man walking back to his corner (0x92). The
// legal man has flags_e9 0x10; flags_e9 0x20 picks the team: its corner is the (+x, +z) one (index 0 of
// the corner table 0x8014D1A8), the other team's the (-x, -z) one (index 2). Read from the code
// (2026-11-03) and not trace-checked: no trace has a tag match yet.
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_CORNERS = 0x8014D1A8,      // Vec3[4]: the ring corners (nearest_corner's points)
    A_SIDE_DPAD = 0x8014D218,    // u16[8]: the d-pad bit that points at each apron side (ring_zone)
    A_CORNER_DPAD = 0x8014D228,  // u16[2] by team (stride 4): the two d-pad bits toward the own corner
    A_SIDE_FACING = 0x8014D1E8,  // s32[4]: the facing of each apron side, into the ring
    A_DIR_ANGLES = 0x8014D1D8,   // s32[4]: the angle of each direction index (Wrestler.dir)
};

}  // namespace

// N64 0x8012C0A0: wrestler_move_step(w, none, scale, 0, -1, 2).
s32 wrestler_slide_b(Wrestler *w, f32 scale) { return wrestler_move_step(w, nullptr, scale, 0, -1, 2); }

// N64 0x800EBBB4: moves the whole wrestler (wrestler_translate) so that pos meets point_120 in x
// and z.
void wrestler_move_pos_to_pelvis(Wrestler *w) {
    Vec3 d;
    vec_sub(&w->pos, &w->point_120, &d);
    d.y = 0.0f;
    wrestler_translate(w, &d);
}

// N64 0x801351E4: slides the wrestler over `frames` to the point `dist` in front of its wall
// (zone_info: point_120 moved along the wall normal +0x18 so its plane distance becomes dist) and
// turns it to the normal's direction plus `angle`: count_timer = frames, turn_step = the turn /
// frames.
void place_from_zone(Wrestler *w, s32 frames, s32 angle, f32 dist) {
    u32 z = w->zone_info;
    Vec3 o, n = {mem_f32(z + 24), mem_f32(z + 28), mem_f32(z + 32)};
    vec_from_s16_mem(z, &o);
    f32 pd = plane_distance(&o, &n, &w->point_120);
    Vec3 s, target;
    vec_scale(&n, dist, &s);
    vec_scale(&s, (f32)(1.0 - (f64)(pd / dist)), &s);
    vec_add(&w->point_120, &s, &target);
    wrestler_move_pos_to_pelvis(w);
    target.y = w->pos.y;
    slide_vector(w, &w->pos, &target, frames);
    s32 a = angle_add(vec_xz_angle(n.x, n.z), angle);
    w->count_timer = (u16)frames;
    w->turn_step = angle_diff(a, w->facing) / frames;
}

// N64 0x8013533C: the slide off the cage wall over 6 frames to 60 in front of it (point_120 moved
// along the wall's normal), turned away from it. With `other` up the same side of the wall below
// 400 and within 25 along it, the landing is shifted to 25 from it, and more than 185 along the side
// pulled 50 back toward the middle.
void place_from_zone_pair(Wrestler *w, Wrestler *other) {
    u32 z = w->zone_info;
    Vec3 c0, n = {mem_f32(z + 24), mem_f32(z + 28), mem_f32(z + 32)};
    vec_from_s16_mem(z, &c0);
    f32 d = plane_distance(&c0, &n, &w->point_120);
    Vec3 v, target;
    vec_scale(&n, 60.0f, &v);
    vec_scale(&v, (f32)(1.0 - (f64)d * 0.016666666666666666), &v);
    vec_add(&w->point_120, &v, &target);
    wrestler_move_pos_to_pelvis(w);
    target.y = w->pos.y;
    if (other->location & 0x400) {
        s32 side = angle_to_side(angle_add(vec_xz_angle(n.x, n.z), 2048));
        if (other->ring_zone == side && (f64)other->point_120.y < 400.0) {
            f32 along = (side & 1) ? other->point_120.z - w->point_120.z : other->point_120.x - w->point_120.x;
            if ((f64)absf(along) < 25.0) {
                f32 *t = (side & 1) ? &target.z : &target.x;
                if ((f64)along < 0.0)
                    *t = (f32)((f64)*t + ((f64)along + 25.0));
                else
                    *t = (f32)((f64)*t - (25.0 - (f64)along));
                if (185.0 < (f64)absf(*t))
                    *t = (f32)((f64)*t < 0.0 ? (f64)*t + 50.0 : (f64)*t - 50.0);
            }
        }
    }
    slide_vector(w, &w->pos, &target, 6);
    s32 a = angle_add(vec_xz_angle(n.x, n.z), 2048);
    w->count_timer = 6;
    w->turn_step = angle_diff(a, w->facing) / 6;
}

// N64 0x80134420: places a wrestler for a scripted paired animation against another, by its state
// (0xDC4 bytes; the whip, state 0x0B, and the drop, 0x6E, are decompiled): an opponent whipped into a corner
// (state_flags 0x04) slides the CPU over 15 frames to 56.57 in front of the corner it faces
// (0x8014D1A8 / 0x8014D208 by dir) and turns it to that corner's angle; otherwise place_from_zone
// (15 frames, 40 in front of the wall, 50 for a cage whip, state_flags 0x10).
void pair_scripted_place(Wrestler *w, Wrestler *other) {
    if (w->state == state::DropDown) {
        // Dropping off the apron or a raised place: 6 frames to 30 in front of the wall (55 from
        // the ring), turned to face away from it; from the cage wall place_from_zone_pair.
        if (w->location & 0x3E0)
            place_from_zone(w, 6, 2048, 30.0f);
        else if (w->location & 0x400)
            place_from_zone_pair(w, other);
        else
            place_from_zone(w, 6, 2048, 55.0f);
        return;
    }
    if (w->state == state::ClimbOnto) {
        if (w->state_counter & 0xC) {
            // Onto a ringside table (0x04 side 0, 0x08 side 1): 6 frames to (+-370.75, y, +-370.75),
            // turning to -1536 / 512.
            f32 c = (w->state_counter & 4) ? 370.748749f : -370.748749f;
            s32 a = (w->state_counter & 4) ? -1536 : 512;
            wrestler_move_pos_to_pelvis(w);
            Vec3 target = {c, w->pos.y, c};
            slide_vector(w, &w->pos, &target, 6);
            w->count_timer = 6;
            w->turn_step = angle_diff(a, w->facing) / 6;
            return;
        }
        // Up to the apron or a raised place: 6 frames to 55 (65 for the steps and raised places,
        // state_counter 0x33) in front of the wall, turned away from it.
        place_from_zone(w, 6, 2048, (w->state_counter & 0x33) ? 65.0f : 55.0f);
        return;
    }
    if (w->state == state::RunOffEdge || w->state == state::RunOntoRaisedPlace) {
        // The run out over the ropes (80) or onto a table (55): 3 frames, turned away from the wall.
        place_from_zone(w, 3, 2048, w->state == state::RunOffEdge ? 80.0f : 55.0f);
        return;
    }
    if (w->state == state::DrapedOverRopes) {
        // Knocked onto the ropes: 7 frames to 30 in front of them, facing them.
        place_from_zone(w, 7, 0, 30.0f);
        return;
    }
    if (w->state == state::KnockedOverTopRope) {
        // Knocked over the ropes: 6 frames to 55 in front of them, facing them (turned away with
        // hit_flags 0x2000000, from behind).
        place_from_zone(w, 6, (s32)((w->hit_flags >> 14) & 0x800), 55.0f);
        return;
    }
    if (w->state == state::CatchWhippedRunnerDefender) {
        // Caught out of the run: 6 frames to 80 in front of the catcher, facing it.
        Vec3 base = other->point_120;
        Vec3 off = {0.0f, 0.0f, 80.0f};
        s32 a = angle_add(other->facing, 2048);
        Vec3 target;
        wrestler_move_pos_to_pelvis(w);
        point_at_offset(&base, &off, other->facing, &target);
        target.y = w->pos.y;
        slide_vector(w, &w->pos, &target, 6);
        w->count_timer = 6;
        w->turn_step = angle_diff(a, w->facing) / 6;
        return;
    }
    if (w->state == state::CageYankAttacker || w->state == state::TurnbucklePerch) {
        // 0xCD yanking `other` off the cage: 12 frames to 50 behind it, facing as it does. 0x0A up onto
        // the turnbuckle: 8 frames to 73.54 from the corner (0x8014D1A8), turning to 0x8014D1F8, the
        // offset along 0x8014D208.
        Vec3 base, off;
        s32 a, along, frames;
        if (w->state == state::CageYankAttacker) {
            base = other->point_120;
            off = {0.0f, 0.0f, 50.0f};
            a = other->facing;
            along = angle_add(a, 2048);
            frames = 12;
        } else {
            u32 c = 0x8014D1A8 + 12u * (u32)w->dir;
            base = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
            off = {0.0f, 0.0f, 73.5391083f};
            a = mem_s32(0x8014D1F8 + 4u * (u32)w->dir);
            along = mem_s32(0x8014D208 + 4u * (u32)w->dir);
            frames = 8;
        }
        Vec3 target;
        wrestler_move_pos_to_pelvis(w);
        point_at_offset(&base, &off, along, &target);
        target.y = w->pos.y;
        slide_vector(w, &w->pos, &target, frames);
        w->count_timer = (u16)frames;
        w->turn_step = angle_diff(a, w->facing) / frames;
        return;
    }
    if (w->state == state::StrikeCounterDefender || w->state == state::WeaponTakeAwayAttacker || w->state == state::CaughtLegHeld || w->state == state::CatchFlyerAttacker || w->state == state::CaughtLegHolder) {
        // Countered: 6 frames to 100 ahead of the other (0x65 from its pos, 0x66 its pelvis), or 0x6A held
        // from where the holder is; 0x69 on the spot turning to it; 0x88 10 frames to 91.9 from corner `dir`.
        Vec3 base, off = {0.0f, 0.0f, 100.0f}, target;
        s32 along, face, frames = 6;
        if (w->state == state::CaughtLegHolder) {
            target = w->point_120;
            face = angle_to(&w->point_120, &other->point_120);
            wrestler_move_pos_to_pelvis(w);
            target.y = w->pos.y;
            slide_vector(w, &w->pos, &target, frames);
            w->count_timer = (u16)frames;
            w->turn_step = angle_diff(face, w->facing) / frames;
            return;
        }
        if (w->state == state::CatchFlyerAttacker) {
            u32 c = 0x8014D1A8 + 12u * (u32)w->dir;
            base = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
            off.z = 91.9238815f;
            face = mem_s32(0x8014D1F8 + 4u * (u32)w->dir);
            along = mem_s32(0x8014D208 + 4u * (u32)w->dir);
            frames = 10;
        } else {
            base = w->state == state::WeaponTakeAwayAttacker ? other->point_120 : other->pos;
            along = w->state == state::CaughtLegHeld ? angle_to(&other->pos, &w->point_120) : other->facing;
            face = angle_add(along, 2048);
        }
        wrestler_move_pos_to_pelvis(w);
        point_at_offset(&base, &off, along, &target);
        target.y = w->pos.y;
        slide_vector(w, &w->pos, &target, frames);
        w->count_timer = (u16)frames;
        w->turn_step = angle_diff(face, w->facing) / frames;
        return;
    }
    if (w->state == state::ShoveAtWallAttacker || w->state == state::PutOntoTableOrStepsAttacker) {
        // A shove at a wall: 15 frames to 40 from it, facing it.
        place_from_zone(w, 15, 0, 40.0f);
        return;
    }
    if (w->state == state::ObjectPairedMove || w->state == state::WeaponPickup) {
        // A paired move with a held object, or a pickup, at a wall: 6 frames to 55, turned away from it.
        place_from_zone(w, 6, 2048, 55.0f);
        return;
    }
    if (w->state == state::CrowdWeaponGrab) {
        // Taking a weapon from the crowd: 8 frames to 55 in front of the wall, turned away from it.
        place_from_zone(w, 8, 2048, 55.0f);
        return;
    }
    if (w->state != state::IrishWhipAttacker)
        NM_UNPORTED("pair_scripted_place: a state other than the whip (0x0B), the drop (0x6E) or the grab (0x8A)");
    if (other->state_flags & 4) {
        u32 c = 0x8014D1A8 + 12u * (u32)w->dir;
        Vec3 base = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
        Vec3 off = {0.0f, 0.0f, 56.5685425f};
        s32 a = mem_s32(0x8014D208 + 4u * (u32)w->dir);
        Vec3 target;
        wrestler_move_pos_to_pelvis(w);
        point_at_offset(&base, &off, a, &target);
        target.y = w->pos.y;
        slide_vector(w, &w->pos, &target, 15);
        w->count_timer = 15;
        w->turn_step = angle_diff(a, w->facing) / 15;
        return;
    }
    place_from_zone(w, 15, 0, (other->state_flags & 0x10) ? 50.0f : 40.0f);
}

// N64 0x801356A4: one step of the slide pair_scripted_place set up: while count_timer runs, move
// the wrestler (and `other`, when given) by step_vec and turn by turn_step; then count down. 1 once
// the count has run out.
s32 pair_slide_step(Wrestler *w, Wrestler *other) {
    if ((s16)w->count_timer > 0) {
        wrestler_translate(w, &w->step_vec);
        if (other)
            wrestler_translate(other, &w->step_vec);
        w->facing = angle_add(w->facing, w->turn_step);
    }
    w->count_timer = (u16)(w->count_timer - 1);
    return (s16)w->count_timer < 1 ? 1 : 0;
}

// N64 0x801381E0: whether a wrestler is where a tag can be made from: in the ring perched on its own
// team's corner (state 0x0A, dir 0 for team 0x20, else 2), or on the apron in its own team's quarter
// (team 0x20: x and z both above 20; the other: both below -20).
s32 apron_side_ok(const Wrestler *w) {
    s32 own_dir = (w->flags_e9 & 0x20) ? 0 : 2;
    if ((w->location & 1) && w->state == state::TurnbucklePerch && w->dir == own_dir)
        return 1;
    if (!(w->location & 2))
        return 0;
    if (w->flags_e9 & 0x20)
        return (f64)w->pos.x > 20.0 && (f64)w->pos.z > 20.0;
    return (f64)w->pos.x < -20.0 && (f64)w->pos.z < -20.0;
}

// N64 0x801384A0: the two wrestlers' point_120 within 130 of each other (the tag's reach).
s32 dist_between(const Wrestler *a, const Wrestler *b) {
    return (f64)dist_to(&a->point_120, &b->point_120) < 130.0 ? 1 : 0;
}

// N64 0x801384E4: the tag: the legal man (flags_e9 0x10) goes to 0x94 (tagging out), the other to
// 0x93 (taking the tag), both flags80 0x01, b's flags88 0x02; `at_step2` starts both at step 2 (the
// tag made from the corner pad or the turnbuckle, which skips the reach-out).
void tag_exchange(Wrestler *a, Wrestler *b, s32 at_step2) {
    wrestler_set_state(a, (a->flags_e9 & 0x10) ? state::TagOut : state::TagReceived);
    a->flags80 |= 1;
    wrestler_set_state(b, (a->flags_e9 & 0x10) ? state::TagReceived : state::TagOut);
    b->flags80 |= 1;
    b->flags88 |= 2;
    if (at_step2) {
        a->state_step = 2;
        b->state_step = 2;
    }
}

// N64 0x801382BC: C-up: tries the tag (1 when made). Only in tag matches without rule 0x40, with a
// partner. The legal man in the ring tags a partner who is on its own apron quarter (apron_side_ok)
// within 130: when the partner holds its hand out (idle_flags 0x08) and the d-pad held points at the
// partner's apron side, or (idle_flags 0x10) the d-pad holds both bits toward the own corner (then
// from step 2). Otherwise (the partner, or the legal man outside) a wrestler on its own apron quarter
// within 130 of the partner tags it when the partner stands (0x01) in the ring and this one is on
// the apron, or perched on the corner (0x0A, from step 2).
s32 tag_try_partner(WrestlerCtx *ctx) {
    if (!(g_match_type & 0x2000))
        return 0;
    if (g_match_rules & 0x40)
        return 0;
    Wrestler *w = ctx->self;
    if (w->partner == w->slot)
        return 0;
    Wrestler *p = &g_wrestlers[w->partner];
    if (w->location & 1) {
        if (w->flags_e9 & 0x10) {
            if (apron_side_ok(p) && dist_between(w, p)) {
                if ((p->idle_flags & 8) && (mem_u16(A_SIDE_DPAD + 2u * (u32)p->ring_zone) & ctx->buttons)) {
                    tag_exchange(w, p, 0);
                    return 1;
                }
                if (p->idle_flags & 0x10) {
                    u16 want = mem_u16(A_CORNER_DPAD + ((w->flags_e9 & 0x20) ? 0u : 4u));
                    if ((want & ctx->buttons) == want) {
                        tag_exchange(w, p, 1);
                        return 1;
                    }
                }
            }
            if (w->flags_e9 & 0x10)
                return 0;
        }
    }
    if (!apron_side_ok(w))
        return 0;
    if (!dist_between(w, p))
        return 0;
    if (p->state != state::Standing || !(p->location & 1))
        return 0;
    if (w->state == state::TurnbucklePerch) {
        tag_exchange(w, p, 1);
        return 1;
    }
    if (!(w->location & 2))
        return 0;
    tag_exchange(w, p, 0);
    return 1;
}

// N64 0x80138994: the tagged-in partner becomes the legal man: flags_e9 0x10 moves to it from the
// partner (who gets idle_flags 0x04), tag_state (+0xDC) restarts at 0 and tag_health (+0xDE) takes its health;
// the 0x08 bit of flags_e9 moves to it too when the two differ; then the opponent is re-picked
// (slot ^ 2, or its partner slot ^ 3 when that one is out or not legal) and both opponents now
// work on this wrestler.
void tag_in(Wrestler *w) {
    Wrestler *p = &g_wrestlers[w->partner];
    w->flags_e9 |= 0x10;
    p->flags_e9 &= ~0x10;
    p->idle_flags |= 4;
    w->tag_state = 0;
    w->tag_health = w->health;
    if ((w->flags_e9 & 8) != (p->flags_e9 & 8)) {
        w->flags_e9 &= 0xF7;
        p->flags_e9 |= 8;
    }
    s32 opp = (s16)(w->slot ^ 2);
    w->opponent = opp;
    if (g_wrestlers[opp].roster_id == -1 || !(g_wrestlers[opp].flags_e9 & 0x10))
        w->opponent = opp ^ 1;
    if (g_wrestlers[w->opponent].roster_id != -1)
        g_wrestlers[w->opponent].opponent = w->slot;
    s32 other = w->opponent ^ 1;
    if (g_wrestlers[other].roster_id != -1)
        g_wrestlers[other].opponent = w->slot;
}

// N64 0x80138C54: whether an illegal man may stay in the ring: always outside tag rules, for the
// legal man, or while its partner is in a hold (partner_flags80 0xC0); otherwise only while some
// other illegal wrestler still in play is in the ring with idle_flags 0x04 and not 0x02 (not yet
// past its time).
s32 may_stay_inside(const Wrestler *w) {
    if (!(g_match_type & 0x2000) || (g_match_rules & 0x40))
        return 1;
    if (w->flags_e9 & 0x10)
        return 1;
    if (w->partner_flags80 & 0xC0)
        return 1;
    for (s32 k = 0; k < 4; k++) {
        const Wrestler *o = &g_wrestlers[k];
        if (o->roster_id == -1 || (o->end_flags & 1) || (o->flags_e9 & 0x10) || !(o->location & 1))
            continue;
        if ((o->idle_flags & 4) && !(o->idle_flags & 2))
            return 1;
    }
    return 0;
}

// N64 0x80138BE0: whether the wrestler may keep standing where it is (1) or must head back (0, from
// stand_flags_update into 0x92): the legal man may; with the partner in a hold (partner_flags80
// 0xC0) it may unless that hold has flags80 0x400; else wrestler_may_act, then may_stay_inside.
s32 idle_check(Wrestler *w) {
    if (w->flags_e9 & 0x10)
        return 1;
    if (w->partner_flags80 & 0xC0)
        return (w->partner_flags80 & 0x400) ? 0 : 1;
    if (wrestler_may_act(w))
        return 1;
    return may_stay_inside(w) ? 1 : 0;
}

// N64 0x8013F5D4: C-up pressed: the tag (1 when made).
s32 input_tag(WrestlerCtx *ctx) {
    if (!(ctx->pressed & 8))
        return 0;
    return tag_try_partner(ctx) ? 1 : 0;
}

// N64 0x8011FD20: walks toward `angle` at walk speed (class 2), turning at 341 a frame, with the
// walk animation for the direction moved, the 2.0 slide and the arena walls (1 when a wall stopped it).
s32 walk_to_angle(WrestlerCtx *ctx, s32 angle) {
    Wrestler *w = ctx->self;
    f32 speed = walk_speed(w, 2);
    wrestler_turn_toward(w, angle, 341, 0);
    Vec3 d;
    vec_from_angle(angle, speed, &d);
    wrestler_translate(w, &d);
    wrestler_set_anim(w, anim_pick(ctx, 0, vec_xz_angle(d.x, d.z)), 3);
    wrestler_slide_b(w, 2.0f);
    return keep_in_bounds_self(w, 1.0f, 0, 2);
}

// N64 0x8011F8D8: an illegal man who may not act walks back to its corner (from 0x01 through
// stand_flags_update); as soon as it may act again, back to 0x01. down_count is cleared.
// - In the ring: toward its team's corner (0x8014D1A8: index 0 for team 0x20, 2 for the other),
//   aimed 170 angle units off it (the sign by team and whether point_120 is in ring zone 0 or 3),
//   at walk speed; when a wall stops it at that corner (nearest_corner), and the wall's zone (+0x24)
//   is 1 / 2 (team 0x20) or 3 / 4, it steps out to the apron (0x6E) dropping any weapon.
// - On the apron: 0x6E (it goes on down).
// - On the floor (location 0x04 / 0x40): toward a point by its floor side (ring_zone 0-3) and team:
//   team 0x20 (180, 310) / (310, 180) for sides 0 / 1 (with the path segments 0x801475D0 /
//   0x80147660), (530, -310) / (-310, 530) for 2 / 3; the other team (-530, 310) / (310, -530) for 0 /
//   1, (-180, -310) / (-310, -180) for 2 / 3 (paths 0x801476F0 / 0x80147780). Within 10 of it (and
//   not out of the fall) it climbs up (0x6F, zone_info = the path); else walk_to_angle toward it,
//   and a wall stop on a raised place (location 0x40) drops down (0x6E).
void state_92_illegal_return(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (wrestler_may_act(w)) {
        wrestler_set_state_now(w, state::Standing);
        return;
    }
    w->down_count = 0;
    if (w->location & 1) {
        s32 corner = 0;
        s32 off;
        bool z03 = w->joint_zone == 0 || w->joint_zone == 3;
        if (w->flags_e9 & 0x20) {
            off = z03 ? -170 : 170;
        } else {
            corner = 2;
            off = z03 ? 170 : -170;
        }
        Vec3 c = {mem_f32(A_CORNERS + 12u * (u32)corner), mem_f32(A_CORNERS + 12u * (u32)corner + 4),
                  mem_f32(A_CORNERS + 12u * (u32)corner + 8)};
        s32 to = angle_to(&w->point_120, &c);
        s32 head = angle_add(to, off);
        f32 speed = walk_speed(w, 2);
        Vec3 d;
        vec_from_angle(head, speed, &d);
        wrestler_translate(w, &d);
        wrestler_set_anim(w, anim_pick(ctx, 0, head), 3);
        wrestler_turn_toward(w, to, 341, 0);
        wrestler_slide_b(w, 2.0f);
        if (!keep_in_bounds_self(w, 1.0f, 0, 2))
            return;
        if (nearest_corner(&w->point_120, 3) != corner)
            return;
        u16 zone = mem_u16(w->zone_info + 0x24);
        bool at_side = (w->flags_e9 & 0x20) ? (u16)(zone - 1) < 2 : (u16)(zone - 3) < 2;
        if (!at_side)
            return;
        wrestler_set_state_now(w, state::DropDown);
        weapon_release(w, 0);
        return;
    }
    if (w->location & 2) {
        wrestler_set_state_now(w, state::DropDown);
        return;
    }
    if (!(w->location & 0x44))
        return;
    u32 path = 0;
    f32 tx, tz;
    bool team = (w->flags_e9 & 0x20) != 0;
    switch (w->ring_zone) {
    case 0:
        tx = team ? 180.0f : -530.0f;
        tz = 310.0f;
        path = team ? 0x801475D0u : 0;
        break;
    case 1:
        tx = 310.0f;
        tz = team ? 180.0f : -530.0f;
        path = team ? 0x80147660u : 0;
        break;
    case 2:
        tx = team ? 530.0f : -180.0f;
        tz = -310.0f;
        path = team ? 0 : 0x801476F0u;
        break;
    case 3:
        tx = -310.0f;
        tz = team ? 530.0f : -180.0f;
        path = team ? 0 : 0x80147780u;
        break;
    default:
        // The original reads an unset stack point here.
        NM_UNPORTED("state_92_illegal_return: a floor side outside 0-3");
    }
    Vec3 target = {tx, 0.0f, tz};
    f32 seg[9];
    segment_distance(&w->point_120, &target, seg);
    if (!(w->end_flags & 1) && (f64)seg[4] < 10.0) {
        w->zone_info = path;
        wrestler_set_state_now(w, state::ClimbOnto);
        return;
    }
    if (!walk_to_angle(ctx, (s32)seg[7]))
        return;
    if (w->location & 0x40)
        wrestler_set_state_now(w, state::DropDown);
}

// N64 0x8013874C: taking the tag (0x93, from tag_exchange). Step 0: placed on the apron line facing
// into the ring from its side (0x8014D1E8), the reach 12923 (mode 2), weapon dropped; step 1:
// move_event, then the tag-in (tag_in) when it reports the tag landed. Step 2 (a tag from the
// corner): 12926, or 12927 when facing more than 90 degrees off its direction, weapon dropped; step
// 3 at the end: tag_in, then perched on the corner (0x0A at step 3, 12092, count_timer 0).
void state_93_tag_received(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        apron_line_pos(w, &w->pos);
        w->facing = mem_s32(A_SIDE_FACING + 4u * (u32)w->ring_zone);
        wrestler_set_anim(w, 12923, 2);
        weapon_release(w, 0);
        w->state_step = w->state_step + 1;
        return;
    case 1:
        if (move_event(ctx, 0))
            tag_in(w);
        return;
    case 2: {
        s32 off = abs_i(angle_diff(mem_s32(A_DIR_ANGLES + 4u * (u32)w->dir), w->facing));
        wrestler_set_anim(w, (off >= 1024) ? 12927 : 12926, 2);
        weapon_release(w, 0);
        w->state_step = w->state_step + 1;
        return;
    }
    case 3:
        if (w->anim_frames_left > 0)
            return;
        tag_in(w);
        wrestler_set_anim(w, 12092, 2);
        wrestler_set_state_now(w, state::TurnbucklePerch);
        w->state_step = 3;
        w->count_timer = 0;
        return;
    default:
        return;
    }
}

// N64 0x801388B0: tagging out (0x94, from tag_exchange): 12924 placed against the partner
// (pair_scripted_place), weapon dropped; then pair_slide_step and move_event (from step 0 or 2;
// steps 1 and 3 run the slide).
void state_94_tag_out(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
    case 2:
        wrestler_set_anim(w, 12924, 2);
        pair_scripted_place(w, &g_wrestlers[w->partner]);
        weapon_release(w, 0);
        w->state_step = w->state_step + 1;
        return;
    case 1:
    case 3:
        pair_slide_step(w, nullptr);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80138588: the partner on the apron (0x95, from illegal_man_leave): illegal_man_check every
// frame. Step 0: 12919 (12920 when its partner is down, flags80 0x08), mode 2; step 1: C-up while
// its partner is in a hold without flags80 0x400 steps back to 0x01 (to break it up), else
// move_event; step 2: 12921, or 12922 when facing more than 90 degrees off its direction, mode 1;
// step 3: location 0x800 each frame, count_timer counts up, R sets flags_f0 0x02, and at the end it
// perches on the corner (0x0A at step 3, 12092).
void state_95_apron_partner(WrestlerCtx *ctx) {
    illegal_man_check(ctx->self);
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, (g_wrestlers[w->partner].flags80 & 8) ? 12920 : 12919, 2);
        w->state_step = w->state_step + 1;
        return;
    case 1:
        if ((ctx->pressed & 8) && (w->partner_flags80 & 0xC0) && !(w->partner_flags80 & 0x400)) {
            wrestler_set_state_now(w, state::Standing);
            return;
        }
        move_event(ctx, 0);
        return;
    case 2: {
        s32 off = abs_i(angle_diff(mem_s32(A_DIR_ANGLES + 4u * (u32)w->dir), w->facing));
        wrestler_set_anim(w, (off < 1024) ? 12921 : 12922, 1);
        w->state_step = w->state_step + 1;
        return;
    }
    case 3:
        w->location |= 0x800;
        w->count_timer = (u16)(w->count_timer + 1);
        if (ctx->pressed & 0x10)
            w->flags_f0 |= 2;
        if (w->anim_frames_left <= 0) {
            wrestler_set_anim(w, 12092, 1);
            wrestler_set_state_now(w, state::TurnbucklePerch);
            w->state_step = 3;
        }
        return;
    default:
        return;
    }
}

// N64 0x8010E550: state 0x02, the apron corner walk: sliding round the apron's outer corner to the
// next side holding the ropes, animation 11796 (11795 the other way round, flags8a 0x04), then the
// animation's events (move_event) until it ends.
void state_02_apron_corner_walk(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->state_step == 0) {
        wrestler_set_anim(w, (w->flags8a & 4) ? 11795 : 11796, 0);
        w->state_step++;
    } else if (w->state_step == 1) {
        move_event(ctx, 0);
    }
}

// N64 0x800EDC84: whether nobody is getting onto or off the table of side `side` (0 / 1): no wrestler
// in 0x6E / 0x6F with state_counter 0x04 / 0x08.
s32 climb_place_free(s32 side) {
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *o = &g_wrestlers[i];
        if ((u32)(o->state - state::DropDown) >= 2)
            continue;
        if (o->state_counter & (side ? 8 : 4))
            return 0;
    }
    return 1;
}

// N64 0x801380F8: the illegal man of a tag match going back to his apron (0x95). Standing (state
// 1, not under rule 0x40) when apron_side_ok says he is in his own corner; otherwise, not the legal
// man, on his team's ring side (dir 0 for team 0x20, else 2), at step 2.
s32 illegal_man_leave(Wrestler *w) {
    if (w->state == state::Standing) {
        if (!(g_match_type & 0x2000) || (g_match_rules & 0x40))
            return 0;
        if (!apron_side_ok(w))
            return 0;
        wrestler_set_state_now(w, state::ApronPartnerWaiting);
        return 1;
    }
    if (!(g_match_type & 0x2000) || (w->flags_e9 & 0x10))
        return 0;
    if (w->dir != ((w->flags_e9 & 0x20) ? 0 : 2))
        return 0;
    wrestler_set_state_now(w, state::ApronPartnerWaiting);
    w->state_step = 2;
    return 1;
}

// N64 0x80138D40: the legal man re-targets while his partner is in a move: the opponent's partner
// when the partner is the attacker of a hold or a carry (0x44, 0x48, 0x4A, 0x7F, 0x81), the partner's
// opponent when he is its defender (0x45, 0x49, 0x4B, 0x80, 0x82).
void partner_in_move(Wrestler *w) {
    s32 p = w->partner;
    if (w->slot == p)
        return;
    const Wrestler *pw = &g_wrestlers[p];
    switch (pw->state) {
    case state::PinAttacker: case state::GroundHoldAttacker: case state::PinFromMoveAttacker: case state::RearLiftAttacker: case state::CarryAttacker:
        w->opponent = g_wrestlers[pw->opponent].partner;
        return;
    case state::PinDefender: case state::GroundHoldDefender: case state::PinFromMoveDefender: case state::RearLiftDefender: case state::CarryDefender:
        w->opponent = pw->opponent;
        return;
    default:
        return;
    }
}

}  // namespace nm

// The ladder match: a standing ladder (Object type2 25, flags 0x100) and the states of the wrestlers
// on it. A climber has held_obj (the ladder), ladder_side (0 / 1), ladder_rung (0-5) and location 0x08,
// and is held in the ladder's climber0 / climber1. ladder_pose runs first in every state on the ladder.
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_OBJECTS = 0x8016AE10,   // Object[7]
    A_OBJ_MODELS = 0x800A9C70,  // 140 bytes per object: +1 s8 the probe count, +0x2C the probe points
    A_RUNG_POINTS = 0x80146A10,  // s8 x, s8 z per rung, relative to the ladder and its tilt
    // A quad record for segment_hit, which reads its wall from RDRAM. The N64 keeps it on the stack.
    A_QUAD_SCRATCH = 0x807FFE00,
};

inline u32 obj_addr(s32 obj) { return A_OBJECTS + 180u * (u32)obj; }
inline Vec3 mem_vec(u32 a) { return {mem_f32(a), mem_f32(a + 4), mem_f32(a + 8)}; }
inline void mem_wvec(u32 a, const Vec3 &v) {
    mem_wf32(a, v.x);
    mem_wf32(a + 4, v.y);
    mem_wf32(a + 8, v.z);
}

inline void ladder_let_go(Wrestler *w) {
    w->location &= 0xFFE7;
    mem_w32(obj_addr(w->held_obj) + 0x14 + 4u * (u32)w->ladder_side, (u32)-1);
}

inline bool ladder_falling(const Wrestler *w) { return (mem_u16(obj_addr(w->held_obj) + 0x20) & 0x4000) != 0; }

// N64 0x8012EF94: the unit normal of the plane through three points.
void plane_normal(const Vec3 *a, const Vec3 *b, const Vec3 *c, Vec3 *out) {
    Vec3 u, v;
    vec_sub(a, b, &u);
    vec_sub(b, c, &v);
    vec_cross(&u, &v, out);
    vec_normalize(out);
}

// Probe point k of an object's model record, cut to whole units, x negated, stored at `a`.
void quad_point(u32 m, s32 k, u32 a) {
    u32 p = m + 0x2C + 12u * (u32)k;
    mem_w16(a, (u16)-(s32)mem_f32(p));
    mem_w16(a + 2, (u16)(s32)mem_f32(p + 4));
    mem_w16(a + 4, (u16)(s32)mem_f32(p + 8));
}

}  // namespace

// N64 0x80143B94: an object's contact faces from its probe points (the model record's +0x2C), written
// as wall records at `out` (48 bytes each: 4 s16 points, the normal at +0x18). By probe count: 2, a
// long thin object: a rectangle 30 wide around the line between the two points, turned along it (its
// y from the points' heights); 4, one face (points 2 and 3 swapped); 8, two faces, the second's normal
// the other way. Other counts: nothing. Clears 48 + 1 bytes, not the second record: the N64's second
// mem_zero is at out + 1. Named grapple_contact from World Tour by call graph; it is not that.
void object_contact_faces(s32 obj, u32 out) {
    for (u32 k = 0; k < 49; k++)
        mem_w8(out + k, 0);
    u32 m = A_OBJ_MODELS + 140u * (u32)obj;
    s32 n = mem_s8(m + 1);
    Vec3 a, b, c;
    if (n == 2) {
        Vec3 p[2];
        for (s32 k = 0; k < 2; k++) {
            u32 q = m + 0x2C + 12u * (u32)k;
            p[k].x = (f32)(s16)-(s32)mem_f32(q);
            p[k].y = (f32)(s16)(s32)mem_f32(q + 4);
            p[k].z = (f32)(s16)(s32)mem_f32(q + 8);
        }
        Vec3 mid = {(f32)((f64)(p[0].x + p[1].x) * 0.5), 0.0f, (f32)((f64)(p[0].z + p[1].z) * 0.5)};
        s32 along = angle_to(&p[0], &p[1]);
        f32 half = (f32)((f64)dist_to(&p[0], &p[1]) * 0.5);
        Vec3 c4[4] = {{-15.0f, p[0].y, half}, {15.0f, p[0].y, half}, {15.0f, p[1].y, -half}, {-15.0f, p[1].y, -half}};
        for (s32 k = 0; k < 4; k++) {
            point_at_offset(&mid, &c4[k], along, &c4[k]);
            mem_w16(out + 6u * (u32)k, (u16)(s32)c4[k].x);
            mem_w16(out + 6u * (u32)k + 2, (u16)(s32)c4[k].y);
            mem_w16(out + 6u * (u32)k + 4, (u16)(s32)c4[k].z);
        }
    } else if (n == 4 || n == 8) {
        for (s32 k = 0; k < 4; k++)
            quad_point(m, k, out + 6u * (u32)k);
        if (n == 4) {
            for (u32 k = 0; k < 6; k += 2) {
                u16 t = mem_u16(out + 12 + k);
                mem_w16(out + 12 + k, mem_u16(out + 18 + k));
                mem_w16(out + 18 + k, t);
            }
        }
    } else {
        return;
    }
    vec_from_s16_mem(out, &a);
    vec_from_s16_mem(out + 6, &b);
    vec_from_s16_mem(out + 12, &c);
    Vec3 nrm;
    plane_normal(&a, &b, &c, &nrm);
    mem_wvec(out + 24, nrm);
    if (n != 8)
        return;
    u32 o2 = out + 48;
    for (s32 k = 0; k < 4; k++)
        quad_point(m, k + 4, o2 + 6u * (u32)k);
    vec_from_s16_mem(o2 + 6, &a);
    vec_from_s16_mem(o2, &b);
    vec_from_s16_mem(o2 + 12, &c);
    plane_normal(&a, &b, &c, &nrm);
    mem_wvec(o2 + 24, nrm);
}

// N64 0x80144098: the loose object (holder -1, lying, not falling) a vertical segment through `p` (from
// 10 below to 90 above) crosses a face of, either way, or one within `radius` of `p` whose position is
// in that height range; -1 none.
s32 object_at_point(const Vec3 *p, f32 radius) {
    Vec3 top = *p, bottom = *p;
    top.y = (f32)((f64)top.y + 90.0);
    bottom.y = (f32)((f64)bottom.y - 10.0);
    for (s32 j = 0; j < 7; j++) {
        u32 o = obj_addr(j);
        if (mem_s32(o + 8) != -1)
            continue;
        u16 fl = mem_u16(o + 0x20);
        if (!(fl & 4) || (fl & 0x4000))
            continue;
        object_contact_faces(j, A_QUAD_SCRATCH);
        if (segment_hit(&top, &bottom, A_QUAD_SCRATCH, nullptr, nullptr, 0))
            return j;
        if (segment_hit(&bottom, &top, A_QUAD_SCRATCH, nullptr, nullptr, 0))
            return j;
        Vec3 pos = mem_vec(o + 0x44);
        if (!(dist_to(p, &pos) < radius))
            continue;
        if (bottom.y < pos.y && pos.y < top.y)
            return j;
    }
    return -1;
}

// N64 0x80141D70: sets a ladder or other object moving, knocked in the direction `dir` (ignored unless
// it falls). `kind`, how hard, gives the chance in 1000 that it topples: 0, 1, 5 never; 2, 3, 4, 11
// always; 6, 7 1000, or 250 with someone on it; 8, 9, 10 1000, or 500 with someone on it. Otherwise it
// shakes (flags 0x2000; a position and angles to shake around in +0x28, for 9-18 frames for kind 0,
// 20-39 for the others); not while falling. Toppling (flags 0x4000, shaking off, velocity and spin
// cleared): with nobody on it, thrown along `dir` at 5-7 with drag and the spin doubled. It spins
// 10-24 a frame (x or z by how `dir` lies against its yaw), growing by a sixth; its base at the ring
// (90) or floor (0) height.
void object_set_motion(s32 obj, s32 kind, const Vec3 *dir) {
    u32 o = obj_addr(obj);
    bool occupied = mem_s32(o + 0x14) != -1 || mem_s32(o + 0x18) != -1;
    s32 chance = 0;
    switch (kind) {
    case 0: case 1: case 5:
        chance = 0;
        break;
    case 2: case 3: case 4: case 11:
        chance = 1000;
        break;
    case 6: case 7:
        chance = occupied ? 250 : 1000;
        break;
    case 8: case 9: case 10:
        chance = occupied ? 500 : 1000;
        break;
    default:
        NM_UNPORTED("object_set_motion: a kind above 11");
    }
    auto save_base = [&]() {
        for (u32 k = 0; k < 7; k++)
            mem_w32(o + 0x28 + 4 * k, mem_u32(o + 0x44 + 4 * k));
    };
    if (!chance_check(chance)) {
        u16 fl = mem_u16(o + 0x20);
        if (fl & 0x4000)
            return;
        mem_w16(o + 0x20, (u16)(fl | 0x2000));
        s32 t = kind == 0 ? rand_pool_next() % 10 + 9 : rand_pool_next() % 20 + 20;
        mem_w32(o + 0x24, (u32)t);
        save_base();
        return;
    }
    mem_w16(o + 0x20, (u16)((mem_u16(o + 0x20) | 0x4000) & 0xDFFF));
    for (u32 k = 0; k < 56; k++)
        mem_w8(o + 0x60 + k, 0);
    s32 spin = rand_pool_next() % 15 + 10;
    if (mem_s32(o + 0x14) == -1 && mem_s32(o + 0x18) == -1) {
        Vec3 v = *dir;
        vec_normalize(&v);
        spin <<= 1;
        vec_scale(&v, (f32)(rand_pool_next() % 3 + 5), &v);
        mem_wf32(o + 0x60, v.x);
        mem_w32(o + 0x64, 0);
        mem_wf32(o + 0x68, v.z);
        mem_wf32(o + 0x7C, (f32)(-((f64)v.x * 0.025)));
        mem_wf32(o + 0x84, (f32)(-((f64)v.z * 0.025)));
    }
    s32 d = angle_diff(-angle_add(mem_s32(o + 0x58), mem_s32(o + 0x5C)), vec_xz_angle(dir->x, dir->z));
    s32 ad = abs_i(d);
    if (ad >= 285 && ad < 1763)
        mem_w32(o + 0x6C, (u32)(d > 0 ? -spin : spin));
    else
        mem_w32(o + 0x70, (u32)(ad < 1025 ? spin : -spin));
    mem_w32(o + 0x88, (u32)(s32)((f64)mem_s32(o + 0x6C) * 0.16666666666666666));
    mem_w32(o + 0x8C, (u32)(s32)((f64)mem_s32(o + 0x70) * 0.16666666666666666));
    save_base();
    mem_wf32(o + 0x2C, 153.0 <= (f64)mem_f32(o + 0x2C) ? 90.0f : 0.0f);
}

// N64 0x80025674 (main segment): the camera's ladder shake: mode, kind, and its timer kept to 0-9.5.
void ladder_camera(s32 mode, s32 kind) {
    mem_w8(0x80089037, 1);
    mem_w16(0x80089034, (u16)(mode & 0xFF));
    mem_w8(0x80089036, (u8)kind);
    f32 t = mem_f32(0x80089030);
    if (9.5f < t)
        mem_wf32(0x80089030, 9.5f);
    else if (t < 0.0f)
        mem_wf32(0x80089030, 0.0f);
}

// N64 0x80142190: a ladder's frame (type2 25). Standing and set up, on frame 23 after it was set up it
// topples unless its owner is still setting it up (0x8D: the camera, flags 0x8000), either way along
// its yaw by chance. Shaking (flags 0x2000): its position jumps within 1.25 of the base every 4 frames.
// Toppling (0x4000): moves and spins, against the arena (object_fly), with the x tilt going to 0 or 2048
// and z to +-1024, pivoting on its base 126 below; once past the upright it flies loose (flags 0x18, up
// -5.25, gravity 1.75, from 90 above where it is).
void ladder_object_update(s32 obj) {
    u32 o = obj_addr(obj);
    if (mem_s32(o + 4) != 25)
        return;
    u16 fl = mem_u16(o + 0x20);
    if ((fl & 0x100) && !(fl & 0x4000)) {
        s32 t = mem_s32(o + 0x1C) + 1;
        mem_w32(o + 0x1C, (u32)t);
        if (t == 23) {
            if (g_wrestlers[mem_s32(o + 0xC)].state == state::LadderSetUp) {
                ladder_camera(0, 4);
                mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x8000));
            } else {
                s32 a = -angle_add(mem_s32(o + 0x58), mem_s32(o + 0x5C));
                if (chance_check(500))
                    a = angle_add(a, 2048);
                Vec3 d;
                vec_from_angle(a, 1.0f, &d);
                object_set_motion(obj, 2, &d);
            }
        }
    }
    if (mem_u16(o + 0x20) & 0x2000) {
        if (mem_s32(o + 0x24) & 4) {
            mem_wf32(o + 0x44, (f32)((f64)(mem_f32(o + 0x28) + (f32)(rand_pool_next() % 3)) - 1.25));
            mem_wf32(o + 0x4C, (f32)((f64)(mem_f32(o + 0x30) + (f32)(rand_pool_next() % 3)) - 1.25));
        } else {
            mem_wf32(o + 0x44, mem_f32(o + 0x28));
            mem_wf32(o + 0x4C, mem_f32(o + 0x30));
        }
        s32 t = mem_s32(o + 0x24) - 1;
        mem_w32(o + 0x24, (u32)t);
        if (t < 0)
            mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) & 0xDFFF));
    }
    if (!(mem_u16(o + 0x20) & 0x4000))
        return;
    Vec3 pos = mem_vec(o + 0x44), base = mem_vec(o + 0x28), vel = mem_vec(o + 0x60), acc = mem_vec(o + 0x7C), d;
    vec_add(&pos, &vel, &pos);
    vec_add(&base, &vel, &base);
    vec_add(&vel, &acc, &vel);
    mem_wvec(o + 0x44, pos);
    mem_wvec(o + 0x28, base);
    mem_wvec(o + 0x60, vel);
    object_fly(obj, &d);
    vec_add(&pos, &d, &pos);
    vec_add(&base, &d, &base);
    mem_wvec(o + 0x44, pos);
    mem_wvec(o + 0x28, base);
    for (u32 k = 0; k < 3; k++)
        mem_w32(o + 0x50 + 4 * k, (u32)angle_add(mem_s32(o + 0x50 + 4 * k), mem_s32(o + 0x6C + 4 * k)));
    for (u32 k = 0; k < 3; k++)
        mem_w32(o + 0x6C + 4 * k, (u32)angle_add(mem_s32(o + 0x6C + 4 * k), mem_s32(o + 0x88 + 4 * k)));
    s32 ax = mem_s32(o + 0x50);
    if (ax >= 0)
        mem_w32(o + 0x50, ax < 1024 ? 0u : 2048u);
    if (mem_s32(o + 0x54) >= 1024)
        mem_w32(o + 0x54, 1024);
    if (mem_s32(o + 0x54) < -1023)
        mem_w32(o + 0x54, (u32)-1024);
    Vec3 tip = ladder_point(o, 0.0f, 126.0f);
    vec_add(&base, &tip, &pos);
    mem_wvec(o + 0x44, pos);
    if (mem_s32(o + 0x50) < 0 && (u32)(mem_s32(o + 0x54) + 1023) < 2047)
        return;
    mem_w16(o + 0x20, 24);
    Vec3 prev = pos;
    prev.y = (f32)((f64)prev.y + 90.0);
    mem_wvec(o + 0xA8, prev);
    mem_wf32(o + 0x64, -5.25f);
    mem_wf32(o + 0x80, -1.75f);
}

// N64 0x8012A400: while on a ladder (location 0x08): flags80 0x01 while it falls, the wrestler at its
// rung point (0x80146A10, x mirrored on side 1), facing and tilted with the ladder (mirrored on side 1).
// Returns 1 with the wrestler thrown off (0xBC, from 20 a frame) when the ladder is flying (flags 0x08).
s32 ladder_pose(Wrestler *w) {
    if (!(w->location & 8))
        return 0;
    u32 o = obj_addr(w->held_obj);
    if (mem_u16(o + 0x20) & 0x4000)
        w->flags80 |= 1;
    u32 r = A_RUNG_POINTS + 2u * (u32)w->ladder_rung;
    f32 x = (f32)mem_s8(r);
    bool side1 = w->ladder_side == 1;
    if (side1)
        x = -x;
    Vec3 p = ladder_point(o, x, (f32)mem_s8(r + 1)), base = mem_vec(o + 0x44);
    vec_add(&base, &p, &w->pos);
    s32 f = -angle_add(mem_s32(o + 0x58), mem_s32(o + 0x5C));
    w->facing = f;
    if (side1)
        w->facing = angle_add(f, 2048);
    s32 tx = mem_s32(o + 0x54);
    w->tilt_x = side1 ? -tx : tx;
    s32 tz = angle_add(-mem_s32(o + 0x50), -1024);
    w->tilt_z = side1 ? -tz : tz;
    if (!(mem_u16(o + 0x20) & 8))
        return 0;
    w->step_vec = {0.0f, -20.0f, 0.0f};
    wrestler_set_state_now(w, state::LadderThrownOff);
    return 1;
}

// N64 0x8012A648: states 0xAE / 0xB1, on the ladder taking input; 0xB1 at the top (rung 5, 12989). Step
// 0 clears the hold count. Step 1, not while it falls, after the target switch. Holding down with
// C-down or C-up jumps off (0xB6). On the ladder (0xAE): B strikes (0xB9); up climbs (0xAC, 0xAF to the
// top from rung 4); down for 16 frames climbs down (0xAD, or off 0xAB from rung 0 or 1). At the top (0xB1):
// up for 16 frames reaches for the prize (0xBD), down for 16 frames climbs down (0xB0), A readies a dive
// (0xB8), B strikes (0xB7), R guards (0x4C step 5).
void state_AE_ladder_control(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a |= 0x100;
    wrestler_set_anim(w, w->ladder_rung == 5 ? 12989 : 12971, 0);
    if (ladder_pose(w))
        return;
    if (w->state_step == 0) {
        w->aux_timer = 0;
        w->state_step++;
        return;
    }
    if (w->state_step != 1)
        return;
    if (ladder_falling(w))
        return;
    if (opponent_switch_nearest(w, ctx->pressed))
        return;
    u16 b = ctx->buttons;
    if ((b & 0x400) && (b & 0xC)) {
        wrestler_set_state_now(w, state::LadderJumpOff);
        return;
    }
    if (b & 0xC00)
        w->aux_timer = (u16)(w->aux_timer + 1);
    else
        w->aux_timer = 0;
    bool held = (s16)w->aux_timer >= 16;
    if (w->state == state::OnLadderTop) {
        if ((b & 0x800) && held)
            wrestler_set_state_now(w, state::LadderReachForPrize);
        else if ((b & 0x400) && held)
            wrestler_set_state_now(w, state::LadderLeaveTop);
        else if (b & 0x8000)
            wrestler_set_state_now(w, state::LadderClimbToDive);
        else if (b & 0x4000)
            wrestler_set_state_now(w, state::LadderTopStrike);
        else if (b & 0x10) {
            wrestler_set_state_now(w, state::Blocking);
            w->state_step = 5;
        }
        return;
    }
    if (b & 0x4000)
        wrestler_set_state_now(w, state::LadderRungStrike);
    else if (b & 0x800)
        wrestler_set_state_now(w, w->ladder_rung < 4 ? state::LadderClimbUp : state::LadderClimbToTop);
    else if ((b & 0x400) && held)
        wrestler_set_state_now(w, w->ladder_rung < 2 ? state::LadderGetOff : state::LadderClimbDown);
}

// N64 0x8012A888: state 0xAA, getting on the ladder (input_ladder_climb). Step 0: anim_pick's walk,
// the side taken, rung 0, to 80 out along the side's normal, facing it. Step 1 walks there (flags80
// 0x01; the ladder falling meanwhile: off, standing); then 12967. Steps 1-2: the bounds push moves the
// ladder too. Step 2 (location 0x08): ladder_pose, each end of the events up a rung.
void state_AA_ladder_climb(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    auto push_ladder = [&]() {
        Vec3 before = w->pos, d, p = mem_vec(obj_addr(w->held_obj) + 0x44);
        keep_in_bounds_self(ctx->self, 8.0f, 0, 2);
        vec_sub(&before, &w->pos, &d);
        vec_add(&p, &d, &p);
        mem_wvec(obj_addr(w->held_obj) + 0x44, p);
    };
    switch (w->state_step) {
    case 0: {
        wrestler_set_anim(w, anim_pick(ctx, 0, 0), 3);
        u32 o = obj_addr(w->held_obj);
        mem_w32(o + 0x14 + 4u * (u32)w->ladder_side, (u32)(s32)w->slot);
        w->ladder_rung = 0;
        object_contact_faces(w->held_obj, A_QUAD_SCRATCH);
        Vec3 n = mem_vec(A_QUAD_SCRATCH + 48u * (u32)w->ladder_side + 24), d, t, p = mem_vec(o + 0x44);
        vec_scale(&n, 80.0f, &d);
        vec_add(&p, &d, &t);
        t.y = w->pos.y;
        ground_approach_plan(ctx, &t, 0);
        w->lockup_facing = angle_add(vec_xz_angle(n.x, n.z), 2048);
        w->state_step++;
        [[fallthrough]];
    }
    case 1: {
        w->flags80 |= 1;
        s32 done = ground_approach_plan(ctx, nullptr, 1);
        done += wrestler_turn_toward(w, w->lockup_facing, 227, 0);
        if (done == 2) {
            if (ladder_falling(w)) {
                mem_w32(obj_addr(w->held_obj) + 0x14 + 4u * (u32)w->ladder_side, (u32)-1);
                wrestler_set_state_now(w, state::Standing);
                return;
            }
            wrestler_set_anim(w, 12967, 2);
            w->state_step++;
        }
        push_ladder();
        return;
    }
    case 2:
        w->location |= 8;
        push_ladder();
        if (ladder_pose(ctx->self))
            return;
        if (move_event(ctx, 0))
            w->ladder_rung++;
        return;
    default:
        return;
    }
}

// N64 0x8012ABD4: state 0xAB, off the ladder at the bottom (12968): at the end on the ring (90) or floor.
void state_AB_ladder_off(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->location &= 0xFFF7;
    mem_w32(obj_addr(w->held_obj) + 0x14 + 4u * (u32)w->ladder_side, (u32)-1);
    keep_in_bounds_self(w, 4.0f, 0, 2);
    wrestler_set_anim(w, 12968, 6);
    if (move_event(ctx, 0))
        w->pos.y = (w->location & 1) ? 90.0f : 0.0f;
}

// N64 0x8012AC9C: state 0xAC, a rung up (12969).
void state_AC_ladder_up(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12969, 6);
    if (move_event(ctx, 0))
        w->ladder_rung++;
}

// N64 0x8012ACFC: state 0xAD, a rung down (12970).
void state_AD_ladder_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12970, 6);
    if (move_event(ctx, 0))
        w->ladder_rung--;
}

// N64 0x8012AD5C: state 0xAF, up onto the top (12987; location 0x10 at the end).
void state_AF_ladder_top(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12987, 6);
    if (move_event(ctx, 0)) {
        w->ladder_rung++;
        w->location |= 0x10;
    }
}

// N64 0x8012ADCC: state 0xB0, down from the top (12988; location 0x10 off at the end).
void state_B0_ladder_top_leave(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12988, 6);
    if (move_event(ctx, 0)) {
        w->ladder_rung--;
        w->location &= 0xFFEF;
    }
}

// N64 0x8012AE3C: state 0xB2, hit on the ladder. Step 0: the damage unless state_counter 0x04; the
// ladder rocks along the facing (object_set_motion 1, or 2 on state_counter 0x02). Falls (0xB3) when
// state_counter 0x01, on chance_roll kind 19, or when just reaching for the prize (came from 0xBD);
// otherwise holds on (12978, 12995 at the top). Step 1 the events.
void state_B2_ladder_knocked(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(ctx->self))
        return;
    switch (w->state_step) {
    case 0: {
        if (!(w->state_counter & 4))
            apply_damage(ctx->rec_a, w);
        s32 kind = (w->state_counter & 2) ? 2 : 1;
        Vec3 d;
        vec_from_angle(w->facing, 1.0f, &d);
        object_set_motion(w->held_obj, kind, &d);
        if ((w->state_counter & 1) || chance_roll(w, ctx->rec_a, 19) || w->prev_state == 189) {
            wrestler_set_state_now(w, state::LadderFallOff);
            return;
        }
        wrestler_set_anim(w, w->ladder_rung == 5 ? 12995 : 12978, 6);
        w->state_step++;
        return;
    }
    case 1:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012AF68: states 0xB3 / 0xB5, falling off the ladder. Step 0: it rocks (object_set_motion 1),
// 12979 / 12984, off it. Step 1 near the animation's end the next one, falling at 20 a frame. Step 2 down
// (3 a frame faster) to pelvis height 100 in the ring or 10 on the floor, then the next, on the ground.
// Step 3 the events (0x25A / 0x25B).
void state_B3_ladder_fall(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        object_set_motion(w->held_obj, 1, nullptr);
        wrestler_set_anim(w, w->state == state::LadderFallOff ? 12979 : 12984, 9);
        ladder_let_go(w);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left != 1)
            return;
        w->step_vec = {0.0f, -20.0f, 0.0f};
        wrestler_set_anim(w, w->anim_id + 1, 9);
        wrestler_set_step(w, 2);
        return;
    case 2: {
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        wrestler_translate(w, &w->step_vec);
        w->step_vec.y = (f32)((f64)w->step_vec.y + -3.0);
        f32 lim = (w->location & 1) ? 100.0f : 10.0f;
        if (!(w->point_120.y <= lim))
            return;
        w->pos.y = (f32)((f64)lim - 10.0);
        wrestler_set_anim(w, w->anim_id + 1, 1);
        wrestler_set_step(w, 3);
        return;
    }
    case 3:
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        move_event(ctx, w->state == state::LadderFallOff ? 0x25A : 0x25B);
        return;
    default:
        return;
    }
}

// N64 0x8012B17C: state 0xB4, pulled off the ladder by a grapple (12982, on the apron 12983).
void state_B4_ladder_pull_off(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->location & 2)
        wrestler_set_anim(w, 12983, 2);
    else
        wrestler_set_anim(w, 12982, 3);
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    move_event(ctx, 0);
}

// N64 0x8012B1EC: state 0xB6, jumping off the ladder (12972, the ladder rocks). Step 1 near its end, down
// at 20 a frame, 3 faster each (12973 in step 2) until joint 13 is at 100 in the ring or 10 on the floor;
// step 3 12974 and the events.
void state_B6_ladder_jump(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12972, 9);
        object_set_motion(ctx->self->held_obj, 1, nullptr);
        ladder_let_go(w);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left != 1)
            return;
        w->step_vec = {0.0f, -20.0f, 0.0f};
        wrestler_set_step(w, 2);
        return;
    case 2: {
        wrestler_set_anim(w, 12973, 9);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        wrestler_translate(w, &w->step_vec);
        w->step_vec.y = (f32)((f64)w->step_vec.y + -3.0);
        f32 lim = (w->location & 1) ? 100.0f : 10.0f;
        if (!(w->joints[13].y <= lim))
            return;
        w->pos.y = (f32)((f64)lim - 10.0);
        wrestler_set_step(w, 3);
        return;
    }
    case 3:
        wrestler_set_anim(w, 12974, 1);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012B3D8: state 0xB7, a strike from the top (generic move 87, 12992; strike_hit_test kind 26).
void state_B7_ladder_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(ctx->self))
        return;
    switch (w->state_step) {
    case 0:
        wrestler_set_generic_move(w, 87);
        wrestler_set_anim(w, 12992, 6);
        w->state_step++;
        return;
    case 1: {
        s32 t = strike_hit_test(w, 0, 26);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 1);
        }
        move_event(ctx, 0);
        return;
    }
    case 2:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012B4B0: state 0xB9, a strike from a rung: by the angle to the opponent, ahead (within 170, or
// beyond 1878) generic move 86 and 12977, to one side 85 / 12976 or 84 / 12975; kind 25.
void state_B9_ladder_mid_strike(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(ctx->self))
        return;
    switch (w->state_step) {
    case 0: {
        s32 d = angle_diff(w->facing, w->facing_to_opponent);
        s32 ad = abs_i(d);
        s32 k = (ad < 1878 && ad >= 170) ? (d >= 0 ? 1 : 0) : 2;
        wrestler_set_generic_move(w, k | 0x54);
        wrestler_set_anim(w, 12975 + k, 6);
        w->state_step++;
        return;
    }
    case 1: {
        s32 t = strike_hit_test(w, 0, 25);
        if (t != -1) {
            w->state_step++;
            strike_land(w, t, 1);
        }
        move_event(ctx, 0);
        return;
    }
    case 2:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012B5DC: state 0xBA, at the top (12993), at the events' end the strike 0xB7.
void state_BA_ladder_top_shove(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12993, 6);
    if (move_event(ctx, 0))
        wrestler_set_state_now(w, state::LadderTopStrike);
}

// N64 0x8012B638: state 0xBB, yanked at the top (12994, events 92).
void state_BB_ladder_top_yank(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    wrestler_set_anim(w, 12994, 6);
    move_event(ctx, 92);
}

// N64 0x8012B680: state 0xB8, climbing to dive off the top. Step 0: back to 0xB1 if another in the match
// is at this or the prize (0xB8, 0xBD), or no dive is available (climb_check); else the move and 12996.
// Step 1 near its end, unless the ladder falls (step 2: nothing), the dive (states 0x79-0x7D).
void state_B8_ladder_climb_top(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(ctx->self))
        return;
    if (w->state_step == 0) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *x = &g_wrestlers[i];
            if (x->roster_id != -1 && i != ctx->slot && (x->state == state::LadderClimbToDive || x->state == state::LadderReachForPrize)) {
                wrestler_set_state(w, state::OnLadderTop);
                return;
            }
        }
        s32 m = climb_check(w, ctx->rec_a);
        if (!m) {
            wrestler_set_state(w, state::OnLadderTop);
            return;
        }
        w->move_num = (s16)m;
        wrestler_set_anim(w, 12996, 6);
        w->state_step++;
        return;
    }
    if (w->state_step != 1)
        return;
    if (!(w->anim_frames_left < 2))
        return;
    if (ladder_falling(w)) {
        w->state_step = 2;
        return;
    }
    s32 m = climb_check(w, ctx->rec_a);
    wrestler_start_move(w, ctx->slot, m ? m : w->move_num);
    s32 st;
    switch (w->move_num) {
    case 256: case 257: st = state::TurnbuckleDiveMoves256And257; break;
    case 263: case 264: st = state::TurnbuckleDiveMoves263And264; break;
    case 261: case 262: st = state::TurnbuckleDiveMoves261And262; break;
    case 279: st = state::TurnbuckleDiveMove279; break;
    case 280: st = state::TurnbuckleDiveMove280; break;
    default: NM_UNPORTED("state_B8: a dive of no state");
    }
    wrestler_set_state_now(w, st);
}

// N64 0x8012B86C: state 0xBC, thrown off a ladder that flew (ladder_pose). Step 0 down (3 a frame faster
// from 20) to pelvis height 100 in the ring or 10 on the floor, off it. Step 1 the landing by its tilt
// (13002 + 0-3: flat on the back or front, or to a side), hurt by itself (generic move 81). Step 2 the
// events (512). Was state_BC_ladder_c_up.
void state_BC_ladder_thrown_off(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        wrestler_translate(w, &w->step_vec);
        w->step_vec.y = (f32)((f64)w->step_vec.y + -3.0);
        f32 lim = (w->location & 1) ? 100.0f : 10.0f;
        if (!(w->point_120.y <= lim))
            return;
        ladder_let_go(w);
        w->pos.y = (f32)((f64)lim - 10.0);
        wrestler_set_step(w, 1);
        return;
    }
    case 1: {
        s32 k;
        if ((s32)absf((f32)angle_diff(0, w->tilt_x)) < 513)
            k = w->tilt_z > 0 ? 3 : 1;
        else
            k = w->tilt_x < 1 ? 2 : 0;
        wrestler_set_anim(w, 13002 + k, 1);
        wrestler_set_generic_move(w, 81);
        hit_move_copy(w, w);
        apply_damage(w, w);
        w->state_step++;
        [[fallthrough]];
    }
    case 2:
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 512);
        return;
    default:
        return;
    }
}

// N64 0x8012BA50: state 0xBD, reaching for the prize. Step 0: back to 0xB1 if another is readying a dive
// from the top (0xB8). Step 1 the reach (12997); near its end the mash count, slide_frames = 40 -
// spirit / 5, and step 3 with the ladder within 30 of the ring's centre (else step 2, 13001, the events:
// it is not under the prize). Step 3 (12998), not while the ladder falls: each press of A, B, L or R takes one off;
// below 0 step 4. A wrestler that has lost (end_flags 0x801), or holding down for 16 frames, gets
// back to step 2. Step 4 (12999), near its end: the opponent loses (end_flags 0x800, pair_other this
// one), off the ladder, the prize in the hand (held_obj 3, weapon_pick_type 8), step 5. Step 5 (13000):
// the win message 2 frames before the end, at the end it lets the prize go on the ring (90) or floor.
void state_BD_ladder_grab_prize(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (ladder_pose(w))
        return;
    switch (w->state_step) {
    case 0:
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *x = &g_wrestlers[i];
            if (x->roster_id != -1 && i != ctx->slot && x->state == state::LadderClimbToDive) {
                wrestler_set_state(w, state::OnLadderTop);
                return;
            }
        }
        w->state_step++;
        [[fallthrough]];
    case 1: {
        wrestler_set_anim(w, 12997, 6);
        if (!(w->anim_frames_left < 2))
            return;
        w->aux_timer = 0;
        w->slide_frames = (s16)(40 - w->spirit / 5);
        u32 o = obj_addr(w->held_obj);
        wrestler_set_step(w, (f64)vec2_len(mem_f32(o + 0x44), mem_f32(o + 0x4C)) < 30.0 ? 3 : 2);
        return;
    }
    case 2:
        wrestler_set_anim(w, 13001, 6);
        move_event(ctx, 0);
        return;
    case 3:
        wrestler_set_anim(w, 12998, 0);
        if (ladder_falling(w))
            return;
        if (w->end_flags & 0x801) {
            wrestler_set_step(w, 2);
            return;
        }
        if (w->slide_frames < 0) {
            wrestler_set_step(w, 4);
            return;
        }
        if (ctx->pressed & 0xC030)
            w->slide_frames = (s16)(w->slide_frames - 1);
        if (!(ctx->buttons & 0x400)) {
            w->aux_timer = 0;
            return;
        }
        w->aux_timer = (u16)(w->aux_timer + 1);
        if ((s16)w->aux_timer >= 16)
            wrestler_set_step(w, 2);
        return;
    case 4: {
        wrestler_set_anim(w, 12999, 6);
        if (ladder_falling(w))
            return;
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *x = &g_wrestlers[i];
            if (x->roster_id != -1 && (x->end_flags & 0x801)) {
                wrestler_set_step(w, 2);
                return;
            }
        }
        if (!(w->anim_frames_left < 2))
            return;
        ctx->rec_a->end_flags |= 0x800;
        ctx->rec_a->pair_other = w->slot;
        ladder_let_go(w);
        object_set_motion(w->held_obj, 1, nullptr);
        w->held_obj = 3;
        weapon_pick_type(w, 8);
        wrestler_set_step(w, 5);
        return;
    }
    case 5:
        wrestler_set_anim(w, 13000, 0);
        if (w->anim_frame == w->anim_player[AP_FRAMES] - 2)
            host_hud("ladder_message", 0, 0);
        if (move_event(ctx, 0)) {
            weapon_release(w, 1);
            w->pos.y = (w->location & 1) ? 90.0f : 0.0f;
        }
        return;
    default:
        return;
    }
}

// N64 0x80143750: state 0x8D, standing a carried ladder up (12965): near the end it counts as set up
// (flags 0x100), put down in place (weapon_release 2) rocking; 12966 and the events.
void state_8D_ladder_set_up(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    switch (w->state_step) {
    case 0: {
        wrestler_set_anim(w, 12965, 3);
        if (w->anim_frames_left > 0)
            return;
        u32 o = obj_addr(w->held_obj);
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x100));
        weapon_release(w, 2);
        object_set_motion(w->held_obj, 0, nullptr);
        wrestler_set_step(w, 1);
        return;
    }
    case 1:
        wrestler_set_anim(w, 12966, 0);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80143840: state 0x8E, a paired move with a held object at a wall (12461 by its kind; the slide
// to 55 from the wall): at frame 10 the object goes (weapon_release 2).
void state_8E_object_pair_move(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, wrestler_move_anim(w, 0) + 12461, 2);
        pair_scripted_place(w, nullptr);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        return;
    case 2:
        if (w->anim_frame == 10)
            weapon_release(w, 2);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x80143A34: state 0x91, picking a weapon up off the steps or a wall (11901; the slide to 55, the
// animation's strike window one frame wider each way). Step 2, within it, an object at the hand (joint
// 16, within 100) goes in the hand (weapon_pick_type 8, 12466 by its kind), step 3. Steps 2-3 the events.
void state_91_weapon_pickup(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 11901, 2);
        w->anim_player[AP_HIT_FROM]--;
        w->anim_player[AP_HOLD_FRAME]++;
        pair_scripted_place(w, nullptr);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        return;
    case 2:
        if (!(w->anim_frame < w->anim_player[AP_HIT_FROM]) && !(w->anim_player[AP_HOLD_FRAME] < w->anim_frame)) {
            s32 j = object_at_point(&w->joints[16], 100.0f);
            if (j != -1) {
                w->held_obj = j;
                weapon_pick_type(w, 8);
                wrestler_set_anim(w, wrestler_move_anim(w, 0) + 12466, 3);
                w->state_step++;
            }
        }
        move_event(ctx, 0);
        return;
    case 3:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x8012DCC0: opens the arena's walls where a table or the steps stood, after they are picked up
// (place 0-1 the ringside tables, 5-6 the arena's; each relinks a wall list past the removed ones).
static void arena_zones_swap(s32 place) {
    switch (place) {
    case 0:
        mem_w32(0x8014762C, 0x801475A0);
        mem_w32(0x8014765C, 0x80147660);
        break;
    case 1:
        mem_w32(0x8014774C, 0x801476C0);
        mem_w32(0x8014777C, 0x80147780);
        mem_w32(0x80147C40, 0x80147780);
        break;
    case 2:
        mem_w32(0x80147214, 0x80147218);
        mem_w32(0x8014CACC, 0);
        break;
    case 3:
        mem_w32(0x8014B5D8, 0x8014B530);
        mem_w32(0x8014B5DC, 0x8014B530);
        mem_w32(0x8014CB94, 0);
        mem_w32(0x8014B61C, 0);
        break;
    case 4:
        mem_w32(0x8014C39C, 0x8014C2B8);
        mem_w32(0x8014C3A0, 0x8014C2B8);
        mem_w32(0x8014CBD0, 0);
        break;
    case 5:
        mem_w32(0x8014C3A4, 0x8014C2B8);
        mem_w32(0x8014C3A8, 0x8014C2B8);
        break;
    case 6:
        mem_w32(0x8014C3AC, 0x8014C2B8);
        mem_w32(0x8014C3B0, 0x8014C2B8);
        break;
    default:
        break;
    }
}

// N64 0x8001B1D8 (main segment): hides the three render nodes of a broken table.
static void crowd_group_show(s32 group) {
    u32 p = 0x8007B098 + 660u * (u32)group;
    for (u32 k = 0; k < 3; k++)
        mem_w8(p + 220 * k, (u8)(mem_u8(p + 220 * k) | 0x80));
}

// N64 0x8001F264 (main segment): the same for a group's five nodes of another table.
static void effect_group_show(s32 group) {
    u32 p = 0x8007EC78 + 1100u * (u32)group;
    for (u32 k = 0; k < 5; k++)
        mem_w8(p + 220 * k, (u8)(mem_u8(p + 220 * k) | 0x80));
}

// N64 0x80143378: state 0x8B, letting a big weapon go (12390; at frame 3 weapon_release 1).
void state_8B_weapon_drop(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    wrestler_set_anim(w, 12390, 3);
    if (w->anim_frame == 3)
        weapon_release(w, 1);
    move_event(ctx, 0);
}

// N64 0x801433E8: state 0x8C, lifting something big off the ground: a table (state_counter 0x06, 0x18:
// which one in aux_timer), or the object in held_obj, a set-up ladder being state_counter 0x01 (12962,
// else 12379). Steps 0-1 turn to it (227 a frame); near the end it comes up unless someone has it or it
// falls, a ladder with anyone on it, or a table whose place is taken: then 12384 / 12964, the miss (step
// 3). A table lifted becomes object type 19 or 11, leaving its intact bit and its walls (arena_zones_swap);
// a ladder shakes the camera (ladder_camera 1, 4). Step 2: 12380 + its kind, or 12963, the events.
void state_8C_lift_object(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u32 o = 0x8016AE10 + 180u * (u32)w->held_obj;
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    switch (w->state_step) {
    case 0: {
        u16 c = w->state_counter;
        Vec3 t = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
        u32 tbl = 0;
        if (c & 6) {
            w->aux_timer = (c & 2) ? 0 : 1;
            tbl = 0x8014E07C;
        } else if (c & 0x18) {
            w->aux_timer = (c & 8) ? 0 : 1;
            tbl = 0x8014E094;
        } else if (mem_s32(o + 4) == 25 && (mem_u16(o + 0x20) & 0x100)) {
            w->state_counter = c | 1;
        }
        if (tbl) {
            u32 p = tbl + 12u * (u32)(s32)(s16)w->aux_timer;
            t = {mem_f32(p), mem_f32(p + 4), mem_f32(p + 8)};
        }
        w->lockup_facing = angle_to(&w->point_120, &t);
        wrestler_set_anim(w, (w->state_counter & 1) ? 12962 : 12379, 3);
        [[fallthrough]];
    }
    case 1: {
        wrestler_turn_toward(w, w->lockup_facing, 227, 0);
        if (!(w->anim_frames_left < 2))
            return;
        if (mem_s32(o + 8) != -1 || (mem_u16(o + 0x20) & 0x4000)) {
            wrestler_set_step(w, 3);
            return;
        }
        u16 c = w->state_counter;
        s32 place = (s16)w->aux_timer;
        if (c & 1) {
            if (mem_s32(o + 0x14) != -1 || mem_s32(o + 0x18) != -1) {
                wrestler_set_step(w, 3);
                return;
            }
        } else if (c & 6) {
            if (!climb_place_free(place)) {
                wrestler_set_step(w, 3);
                return;
            }
            mem_w8(0x8016B928, (u8)(mem_u8(0x8016B928) & ((c & 2) ? 0xFE : 0xFD)));
            mem_w32(o, 19);
            mem_w32(o + 4, 19);
            crowd_group_show(place);
            arena_zones_swap(place);
        } else if (c & 0x18) {
            mem_w8(0x8016B928, (u8)(mem_u8(0x8016B928) & ((c & 8) ? 0xDF : 0xBF)));
            mem_w32(o, 11);
            mem_w32(o + 4, 11);
            effect_group_show(place);
            arena_zones_swap(place + 5);
        }
        weapon_pick_type(w, 8);
        if (mem_s32(o + 4) == 25) {
            ladder_camera(1, 4);
            mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) & 0x7FFF));
        }
        wrestler_set_step(w, 2);
        return;
    }
    case 2:
        wrestler_set_anim(w, (w->state_counter & 1) ? 12963 : wrestler_move_anim(w, 0) + 12380, 3);
        move_event(ctx, 0);
        return;
    case 3:
        wrestler_set_anim(w, (w->state_counter & 1) ? 12964 : 12384, 3);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x801442F8: a wrestler's animation frame that touches the ground (anim_player byte 10): its feet
// and joints (0x8014E0CC: 1 point_120, 2+ joints) against the faces of loose objects, 90 up to 45 down.
// The object of highest priority (0x8014E0B0 by type2) gives a step sound (object_sfx kind 4), and that
// sound marks flags8e: 47 0x04, 55 0x20, 40 0x08, none 0x10, others 0x02.
void objects_wrestler_contact() {
    const u32 faces = 0x807FFE00;
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1)
            continue;
        u8 frame = w->anim_player[10];
        if (frame == 0 || w->anim_frame != frame)
            continue;
        s32 best = 0, best_obj = 0;
        for (s32 j = 0; j < 7; j++) {
            u32 obj = 0x8016AE10 + 180u * (u32)j;
            s32 type = mem_s32(obj + 4);
            if (type == -1 || mem_s32(obj + 8) != -1)
                continue;
            u16 fl = mem_u16(obj + 0x20);
            if (!(fl & 4) || (fl & 0x4000))
                continue;
            object_contact_faces(j, faces);
            bool hit = false;
            for (u32 k = 0; !hit && mem_s8(0x8014E0CC + k) != -1; k++) {
                s32 n = mem_s8(0x8014E0CC + k);
                Vec3 top = n == 1 ? w->point_120 : w->joints[n - 2], bottom = top;
                top.y = (f32)((f64)top.y + 90.0);
                bottom.y = (f32)((f64)bottom.y - 45.0);
                hit = segment_hit(&top, &bottom, faces, nullptr, nullptr, 0) ||
                      segment_hit(&bottom, &top, faces, nullptr, nullptr, 0);
            }
            if (!hit)
                continue;
            s32 prio = mem_u8(0x8014E0B0 + (u32)type);
            if (best < prio) {
                best = prio;
                best_obj = j;
            }
        }
        if (!best)
            continue;
        s32 id = object_sfx(i, best_obj, 4);
        w->flags8e |= id == 47 ? 4 : id == 55 ? 32 : id == 40 ? 8 : id == 0 ? 16 : 2;
    }
}

}  // namespace nm

// The match's loose objects (weapons, ladders, the debris of spawned moves): the seven 180-byte
// records at 0x8016AE10 (Object) and, in the main segment, each one's model record (140 bytes at
// 0x800A9C70) that carries a held object's place in its holder's hand.
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_OBJECTS = 0x8016AE10,      // Object[7]
    A_OBJ_MODELS = 0x800A9C70,   // 140 bytes per object (below)
    A_OBJ_NODES = 0x80084630,    // the objects' render nodes, 220 bytes each
    A_OBJ_PROBES = 0x800528F0,   // u8 per type: the object's collision probe count
    A_OBJ_DEBRIS = 0x80088268,   // s8 per object: which of the 8 debris models (type 32)
    A_DEBRIS_KEEP = 0x800A98C0,  // s8: nonzero keeps the debris model already picked
    A_MODELS = 0x800A1130,       // the wrestlers' 2852-byte model blocks
};

// The model record (A_OBJ_MODELS + 140 * obj): +0 u8 cleared on a new type; +1 u8 the probe count;
// +2 s8 the holder it was last posed for (-1 loose); +3 s8 the type it was last posed as (-1 none);
// +4 Vec3 the place relative to the holder's hand (x stored negated against the object's own pos);
// +0x10 / +0x14 / +0x18 / +0x1C s32 the angles (x, y, z, yaw) in 4096ths of a turn; +0x20 Vec3 the
// holder's hand point, which a release copies to +4.
inline u32 orec(s32 obj) { return A_OBJ_MODELS + 140u * (u32)obj; }

// The render side of a new type (node_bind_model_ex / sub_8002360C, main segment): which model the
// object's render node draws. It is left to the host renderer; only the debris pick, which draws from
// rng_main, is kept.
void object_bind_model(s32 obj, s32 type) {
    if (type != 32)
        return;
    if (mem_s8(A_DEBRIS_KEEP) == 0) {
        s32 r = (s32)rng_main();
        mem_w8(A_OBJ_DEBRIS + (u32)obj, (u8)(r % 8));
    }
}

}  // namespace

// N64 0x80024F84 (main segment): poses object `obj` for holder `holder` (-1 loose) as type `type`
// (-1 or 33+ none). A new type takes the probe count by type (0x800528F0) and binds the model (left
// to the renderer; type 32 picks one of 8 debris models with rng_main unless 0x800A98C0 is set). A new
// holder clears the angles; a release (holder -1) takes the angles from the old holder's model (yaw
// and joint 17's rotation, scaled to 4096ths) and the place from the hand point, and hands both back
// in `io` (x negated). Loose and posed before, the record takes the place and angles from `io`. With
// a holder and a new type or holder, the hand offset by type: below 11 and 26-28 / 30 (-20, -5, 30);
// 11-24 (10, -5, 40) but 22 (5, -5, 45); 29 (-20, -5, -10); 25 and 31+ none.
void object_model_update(s32 obj, s32 holder, s32 type, ObjectPose *io) {
    u32 r = orec(obj);
    s16 t = (s16)type, h = (s16)holder;
    bool changed = false;
    if (t != mem_s8(r + 3)) {
        if ((u16)t < 33) {
            mem_w8(r + 1, mem_u8(A_OBJ_PROBES + (u32)(s32)t));
            object_bind_model(obj, t);
        } else {
            t = -1;
            mem_w8(r + 1, 0);
        }
        changed = true;
        mem_w8(r + 0, 0);
        mem_w8(r + 3, (u8)t);
    }
    s8 prev = mem_s8(r + 2);
    if (h != prev) {
        if (h >= 0) {
            // The render node hangs under the holder's hand node (joint 17 of its tree).
            mem_w32(A_OBJ_NODES + 220u * (u32)obj, 0x80092B5C + 6160u * (u32)(s32)h);
            mem_w32(r + 0x1C, 0);
            mem_w32(r + 0x10, 0);
            mem_w32(r + 0x14, 0);
            mem_w32(r + 0x18, 0);
            changed = true;
        } else {
            mem_w32(A_OBJ_NODES + 220u * (u32)obj, 0);
            u32 m = A_MODELS + 2852u * (u32)(s32)prev;
            const f64 k = 11.377777777777778;
            mem_w32(r + 0x1C, (u32)(s32)((f64)mem_f32(m + 0x78) * k));
            mem_w32(r + 0x10, (u32)(s32)((f64)mem_f32(m + 0x83C) * k));
            mem_w32(r + 0x14, (u32)(s32)((f64)mem_f32(m + 0x840) * k));
            mem_w32(r + 0x18, (u32)(s32)((f64)mem_f32(m + 0x844) * k));
            mem_w32(r + 0x04, mem_u32(r + 0x20));
            mem_w32(r + 0x0C, mem_u32(r + 0x28));
            mem_w32(r + 0x08, mem_u32(r + 0x24));
            if (io) {
                io->pos.x = -mem_f32(r + 0x04);
                io->pos.y = mem_f32(r + 0x08);
                io->pos.z = mem_f32(r + 0x0C);
                io->angle[0] = mem_s32(r + 0x10);
                io->angle[1] = mem_s32(r + 0x14);
                io->angle[2] = mem_s32(r + 0x18);
                io->angle[3] = mem_s32(r + 0x1C);
            }
        }
        mem_w8(r + 2, (u8)h);
    } else if (prev == -1 && mem_s8(r + 3) != -1 && io) {
        mem_wf32(r + 0x04, -io->pos.x);
        mem_wf32(r + 0x08, io->pos.y);
        mem_wf32(r + 0x0C, io->pos.z);
        mem_w32(r + 0x10, (u32)io->angle[0]);
        mem_w32(r + 0x14, (u32)io->angle[1]);
        mem_w32(r + 0x18, (u32)io->angle[2]);
        mem_w32(r + 0x1C, (u32)io->angle[3]);
    }
    if (h < 0 || !changed)
        return;
    s32 ty = mem_s8(r + 3);
    f32 x, y, z;
    if (ty < 11 || (ty >= 26 && ty < 31 && ty != 29)) {
        x = -20.0f; y = -5.0f; z = 30.0f;
    } else if (ty == 22) {
        x = 5.0f; y = -5.0f; z = 45.0f;
    } else if (ty == 29) {
        x = -20.0f; y = -5.0f; z = -10.0f;
    } else if (ty < 26 && ty != 25) {
        x = 10.0f; y = -5.0f; z = 40.0f;
    } else {
        x = y = z = 0.0f;
    }
    mem_wf32(r + 0x04, x);
    mem_wf32(r + 0x08, y);
    mem_wf32(r + 0x0C, z);
}

// N64 0x80140000: object_model_update for the object as it stands (its holder and type2), with its
// position and the four angles (+0x50-0x5C) read and written back.
void object_pose_in_place(s32 obj) {
    u32 o = A_OBJECTS + 180u * (u32)obj;
    ObjectPose p;
    p.pos.x = mem_f32(o + 0x44);
    p.pos.y = mem_f32(o + 0x48);
    p.pos.z = mem_f32(o + 0x4C);
    for (s32 k = 0; k < 4; k++)
        p.angle[k] = mem_s32(o + 0x50 + 4u * (u32)k);
    object_model_update(obj, mem_s16(o + 0xA), mem_s16(o + 0x6), &p);
    mem_wf32(o + 0x44, p.pos.x);
    mem_wf32(o + 0x48, p.pos.y);
    mem_wf32(o + 0x4C, p.pos.z);
    for (s32 k = 0; k < 4; k++)
        mem_w32(o + 0x50 + 4u * (u32)k, (u32)p.angle[k]);
}

// N64 0x801400C4: empties an object record (zeroes, with type, type2, holder, owner, last_hit and the
// two ladder climbers -1).
void object_clear(s32 obj) {
    u32 o = A_OBJECTS + 180u * (u32)obj;
    for (u32 k = 0; k < 180; k += 4)
        mem_w32(o + k, 0);
    for (u32 k = 0; k < 28; k += 4)
        mem_w32(o + k, (u32)-1);
}

// N64 0x80141880: lets go of the held object (flags_ef 0x01). how 4 removes it (object_clear).
// Otherwise it comes loose (holder and last_hit -1) at point_120 (in the ring kept to +-200 when at
// 225 or more out), is posed in place and starts flying (flags 0x08, velocity +0x60 and its change
// +0x7C cleared, +0x1C 0): how 0 dropped (0x10: up 16-22, fall 1.75 a frame, a spin of -5..4
// x 11.38); 1 tossed aside (0x20: 3.0 along facing + 910, up -5.25, fall 1.75); 2 set down (0x440:
// down 10; flat (-1024, 0) with flags 0x100, else the x and z angles turned a quarter turn toward 0 or
// a half turn); 3 thrown (0x80: 30 along facing, up 5, fall 1.75, a spin of -10..9 x 11.38; from the
// floor at an opponent in the ring 65% as far and up 17-21), then a drag of 1/75 of the velocity.
void weapon_release(Wrestler *w, s32 how) {
    if (!(w->flags_ef & 1))
        return;
    if (how == 4) {
        w->flags_ef = 0;
        object_clear(w->held_obj);
        return;
    }
    w->flags_ef = 0;
    u32 o = A_OBJECTS + 180u * (u32)w->held_obj;
    mem_w32(o + 0x08, (u32)-1);
    mem_w32(o + 0x10, (u32)-1);
    mem_wf32(o + 0xA8, w->point_120.x);
    mem_wf32(o + 0xAC, w->point_120.y);
    mem_wf32(o + 0xB0, w->point_120.z);
    if (w->location & 1) {
        if (225.0 <= (f64)mem_f32(o + 0xA8))
            mem_wf32(o + 0xA8, 200.0f);
        if ((f64)mem_f32(o + 0xA8) <= -225.0)
            mem_wf32(o + 0xA8, -200.0f);
        if (225.0 <= (f64)mem_f32(o + 0xB0))
            mem_wf32(o + 0xB0, 200.0f);
        if ((f64)mem_f32(o + 0xB0) <= -225.0)
            mem_wf32(o + 0xB0, -200.0f);
    }
    object_pose_in_place(w->held_obj);
    mem_w32(o + 0x1C, 0);
    mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 8));
    for (u32 k = 0; k < 28; k += 4) {
        mem_w32(o + 0x60 + k, 0);
        mem_w32(o + 0x7C + k, 0);
    }
    switch (how) {
    case 0: {
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x10));
        s32 r = rand_pool_next();
        mem_wf32(o + 0x80, -1.75f);
        mem_wf32(o + 0x64, (f32)((f64)(r % 7) + 16.0));
        r = rand_pool_next();
        mem_w32(o + 0x78, (u32)(s32)((f64)(r % 10 - 5) * 11.377777777777778));
        return;
    }
    case 1: {
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x20));
        Vec3 v;
        vec_from_angle(angle_add(w->facing, 910), 3.0f, &v);
        mem_wf32(o + 0x60, v.x);
        mem_wf32(o + 0x64, v.y);
        mem_wf32(o + 0x68, v.z);
        mem_wf32(o + 0x64, -5.25f);
        mem_wf32(o + 0x80, -1.75f);
        return;
    }
    case 2: {
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x440));
        mem_wf32(o + 0x64, -10.0f);
        if (mem_u16(o + 0x20) & 0x100) {
            mem_w32(o + 0x50, (u32)-1024);
            mem_w32(o + 0x54, 0);
            return;
        }
        s32 a = mem_s32(o + 0x50);
        angle_approach(&a, abs_i(a) >= 1024 ? 2048 : 0, 1024);
        mem_w32(o + 0x50, (u32)a);
        a = mem_s32(o + 0x58);
        angle_approach(&a, abs_i(a) >= 1024 ? 2048 : 0, 1024);
        mem_w32(o + 0x58, (u32)a);
        return;
    }
    case 3: {
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x80));
        Vec3 v;
        vec_from_angle(w->facing, 30.0f, &v);
        mem_wf32(o + 0x60, v.x);
        mem_wf32(o + 0x64, v.y);
        mem_wf32(o + 0x68, v.z);
        mem_wf32(o + 0x64, 5.0f);
        mem_wf32(o + 0x80, -1.75f);
        s32 r = rand_pool_next();
        mem_w32(o + 0x78, (u32)(s32)((f64)(r % 20 - 10) * 11.377777777777778));
        if ((w->location & 4) && (g_wrestlers[w->opponent].location & 1)) {
            r = rand_pool_next();
            f32 vx = (f32)((f64)mem_f32(o + 0x60) * 0.65);
            f32 vz = (f32)((f64)mem_f32(o + 0x68) * 0.65);
            f32 vy = (f32)((f64)(r % 5) + 17.0);
            mem_wf32(o + 0x60, vx);
            mem_wf32(o + 0x68, vz);
            mem_wf32(o + 0x64, vy);
        }
        mem_wf32(o + 0x7C, (f32)-((f64)mem_f32(o + 0x60) * 0.013333333333333334));
        mem_wf32(o + 0x84, (f32)-((f64)mem_f32(o + 0x68) * 0.013333333333333334));
        return;
    }
    default:
        return;
    }
}

// N64 0x8014391C: state 0x8F, throwing the held weapon: kept in bounds (4.0); step 0 the throw
// 12515 + the weapon's family; step 1 lets go at frame 14 (weapon_release 3), with the events.
void state_8F_weapon_throw(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, wrestler_move_anim(w, 0) + 12515, 3);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frame == 14)
            weapon_release(w, 3);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}



// N64 0x80142730: where a held or thrown object strikes from: of its probe points (the model
// record's +0x2C, x negated) the nearest to and farthest from w's point_120 (in xz) give the middle
// (x, z) and the lowest probe the y. Returns half the distance between those two (the reach).
f32 weapon_tip_point(const Wrestler *w, s32 obj, Vec3 *out) {
    u32 r = orec(obj);
    f32 near_d = 1000.0f, far_d = 0.0f, low = 1000.0f;
    Vec3 near_p = {0.0f, 0.0f, 0.0f}, far_p = {0.0f, 0.0f, 0.0f};
    for (s32 k = 0; k < mem_s8(r + 1); k++) {
        u32 q = r + 0x2C + 12u * (u32)k;
        Vec3 p = {-mem_f32(q), mem_f32(q + 4), mem_f32(q + 8)};
        f32 d = dist_to(&w->point_120, &p);
        if (d < near_d) {
            near_d = d;
            near_p = p;
        }
        if (far_d < d) {
            far_d = d;
            far_p = p;
        }
        if (p.y < low)
            low = p.y;
    }
    out->x = (f32)((f64)(near_p.x + far_p.x) * 0.5);
    out->y = low;
    out->z = (f32)((f64)(near_p.z + far_p.z) * 0.5);
    return (f32)((f64)dist_to(&near_p, &far_p) * 0.5);
}

// N64 0x80141508: a thrown object's hit (strike_hit_test kind 24 from its owner), once per target
// (last_hit). A wrestler takes the object's move (hit_by the owner, move 5 x type2 + 111 from the
// generic moves at 0x800A6040, its reaction by the height above the target's feet: over 140 none,
// over 70 2, else 3) and the strike (strike_land 0x100000). One catching it (state 0x90) holds it
// (1 returned: holder and owner, flags 0, flags_ef by type from 0x8014DF80) unless the reaction is
// 3 (it drops: flags 0x80 off, 0x20 on); others make the hit sound. Another object is knocked along
// (object_set_motion 9) and the thrown one drops (0x10: a tenth of its speed, drag 0.2 of it, up 10-16
// with gravity 1.75, from where it is).
s32 object_hit_check(s32 obj) {
    u32 o = A_OBJECTS + 180u * (u32)obj;
    Wrestler *owner = &g_wrestlers[mem_s32(o + 0xC)];
    s32 t = strike_hit_test(owner, obj, 24);
    if (t == -1)
        return 0;
    if (t == mem_s32(o + 0x10))
        return 0;
    mem_w32(o + 0x10, (u32)t);
    if (t >= 100) {
        object_sfx(0, obj, 1);
        u32 op = A_OBJECTS + 180u * (u32)(t - 100) + 0x44;
        Vec3 from = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
        Vec3 to = {mem_f32(op), mem_f32(op + 4), mem_f32(op + 8)}, d;
        vec_from_angle(angle_to(&from, &to), 1.0f, &d);
        object_set_motion(t - 100, 9, &d);
        mem_w16(o + 0x20, (u16)((mem_u16(o + 0x20) & 0xFF7F) | 0x10));
        mem_wf32(o + 0xA8, from.x);
        mem_wf32(o + 0xAC, from.y);
        mem_wf32(o + 0xB0, from.z);
        f32 vx = (f32)((f64)mem_f32(o + 0x60) * 0.1), vz = (f32)((f64)mem_f32(o + 0x68) * 0.1);
        mem_wf32(o + 0x60, vx);
        mem_wf32(o + 0x68, vz);
        mem_wf32(o + 0x7C, (f32)(-((f64)vx * 0.2)));
        mem_wf32(o + 0x84, (f32)(-((f64)vz * 0.2)));
        mem_wf32(o + 0x64, (f32)((f64)(rand_pool_next() % 7) + 10.0));
        mem_wf32(o + 0x80, -1.75f);
        return 0;
    }
    Wrestler *d = &g_wrestlers[t];
    d->hit_move_repeat = 0;
    d->hit_by = (u8)mem_s32(o + 0xC);
    d->hit_move_num = (s16)(5 * mem_s32(o + 4) + 111);
    d->hit_move_anim = 0;
    d->hit_move_anim2 = 0;
    d->hit_move = move_record_at(0x800A6040 + 36u * (u32)(s32)d->hit_move_num);
    f64 dy = (f64)(mem_f32(o + 0x48) - d->pos.y);
    d->hit_move.reaction = (u8)(140.0 < dy ? 0 : (70.0 < dy ? 2 : 3));
    strike_land(owner, t, 0x100000);
    if (d->state != state::HitHoldingBigWeapon) {
        object_sfx(t, obj, 1);
        return 0;
    }
    if ((s8)d->hit_move.reaction == 3) {
        mem_w16(o + 0x20, (u16)((mem_u16(o + 0x20) & 0xFF7F) | 0x20));
        return 0;
    }
    mem_w32(o + 0x08, (u32)t);
    mem_w32(o + 0x0C, (u32)t);
    mem_w16(o + 0x20, 0);
    d->held_obj = obj;
    d->flags_ef |= mem_u8(0x8014DF80 + (u32)mem_s32(o + 4));
    return 1;
}

// N64 0x80140FD4: one frame of an object's motion against the arena: its probe points (the model
// record's +0x2C, x negated, the count at +1) and its position, each moved by the velocity (+0x60),
// are pushed out of the walls of the floor area under it (arena_area_lookup from prev_pos when tossed
// or dropped, flags 0x30, else from pos): for each point up to three passes, each taking the first
// wall in the area's groups that the segment from there to the point crosses (segment_hit kind 1,
// 3 for a ladder, whose areas map 0x80147BB4-0x80147BE4 to 0x80147BF4-0x80147C24), and the push
// carries to the points after it. A wall with +0x26 0x01 is ground. *out gets the position's
// move. Lying (0x04, not a ladder) with no ground under it, it falls (flags 0x08). Flying onto the
// ground: the first touch (0x400 not yet set) slows it to a third, bounces it up |vy| / 5 (10 for a
// ladder) with gravity 1.75 and a landing sound; a second touch sets 0x800 and drops it (vy -10),
// with the sound unless 0x800 was set or both 0x100 and 0x40 are.
void object_fly(s32 obj, Vec3 *out) {
    u32 o = A_OBJECTS + 180u * (u32)obj;
    u32 r = orec(obj);
    s32 n = mem_s8(r + 1);
    Vec3 pts[16];
    Vec3 vel = {mem_f32(o + 0x60), mem_f32(o + 0x64), mem_f32(o + 0x68)};
    for (s32 k = 0; k < n; k++) {
        u32 q = r + 0x2C + 12u * (u32)k;
        Vec3 p = {-mem_f32(q), mem_f32(q + 4), mem_f32(q + 8)};
        vec_add(&p, &vel, &pts[k]);
    }
    Vec3 pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
    vec_add(&pos, &vel, &pts[n]);
    Vec3 base;
    if (mem_u16(o + 0x20) & 0x30)
        base = {mem_f32(o + 0xA8), mem_f32(o + 0xAC), mem_f32(o + 0xB0)};
    else
        base = pos;
    u32 area;
    arena_area_lookup(base.x, base.z, &area);
    s32 ground = 0;
    s32 kind = 1;
    if (mem_s32(o + 4) == 25) {
        kind = 3;
        if (area == 0x80147BB4) area = 0x80147BF4;
        if (area == 0x80147BC4) area = 0x80147C04;
        if (area == 0x80147BD4) area = 0x80147C14;
        if (area == 0x80147BE4) area = 0x80147C24;
    }
    if (area) {
        for (s32 k = 0; k <= n; k++) {
            Vec3 cur = pts[k];
            u32 hits[3];
            for (s32 pass = 0; pass < 3; pass++) {
                hits[pass] = 0;
                for (u32 g = area; mem_u32(g) && !hits[pass]; g += 4) {
                    u32 group = mem_u32(g);
                    for (u32 wall = mem_u32(group);; wall = mem_u32(wall + 0x2C)) {
                        Vec3 push;
                        if (segment_hit(&base, &cur, wall, &push, nullptr, kind)) {
                            hits[pass] = wall;
                            vec_add(&cur, &push, &cur);
                            break;
                        }
                        if (wall == mem_u32(group + 4))
                            break;
                    }
                }
                if (!hits[pass])
                    break;
                ground += mem_u16(hits[pass] + 0x26) & 1;
            }
            if (hits[0]) {
                Vec3 d;
                vec_sub(&pts[k], &cur, &d);
                for (s32 i = k; i <= n; i++)
                    vec_add(&pts[i], &d, &pts[i]);
            }
        }
    }
    vec_sub(&pos, &pts[n], out);
    u16 fl = mem_u16(o + 0x20);
    if (mem_s32(o + 4) != 25 && (fl & 4)) {
        if (!ground)
            mem_w16(o + 0x20, 8);
        return;
    }
    if (!ground)
        return;
    if (fl & 0x400) {
        if (!(fl & 0x800) && (fl & 0x140) != 0x140)
            object_sfx(mem_s32(o + 0xC), obj, 2);
        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x800));
        mem_wf32(o + 0x64, -10.0f);
        return;
    }
    mem_wf32(o + 0x60, (f32)((f64)mem_f32(o + 0x60) * 0.3333333333333333));
    mem_wf32(o + 0x68, (f32)((f64)mem_f32(o + 0x68) * 0.3333333333333333));
    mem_w16(o + 0x20, (u16)(fl | 0x400));
    f32 div = mem_s32(o + 4) == 25 ? 10.0f : 5.0f;
    mem_wf32(o + 0x64, absf(mem_f32(o + 0x64) / div));
    mem_wf32(o + 0x80, -1.75f);
    object_sfx(mem_s32(o + 0xC), obj, 2);
}

// N64 0x80140A84: every object's frame. Lying (flags 0x04): no sideways velocity or drag, and
// object_fly (to fall off an edge), but not a ladder. Flying (0x08): a throw (0x80, not yet landed)
// first tests its hits (object_hit_check; a hit ends its frame); then object_fly, the move, the
// velocity's change, the spin (+0x5C by +0x78, which changes by +0x94); a velocity not above its
// change, or under 1.5, stops sideways. Landed (0x400) and down again (0x800): a set-down
// (0x100) lies at 216 above 153 or else 126 (flags 0x2104); spinning down (0x200) slows the
// sideways velocity and spin by 1 / (5 - n) for 5 frames, then lies (0x04, falling); else the x / z
// angles (+0x50 / +0x58) settle 113 a frame toward flat (0 or a half turn), the larger first unless
// it is flat, then 0x200 once both are within 11. A landed object frees the ladder sides (a ladder
// flat on the ground also moves the camera, ladder_camera). Below -20 it is gone (object_clear).
// Carried (0x03): moved along +0x5C by +0x98 (up +0xA0), both growing by +0x9C / +0xA4 to at
// least 1. Then the ladder rules.
void objects_update() {
    for (s32 j = 0; j < 7; j++) {
        u32 o = A_OBJECTS + 180u * (u32)j;
        u16 fl = mem_u16(o + 0x20);
        if (fl & 4) {
            mem_w32(o + 0x60, 0);
            mem_w32(o + 0x68, 0);
            mem_w32(o + 0x7C, 0);
            mem_w32(o + 0x84, 0);
            if (mem_s32(o + 4) != 25) {
                Vec3 d;
                object_fly(j, &d);
            }
            ladder_object_update(j);
            continue;
        }
        if (fl & 8) {
            if ((fl & 0x80) && !(fl & 0x400) && object_hit_check(j))
                continue;
            Vec3 d;
            object_fly(j, &d);
            Vec3 pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
            vec_add(&pos, &d, &pos);
            mem_wf32(o + 0x44, pos.x);
            mem_wf32(o + 0x48, pos.y);
            mem_wf32(o + 0x4C, pos.z);
            Vec3 v = {mem_f32(o + 0x60), mem_f32(o + 0x64), mem_f32(o + 0x68)};
            Vec3 a = {mem_f32(o + 0x7C), mem_f32(o + 0x80), mem_f32(o + 0x84)};
            vec_add(&v, &a, &v);
            mem_wf32(o + 0x60, v.x);
            mem_wf32(o + 0x64, v.y);
            mem_wf32(o + 0x68, v.z);
            mem_w32(o + 0x5C, (u32)angle_add(mem_s32(o + 0x5C), mem_s32(o + 0x78)));
            mem_w32(o + 0x78, (u32)(mem_s32(o + 0x78) + mem_s32(o + 0x94)));
            f32 lv = vec2_len(mem_f32(o + 0x60), mem_f32(o + 0x68));
            f32 la = vec2_len(mem_f32(o + 0x7C), mem_f32(o + 0x84));
            if (lv <= la || (f64)lv < 1.5) {
                mem_w32(o + 0x60, 0);
                mem_w32(o + 0x68, 0);
                mem_w32(o + 0x7C, 0);
                mem_w32(o + 0x84, 0);
            }
            fl = mem_u16(o + 0x20);
            if (fl & 0x400) {
                if (!(fl & 0x800)) {
                    // the first bounce: nothing to settle yet
                } else if (fl & 0x100) {
                    mem_w16(o + 0x20, 0x2104);
                    mem_w32(o + 0x1C, 0);
                    mem_wf32(o + 0x48, 153.0 <= (f64)mem_f32(o + 0x48) ? 216.0f : 126.0f);
                } else if (fl & 0x200) {
                    s32 t = mem_s32(o + 0x1C);
                    if (t < 5) {
                        f32 k = (f32)(5 - t);
                        f32 vx = mem_f32(o + 0x60), vz = mem_f32(o + 0x68);
                        f32 spin = (f32)mem_s32(o + 0x78);
                        spin = spin - spin / k;
                        mem_w32(o + 0x78, (u32)(s32)spin);
                        mem_wf32(o + 0x60, vx - vx / k);
                        mem_wf32(o + 0x68, vz - vz / k);
                    } else {
                        mem_w16(o + 0x20, 4);
                        mem_w32(o + 0x60, 0);
                        mem_w32(o + 0x68, 0);
                        mem_w32(o + 0x7C, 0);
                        mem_w32(o + 0x84, 0);
                        mem_wf32(o + 0x64, -5.25f);
                        mem_wf32(o + 0x80, -1.75f);
                    }
                    mem_w32(o + 0x1C, (u32)(mem_s32(o + 0x1C) + 1));
                } else {
                    s32 ax = mem_s32(o + 0x50), az = mem_s32(o + 0x58);
                    s32 dx = abs_i(angle_diff(0, ax)), dz = abs_i(angle_diff(0, az));
                    bool use_x = dz < dx ? !(dz != 0 && dz != 0x800) : (dx != 0 && dx != 0x800);
                    if (use_x) {
                        angle_approach(&ax, dx < 1024 ? 0 : 2048, 113);
                        mem_w32(o + 0x50, (u32)ax);
                    } else {
                        angle_approach(&az, dz < 1024 ? 0 : 2048, 113);
                        mem_w32(o + 0x58, (u32)az);
                    }
                    ax = mem_s32(o + 0x50);
                    az = mem_s32(o + 0x58);
                    bool flat_x = abs_i(angle_diff(ax, 0)) < 11 || abs_i(angle_diff(ax, 2048)) < 11;
                    bool flat_z = abs_i(angle_diff(az, 0)) < 11 || abs_i(angle_diff(az, 2048)) < 11;
                    if (flat_x && flat_z)
                        mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) | 0x200));
                }
                mem_w32(o + 0x14, (u32)-1);
                mem_w32(o + 0x18, (u32)-1);
                if (mem_s32(o + 4) == 25 && !(mem_u16(o + 0x20) & 0x100)) {
                    ladder_camera(1, 1);
                    mem_w16(o + 0x20, (u16)(mem_u16(o + 0x20) & 0x7FFF));
                }
            }
            if ((f64)mem_f32(o + 0x48) < -20.0)
                object_clear(j);
        }
        if (mem_u16(o + 0x20) & 3) {
            Vec3 d, pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
            vec_from_angle(mem_s32(o + 0x5C), mem_f32(o + 0x98), &d);
            d.y = mem_f32(o + 0xA0);
            vec_add(&pos, &d, &pos);
            mem_wf32(o + 0x44, pos.x);
            mem_wf32(o + 0x48, pos.y);
            mem_wf32(o + 0x4C, pos.z);
            f32 v = mem_f32(o + 0x98) + mem_f32(o + 0x9C);
            mem_wf32(o + 0x98, (f64)v <= 1.0 ? 1.0f : v);
            v = mem_f32(o + 0xA0) + mem_f32(o + 0xA4);
            mem_wf32(o + 0xA0, (f64)v <= 1.0 ? 1.0f : v);
        }
        ladder_object_update(j);
    }
}

// N64 0x801409B8: the first free object slot of 0-5 (type -1), or -1. Slots 4 and 5 hold the
// ringside tables while they stand (g_tables_intact 0x01 / 0x02; in arena 24 0x20 / 0x40).
s32 object_free_slot() {
    s32 arena = mem_s32(A_ARENA);
    u8 t = mem_u8(0x8016B928);
    for (s32 j = 0; j < 6; j++) {
        if (mem_s32(A_OBJECTS + 180u * (u32)j) != -1)
            continue;
        if (arena < 10) {
            if ((j == 4 && (t & 1)) || (j == 5 && (t & 2)))
                continue;
        } else if (arena == 24) {
            if ((j == 4 && (t & 0x20)) || (j == 5 && (t & 0x40)))
                continue;
        }
        return j;
    }
    return -1;
}

// N64 0x80025C48 (main segment): whether weapon type `type` is allowed: one of the 12 types the
// options can switch off (0x80052D18) whose bit in 0x8009FDEA is set is not.
s32 weapon_type_allowed(s32 type) {
    u16 off = mem_u16(0x8009FDEA);
    for (s32 i = 0; i < 12; i++)
        if (type == mem_u8(0x80052D18 + (u32)i))
            return (off & (1u << i)) ? 0 : 1;
    return 1;
}

// N64 0x80140694: the object a grab gives the wrestler (states 0x8A, 0x8C, 0x91, 0xBD). how 0x08
// hands it the object it already has (holder and owner its slot, flags 0, ladder sides free). Else
// the type: with 0x04 the place's type (0x8014E05B by attrs +8; none, 0 returned, without one); a
// special (attrs +9, less 1) on an 80% roll; otherwise a random one of the crowd types
// (g_crowd_weapon_types, 0x8014E064, -1 ended) that are allowed (weapon_type_allowed; 14 and 22 not
// in type 0x100000). It goes into a free slot (object_free_slot; 0 returned without one) at joint 16,
// held by the wrestler (held_obj), type2 too with 0x02. flags_ef takes the type's bits (0x8014DF80).
s32 weapon_pick_type(Wrestler *w, s32 how) {
    if (how & 8) {
        u32 o = A_OBJECTS + 180u * (u32)w->held_obj;
        mem_w32(o + 0x08, (u32)(s32)w->slot);
        mem_w32(o + 0x0C, (u32)(s32)w->slot);
        mem_w16(o + 0x20, 0);
        mem_w32(o + 0x14, (u32)-1);
        mem_w32(o + 0x18, (u32)-1);
        w->flags_ef |= mem_u8(0x8014DF80 + (u32)mem_s32(o + 4));
        return 1;
    }
    s32 type;
    if (how & 4) {
        s8 k = mem_s8(w->attrs + 8);
        if (k == 0)
            return 0;
        type = mem_s8(0x8014E05B + (u32)(s32)k);
    } else if (mem_s8(w->attrs + 9) != 0 && chance_check(800)) {
        type = mem_s8(w->attrs + 9) - 1;
    } else {
        s8 pick[32];
        s32 n = 0;
        for (s32 i = 0;; i++) {
            s32 t = mem_s8(0x8014E064 + (u32)i);
            if (t == -1)
                break;
            if (!weapon_type_allowed(t))
                continue;
            if ((t == 14 || t == 22) && (g_match_type & 0x100000))
                continue;
            pick[n++] = (s8)t;
        }
        s32 r = rand_pool_next();
        type = pick[r % n];
    }
    s32 slot = object_free_slot();
    if (slot == -1)
        return 0;
    w->held_obj = slot;
    u32 o = A_OBJECTS + 180u * (u32)slot;
    mem_w32(o + 0x00, (u32)type);
    mem_w32(o + 0x08, (u32)(s32)w->slot);
    mem_w32(o + 0x0C, (u32)(s32)w->slot);
    mem_wf32(o + 0x44, w->joints[15].x);
    mem_wf32(o + 0x48, w->joints[15].y);
    mem_wf32(o + 0x4C, w->joints[15].z);
    w->flags_ef |= mem_u8(0x8014DF80 + (u32)type);
    if (how & 2)
        mem_w32(o + 0x04, (u32)type);
    return 1;
}

// N64 0x8014310C: state 0x8A, taking a weapon from the crowd (or a table / the steps). Step 0: a
// ladder (family 3) is put down first; the stand set's 1 and the slide to 55 from the wall over 8
// frames (pair_scripted_place); step 1 (flags80 0x01) the slide, then step 2. Step 2: at the
// steps or a table (wall flags +0x26 0x08, ids 174-187, 367-379 or 147) the lift 12374
// (state_counter 0x01), else the reach into the crowd 13062; two frames from its end any held
// object is dropped for good; at the end, with a crowd type (attrs +6) and the chance (kind 9), the
// weapon (weapon_pick_type 1), then step 3. Step 3: with the weapon (its type2 set from its type)
// 12376 / 13063 + its family, without 12375 / 13066; the events.
void state_8A_crowd_weapon_grab(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        if ((w->flags_ef & 1) && wrestler_move_anim(w, 0) == 3)
            weapon_release(w, 0);
        wrestler_set_anim(w, anim_pick(ctx, 1, 0), 2);
        pair_scripted_place(w, nullptr);
        w->state_step++;
        [[fallthrough]];
    case 1:
        w->flags80 |= 1;
        if (pair_slide_step(w, nullptr))
            wrestler_set_step(w, 2);
        return;
    case 2: {
        u32 z = w->grab_zone_info;
        u16 id = mem_u16(z + 36);
        s32 anim;
        if ((mem_u16(z + 38) & 8) || (u32)(id - 174) < 14 || (u32)(id - 367) < 13 || (s16)id == 147) {
            anim = 12374;
            w->state_counter |= 1;
        } else {
            anim = 13062;
        }
        wrestler_set_anim(w, anim, 2);
        if (w->anim_frames_left == 2)
            weapon_release(w, 4);
        if (w->anim_frames_left > 0)
            return;
        if (mem_s8(w->attrs + 6) != 0 && chance_roll(w, nullptr, 9))
            weapon_pick_type(w, 1);
        wrestler_set_step(w, 3);
        return;
    }
    case 3: {
        s32 anim;
        if (w->flags_ef & 1) {
            u32 o = A_OBJECTS + 180u * (u32)w->held_obj;
            mem_w32(o + 4, mem_u32(o));
            anim = ((w->state_counter & 1) ? 12376 : 13063) + wrestler_move_anim(w, 0);
        } else {
            anim = (w->state_counter & 1) ? 12375 : 13066;
        }
        wrestler_set_anim(w, anim, 1);
        move_event(ctx, 0);
        return;
    }
    default:
        return;
    }
}



}  // namespace nm

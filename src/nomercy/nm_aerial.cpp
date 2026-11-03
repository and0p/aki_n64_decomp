// Dives and aerial moves: off the apron, the turnbuckle, a raised place or the ladder. The state sets
// the move up; dive_aim picks where it flies (the move's reach and an aim clamped by the move), and
// aerial_step carries the wrestler there over the move's flight frames; special_move_kind tests the
// landing, dive_counter / dive_land_hit settle what it hit, special_move_land a miss.
#include "nm_funcs.h"

namespace nm {

// N64 0x80139D80: a ladder dive at an opponent at a raised place (location 0x40) when the aim lies on
// its table (the vertical line through dive_target crosses the wall 0x80147E38): down onto it (height
// 90 above 23.7), location 0x40.
static s32 dive_aim_target(Wrestler *w, const Wrestler *o) {
    if (!(o->location & 0x40))
        return 0;
    Vec3 a = w->dive_target, b = w->dive_target;
    a.y = 9984.0f;
    b.y = -9984.0f;
    if (!segment_hit(&a, &b, 0x80147E38, nullptr, nullptr, 0))
        return 0;
    w->step_vec.y = (f32)((90.0 - ((f64)w->pos.y - 23.7)) / (f64)(f32)(s16)w->aux_timer);
    w->location = 64;
    return 1;
}

// A dive that lands on an object knocks it over along the line from the wrestler (object_set_motion 3).
static void knock_object(Wrestler *w, s32 obj) {
    u32 p = 0x8016AE10 + 180u * (u32)obj + 0x44;
    Vec3 pos = {mem_f32(p), mem_f32(p + 4), mem_f32(p + 8)}, d;
    vec_from_angle(angle_to(&w->point_120, &pos), 1.0f, &d);
    object_set_motion(obj, 3, &d);
}

// N64 0x80138F54: aims a dive at `o`. The flight lasts from the move record's +0x12 to +0x13 frame
// (aux_timer, at least 1), with 6 turn frames (slide_frames); flags_ea keeps 0x08 and gets 0x40 (0xC0
// from a ladder, location 0x08). By state the minimum reach and where it ends: 0x72 / 0x75 the floor
// (0x04, no turn 0x40), 0x73 / 0x78 100, 0x74 60, 0x76 the floor 100, 0x77 the ring 100, 0x79 / 0x7B
// / 0x7C into the ring (0x03; from a ladder 120 and the climb to 90 above 23.7 as step_vec.y, else 60),
// 0x7A / 0x7D onto the floor (0x06; from a ladder 120 and down to 23.7, else 60 and down 90), 0xC0 the
// floor 60. The aim is the line from point_120 to the opponent's joint by the record's aim byte,
// clamped by move: 272 within 512 of the corner past the side (0x8014D1F8, 480.83 out); 269 / 278
// within 227 of the side's angle (0x8014D1E8) and 270 of 0x8014D208, lockup_facing turned about; 273-275
// / 281 within 341 of the facing; 256 / 257 / 279 (and 261 / 262 with flags_ea 0x02) within 455 of
// 0x8014D208, 263 / 280 (264 with 0x02) within 1251 of 0x8014D1F8, unless from a ladder; lockup_facing
// takes the aim (turned about with record flag 0x08). The reach is the distance, at most twice the
// record's +0x02 scaled by the build (0x8014DCC0; flags_ea 0x01 when cut short) and at least the minimum;
// dive_target = point_120 + the reach along the aim. From a ladder the states 0x79-0x7D re-aim at a
// target off the ring (dive_aim_target) and pick the ring or the floor by where that lands.
void dive_aim(Wrestler *w, Wrestler *o) {
    s32 frames = (s32)w->anim_player[0x13] - (s32)w->anim_player[0x12] + 1;
    w->aux_timer = (u16)(frames > 0 ? frames : 1);
    w->slide_frames = 6;
    u8 ea = w->flags_ea & 8;
    ea |= (w->location & 8) ? 0xC0 : 0x40;
    w->flags_ea = ea;
    f32 least = 0.0f;
    switch (w->state) {
    case state::RopeTaunt: case state::ApronDiveMove271:
        w->location = (u16)((w->location & 0xF800) | 4);
        w->flags_ea &= 0xBF;
        break;
    case state::ApronMove: case state::RunningFlyingMove:
        least = 100.0f;
        break;
    case state::AerialAttackAtDownedOpponent:
        least = 60.0f;
        break;
    case state::ApronDiveMove272:
        least = 100.0f;
        w->location = (u16)((w->location & 0xFFFD) | 4);
        break;
    case state::ApronToRingMove:
        least = 100.0f;
        w->location = (u16)((w->location & 0xF800) | 1);
        break;
    case state::TurnbuckleDiveMoves256And257: case state::TurnbuckleDiveMoves261And262: case state::TurnbuckleDiveMove279:
        w->location = (u16)((w->location & 0xFFFB) | 3);
        if (w->flags_ea & 0x80) {
            least = 120.0f;
            w->step_vec.y = (f32)((90.0 - ((f64)w->pos.y - 23.7)) / (f64)(f32)(s16)w->aux_timer);
        } else {
            least = 60.0f;
            w->step_vec.y = 0.0f;
        }
        break;
    case state::TurnbuckleDiveMoves263And264: case state::TurnbuckleDiveMove280:
        w->location = (u16)((w->location & 0xFFFE) | 6);
        if (w->flags_ea & 0x80) {
            least = 120.0f;
            w->step_vec.y = (f32)((23.7 - (f64)w->pos.y) / (f64)(f32)(s16)w->aux_timer);
        } else {
            least = 60.0f;
            w->step_vec.y = (f32)(-90.0 / (f64)(f32)(s16)w->aux_timer);
        }
        break;
    case state::ApronDiveGenericMove75:
        least = 60.0f;
        w->location = 4;
        break;
    default:
        break;
    }
    f32 seg[9];
    segment_distance(&w->point_120, wrestler_joint(o, mem_s8(w->move + 0x10) - 1), seg);
    f32 aim = seg[7];
    s32 m = w->move_num;
    s32 dir = w->dir;
    // The clamp kinds: 1 the corner past the side, 2 the side's angle, 3 0x8014D208, 4 the facing,
    // 5 0x8014D208 wide, 6 0x8014D1F8 wide; 0 none.
    s32 kind = 0;
    switch (m) {
    case 272: kind = 1; break;
    case 269: case 278: kind = 2; break;
    case 270: kind = 3; break;
    case 273: case 274: case 275: case 281: kind = 4; break;
    case 261: case 262: w->flags_ea |= 2; kind = 5; break;
    case 256: case 257: case 279: kind = 5; break;
    case 264: w->flags_ea |= 2; kind = 6; break;
    case 263: case 280: kind = 6; break;
    case 75: kind = 7; break;
    default: break;
    }
    auto clamp_to = [&](s32 base, s32 lim) {
        s32 d = angle_diff(base, (s32)aim);
        if (abs_i(d) >= lim)
            aim = (f32)angle_add(base, d >= 0 ? -(lim - 1) : (lim - 1));
    };
    switch (kind) {
    case 1: {
        s32 side = (w->flags8a & 4) ? ((w->ring_zone - 1) & 3) : w->ring_zone;
        Vec3 c;
        vec_from_angle(mem_s32(0x8014D1F8 + 4u * (u32)side), 480.832611f, &c);
        s32 a = angle_to(&w->point_120, &c);
        s32 d = angle_diff(a, (s32)aim);
        if (w->flags8a & 4) {
            if (d > 0)
                aim = (f32)a;
            else if (d < -512)
                aim = (f32)angle_add(a, 512);
        } else {
            if (d < 0)
                aim = (f32)a;
            else if (d >= 513)
                aim = (f32)angle_add(a, -512);
        }
        w->lockup_facing = (s32)aim;
        break;
    }
    case 2:
        clamp_to(mem_s32(0x8014D1E8 + 4u * (u32)dir), 228);
        w->lockup_facing = angle_add((s32)aim, 2048);
        break;
    case 3:
        clamp_to(mem_s32(0x8014D208 + 4u * (u32)dir), 228);
        w->lockup_facing = angle_add((s32)aim, 2048);
        break;
    case 4:
        clamp_to(w->facing, 342);
        w->lockup_facing = (s32)aim;
        break;
    case 5: case 6:
        if (!(w->flags_ea & 0x80))
            clamp_to(mem_s32((kind == 5 ? 0x8014D208u : 0x8014D1F8u) + 4u * (u32)dir), kind == 5 ? 456 : 1252);
        w->lockup_facing = (s32)aim;
        if (mem_u32(w->move + 0x20) & 8)
            w->lockup_facing = angle_add((s32)aim, 2048);
        break;
    case 7:
        w->lockup_facing = (s32)aim;
        break;
    default:
        break;
    }
    f32 reach = (f32)((f64)mem_s16(w->move + 2) * 2.0 * (f64)mem_f32(0x8014DCC0 + 4u * (u32)(s32)mem_s8(w->attrs + 20)));
    f32 dist = seg[4];
    if (reach <= dist) {
        dist = reach;
        w->flags_ea |= 1;
    }
    if (dist <= least)
        dist = least;
    Vec3 unit, off;
    vec_from_angle((s32)aim, 1.0f, &unit);
    vec_scale(&unit, dist, &off);
    vec_add(&w->point_120, &off, &w->dive_target);
    if (!(w->flags_ea & 0x80) || (u32)(w->state - state::TurnbuckleDiveMoves256And257) >= 5)
        return;
    // From a ladder: down to the place the landing is over.
    if (dive_aim_target(w, o))
        return;
    auto to_floor = [&]() {
        w->step_vec.y = (f32)((23.7 - (f64)w->pos.y) / (f64)(f32)(s16)w->aux_timer);
        w->location = 6;
    };
    auto to_ring = [&]() {
        w->step_vec.y = (f32)((90.0 - ((f64)w->pos.y - 23.7)) / (f64)(f32)(s16)w->aux_timer);
        w->location = 1;
    };
    Vec3 back;
    Vec3 *t = &w->dive_target;
    if (w->state == state::TurnbuckleDiveMoves263And264 || w->state == state::TurnbuckleDiveMove280) {
        if ((f64)absf(t->x) < 230.0 && (f64)absf(t->z) < 230.0) {
            to_ring();
            return;
        }
        if (!((f64)absf(t->x) < 280.0) || !((f64)absf(t->z) < 280.0))
            return;
        vec_scale(&unit, -50.0f, &back);
        vec_add(t, &back, t);
        if ((f64)absf(w->pos.x) < 280.0 && (f64)absf(w->pos.z) < 280.0)
            to_ring();
        return;
    }
    if (280.0 < (f64)absf(t->x) || 280.0 < (f64)absf(t->z)) {
        to_floor();
        return;
    }
    if (!(230.0 < (f64)absf(t->x)) && !(230.0 < (f64)absf(t->z)))
        return;
    vec_scale(&unit, -50.0f, &back);
    vec_add(t, &back, t);
    if (280.0 < (f64)absf(w->pos.x) || 280.0 < (f64)absf(w->pos.z))
        to_floor();
}

// N64 0x801399B0: one frame of a dive's flight. With `aim` the aim is taken at the take-off frame
// (record +0x12). From take-off to landing (+0x12 to +0x13): flags8a 0x40, location off 0x18, the ladder
// side it climbed freed; with flags_ea 0x40 first the turn (slide_frames: about the pelvis toward
// lockup_facing), then the flight (aux_timer: toward dive_target, measured from the move's lead joint
// or the middle of joints 15 / 17 or 8 / 9, a frame's share of the distance; 0x79-0x7D also climb or
// drop by step_vec.y). flags_ea 0x10 stays only while the opponent lies (0x31, step 3) for a flags_ea
// 0x02 dive. From the record's +0x08 frame (0x7A) or +0x09 (others) on: off the apron (location 0x02)
// and the air (flags8a 0x40), and kept in bounds (4.0).
void aerial_step(Wrestler *w, Wrestler *o, s32 aim) {
    if (aim && w->anim_frame == w->anim_player[0x12])
        dive_aim(w, o);
    w->flags8a |= 0x40;
    if (!(w->anim_frame < w->anim_player[0x12]) && !(w->anim_player[0x13] < w->anim_frame)) {
        w->location &= 0xFFE7;
        mem_w32(0x8016AE24 + 180u * (u32)w->held_obj + 4u * (u32)w->ladder_side, (u32)-1);
        if (w->flags_ea & 0x40) {
            if ((s16)w->slide_frames > 0) {
                Vec3 rel, p, rel2, q, d;
                vec_sub(&w->pos, &w->point_120, &rel);
                point_at_offset(&w->pos, &rel, angle_diff(0, w->facing), &p);
                p.y = w->pos.y;
                vec_sub(&p, &w->pos, &rel2);
                w->facing = angle_add(w->facing, angle_diff(w->lockup_facing, w->facing) / (s16)w->slide_frames);
                point_at_offset(&w->point_120, &rel2, w->facing, &q);
                q.y = w->pos.y;
                vec_sub(&w->pos, &q, &d);
                wrestler_translate(w, &d);
                w->slide_frames = (u16)(w->slide_frames - 1);
            }
            if ((s16)w->aux_timer > 0) {
                s32 j = mem_s8(w->move + 13);
                Vec3 lead;
                if (j >= 20) {
                    s32 a = j == 20 ? 17 : 15, b = j == 20 ? 9 : 8;
                    const Vec3 *ja = wrestler_joint(w, a - 1), *jb = wrestler_joint(w, b - 1);
                    lead.x = (f32)((f64)(ja->x - jb->x) * 0.5 + (f64)jb->x);
                    lead.z = (f32)((f64)(ja->z - jb->z) * 0.5 + (f64)jb->z);
                } else {
                    lead.x = wrestler_joint(w, j - 1)->x;
                    lead.z = wrestler_joint(w, j - 1)->z;
                }
                lead.y = 0.0f;
                f32 seg[9];
                segment_distance(&lead, &w->dive_target, seg);
                Vec3 v;
                vec_from_angle((s32)seg[7], seg[4] / (f32)(s16)w->aux_timer, &v);
                wrestler_translate(w, &v);
                if ((u32)(w->state - state::TurnbuckleDiveMoves256And257) < 5)
                    w->pos.y = w->pos.y + w->step_vec.y;
                w->aux_timer = (u16)(w->aux_timer - 1);
            }
        }
    }
    if ((w->flags_ea & 2) && !(o->state == state::LyingDown && o->state_step == 3))
        w->flags_ea &= 0xEF;
    u8 from = w->state == state::TurnbuckleDiveMoves263And264 ? w->anim_player[AP_HIT_FROM] : w->anim_player[AP_HOLD_FRAME];
    if (!(w->anim_frame < from)) {
        w->location &= 0xFFFD;
        w->flags8a &= 0xFFBF;
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
    }
}

// N64 0x8013A63C: the height a dive carries by sliding: with `always` by the frames left of the
// player's frame count, else past the hold frame by the frames left of the rest (8 x that share + 6);
// a lying pose sets flags8a 0x6000.
void aerial_height(Wrestler *w, s32 always) {
    f32 scale;
    if (always) {
        scale = (f32)((f64)((f32)w->anim_frames_left / (f32)w->anim_player[AP_FRAMES]) * 8.0 + 6.0);
    } else {
        if (!(w->anim_player[AP_HOLD_FRAME] < w->anim_frame))
            return;
        scale = (f32)((f64)((f32)w->anim_frames_left /
                            (f32)((s32)w->anim_player[AP_FRAMES] - (s32)w->anim_player[AP_HOLD_FRAME])) * 8.0 + 6.0);
    }
    wrestler_slide(w, scale);
    if (pose_lying(w))
        w->flags8a |= 0x6000;
}

// N64 0x8013A1A4: whether `d` counters w's dive: not w's partner; one in a corner or the ropes
// (flags8a 0x02) not mid-roll (0x82) with a timed press dodges (0x68); one blocking (0x4C, under 3
// frames in) or with a timed press, unless a cage escaper (flags_e9 0x01), catches it (0x4C step 3,
// the two paired); the counter sound and statistics either way.
s32 dive_counter(Wrestler *d, Wrestler *w) {
    relative_facing(w, d, 0);
    if (w->partner == d->slot)
        return 0;
    if (d->flags8a & 2) {
        if (d->state == state::CarryDefender)
            return 0;
        if (!timed_press_check(d, 0, 0, 2))
            return 0;
        // sfx_play_slot(d->slot, 21): the counter sound
        stats_count(d, w, 1);
        wrestler_set_state_now(d, state::DiveDodge);
        return 1;
    }
    if (!(d->state == state::Blocking && (s16)d->count_timer < 3) && !timed_press_check(d, 0, 0, 2))
        return 0;
    if (d->flags_e9 & 1)
        return 0;
    // sfx_play_slot(d->slot, 21): the counter sound
    stats_count(d, w, 1);
    d->opponent = w->slot;
    w->pair_other = d->slot;
    w->opponent = d->slot;
    wrestler_set_state_now(d, state::Blocking);
    d->state_step = 3;
    return 1;
}

// N64 0x8013A2F0: w's dive lands on d. On a grapple pair (0x44 / 0x48 / 0x4A the attacker, 0x45 /
// 0x49 / 0x4B the defender's attacker) state_flags 0x10 breaks it up. Otherwise d is paired with w
// (and targets w unless partners), drops its weapon and takes the move (hit_move_copy; x1.25 damage
// with flags_ea 0x80): on a ladder it falls off (0xB2), lying the hit while down (0x63); else strike
// flags 0x800 from the side it faces, 10 spirit off in a taunt, off the apron over the ropes (0x70),
// 0x81's catcher (0x82) passes it on, by the frame flags (0x01 knocked down 0x04, 0x08 0x02, 0x10 over
// the ropes) or the aftermath, else the stagger (0x2D).
void dive_land_hit(Wrestler *w, Wrestler *d) {
    w->hits_taken = 0;
    switch (d->state) {
    case state::PinAttacker: case state::GroundHoldAttacker: case state::PinFromMoveAttacker:
        d->state_flags |= 0x10;
        return;
    case state::PinDefender: case state::GroundHoldDefender: case state::PinFromMoveDefender:
        g_wrestlers[d->opponent].state_flags |= 0x10;
        return;
    default:
        break;
    }
    d->pair_other = w->slot;
    if (w->partner != d->slot)
        d->opponent = w->slot;
    weapon_release(d, 0);
    hit_move_copy(w, d);
    if (w->flags_ea & 0x80)
        d->hit_move.damage = (u8)(s32)((f64)(s8)d->hit_move.damage * 1.25);
    if (d->location & 8) {
        wrestler_set_state_free_now(d, state::LadderHit);
        d->state_counter |= 3;
        return;
    }
    if (d->flags80 & 8) {
        wrestler_set_state_now(d, state::HitWhileDown);
        return;
    }
    d->strike_flags = 0x800;
    d->hit_flags = 0;
    relative_facing(w, d, 0);
    if (d->flags8a & 2)
        d->hit_flags |= 0x2000000;
    if (d->state == state::Taunt)
        wrestler_add_spirit(d, -10);
    if (d->location & 2) {
        wrestler_set_state_free_now(d, state::KnockedOverTopRope);
        return;
    }
    if (d->state == state::CarryAttacker) {
        d->state_flags |= 4;
        return;
    }
    if (d->state == state::CarryDefender) {
        for (s32 i = 0; i < 4; i++) {
            Wrestler *c = &g_wrestlers[i];
            if (c->state != state::CarryAttacker)
                continue;
            if (d->flags8a & 2)
                c->flags8a |= 2;
            else
                c->flags8a &= 0xFFFD;
            c->state_flags |= 0x10;
            d->opponent = i;
            d->slot_d = w->slot;
            return;
        }
        return;
    }
    if (d->frame_flags & 1) {
        d->strike_flags &= ~0x800u;
        d->hit_flags |= 4;
        if (d->flags8a & 1)
            d->hit_flags |= 0x2000000;
        else
            d->hit_flags &= 0xFDFFFFFF;
    }
    if (d->frame_flags & 8) {
        d->strike_flags &= ~0x800u;
        d->hit_flags |= 2;
    }
    if (d->frame_flags & 0x10) {
        wrestler_set_state_now(d, state::KnockedOverTopRope);
        d->state_counter |= 1;
        return;
    }
    if (move_aftermath_check(w, w, d, 0))
        return;
    wrestler_set_state_free_now(d, state::KnockedDown);
}

// N64 0x80139F10: the landing test of w's special / aerial move: the hit kind by move (270 7, 268 5,
// 269 6, 271 / 272 8, 273 / 275 10, 274 11 (a held opponent too: both marked lying for the test), 278
// 9, 281 16, 265 / 266 12, 61 31, 75 28, else `kind`) and strike_hit_test. A wrestler hit may counter
// (dive_counter: 0 returned), else the hit lands (dive_land_hit, flags_ea 0x08, 1 returned); an object
// is knocked along the line from w (object_set_motion 3).
s32 special_move_kind(Wrestler *w, Wrestler *o, s32 kind) {
    switch (w->move_num) {
    case 270: kind = 7; break;
    case 268: kind = 5; break;
    case 269: kind = 6; break;
    case 265: case 266: kind = 12; break;
    case 61: kind = 31; break;
    case 75: kind = 28; break;
    case 271: case 272: kind = 8; break;
    case 273: case 275: kind = 10; break;
    case 274: kind = 11; break;
    case 278: kind = 9; break;
    case 281: kind = 16; break;
    default: break;
    }
    s32 t;
    if (kind == 11 && (o->flags80 & 0xC0)) {
        o->flags80 |= 8;
        g_wrestlers[o->opponent].flags80 |= 8;
        t = strike_hit_test(w, 0, 11);
        o->flags80 &= 0xFFF7;
        g_wrestlers[o->opponent].flags80 &= 0xFFF7;
    } else {
        t = strike_hit_test(w, 0, kind);
    }
    if (t == -1)
        return 0;
    if (t >= 100) {
        knock_object(w, t - 100);
        return 1;
    }
    Wrestler *d = &g_wrestlers[t];
    if (dive_counter(d, w))
        return 0;
    dive_land_hit(w, d);
    w->flags_ea |= 8;
    return 1;
}

// N64 0x80139E74: the end of a special / aerial move: the move's spirit (record +0x18), and a landing
// on the mat (lying) hurts w itself (generic move 25, x1.25 from a ladder); then the settle.
void special_move_land(Wrestler *w) {
    wrestler_add_spirit(w, mem_s8(w->move + 0x18));
    if (pose_is_lying(w)) {
        wrestler_set_generic_move(w, 25);
        hit_move_copy(w, w);
        if (w->flags_ea & 0x80)
            w->hit_move.damage = (u8)(s32)((f64)(s8)w->hit_move.damage * 1.25);
        apply_damage(w, w);
    }
    wrestler_settle_pose(w);
}

// N64 0x8013B538: states 0x75 / 0x76 / 0xC0, a dive from the apron or a raised place onto the floor.
// Step 0 (flags_ea cleared): 0xC0 generic move 75 (13069); 0x75 271, 0x76 272 (the move's animation).
// Step 1 the landing test (special_move_kind, a hit with flags_ea 0x08 counted; step 2 on a hit).
// Steps 1-2: without a hit the frame bits cleared, the height, the flight (aerial_step, aimed); at the
// end the stun (0x30) after a hit, else special_move_land.
void state_75_apron_dive(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0: {
        w->flags_ea = 0;
        if (w->state == state::ApronDiveGenericMove75) {
            wrestler_set_generic_move(w, 75);
            wrestler_set_anim(w, 13069, 3);
        } else {
            wrestler_start_move(w, ctx->slot, w->state == state::ApronDiveMove271 ? 271 : 272);
            wrestler_set_anim(w, anim_style(w, 0), 3);
        }
        w->state_step++;
        return;
    }
    case 1:
        if (special_move_kind(w, ctx->rec_a, 0)) {
            if (w->flags_ea & 8)
                stats_count(w, ctx->rec_a, 0);
            w->state_step++;
        }
        [[fallthrough]];
    case 2:
        if (!(w->flags_ea & 8))
            g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        aerial_height(w, 0);
        aerial_step(w, ctx->rec_a, 1);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (w->flags_ea & 8)
            wrestler_set_state_now(w, state::SettleAfterAnimation);
        else
            special_move_land(w);
        return;
    default:
        return;
    }
}




// N64 0x8013BE70: state 0xCF, the dive off the top of the cage (generic move 61), flags8a 0x40 in the
// air. Step 0 the leap (12843); step 1 at its last frame the drop (step_vec down 8, 12844, location
// 0x01, a hit window open the whole fall); step 2 the fall (bounds 4.0, the landing test until a hit,
// state_counter 0x01; 3 more a frame) to 161.5 (12845, hit window frames 1-2); step 3 the landing test,
// the slide (4.0, flags8a 0x2000), bounds and the events after a hit; a miss at its end costs the
// move's spirit and hurts w itself (generic move 25), then the settle.
void state_CF_cage_top_attack(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        w->flags8a |= 0x40;
        wrestler_set_generic_move(w, 61);
        wrestler_set_anim(w, 12843, 14);
        w->state_step++;
        return;
    case 1:
        w->flags8a |= 0x40;
        if (w->anim_frames_left != 1)
            return;
        w->step_vec = {0.0f, -8.0f, 0.0f};
        wrestler_set_anim(w, 12844, 9);
        w->location = 1;
        w->anim_player[AP_HIT_FROM] = 0;
        w->anim_player[AP_HOLD_FRAME] = 255;
        wrestler_set_step(w, 2);
        return;
    case 2:
        w->flags8a |= 0x40;
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (!(w->state_counter & 1) && special_move_kind(w, ctx->rec_a, 0))
            w->state_counter |= 1;
        wrestler_translate(w, &w->step_vec);
        w->step_vec.y = (f32)((f64)w->step_vec.y + -3.0);
        if (!((f64)w->pos.y <= 161.5))
            return;
        w->pos.y = 161.5f;
        wrestler_set_anim(w, 12845, 9);
        w->anim_player[AP_HIT_FROM] = 1;
        w->anim_player[AP_HOLD_FRAME] = 2;
        w->state_step++;
        return;
    case 3:
        if (!(w->state_counter & 1) && special_move_kind(w, ctx->rec_a, 0))
            w->state_counter |= 1;
        w->flags8a |= 0x2000;
        wrestler_slide(w, 4.0f);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        if (w->state_counter & 1) {
            move_event(ctx, 0);
            return;
        }
        if (w->anim_frames_left > 0)
            return;
        wrestler_add_spirit(w, mem_s8(w->move + 0x18));
        wrestler_set_generic_move(w, 25);
        hit_move_copy(w, w);
        apply_damage(w, w);
        wrestler_settle_pose(w);
        return;
    default:
        return;
    }
}

// N64 0x8013A9D4: state 0x0A, up on the turnbuckle (input_climb_turnbuckle, run_step). Step 0 (weapon
// dropped, state_counter 0x01): along the apron the short climb (12093-12096 by the way it goes and
// flags8a 0x1000, aux_timer 4 / 5, step 2); from the ring the move's climb (12084 + k, k from record flag
// 0x08 and the build; a downed opponent gets the animation's length of escape time) with the slide
// (pair_scripted_place), step 1. Steps 1-2: R sets state_counter 0x02 (get down), the stay-down input
// 0x04 (taunt). Step 1 the slide. Step 2 at its end the facing by k (0x8014D1F8, or 0x8014D208 from 2),
// the perch 12092 (flags80 0x01), step 3. Step 3 (act_flags 0x02, flags80 0x04, location 0x800,
// flags8a 0x100) waits for the dive: 0x04 within 150 frames taunts (move 233, step 6); A / B / C-down
// held runs the 150 frames out; a tag or the illegal man's way back with C-up / L; it gets down (0x02,
// by k 12090 / 12089 / 12098 / 12097, step 4) when told to, unable to act, on end_flags 0x01, or under
// over-the-top or cage rules with the opponent out of the ring; otherwise the dive by climb_check
// (states 0x79-0x7D), none also getting down. Step 4 the climb down and events; step 5 the events
// (74); step 6 the taunt (act_flags 0x08, object_spawn_for_move, R gets down), at its end the spirit,
// flags8c 0x80, back to the perch.
void state_0A_turnbuckle_perch(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    illegal_man_check(w);
    switch (w->state_step) {
    case 0:
        weapon_release(w, 0);
        w->state_counter |= 1;
        if (w->location & 2) {
            s32 anim, k;
            if (w->flags8a & 4) {
                k = 4;
                anim = (w->flags8a & 0x1000) ? 12095 : 12093;
            } else {
                k = 5;
                anim = (w->flags8a & 0x1000) ? 12096 : 12094;
            }
            w->aux_timer = (u16)k;
            w->flags8a &= 0xEFFF;
            wrestler_set_anim(w, anim, 3);
            w->state_step = 2;
            return;
        }
        {
            wrestler_start_move(w, ctx->slot, w->move_num);
            s32 k = ((mem_u32(w->move + 0x20) & 8) ? 0 : 2) + (mem_u8(w->attrs + 0x34) & 1);
            w->aux_timer = (u16)k;
            wrestler_set_anim(w, 12084 + k, 2);
            if (o->flags80 & 8)
                o->escape = (s16)(o->escape + w->anim_player[AP_FRAMES]);
            pair_scripted_place(w, nullptr);
            w->state_step++;
        }
        [[fallthrough]];
    case 1:
        if (ctx->pressed & 0x10)
            w->state_counter |= 2;
        if (ctx->in_byte)
            w->state_counter |= 4;
        if (!pair_slide_step(w, nullptr))
            return;
        w->state_step = (w->state_counter & 1) ? w->state_step + 1 : 5;
        return;
    case 2: {
        w->act_flags |= 1;
        if (ctx->pressed & 0x10)
            w->state_counter |= 2;
        if (ctx->in_byte)
            w->state_counter |= 4;
        if (w->anim_frames_left > 0)
            return;
        s32 k = (s16)w->aux_timer;
        if (k >= 0 && k < 2)
            w->facing = mem_s32(0x8014D1F8 + 4u * (u32)w->dir);
        else if (k >= 2 && k < 6)
            w->facing = mem_s32(0x8014D208 + 4u * (u32)w->dir);
        wrestler_set_anim(w, 12092, 1);
        w->count_timer = 0;
        w->flags80 |= 1;
        w->state_step++;
        return;
    }
    case 3: {
        w->act_flags |= 2;
        w->flags80 |= 4;
        w->location |= 0x800;
        w->flags8a |= 0x100;
        if (match_can_interrupt())
            w->state_counter |= 2;
        if (ctx->pressed & 0x10)
            w->state_counter |= 2;
        if (ctx->in_byte)
            w->state_counter |= 4;
        if (!(w->state_counter & 2)) {
            if ((w->state_counter & 4) && (s16)w->count_timer < 150) {
                w->state_counter &= 0xFFFB;
                wrestler_start_move(w, ctx->slot, 233);
                wrestler_set_anim(w, anim_style(w, 0), 1);
                spirit_update(w, 5);
                w->flags8c &= 0xFF7F;
                taunt_hold_opponent(ctx);
                w->state_step = 6;
                return;
            }
            if ((ctx->pressed & 0x28) && !(w->flags_e9 & 0x10)) {
                if (tag_try_partner(ctx) || illegal_man_leave(w))
                    return;
            }
            if (ctx->buttons & 0xC004) {
                w->count_timer = (u16)(w->count_timer + 1);
                if ((s16)w->count_timer < 150)
                    return;
            }
        }
        if (!wrestler_may_act(w) && (o->location & 1))
            w->state_counter |= 2;
        if ((w->end_flags & 1) && (o->location & 1))
            w->state_counter |= 2;
        if (((g_match_rules & 0x400) || (g_match_type & 0x10000)) && (o->location & 0x406) && (w->flags_e9 & 0x14))
            w->state_counter |= 2;
        if (w->state_counter & 2) {
            static const s16 kDown[6] = {12090, 12090, 12089, 12089, 12098, 12097};
            u32 k = (u32)(s32)(s16)w->aux_timer;
            if (k >= 6)
                NM_UNPORTED("state_0A: getting down with an out-of-range aux_timer");
            wrestler_set_anim(w, kDown[k], 1);
            w->state_step++;
            return;
        }
        s32 m = climb_check(w, o);
        if (!m) {
            w->state_counter |= 2;
            return;
        }
        wrestler_start_move(w, ctx->slot, m);
        s32 st;
        switch (m) {
        case 256: case 257: st = state::TurnbuckleDiveMoves256And257; break;
        case 263: case 264: st = state::TurnbuckleDiveMoves263And264; break;
        case 261: case 262: st = state::TurnbuckleDiveMoves261And262; break;
        case 279: st = state::TurnbuckleDiveMove279; break;
        case 280: st = state::TurnbuckleDiveMove280; break;
        default: NM_UNPORTED("state_0A: a dive of no state");
        }
        wrestler_set_state_now(w, st);
        return;
    }
    case 4:
        w->act_flags |= 4;
        if (w->anim_frame < w->anim_player[AP_ACTIVE_FROM]) {
            w->flags80 |= 4;
            w->location |= 0x800;
        }
        move_event(ctx, 0);
        return;
    case 5:
        move_event(ctx, 74);
        return;
    case 6:
        object_spawn_for_move(w, nullptr, 1);
        w->act_flags |= 8;
        w->count_timer = (u16)(w->count_timer + 1);
        w->flags80 |= 4;
        w->location |= 0x800;
        if (ctx->pressed & 0x10)
            w->state_counter |= 2;
        if (w->anim_frames_left > 0)
            return;
        w->flags8c |= 0x80;
        hit_move_copy(w, w);
        spirit_exchange(w, o, w);
        wrestler_set_anim(w, 12092, 1);
        w->state_step = 3;
        return;
    default:
        return;
    }
}

// N64 0x8013C164: states 0x79-0x7D, the dive off the turnbuckle (or a ladder, flags_ea 0x80), flags80
// 0x04 throughout. Step 0: the leap (the move's animation, aimed by dive_aim, then 12091). Step 1 the
// turn to lockup_facing (136 a frame), then the move's animation. Step 2 the landing test into
// dive_hit: flags_ea 0x02 dives kind 14 (a held opponent marked lying for it): on a wrestler lying or
// in a grapple pair flags_ea 0x08 and either breaks the pair up (0x20) or adds 30 to its count_timer;
// on one standing 0x0C and dive_land_hit. 0x7C / 0x7D kind 15 with dive_land_hit; the others kind 13,
// with dive_counter first. Steps 2-3: the height (not flags_ea 0x02) and the flight (aerial_step). At
// its end pos.y 90 in the ring or on the apron, else 0. flags_ea 0x02 dives: a miss 5 (style 3); a
// 0x04 hit, step 4 (style 4); a 0x10 hit sets the target down (0x31 step 7), step 6 (style 2) with
// record flag 0x10 when it faces up; record flag 0x200 moves that land from the side (683-1365 off the
// target's facing, not cut short, on the opponent, face up) go into the pin pair (0x44 / 0x45 step 3),
// else 0x63 on the target (x1.25 damage from a ladder), step 4. Others at the end the stun (0x30, with
// a hit) or special_move_land. Steps 4-6: the height, bounds, finisher effects. Step 4 the events
// (0x80); step 5 special_move_land at the end; step 6 at the end hurts w too (generic move 26 from the
// opponent), then the settle.
void state_79_dive(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    w->flags80 |= 4;
    auto hit_copy = [&](Wrestler *d) {
        hit_move_copy(w, d);
        if (w->flags_ea & 0x80)
            d->hit_move.damage = (u8)(s32)((f64)(s8)d->hit_move.damage * 1.25);
    };
    switch (w->state_step) {
    case 0: {
        s32 mode = (w->location & 8) ? 0 : 1;
        w->flags_ea = 0;
        wrestler_set_anim(w, anim_style(w, 0), mode);
        dive_aim(w, o);
        wrestler_set_anim(w, 12091, mode ? 1 : 9);
        w->state_step++;
        return;
    }
    case 1:
        if (!wrestler_turn_toward(w, w->lockup_facing, 136, 0))
            return;
        wrestler_set_anim(w, anim_style(w, 0), 1);
        w->state_step++;
        return;
    case 2: {
        s32 t;
        if (w->flags_ea & 2) {
            if (o->flags80 & 0xC0) {
                o->flags80 |= 8;
                g_wrestlers[o->opponent].flags80 |= 8;
                t = strike_hit_test(w, 0, 14);
                o->flags80 &= 0xFFF7;
                g_wrestlers[o->opponent].flags80 &= 0xFFF7;
            } else {
                t = strike_hit_test(w, 0, 14);
            }
        } else {
            t = strike_hit_test(w, 0, (u32)(w->state - state::TurnbuckleDiveMove279) < 2 ? 15 : 13);
        }
        w->dive_hit = t;
        if (t != -1) {
            if (t >= 100) {
                if (!(w->flags_ea & 0x80))
                    knock_object(w, t - 100);
            } else {
                Wrestler *d = &g_wrestlers[t];
                if (w->flags_ea & 2) {
                    u8 ea = w->flags_ea;
                    w->flags_ea = ea | 8;
                    if (d->flags80 & 0xC8) {
                        switch (d->state) {
                        case state::PinAttacker: case state::GroundHoldAttacker: case state::PinFromMoveAttacker:
                            d->state_flags |= 0x10;
                            w->flags_ea |= 0x20;
                            break;
                        case state::PinDefender: case state::GroundHoldDefender: case state::PinFromMoveDefender:
                            g_wrestlers[d->opponent].state_flags |= 0x10;
                            w->flags_ea |= 0x20;
                            break;
                        default:
                            d->count_timer = (u16)(d->count_timer + 30);
                            break;
                        }
                    } else {
                        w->flags_ea = ea | 0xC;
                        dive_land_hit(w, d);
                    }
                } else if ((u32)(w->state - state::TurnbuckleDiveMove279) < 2) {
                    w->flags_ea |= 8;
                    dive_land_hit(w, d);
                } else if (!dive_counter(d, w)) {
                    w->flags_ea |= 8;
                    dive_land_hit(w, d);
                }
            }
            w->state_step++;
        }
        [[fallthrough]];
    }
    case 3: {
        if (!(w->flags_ea & 8))
            g_frame_bits &= ~mem_u32(A_SLOT_BITS + 4u * (u32)ctx->slot);
        if (!(w->flags_ea & 2))
            aerial_height(w, 0);
        aerial_step(w, o, 0);
        if (w->anim_frames_left > 0)
            return;
        w->pos.y = (w->location & 0x43) ? 90.0f : 0.0f;
        u8 ea = w->flags_ea;
        if (!(ea & 2)) {
            anim_end_apply(w);
            if (ea & 8) {
                stats_count(w, o, 0);
                wrestler_set_state_now(w, state::SettleAfterAnimation);
            } else {
                special_move_land(w);
            }
            return;
        }
        if (!(ea & 8)) {
            wrestler_set_anim(w, anim_style(w, 3), 1);
            w->state_step = 5;
            return;
        }
        Wrestler *d = &g_wrestlers[w->dive_hit];
        if (ea & 4) {
            wrestler_set_anim(w, anim_style(w, 4), 1);
            w->state_step = 4;
            return;
        }
        if (ea & 0x10) {
            wrestler_set_state_now(d, state::LyingDown);
            d->state_step = 7;
            d->opponent = ctx->slot;
            if (!(d->flags8a & 1) && (mem_u32(w->move + 0x20) & 0x10)) {
                wrestler_set_anim(w, anim_style(w, 2), 1);
                w->state_step = 6;
                return;
            }
            wrestler_set_anim(w, anim_style(w, 3), 1);
            w->state_step = 5;
            return;
        }
        if (mem_u32(w->move + 0x20) & 0x200) {
            if (!(ea & 0x20)) {
                if (ctx->slot != d->partner)
                    d->opponent = ctx->slot;
                d->pair_other = ctx->slot;
                u32 a = (u32)abs_i(angle_diff(w->lockup_facing, o->facing));
                if (!(w->flags_ea & 1) && w->dive_hit == ctx->slot_a && !(d->flags8a & 1) && a - 683 < 683) {
                    wrestler_set_anim(w, anim_style(w, 1), 1);
                    w->flags80 |= 1;
                    w->flags88 |= 6;
                    o->flags80 |= 1;
                    o->flags88 |= 2;
                    w->pair_slot = ctx->slot_a;
                    stats_count(w, o, 0);
                    wrestler_set_state(w, state::PinAttacker);
                    wrestler_set_state(o, state::PinDefender);
                    w->state_step = 3;
                    o->state_step = 3;
                    return;
                }
                hit_copy(d);
                wrestler_set_state_now(d, state::HitWhileDown);
            }
            wrestler_set_anim(w, anim_style(w, 4), 1);
            w->state_step = 4;
            return;
        }
        wrestler_set_anim(w, anim_style(w, 1), 1);
        if (!(w->flags_ea & 0x20)) {
            hit_copy(d);
            wrestler_set_state_now(d, state::HitWhileDown);
            if (ctx->slot != d->partner)
                d->opponent = ctx->slot;
            d->pair_other = ctx->slot;
        }
        w->state_step = 4;
        return;
    }
    case 4:
        aerial_height(w, 1);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        finisher_effects(w);
        move_event(ctx, 128);
        return;
    case 5:
        aerial_height(w, 1);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        finisher_effects(w);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        special_move_land(w);
        return;
    case 6:
        aerial_height(w, 1);
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        finisher_effects(w);
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_generic_move(w, 26);
        hit_move_copy(w, w);
        if (w->flags_ea & 0x80)
            w->hit_move.damage = (u8)(s32)((f64)(s8)w->hit_move.damage * 1.25);
        apply_damage(o, w);
        anim_end_apply(w);
        wrestler_settle_pose(w);
        return;
    default:
        return;
    }
}

}  // namespace nm

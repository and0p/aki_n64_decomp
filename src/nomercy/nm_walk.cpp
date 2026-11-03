// Walking: walk_step and its speeds, the walk animations (anim_pick), objects underfoot.
#include "nm_funcs.h"

namespace nm {

// N64 0x8011CC80: a body region's condition (50 in finisher mode).
f32 wrestler_region_condition(const Wrestler *w, s32 region) {
    if (w->flags_ee & 4)
        return 50.0f;
    return w->region[region];
}

// N64 0x80131C40: the walk speed. The speed class (attrs +0) drops one step for each threshold
// the legs' condition (region 3) is under; flags: 0x01 the normal column (otherwise the fast one),
// 0x08 doubled, 0x10 / 0x20 1.5, on the apron 0x02 3.0 and 0x08 12.0, 0x40 8.0 (the overrides
// replace only the fast speed).
f32 walk_speed(Wrestler *w, u32 flags) {
    s32 cls = mem_s8(w->attrs);
    u32 thresholds;  // f32 list, walked down from the class
    switch (cls) {
    case 1: thresholds = 0x8014DB90; break;
    case 2: thresholds = 0x8014DB98; break;
    case 3: thresholds = 0x8014DBA4; break;
    default:
        // Class 0 starts from whatever its caller left in v1 (walk_step: the wrestler record, then 0).
        NM_UNPORTED("walk_speed: speed class 0");
    }
    while (!(mem_f32(thresholds) <= wrestler_region_condition(w, 3))) {
        thresholds += 4;
        cls--;
    }
    f32 fast = mem_f32(0x8014DBB0 + 8 * cls);
    f32 normal = mem_f32(0x8014DBB4 + 8 * cls);
    if (flags & 0x08) {
        fast = (f32)((f64)fast + (f64)fast);
        normal = (f32)((f64)normal + (f64)normal);
    }
    if (flags & 0x10)
        fast = 1.5f;
    if (flags & 0x20)
        fast = 1.5f;
    if (w->location & 2) {
        if (flags & 0x02)
            fast = 3.0f;
        if (flags & 0x08)
            fast = 12.0f;
    }
    if (flags & 0x40)
        fast = 8.0f;
    if (!(flags & 0x01))
        normal = fast;
    return normal;
}

// N64 0x80132F28: the d-pad as one of 8 directions (1 up, then clockwise; 0 none).
s32 dpad_dir8(u16 buttons) {
    u16 d = buttons & 0xF00;
    s32 dir = d == 0x800;
    if (d == 0x900) dir = 2;
    if (d == 0x100) dir = 3;
    if (d == 0x500) dir = 4;
    if (d == 0x400) dir = 5;
    if (d == 0x600) dir = 6;
    if (d == 0x200) dir = 7;
    if (d == 0xA00) dir = 8;
    return dir;
}

// N64 0x80131DC8: on the apron, whether the d-pad points along it (1) or back along it (2).
s32 side_input_check(const Wrestler *w, u16 dpad) {
    if (mem_u16(0x8014DBD0 + 2 * w->ring_zone) & dpad)
        return 1;
    return (mem_u16(0x8014DBD8 + 2 * w->ring_zone) & dpad) ? 2 : 0;
}

// N64 0x801302E0: while a walk animation is in its first steps (frame <= the table's value for
// its direction), the idle is not started over it.
s32 walk_anim_step(const Wrestler *w) {
    u16 anim = (u16)w->anim_id;
    s32 base;
    if ((u32)(anim - 11752) < 42)
        base = 11752;
    else if ((u32)(anim - 11797) < 24)
        base = 11797;
    else
        return 0;
    s32 k = (w->anim_id - base) % 6;
    return !(mem_s8(0x8014DB28 + k) < w->anim_frame);
}

// N64 0x8013061C: the wrestler's animation style from trait bits attrs +0x34 (0x10-0x200 give
// styles 1-6, the highest wins).
s32 anim_style_variant(const Wrestler *w) {
    u32 t = mem_u32(w->attrs + 0x34);
    s32 v = 0;
    if (!(t & 0x3F8))
        return 0;
    if (t & 0x10) v = 1;
    if (t & 0x20) v = 2;
    if (t & 0x40) v = 3;
    if (t & 0x80) v = 4;
    if (t & 0x100) v = 5;
    if (t & 0x200) v = 6;
    return v;
}

// N64 0x80142E24: the held weapon's animation family. `carry` is the value the original finds in
// its v1 when none of the bits match (the caller's last v1).
s32 wrestler_move_anim(const Wrestler *w, s32 carry) {
    u8 f = w->flags_ef;
    if (!(f & 1))
        return -1;
    s32 v = carry;
    if (f & 2) v = 0;
    if (f & 4) v = 1;
    if (f & 8) v = 2;
    if (f & 0x10) v = 0;
    if (f & 0x20) v = 3;
    return v;
}

// N64 0x80130690: the walk animation for moving at `angle`: the direction against the facing in
// six sectors, from the hurt-legs set when region `weakest_region` is under 15.
s32 anim_pick_hurt(Wrestler *w, const Wrestler *opp, s32 angle) {
    (void)opp;
    s32 base = anim_style_variant(w);
    if (w->flags_ef & 1) {
        base = 7 * wrestler_move_anim(w, 1) + 12397;
    } else if ((f64)wrestler_region_condition(w, w->weakest_region) < 15.0) {
        base = 6 * (s8)w->weakest_region + 11797;
    } else {
        base = 6 * base + 11752;
    }
    s32 d = angle_diff(w->facing, angle);
    s32 sector = 0;
    if (!(abs_i(d) < 341)) {
        if (!(abs_i(d) < 1707))
            sector = 3;
        else if (d < 0)
            sector = abs_i(d) < 910 ? 1 : 2;
        else
            sector = abs_i(d) < 910 ? 5 : 4;
    }
    return base + sector;
}

// N64 0x801307B8: the idle (kind 1, or 7 without the hurt set).
s32 anim_pick_hurt_b(Wrestler *w, const Wrestler *opp, s32 kind) {
    (void)opp;
    s32 style = anim_style_variant(w);
    if (kind == 7)
        return style + 11737;
    if (kind != 1)
        return kind;  // the original returns its caller's saved register here (the kind)
    if (w->flags_ef & 1)
        return 7 * wrestler_move_anim(w, (s32)mem_u32(w->attrs + 0x34)) + 12396;
    if ((f64)wrestler_region_condition(w, w->weakest_region) < 15.0)
        return (s8)w->weakest_region + 11746;
    return style + 11737;
}

// N64 0x80130364: picks one of the generic animations: 0 walking (at `angle`), 1 / 7 idle, 2 / 3
// hurt idles, 4 / 5 / 6 the run's start, run and turn.
s32 anim_pick(WrestlerCtx *ctx, s32 kind, s32 angle) {
    Wrestler *w = ctx->self;
    s32 style = anim_style_variant(w);
    u32 traits = mem_u32(w->attrs + 0x34);
    switch (kind) {
    case 0:
        return anim_pick_hurt(w, ctx->rec_a, angle);
    case 1:
    case 7:
        return anim_pick_hurt_b(w, ctx->rec_a, kind);
    case 2:
        if ((f64)wrestler_region_condition(w, (s8)w->weakest_region) < 15.0)
            return (s8)w->weakest_region + 11865;
        return mem_s16(0x8014DB40 + 2 * style);
    case 3:
        if ((f64)wrestler_region_condition(w, (s8)w->weakest_region) < 15.0)
            return (s8)w->weakest_region + 11872;
        return mem_s16(0x8014DB50 + 2 * style);
    case 4:
        if (w->location & 2)
            return (w->flags8a & 4) ? 11837 : 11838;
        if (w->flags_ef & 1)
            return 3 * wrestler_move_anim(w, (s32)traits) + 12478;
        return (traits & 0x200) ? 11836 : 11835;
    case 5:
        if (w->location & 2)
            return (w->flags8a & 4) ? 11843 : 11844;
        if (w->flags_ef & 1)
            return 3 * wrestler_move_anim(w, (s32)traits) + 12479;
        if ((traits & 0x200) && (w->flags86 & 0x20))
            return 11839;
        return mem_s16(0x8014DB30 + 2 * style);
    case 6:
        if (w->location & 2)
            return (w->flags8a & 4) ? 11848 : 11849;
        if (w->flags_ef & 1)
            return 3 * wrestler_move_anim(w, (s32)traits) + 12480;
        if (traits & 0x200)
            return 11847;
        return (traits & 0x400) ? 11845 : 11846;
    default:
        return 0;
    }
}

// N64 0x801445F8: an object's sound for a kind of contact; returns the sound.
s32 object_sfx(s32 slot, s32 obj, s32 kind) {
    s32 type = mem_s32(0x8016AE10 + 180 * obj + 4);
    s32 id = mem_u8(0x8014E0D8 + 5 * type + kind);
    host_sfx_slot((s16)(slot & -(s32)((u32)slot < 4)), id);
    return id;
}

// N64 0x80144260: steps on a loose object: its sound, for the first listed joint touching one.
void object_underfoot(Wrestler *w) {
    if (mem_s8(0x8014E0AC) == -1)
        return;
    for (u32 a = 0x8014E0AC; mem_s8(a) != 0; a++) {
        s32 k = mem_s8(a);
        s32 obj = object_at_point(wrestler_joint(w, k - 1), 0.0f);
        if (obj != -1) {
            object_sfx(w->slot, obj, 3);
            return;
        }
    }
}

// N64 0x800F01B4: the fall off the cage wall: 12861, or with flags_f7 0x08 12872 (12876 with
// state_counter 0x01).
s32 turnbuckle_anim(const Wrestler *w) {
    if (!(w->flags_f7 & 8))
        return 12861;
    return (w->state_counter & 1) ? 12876 : 12872;
}

// N64 0x800EFFB8: on the cage wall, the opponent's attacks from below (its flags_f7): 0x01 at w's side
// (its dir) shakes w (with w's flags_f7 0x04 the 0x80145200 chance by the opponent's flags_ee 0x04 to
// fall 0xC7, else the 0x801451F0 chance by moving / flags_ee to fall off 0xC6); 0x02 hits w (the
// damage, then 0x80145204 to 0xC7 or else 0xCA at step 2; or 0x801451F8 to fall off 0xC6, else 0xCA).
void turnbuckle_check(WrestlerCtx *ctx, s32 moving) {
    Wrestler *w = ctx->self;
    if (!(w->location & 0x400))
        return;
    Wrestler *o = ctx->rec_a;
    u32 k = (u32)((moving ? 2 : 0) | ((o->flags_ee & 4) ? 1 : 0));
    u32 hi = (u32)((o->flags_ee >> 1) & 2);
    if ((o->flags_f7 & 1) && w->ring_zone == o->dir) {
        o->flags_f7 &= 0xFE;
        if (w->flags_f7 & 4) {
            if (chance_check(mem_s16(0x80145200 + hi))) {
                wrestler_set_state_now(w, state::CageFallOff);
                return;
            }
        } else if (chance_check(mem_s16(0x801451F0 + 2u * k))) {
            wrestler_set_anim(w, turnbuckle_anim(w), 14);
            wrestler_set_state_now(w, state::CageHang);
            return;
        }
    }
    if (!(o->flags_f7 & 2))
        return;
    o->flags_f7 &= 0xFD;
    apply_damage(o, w);
    if (w->flags_f7 & 4) {
        if (chance_check(mem_s16(0x80145204 + hi))) {
            wrestler_set_state_now(w, state::CageFallOff);
            return;
        }
        wrestler_set_state_now(w, state::CageHangHit);
        w->state_step = 2;
        return;
    }
    if (chance_check(mem_s16(0x801451F8 + 2u * k))) {
        wrestler_set_anim(w, turnbuckle_anim(w), 14);
        wrestler_set_state_now(w, state::CageHang);
        return;
    }
    wrestler_set_state_now(w, state::CageHangHit);
}

// N64 0x800EEA14: the climb's step on the cage wall for direction `dir` (0 up, 1 down, 2 / 3 along by
// the ring side), in step_vec, and its animation: up 2.58 (3.25 in finisher mode, flags_ee 0x04) less
// 0.37 a step below 80 spirit (in 20s), down -2.05 / -2.61, along 1.65 / 2.14 or 1.77 / 2.23 by side
// and way; outside finisher mode scaled by the climbing attributes (+0x2CC / +0x2D0: their mean plus
// 50, in hundredths). The animation 12807 + dir (12818 + the spirit step going up), 12814 + dir in
// finisher mode.
s32 walk_vector(Wrestler *w, s32 dir) {
    u32 k = (w->flags_ee & 4) ? 1u : 0u;
    w->step_vec = {0.0f, 0.0f, 0.0f};
    s32 n = 0;
    switch (dir) {
    case 0: {
        f32 up = mem_f32(0x801451D0 + 4u * k);
        w->step_vec.y = up;
        if (w->flags_ee & 4)
            return 12814;
        n = (s16)w->spirit / 20;
        if (n >= 5)
            n = 4;
        w->step_vec.y = (f32)((f64)up - (f64)(4 - n) * 0.37);
        break;
    }
    case 1:
        w->step_vec.y = mem_f32(0x801451E0 + 4u * k);
        break;
    case 2:
        switch (w->ring_zone) {
        case 0: w->step_vec.x = mem_f32(0x801451D8 + 4u * k); break;
        case 1: w->step_vec.z = -mem_f32(0x801451D8 + 4u * k); break;
        case 2: w->step_vec.x = -mem_f32(0x801451E8 + 4u * k); break;
        case 3: w->step_vec.z = mem_f32(0x801451D8 + 4u * k); break;
        default: break;
        }
        break;
    case 3:
        switch (w->ring_zone) {
        case 0: w->step_vec.x = -mem_f32(0x801451E8 + 4u * k); break;
        case 1: w->step_vec.z = mem_f32(0x801451E8 + 4u * k); break;
        case 2: w->step_vec.x = mem_f32(0x801451D8 + 4u * k); break;
        case 3: w->step_vec.z = -mem_f32(0x801451E8 + 4u * k); break;
        default: break;
        }
        break;
    default:
        break;
    }
    if (w->flags_ee & 4)
        return dir + 12814;
    f32 m = (f32)(((f64)(w->region[2] + w->region[3]) * 0.5 + 50.0) * 0.01);
    w->step_vec.x = w->step_vec.x * m;
    w->step_vec.z = w->step_vec.z * m;
    w->step_vec.y = w->step_vec.y * m;
    return dir == 0 ? 12818 + n : dir + 12807;
}

// N64 0x800EE768: moving on the cage wall with the d-pad (up 0, down 1, along 2 / 3, those two
// swapped on side 2). During a climb animation (12808-12810, 12814-12822) the step and slide, the
// attacks from below (moving), and at its end the next step's animation (climb_counter reset when
// it changes); else the new step (walk_vector) with its spirit cost (5 for 12807 and 12818-12822, 3
// for 12809 / 12810). Without the d-pad the hang (12804, 2 spirit at its end). In finisher mode
// (flags_ee 0x01) the climb out over the top (12861, 0xC6). Hanging with the opponent getting down
// the wall at the same zone, the slide 6.0. Kept in bounds (1.0); returns the walls' bits.
u16 walk_move(WrestlerCtx *ctx, u16 dpad) {
    Wrestler *w = ctx->self;
    if (dpad & 0xF00) {
        s32 base = (w->flags_ee & 4) ? 12814 : 12807;
        s32 dir = 0;
        if (dpad & 0x100) dir = 2;
        if (dpad & 0x200) dir = 3;
        if (dpad & 0x800) dir = 0;
        if (dpad & 0x400) dir = 1;
        if (w->ring_zone == 2 && dir >= 2)
            dir ^= 1;
        s32 a = w->anim_id;
        bool climbing = (a >= 12808 && a < 12811) || (a >= 12814 && a < 12823);
        if (climbing) {
            wrestler_translate(w, &w->step_vec);
            wrestler_slide(w, 4.0f);
            turnbuckle_check(ctx, 1);
            if (w->anim_frames_left > 0)
                return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
            if (w->anim_id != base + dir)
                w->climb_counter = 0;
            wrestler_restart_anim(w, base + dir, 14);
        }
        wrestler_set_anim(w, walk_vector(w, dir), 14);
        wrestler_translate(w, &w->step_vec);
        wrestler_slide(w, 4.0f);
        w->climb_counter = 0;
        s32 i = (s16)(w->anim_id - 12807);
        if (i == 0 || (i >= 11 && i <= 15))
            wrestler_add_spirit(w, -5);
        else if (i == 2 || i == 3)
            wrestler_add_spirit(w, -3);
        if (w->flags_ee & 1) {
            wrestler_set_anim(w, 12861, 14);
            wrestler_set_state_now(w, state::CageHang);
        }
        return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
    }
    w->climb_counter = 0;
    if (w->anim_id != 12804) {
        wrestler_translate(w, &w->step_vec);
        wrestler_slide(w, 4.0f);
        if (!(w->anim_frames_left > 0))
            wrestler_set_anim(w, 12804, 14);
        turnbuckle_check(ctx, 1);
        return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
    }
    if (w->anim_frames_left == 1)
        wrestler_add_spirit(w, -2);
    if (w->flags_ee & 1) {
        wrestler_set_anim(w, 12861, 14);
        wrestler_set_state_now(w, state::CageHang);
        return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
    }
    const Wrestler *o = ctx->rec_a;
    if (o->state == state::DropDown && w->joint_zone == o->joint_zone)
        wrestler_slide(w, 6.0f);
    turnbuckle_check(ctx, 0);
    return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
}

// N64 0x80131E08: moves the wrestler with the d-pad. In world axes, toward the opponent at the
// fast speed and away at the normal one; diagonals by 0.7071, flag 4 by 0.125; on the apron along
// the apron line. Flag 2 also pushes off the others and picks the walk animation. Returns the
// direction bits of the walls it was kept off (keep_in_bounds).
u16 walk_step(WrestlerCtx *ctx, u16 buttons, s32 flags) {
    Wrestler *w = ctx->self;
    if (w->location & 0x400)
        return walk_move(ctx, buttons);
    if (buttons & 0xF00) {
        Vec3 step = {0.0f, 0.0f, 0.0f};
        f32 fast = walk_speed(w, 2);
        f32 normal = walk_speed(w, 3);
        if (w->location & 2) {
            if (buttons & 0x800) step.z = fast;
            if (buttons & 0x100) step.x = fast;
            if (buttons & 0x400) step.z = -fast;
            if (buttons & 0x200) step.x = -fast;
            wrestler_translate(w, &step);
            s32 along = side_input_check(w, buttons);
            wrestler_set_anim(w, along ? 11794 : 11745, 2);
            w->_unk068 = wrestler_slide(w, 4.0f);
            return keep_in_bounds_self(w, 1.0f, 0, 2);
        }
        const Wrestler *opp = ctx->rec_a;
        if (buttons & 0x800) step.z = w->point_120.z < opp->point_120.z ? fast : normal;
        if (buttons & 0x100) step.x = w->point_120.x < opp->point_120.x ? fast : normal;
        if (buttons & 0x400) step.z = opp->point_120.z < w->point_120.z ? -fast : -normal;
        if (buttons & 0x200) step.x = opp->point_120.x < w->point_120.x ? -fast : -normal;
        s32 dir = dpad_dir8(buttons);
        if (!(dir & 1)) {
            step.x = (f32)((f64)step.x * 0.707106781);
            step.z = (f32)((f64)step.z * 0.707106781);
        }
        if (flags & 4) {
            step.x = (f32)((f64)step.x * 0.125);
            step.z = (f32)((f64)step.z * 0.125);
        }
        if (w->flags8a & 0x20) {
            // Pushing an opponent on the ropes: both move, at half the step.
            if (abs_i(angle_diff(w->facing, mem_s32(0x8014D234 + 4 * dir))) < 257 && ctx->rec_a->state == state::Dazed) {
                step.x = (f32)((f64)step.x * 0.5);
                step.z = (f32)((f64)step.z * 0.5);
                wrestler_translate(w, &step);
                wrestler_translate(ctx->rec_a, &step);
                return keep_in_bounds_self(w, 1.0f, 0, 2);
            }
            w->flags8a &= 0xFFDF;
            wrestler_translate(w, &step);
            wrestler_set_anim(w, anim_pick(ctx, 0, 0), 1);
            return keep_in_bounds_self(w, 1.0f, 0, 2);
        }
        s32 angle = vec_xz_angle(step.x, step.z);
        wrestler_translate(w, &step);
        if (!(flags & 2))
            return keep_in_bounds_self(w, 1.0f, 0, 2);
        w->_unk068 = wrestler_slide(w, 4.0f);
        Wrestler *self = ctx->self;
        if (w->_unk068 == self->opponent && ctx->rec_a->state == state::Dazed && !(self->flags_ef & 1)) {
            s32 to = angle_to(&self->point_120, &ctx->rec_a->point_120);
            if (abs_i(angle_diff(ctx->self->facing, to)) < 23 &&
                abs_i(angle_diff(ctx->self->facing, mem_s32(0x8014D234 + 4 * dir))) < 257)
                ctx->self->flags8a |= 0x20;
        }
        self = ctx->self;
        s32 anim = (self->flags8a & 0x20) ? 11821 : anim_pick(ctx, 0, angle);
        wrestler_set_anim(ctx->self, anim, 3);
        self = ctx->self;
        if (!(self->flags8e & 1) || self->anim_frame == 1) {
            object_underfoot(self);
            ctx->self->flags8e |= 1;
        }
        return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
    }

    // No direction: stand, still pushed off the others.
    w->flags8a &= 0xFFDF;
    Wrestler *self = ctx->self;
    if (!((self->location & 1) && (ctx->rec_a->location & 0x400)))
        ctx->self->_unk068 = wrestler_slide(self, 4.0f);
    if (flags & 2) {
        self = ctx->self;
        if (self->location & 2)
            wrestler_set_anim(self, 11745, 2);
        else if (!walk_anim_step(self))
            wrestler_set_anim(ctx->self, anim_pick(ctx, 1, 0), 3);
    }
    return keep_in_bounds_self(ctx->self, 1.0f, 0, 2);
}

// N64 0x800EF66C: state 0xC6, hanging off the cage wall after being shaken or knocked loose. Step 0
// when the slip ends: the mash count and the escape time (kind 10), 12862. Step 1 (flags_f7 0x04, 2
// spirit at the end of each loop): in finisher mode, or once the match has ended, the fall (0xC7);
// otherwise the recovery by the presses climbs back up (12863, step 2), or with the stay-down input
// held zeroes the escape time; the opponent's attacks from below (turnbuckle_check). Step 2 the events.
void state_C6_cage_hang(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        if (w->anim_frames_left > 0)
            return;
        mash_count(w, 0, 0);
        w->escape = (s16)escape_time(w, nullptr, 10);
        wrestler_set_anim(w, 12862, 9);
        w->state_step++;
        return;
    case 1:
        w->flags_f7 |= 4;
        if (w->anim_frames_left == 1)
            wrestler_add_spirit(w, -2);
        if (!(w->flags_ee & 1)) {
            if (recover_tick(w, ctx->pressed)) {
                if (ctx->in_byte) {
                    w->escape = 0;
                } else {
                    wrestler_set_anim(w, 12863, 14);
                    w->state_step++;
                }
            }
            if (!(g_match_flags[1] & 0x40)) {
                turnbuckle_check(ctx, 0);
                return;
            }
        }
        wrestler_set_state_now(w, state::CageFallOff);
        return;
    case 2:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EF7D4: state 0xC7, falling off the cage wall: step 0 the let-go (12864); step 1 at its
// last frame the drop (step_vec down 20, 12865, flags80 0x01 only); step 2 the fall (bounds 4.0, the
// step, 3 more a frame) until point_120 is 100 up: on the floor (pos.y 90), 12866; step 3 bounds and
// the events (0x265).
void state_C7_drop_down(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12864, 9);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left != 1)
            return;
        w->step_vec = {0.0f, -20.0f, 0.0f};
        wrestler_set_anim(w, 12865, 9);
        w->flags80 = 1;
        wrestler_set_step(w, 2);
        return;
    case 2:
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        wrestler_translate(w, &w->step_vec);
        w->step_vec.y = (f32)((f64)w->step_vec.y + -3.0);
        if (!((f64)w->point_120.y <= 100.0))
            return;
        w->pos.y = 90.0f;
        wrestler_set_anim(w, 12866, 1);
        w->state_step++;
        return;
    case 3:
        keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 0x265);
        return;
    default:
        return;
    }
}

// N64 0x800EF258: state 0xC4, on top of the cage, trying to get over (0xC4 again with state_counter
// 0x01 after a first go; each set of animations is 4 on). Step 0 the climb (12869); step 1 at its last
// frame the straddle (12870) and the mash timer: 25-34 presses, 15-24 in finisher mode (flags_ee 0x04).
// Step 2 (flags_f7 0x08; once the match has ended the fall 0xC7): each press of A, B, L or R (pressed
// & 0xC030) takes one off; below 0 (no end_flags 0x1001, the opponent not on its way, 0xC5) over to
// the next state: 0xC5 from the second go, else 0xC4 anew with state_counter 0x01. Holding down with a
// press of C-up, or for 21 frames (climb_counter), climbs back down (12871, step 3). 5 spirit a loop;
// in the fall condition (flags_ee 0x01) off the wall (0xC6). Step 3 the events.
void state_C4_cage_climb(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    s32 k = (w->state_counter & 1) ? 4 : 0;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12869 + k, 14);
        w->state_step++;
        return;
    case 1: {
        if (w->anim_frames_left != 1)
            return;
        wrestler_set_anim(w, 12870 + k, 9);
        s32 base = (w->flags_ee & 4) ? 20 : 30;
        w->aux_timer = (u16)(base - 5 + rand_pool_next() % 10);
        w->climb_counter = 0;
        w->state_step++;
        return;
    }
    case 2:
        w->flags_f7 |= 8;
        if (g_match_flags[1] & 0x40) {
            wrestler_set_state_now(w, state::CageFallOff);
        } else if ((s16)w->aux_timer < 0) {
            if ((w->end_flags & 0x1001) || ctx->rec_a->state == state::CageEscape)
                return;
            if (w->state_counter & 1) {
                wrestler_set_state(w, state::CageEscape);
                return;
            }
            wrestler_set_state(w, state::CageTopClimbOver);
            w->state_counter |= 1;
            return;
        } else if (ctx->pressed & 0xC030) {
            w->aux_timer = (u16)(w->aux_timer - 1);
        }
        if ((ctx->buttons & 0x400) && (ctx->pressed & 8)) {
            wrestler_set_anim(w, 12871 + k, 14);
            w->state_step++;
            return;
        }
        if (ctx->buttons & 0x400) {
            w->climb_counter = (u16)(w->climb_counter + 1);
            if ((s16)w->climb_counter >= 21) {
                wrestler_set_anim(w, 12871 + k, 14);
                w->state_step++;
                return;
            }
        } else {
            w->climb_counter = 0;
        }
        if (w->anim_frames_left == 1)
            wrestler_add_spirit(w, -5);
        if (w->flags_ee & 1) {
            wrestler_set_anim(w, turnbuckle_anim(w), 14);
            wrestler_set_state_now(w, state::CageHang);
        }
        return;
    case 3:
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EF4E0: state 0xC5, over the top and out: the win. Step 0 12811; step 1 at its last frame the
// opponent gets end_flags 0x1000 and is paired with w, w faces its side's angle and drops (step_vec
// down 2.61, 12815); step 2 down (12815 looping) until below 218 (12806); step 3 at its end on the
// floor (location 0x04, pos.y 0, the stun 0x30).
void state_C5_cage_escape(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12811, 9);
        w->state_step++;
        return;
    case 1:
        if (w->anim_frames_left != 1)
            return;
        // sfx_play(1): the crowd
        ctx->rec_a->end_flags |= 0x1000;
        ctx->rec_a->pair_other = ctx->slot;
        w->facing = mem_s32(0x8014D1E8 + 4u * (u32)w->ring_zone);
        w->step_vec = {0.0f, -2.61f, 0.0f};
        wrestler_set_anim(w, 12815, 9);
        w->state_step++;
        return;
    case 2:
        if (w->anim_frames_left == 1) {
            wrestler_restart_anim(w, 12815, 8);
            return;
        }
        wrestler_translate(w, &w->step_vec);
        if (!((f64)w->pos.y < 218.0))
            return;
        wrestler_set_anim(w, 12806, 14);
        w->state_step++;
        return;
    case 3:
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        w->location = 4;
        w->pos.y = 0.0f;
        wrestler_set_state_now(w, state::SettleAfterAnimation);
        return;
    default:
        return;
    }
}


// N64 0x800EFDB4: state 0xCD, yanking the opponent off the cage: step 0 the slide behind it
// (pair_scripted_place) with 12850; step 1 the slide; step 2 following it (pair_follow 4.0), the events.
void state_CD_cage_yank(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        pair_scripted_place(w, ctx->rec_a);
        wrestler_set_anim(w, 12850, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (pair_slide_step(ctx->self, nullptr))
            w->state_step++;
        return;
    case 2:
        pair_follow(w, ctx->rec_a, nullptr, 4.0f, 0, 0xFFFFE);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800EFE90: state 0xCE, yanked off the cage: step 0 12851, 12 frames to a height of 308, the
// side's bounds; step 1 easing up to 308, past the player's pair frame in the ring (location 0x01,
// bounds 4.0), the events (0x266).
void state_CE_cage_yanked(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, 12851, 14);
        w->aux_timer = 12;
        w->bounds_override = (w->ring_zone & 1) ? 0x80146C30 : 0x80146C28;
        w->state_step++;
        return;
    case 1:
        if ((s16)w->aux_timer > 0) {
            w->pos.y = (f32)((f64)w->pos.y + (308.0 - (f64)w->pos.y) / (f64)(s16)w->aux_timer);
            w->aux_timer = (u16)(w->aux_timer - 1);
        }
        if (w->anim_player[AP_PAIR_FRAME] < w->anim_frame) {
            w->location = 1;
            keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
        }
        move_event(ctx, 0x266);
        return;
    default:
        return;
    }
}

}  // namespace nm

// Grapples: the attempt (state 0x50: the reach, who it catches, what that becomes), the lock-up
// (0x51 attacker / 0x52 defender: the slide into position, walking while locked, the time limit,
// the move input and the defender's reversal roll).
#include "nm_funcs.h"

namespace nm {

namespace {

enum : u32 {
    A_GRAPPLE_PAIRS = 0x80159290, // s32[4][6]: per-slot lock-up record (walk offset, distance, pair angle)
    A_DPAD_ANGLES = 0x8014D234,   // s32[9]: the world angle of each d-pad direction (dpad_dir8)
    A_SIDE_FACINGS = 0x8014D1E8,  // s32[4]: the facing along each ring side
    A_ROPE_IN = 0x801469C0,       // u16[4]: the button toward the ring by side (grapple_walk across the ropes)
    A_ROPE_OUT = 0x801469C8,      // u16[4]: the button away from it
    A_LOCKUP_MOVES = 0x801469D0,  // s16[2][2][4]: front lock-up moves by strong, B, direction
    A_LOCKUP_MOVES_REAR = 0x801469F0, // s16[2][2][2]: rear lock-up moves by strong, B, any direction
    A_CORNER_MOVES = 0x80146A00,  // s16[2][2][2]: moves on an opponent slumped in the corner by rear, strong, B
};

inline u32 pair_rec(s32 slot) { return A_GRAPPLE_PAIRS + 24u * (u32)slot; }

// N64 0x800EC7D8: the variant of a move for this defender: move_num + 1-4, the 2-bit entry for the move's
// animation number in the table of the defender's build (0x80090BA0, 720 bytes each, attrs +1).
s32 move_anim_offset(const Wrestler *w, const Wrestler *d, s32 move) {
    if (move)
        NM_UNPORTED("move_anim_offset: a move other than the current one");
    s32 v = w->move_anim2;
    s32 build = mem_s8(d->attrs + 1);
    u8 b = mem_u8(0x80090BA0 + 720u * (u32)build + (u32)(v / 4));
    return w->move_num + ((b >> (6 - 2 * (v & 3))) & 3) + 1;
}

// N64 0x800ED518: whether the defender reverses this grapple move (record flag 0x01, attrs +0x34 0x1000, not
// from a normal mode against a finisher): flags_f0 0x02 by chance_roll kind 8. Clears both flags_f0 first.
void reversal_check(Wrestler *d, Wrestler *a) {
    a->flags_f0 = 0;
    d->flags_f0 = 0;
    if ((mem_u32(a->move + 0x20) & 1) && (mem_u32(d->attrs + 0x34) & 0x1000) && ((d->flags_ee & 4) || !(a->flags_ee & 4))) {
        if (chance_roll(d, a, 8))
            d->flags_f0 = 2;
    }
}

// N64 0x80128F5C: the defender's reversal of the move just started (its flags_f0 0x04 from the
// roll in grapple_defender_input, the move reversible: record flag 0x01): in the lock-up from
// behind 162 (trait 0x1000), 85 (strong; the defender's flags78 = 4) or 84; across the ropes 148
// / 152; the front lock-up and an opponent slumped in the corner instead give the defender the move's own
// reversal animation (move_anim_offset) and no reversal. Returns 1 for a reversal.
s32 grapple_reversal_start(Wrestler *att, Wrestler *def) {
    if (!(def->flags_f0 & 4))
        return 0;
    if (!(mem_u32(att->move + 0x20) & 1))
        return 0;
    u32 f = att->flags78;
    s32 move = 0;
    if (f & 0x800) {
        if (!(f & 0x80)) {
            def->flags_f0 |= 2;
            wrestler_start_move(def, att->slot, move_anim_offset(att, def, 0));
            return 0;
        }
        if (mem_u32(def->attrs + 0x34) & 0x1000) {
            move = 162;
        } else if (f & 4) {
            def->flags78 = 4;
            move = 85;
        } else {
            move = 84;
        }
    } else if (f & 0x100) {
        def->flags_f0 |= 2;
        wrestler_start_move(def, att->slot, move_anim_offset(att, def, 0));
        return 0;
    } else {
        if (f & 0x200)
            move = 148;
        if (f & 0x400)
            move = 152;
    }
    wrestler_start_move(def, def->slot, move);
    stats_count(def, att, 1);
    return 1;
}

}  // namespace

// N64 0x800EBB28: w's per-frame slide vector from `from` to `to` over `frames` frames (the whole
// way when 0).
void slide_vector(Wrestler *w, const Vec3 *from, const Vec3 *to, s32 frames) {
    Vec3 d;
    vec_sub(from, to, &d);
    if (frames != 0) {
        f32 n = (f32)frames;
        d.x = d.x / n;
        d.y = d.y / n;
        d.z = d.z / n;
    }
    w->step_vec = d;
}

// N64 0x80128328: the lock-up's time limit: 90 frames against a finisher-mode opponent, 210 for
// a strong (flags78 0x04) lock-up, else 150.
s32 lockup_time_limit(const Wrestler *w, const Wrestler *other) {
    if (other->flags_ee & 4)
        return 90;
    return (w->flags78 & 4) ? 210 : 150;
}

// N64 0x8012C974: keep_in_bounds for a pair: w against `other` (and `other2`), scale, joints.
u16 keep_in_bounds_pair(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, u32 joints) {
    return keep_in_bounds(w, other, other2, scale, nullptr, nullptr, nullptr, 0, joints, 1);
}

// N64 0x8012C9B0: keep_in_bounds for a wrestler following its pair: `until` is w's animation
// player.
u16 pair_follow(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, u16 mask, u32 joints) {
    return keep_in_bounds(w, other, other2, scale, w->anim_player, nullptr, nullptr, mask, joints, 1);
}

// N64 0x80127DCC: the attacker's lock-up pose and place (flags78: 0x800 the lock-up, 11905 /
// 11909 from behind, at 73.5 / 53.5; 0x100 an opponent slumped in the corner (0x18), 11913 / 11917 at 72.3 / 30 along
// its facing; 0x200 across the ropes, 11921 at 76 along the side; 0x400 from the apron, 11925;
// 0x200000 the clash 11937): the facing to turn to, the slide into place over 6 frames
// (slide_vector toward the point that far from the opponent, the position snapped to the body),
// and the turn per frame. Starts the animation (mode 2) and clears count_timer.
void grapple_anim_defender(Wrestler *w, Wrestler *other) {
    w->lockup_facing = angle_to(&w->point_120, &other->point_120);
    w->slide_frames = 6;
    Vec3 off = {0.0f, 0.0f, 0.0f};
    s32 anim = 0;
    u32 f = w->flags78;
    if (f & 0x800) {
        if (f & 0x80) {
            anim = 11909;
            off.z = 53.5f;
        } else {
            anim = 11905;
            off.z = 73.5f;
        }
    }
    if (f & 0x100) {
        if (f & 0x80) {
            anim = 11917;
            w->lockup_facing = other->facing;
            off.z = 30.0f;
        } else {
            anim = 11913;
            w->lockup_facing = angle_add(other->facing, 2048);
            off.z = 72.3f;
        }
    }
    if (f & 0x200) {
        w->dir = (s16)other->ring_zone;
        other->dir = (s16)other->ring_zone;
        w->lockup_facing = mem_s32(A_SIDE_FACINGS + 4u * (u32)other->ring_zone);
        anim = 11921;
        off.z = 76.0f;
    }
    if (f & 0x400) {
        w->dir = (s16)w->ring_zone;
        other->dir = (s16)w->ring_zone;
        w->lockup_facing = w->facing;
        anim = 11925;
    }
    if (f & 0x200000)
        anim = 11937;
    if (f & 0x400) {
        w->step_vec.x = 0.0f;
        w->step_vec.y = 0.0f;
        w->step_vec.z = 0.0f;
    } else {
        Vec3 p;
        if (f & 0x200) {
            Vec3 base;
            apron_line_pos(other, &base);
            point_at_offset(&base, &off, angle_add(w->lockup_facing, 2048), &p);
        } else {
            point_at_offset(&other->point_120, &off, angle_add(w->lockup_facing, 2048), &p);
        }
        p.y = w->point_120.y;
        slide_vector(w, &w->point_120, &p, w->slide_frames);
        w->pos.x = w->point_120.x;
        w->pos.z = w->point_120.z;
    }
    w->turn_step = angle_diff(w->lockup_facing, w->facing) / w->slide_frames;
    wrestler_set_anim(w, anim, 2);
    w->count_timer = 0;
}

// N64 0x80128014: the defender's lock-up pose from the attacker's flags78: 11906 / 11910 (the
// lock-up), 11914 / 11918 (slumped in the corner), 11922 (across the ropes, placed on the apron line), 11926
// (from the apron: slides to 82.5 from the attacker), the attacker's animation + 1 for the clash;
// faces the attacker (or along it from behind); the position snaps to the body for 0xC80.
void grapple_defender_anim(Wrestler *w, Wrestler *att) {
    w->slide_frames = 6;
    u32 f = att->flags78;
    s32 anim = 0;
    if (f & 0x800) {
        if (f & 0x80) {
            anim = 11910;
            w->lockup_facing = att->lockup_facing;
        } else {
            anim = 11906;
            w->lockup_facing = angle_add(att->lockup_facing, 2048);
        }
    }
    if (f & 0x100) {
        if (f & 0x80) {
            anim = 11918;
            w->lockup_facing = att->lockup_facing;
        } else {
            anim = 11914;
            w->lockup_facing = angle_add(att->lockup_facing, 2048);
        }
    }
    if (f & 0x200) {
        anim = 11922;
        w->lockup_facing = angle_add(att->lockup_facing, 2048);
        apron_line_pos(w, &w->pos);
    }
    if (f & 0x400) {
        w->lockup_facing = angle_add(att->lockup_facing, 2048);
        Vec3 off = {0.0f, 0.0f, 82.5f};
        Vec3 p;
        point_at_offset(&att->pos, &off, angle_add(w->lockup_facing, 2048), &p);
        p.y = w->point_120.y;
        anim = 11926;
        slide_vector(w, &w->point_120, &p, w->slide_frames);
    }
    if (f & 0xC80) {
        w->pos.x = w->point_120.x;
        w->pos.z = w->point_120.z;
    }
    if (f & 0x200000)
        anim = att->anim_id + 1;
    w->turn_step = angle_diff(w->lockup_facing, w->facing) / w->slide_frames;
    wrestler_set_anim(w, anim, 2);
}

// N64 0x801281FC: the attacker's slide into the lock-up: while count_timer is below the slide
// frames it moves by step_vec, turns by turn_step and (in a lock-up) is kept in bounds with the
// pair at scale 16; done, it faces lockup_facing and count_timer becomes the time limit. Returns 1
// when done.
s32 lockup_slide_in(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (!((s16)w->count_timer < w->slide_frames)) {
        w->facing = w->lockup_facing;
        w->count_timer = (u16)lockup_time_limit(w, ctx->rec_a);
        return 1;
    }
    wrestler_translate(w, &w->step_vec);
    w->facing = angle_add(w->facing, w->turn_step);
    if (w->flags78 & 0x800)
        keep_in_bounds_pair(w, ctx->rec_a, nullptr, 16.0f, 0xFFFFE);
    w->count_timer = (u16)(w->count_timer + 1);
    return 0;
}

// N64 0x801282BC: the defender's slide: past the slide frames it faces lockup_facing (1); else it
// moves by step_vec only from the apron (the attacker's flags78 0x400) and turns by turn_step.
s32 lockup_slide_in_defender(Wrestler *w, const Wrestler *att) {
    if (w->slide_frames < (s16)w->count_timer) {
        w->facing = w->lockup_facing;
        return 1;
    }
    if (att->flags78 & 0x400)
        wrestler_translate(w, &w->step_vec);
    w->facing = angle_add(w->facing, w->turn_step);
    return 0;
}

// N64 0x8012835C: moving while locked up (state 0x51 step 2). The d-pad held 7 frames
// (aux_timer) switches walking on (state_counter 0x02). In a lock-up (flags78 0x800): a direction
// within 20 degrees of the facing (or past 160 from it) walks both 2 units along the facing
// (forward or back); another direction turns both 45 units a frame, sliding the opponent round
// the pair angle; the attacker is placed at the pair distance from the opponent first. Across the
// ropes (0x600): the buttons toward / away from the ring move both 2 units across. Both kept in
// bounds at scale 4; dpad_held keeps the result.
void grapple_walk(WrestlerCtx *ctx, u32 pair) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (!(w->state_counter & 2)) {
        if (ctx->buttons & 0xF00) {
            w->aux_timer = (u16)(w->aux_timer + 1);
            if (!((s16)w->aux_timer < 7))
                w->state_counter |= 2;
        } else {
            w->aux_timer = 0;
        }
        if (!(w->state_counter & 2))
            return;
    }
    u32 f = w->flags78;
    if (f & 0x800) {
        f32 along = 0.0f;
        s32 dir = dpad_dir8(ctx->buttons);
        if (dir != 0) {
            s32 d = angle_diff(w->facing, mem_s32(A_DPAD_ANGLES + 4u * (u32)dir));
            if (abs_i(d) < 228 || !(abs_i(d) < 1821)) {
                along = abs_i(d) < 1024 ? 2.0f : -2.0f;
            } else {
                s32 s = d >= 0 ? 45 : -45;
                w->facing = angle_add(w->facing, s);
                o->facing = angle_add(o->facing, s);
                mem_w32(pair + 16, (u32)angle_add(mem_s32(pair + 16), s));
            }
        }
        Vec3 off = {mem_f32(pair), mem_f32(pair + 4), mem_f32(pair + 8)};
        Vec3 p, d;
        point_at_offset(&o->pos, &off, mem_s32(pair + 16), &p);
        p.y = w->pos.y;
        vec_sub(&w->pos, &p, &d);
        wrestler_translate(w, &d);
        vec_from_angle(w->facing, along, &d);
        wrestler_translate(w, &d);
        wrestler_translate(o, &d);
        w->dpad_held = keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
        return;
    }
    if (!(f & 0x600))
        return;
    u32 zone = (u32)((f & 0x200) ? o->ring_zone : w->ring_zone);
    Vec3 d = {0.0f, 0.0f, 0.0f};
    if (mem_u16(A_ROPE_IN + 2 * zone) & ctx->buttons) {
        switch (zone) {
        case 0: d.x = 2.0f; break;
        case 1: d.z = -2.0f; break;
        case 2: d.x = -2.0f; break;
        case 3: d.z = 2.0f; break;
        default: break;
        }
    }
    if (mem_u16(A_ROPE_OUT + 2 * zone) & ctx->buttons) {
        switch (zone) {
        case 0: d.x = -2.0f; break;
        case 1: d.z = 2.0f; break;
        case 2: d.x = 2.0f; break;
        case 3: d.z = -2.0f; break;
        default: break;
        }
    }
    wrestler_translate(w, &d);
    wrestler_translate(o, &d);
    if (f & 0x200)
        keep_in_bounds_pair(o, w, nullptr, 4.0f, 0xFFFFE);
    else
        keep_in_bounds_pair(w, o, nullptr, 4.0f, 0xFFFFE);
}

// N64 0x80128770: the lock-up's clock: a pause (match_can_interrupt) or R alone held ends it at
// once; count_timer counts down and below 0 the lock-up breaks: the break animation by the
// kind (11907 / 11911, 11915 / 11919, 11923, 11927; mode 1) and 2. Else the stick (in_byte) in
// finisher mode sets flags78 0x400000 and returns 1; else 1 when A, B, L or a C button was pressed.
s32 grapple_timer(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (match_can_interrupt())
        w->count_timer = 0;
    if ((ctx->in_0c & 0xC030) == 0x10)
        w->count_timer = 0;
    w->count_timer = (u16)(w->count_timer - 1);
    if ((s16)w->count_timer < 0) {
        u32 f = w->flags78;
        s32 anim = 0;
        if (f & 0x800)
            anim = (f & 0x80) ? 11911 : 11907;
        if (f & 0x100)
            anim = (f & 0x80) ? 11919 : 11915;
        if (f & 0x200)
            anim = 11923;
        if (f & 0x400)
            anim = 11927;
        wrestler_set_anim(w, anim, 1);
        return 2;
    }
    if (ctx->in_byte != 0 && (w->flags78 & 1)) {
        w->flags78 |= 0x400000;
        return 1;
    }
    return (ctx->pressed & 0xC02D) != 0;
}

// N64 0x8012906C: the defender's reversal roll in the lock-up: with A / B / L / R pressed (always
// for trait 0x1000), once (flags_f0 0x08), not against a finisher-mode attacker unless in finisher
// mode itself: chance_roll kind 1 gives flags_f0 0x04 (a CPU-limited wrestler, flags_e9 0x01,
// only with the trait).
void grapple_defender_input(Wrestler *d, Wrestler *att, u16 pressed) {
    if (!(d->flags_ee & 4) && (att->flags_ee & 4))
        return;
    if (d->flags_f0 & 8)
        return;
    if (mem_u32(d->attrs + 0x34) & 0x1000)
        pressed = 0xC030;
    if (!(pressed & 0xC030))
        return;
    if (chance_roll(d, att, 1)) {
        if (!(d->flags_e9 & 1) || (mem_u32(d->attrs + 0x34) & 0x1000))
            d->flags_f0 |= 4;
    }
    d->flags_f0 |= 8;
}

// N64 0x8011C5AC: C-down / C-up in the lock-up: aims the whip (whip_direction) and checks the arena
// zone in front (zone_info) for a ring edge, the announce table, the steps or a table, starting the
// matching place move (0x0D / 0x0E, 0x0F / 0x10) and returning 1 / 2 for the caller's choice. Not in
// the cage match. The whip's line (100 along the d-pad's angle) must hit a wall the wrestler faces
// (within 135 degrees of its normal); a place wall (+0x28 0x80) picks the move and state_counter bit
// by id: the ring edge 1-4 (0x01, not in the ring under rule 0x400), the crowd 29-54 (0x02), 25-27
// (0x04), +0x26 0x20 (0x10), the announce table 166 (0x08), 0x40 (0x20), the stage 156-159 (0x40), 0x80
// (0x80), 196-199 the tables still standing (0x100), 0x1000 (0x200); 0x0D / 0x0E or 0x0F / 0x10 by kind.
s32 lockup_place_move(WrestlerCtx *ctx) {
    if (g_match_type & 0x10000)
        return 0;
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    Vec3 v, f;
    vec_from_angle(mem_s32(0x8014D238 + 4u * (u32)whip_direction(ctx)), 100.0f, &v);
    if (!move_hits_segment(w, &v))
        return 0;
    vec_from_angle(w->facing, 1.0f, &f);
    {
        u32 z = w->zone_info;
        Vec3 n = {mem_f32(z + 24), mem_f32(z + 28), mem_f32(z + 32)};
        if (!(-0.707106781 < (f64)vec_normalize_two(&f, &n)))
            return 0;
    }
    u32 z = w->zone_info;
    if (mem_u16(z + 0x28) & 0x80) {
        s16 id = mem_s16(z + 0x24);
        u16 d = mem_u16(z + 0x26);
        u8 tables = mem_u8(0x8016B928);
        s32 a = 0, bit = 0;
        if ((u16)(id - 1) < 4 && (!(w->location & 1) || !(g_match_rules & 0x400))) {
            a = 13; bit = 1;
        } else if ((u16)(id - 29) < 26) {
            a = 15; bit = 2;
        } else if ((u16)(id - 25) < 3) {
            a = 15; bit = 4;
        } else if (d & 0x20) {
            a = 13; bit = 0x10;
        } else if (id == 166) {
            a = 15; bit = 8;
        } else if (d & 0x40) {
            a = 13; bit = 0x20;
        } else if ((u16)(id - 156) < 4) {
            a = 15; bit = 0x40;
        } else if (d & 0x80) {
            a = 13; bit = 0x80;
        } else if ((u16)(id - 196) < 4 &&
                   ((id == 197 && !(tables & 0x20)) || (id == 199 && !(tables & 0x40)) || id == 0xC4 || id == 0xC6)) {
            a = 15; bit = 0x100;
        } else if (d & 0x1000) {
            a = 13; bit = 0x200;
        }
        if (a) {
            wrestler_pair_set_state_move(w, a, o, a + 1);
            w->state_counter |= (u16)bit;
            return 1;
        }
    }
    // A cage door (+0x28 0x200) the opponent would be whipped through: the drag (2) for the legal
    // man outside type 0x100000 and rule 0x200000.
    vec_scale(&v, 2.0f, &v);
    if (move_hits_segment(o, &v) && (mem_u16(o->zone_info + 0x28) & 0x200))
        w->zone_info = o->zone_info;
    if (!(mem_u16(w->zone_info + 0x28) & 0x200))
        return 0;
    if (!(o->flags_e9 & 0x10) || (g_match_type & 0x100000) || (g_match_rules & 0x200000))
        return 0;
    return 2;
}

// N64 0x800EDACC: whether a special move may start; attrs +0x3C is the set of specials the wrestler
// has. 94 / 95: bit 0x04, finisher mode, the opponent with flags80 0x20 and flags86 0x220 in the same
// place; 147 / 151 (across the ropes): flags78 0x200 / 0x400, bit 0x80 / 0x100, and a strong
// one-handed grapple (flags78 0x05 both); 257 / 275: bit 0x200 / 0x800, finisher mode, the opponent
// not down (flags80 0x08); 262: bit 0x400, finisher mode, the opponent down. Anything else: no. The
// opponent is not read for 147 / 151 (lockup_input passes none).
s32 move_allowed(const Wrestler *w, const Wrestler *opp, s32 move) {
    u16 specials = mem_u16(w->attrs + 0x3C);
    switch (move) {
    case 94:
    case 95:
        if (!(specials & 4) || !(w->flags_ee & 4) || !(opp->flags80 & 0x20) || !(opp->flags86 & 0x220))
            return 0;
        return (w->location & opp->location & 5) ? 1 : 0;
    case 147:
    case 151: {
        u32 f = w->flags78;
        if (!(f & (move == 147 ? 0x200u : 0x400u)))
            return 0;
        if (!(specials & (move == 147 ? 0x80 : 0x100)))
            return 0;
        return (f & 5) == 5;
    }
    case 257:
    case 275:
        if (!(specials & (move == 257 ? 0x200 : 0x800)) || !(w->flags_ee & 4))
            return 0;
        return !(opp->flags80 & 8);
    case 262:
        if (!(specials & 0x400) || !(w->flags_ee & 4))
            return 0;
        return (opp->flags80 & 8) ? 1 : 0;
    default:
        return 0;
    }
}

// The finisher (A+B in a strong grapple with finisher mode ready, flags78 0x400000) on an opponent
// slumped in the corner: 128 front, 138 rear (0x80 | (rear ? 0x0A : 0)).
static s32 corner_finisher(u32 f) { return (f & 0x80) ? 138 : 128; }

// N64 0x8012888C: the lock-up's move input (state 0x51 step 4): returns 0 with a move started
// (then 0x55 / 0x56), 1 with both states set here, 2 for nothing (back to step 2), 3 for a reversal
// (the defender's roll), 4 for L (the walk round, step 10). By the kind of lock-up (flags78):
// - running into an opponent on the apron (0x240): move 145, and a move with record flags 0x1800
//   from the apron (location 0x02) goes to the pair 0x59 / 0x5A;
// - running into an opponent slumped in the corner (0x140): move 108, rear 130;
// - across the ropes (0x600, one of them on the apron): with finisher mode ready (0x400000) the
//   apron special 147 (defender on the apron, 0x200) or 151 (attacker on the apron) when
//   move_allowed; A / B: 145 / 146 (defender on the apron) or 149 / 150 by strong (0x04), 0x59 / 0x5A
//   as above; C-down: the pair 0x11 / 0x12 (defender on the apron) or 0x13 / 0x14; else as below;
// - an opponent slumped in the corner (0x100, its frame_flags 0x02 / 0x04 from state 0x18): the finisher 128 / 138 when ready (0x400000 and strong); A+B+L+R
//   in a strong one-handed grapple (0x01) in a tag match on the partner (a double team); A+B with
//   0x01 strong at an opponent without flags_e9 0x01 on the opponent's own slot; L from the front
//   flips it into the corner (0x3E / 0x3F: hung upside down); A / B: 0x80146A00 [rear][strong][B]
//   (108 / 113 / 118 / 123 front, 130 / 132 / 134 / 136 rear);
// then for every kind: C-down (not across the ropes or from behind, 0x680) lockup_place_move into
// the whip 0x0B / 0x0C or (its result 2) the drag 0xD0 / 0xD1; C-up (not 0x700 / rear) a place move
// when lockup_place_move returns 1; C-right in a cage match (not rear or at the corner) the cage whip
// (flags78 0x10000000, 0x0B / 0x0C); then the normal standing lock-up (0x800): the finisher 70 /
// 81 when ready, the double-team finisher, A / B with the d-pad from the tables by strong (0x04), B
// and direction (rear: any direction), the trait 0x1000 turning a non-0x2000 move into 159 / 161;
// finally, from behind, C-up / C-down the rear lift 0x81 / 0x82 or 0x7F / 0x80.
// The running, rope, corner, place and cage paths are read from the code and not trace-checked.
s32 lockup_input(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    u32 f = w->flags78;
    if ((f & 0x240) == 0x240) {
        wrestler_start_move(w, ctx->slot, 145);
        if (grapple_reversal_start(w, o))
            return 3;
        if (!(mem_u32(w->move + 0x20) & 0x1800))
            return 0;
        if (!(w->location & 2))
            return 0;
        wrestler_pair_set_state_move(w, state::MoveFollowUpWalkAttacker, o, state::MoveFollowUpDefender);
        return 1;
    }
    if ((f & 0x140) == 0x140) {
        wrestler_start_move(w, ctx->slot, (f & 0x80) ? 130 : 108);
        return grapple_reversal_start(w, o) ? 3 : 0;
    }
    if (f & 0x600) {
        if (f & 0x400000) {
            s32 special = (f & 0x200) ? 0x93 : 0x97;
            if (move_allowed(w, nullptr, special)) {
                wrestler_start_move(w, ctx->slot, special);
                return 0;
            }
        }
        u16 in = ctx->in_0c;
        if (in & 0xC000) {
            u32 g = w->flags78;
            s32 move;
            if (g & 0x200)
                move = (g & 4) ? 146 : 145;
            else
                move = (g & 4) ? 150 : 149;
            wrestler_start_move(w, ctx->slot, move);
            if (grapple_reversal_start(w, o))
                return 3;
            if (!(mem_u32(w->move + 0x20) & 0x1800))
                return 0;
            if (!(w->location & 2))
                return 0;
            wrestler_pair_set_state_move(w, state::MoveFollowUpWalkAttacker, o, state::MoveFollowUpDefender);
            return 1;
        }
        if (in & 4) {
            if (w->flags78 & 0x200)
                wrestler_pair_set_state_move(w, state::RopesLockupPushFromRingAttacker, o, state::RopesLockupPushFromRingDefender);
            else
                wrestler_pair_set_state_move(w, state::RopesLockupPushFromApronAttacker, o, state::RopesLockupPushFromApronDefender);
            return 1;
        }
    }
    f = w->flags78;
    if (f & 0x100) {
        if ((f & 0x400000) && (f & 4)) {
            wrestler_start_move(w, ctx->slot, corner_finisher(f));
            return 0;
        }
        if ((f & 1) && (ctx->in_0c & 0xC030) == 0xC030 && (f & 4) && (g_match_type & 0x2000) &&
            w->partner != w->slot) {
            wrestler_start_move(w, w->partner, corner_finisher(f));
            return 0;
        }
        f = w->flags78;
        if ((f & 4) && (f & 1) && (ctx->in_0c & 0xC000) == 0xC000 && !(o->flags_e9 & 1)) {
            wrestler_start_move(w, ctx->slot_a, corner_finisher(f));
            return 0;
        }
        if ((ctx->in_0c & 0x20) && !(w->flags78 & 0x80)) {
            wrestler_pair_set_state_move(w, state::LockupCornerHangFlipAttacker, o, state::LockupCornerHangFlipDefender);
            return 1;
        }
        u16 in = ctx->in_0c;
        if (in & 0xC000) {
            u32 g = w->flags78;
            u32 is_b = (in & 0x8000) ? 0 : 1;
            s32 move = mem_s16(A_CORNER_MOVES + ((g >> 4) & 8) + (g & 4) + 2 * is_b);
            wrestler_start_move(w, ctx->slot, move);
            return grapple_reversal_start(w, o) ? 3 : 0;
        }
    }
    if ((ctx->in_0c & 4) && !(w->flags78 & 0x680)) {
        if (lockup_place_move(ctx) == 2)
            wrestler_pair_set_state_move(w, state::DragAttacker, o, state::DragDefender);
        else
            wrestler_pair_set_state_move(w, state::IrishWhipAttacker, o, state::IrishWhipRunner);
        return 1;
    }
    if ((ctx->in_0c & 8) && !(w->flags78 & 0x700) && !(w->flags78 & 0x80)) {
        if (lockup_place_move(ctx) == 1)
            return 1;
    }
    if ((ctx->in_0c & 1) && (g_match_type & 0x10000) && !(w->flags78 & 0x180)) {
        w->flags78 |= 0x10000000;
        wrestler_pair_set_state_move(w, state::IrishWhipAttacker, o, state::IrishWhipRunner);
        return 1;
    }
    f = w->flags78;
    if (!(f & 0x800))
        goto no_lockup;
    if ((f & 0x400000) && (f & 4)) {
        wrestler_start_move(w, ctx->slot, (f & 0x80) ? 81 : 70);
        return 0;
    }
    if (f & 1) {
        if ((ctx->in_0c & 0xC030) == 0xC030 && (f & 4) && (g_match_type & 0x2000) && w->partner != w->slot) {
            wrestler_start_move(w, w->partner, (f & 0x80) ? 81 : 70);
            return 0;
        }
        if ((ctx->in_0c & 0xC000) == 0xC000 && (f & 4) && !(o->flags_e9 & 1)) {
            wrestler_start_move(w, ctx->slot_a, (f & 0x80) ? 81 : 70);
            return 0;
        }
    }
    if (!(w->state_counter & 8) && (ctx->in_0c & 0x20))
        return 4;
    if (ctx->in_0c & 0xC000) {
        s32 move;
        u32 is_b = (ctx->in_0c & 0x8000) ? 0 : 1;
        if (f & 0x80) {
            u32 any = (ctx->buttons & 0xF00) ? 1 : 0;
            move = mem_s16(A_LOCKUP_MOVES_REAR + ((f << 1) & 8) + 4 * is_b + 2 * any);
        } else {
            u32 strong = (f & 4) ? 1 : 0;
            u32 dir;
            if (ctx->buttons & 0x800)
                dir = 2;
            else if (ctx->buttons & 0x400)
                dir = 3;
            else
                dir = (ctx->buttons & 0x300) ? 1 : 0;
            move = mem_s16(A_LOCKUP_MOVES + 16 * strong + 8 * is_b + 2 * dir);
        }
        wrestler_start_move(w, ctx->slot, move);
        if ((mem_u32(o->attrs + 0x34) & 0x1000) && !(mem_u32(w->move + 0x20) & 0x2000))
            wrestler_start_move(w, ctx->slot, (w->flags78 & 0x80) ? 161 : 159);
        return grapple_reversal_start(w, o) ? 3 : 0;
    }
no_lockup:
    if (!(w->flags78 & 0x80))
        return 2;
    if (ctx->in_0c & 8) {
        wrestler_pair_set_state_move(w, state::CarryAttacker, o, state::CarryDefender);
        return 1;
    }
    if (ctx->in_0c & 4) {
        wrestler_pair_set_state_move(w, state::RearLiftAttacker, o, state::RearLiftDefender);
        return 1;
    }
    return 2;
}

// N64 0x8012712C: a double-team grapple: when the defender is locked up (state 82, its attacker's
// flags78 0x800, within 11 frames) in the same place as both, its opponent starts it.
s32 grapple_double_team_check(Wrestler *att, Wrestler *def) {
    if ((def->location & g_wrestlers[def->opponent].location & att->location & 0x3C5) && def->state == state::LockupDefender &&
        (def->flags78 & 0x800) && (s16)def->count_timer < 11)
        NM_UNPORTED("grapple_double_team_check: double_team_grapple_start");
    return 0;
}

// N64 0x801260A4: the double-team grab of an opponent slumped (flags80 0x20, no weapon): when w and
// another wrestler both target it from its place, in the attempt (0x50 step 2), facing it (1537+
// apart) and within 180 of w, the one with the longer aux_timer leads: both get flags78 0x1008 and a
// side (0x8000 / 0x10000 by their facings), are linked (slot_d), take move 96 (both on the leader's
// slot) with the side's animation, and go to step 4.
s32 grapple_attempt_double(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    const Wrestler *a = ctx->rec_a;
    if (!(a->flags80 & 0x20) || (a->flags_ef & 1))
        return 0;
    s32 me = -1, other = 0, n = 0;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *c = &g_wrestlers[i];
        if (i == a->slot || c->opponent != a->slot || !(c->location & a->location & 0x3C5))
            continue;
        if (c->state != state::GrappleAttempt || c->state_step != 2)
            continue;
        if (abs_i(angle_diff(a->facing, c->facing)) < 1537)
            continue;
        if (!((f64)dist_to(&w->pos, &c->pos) < 180.0))
            continue;
        n++;
        if (i == w->slot)
            me = i;
        else
            other = i;
    }
    if (me == -1 || n < 2)
        return 0;
    Wrestler *o = &g_wrestlers[other];
    Wrestler *lead = w, *second = o;
    if ((s16)w->aux_timer < (s16)o->aux_timer) {
        lead = o;
        second = w;
    }
    lead->flags78 |= 0x1008;
    second->flags78 |= 0x1008;
    if (angle_diff(lead->facing, second->facing) < 0) {
        lead->flags78 |= 0x8000;
        second->flags78 |= 0x10000;
    } else {
        lead->flags78 |= 0x10000;
        second->flags78 |= 0x8000;
    }
    lead->slot_d = second->slot;
    wrestler_start_move(lead, lead->slot, 96);
    wrestler_set_anim(lead, anim_style(lead, (lead->flags78 & 0x8000) ? 1 : 0), 3);
    lead->state_step = 4;
    second->slot_d = lead->slot;
    wrestler_start_move(second, lead->slot, 96);
    wrestler_set_anim(second, anim_style(second, (second->flags78 & 0x8000) ? 1 : 0), 3);
    second->state_step = 4;
    return 1;
}

// N64 0x80125F5C: a grapple from the floor at someone up on the apron or a raised place: 1 when the
// wall 80 ahead (move_hits_segment) is the one between them (+0x26 0x10 for the ring; a table's zone ids).
s32 grapple_attempt_slide(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (!(w->location & 4))
        return 0;
    Vec3 d;
    vec_from_angle(w->facing, 80.0f, &d);
    if (!move_hits_segment(w, &d))
        return 0;
    u16 loc = ctx->rec_a->location;
    u16 id = mem_u16(w->zone_info + 0x24);
    if ((loc & 1) && (mem_u16(w->zone_info + 0x26) & 0x10))
        return 1;
    if ((loc & 0x40) && (u16)(id - 25) < 3)
        return 1;
    if ((loc & 0x80) && id == 166)
        return 1;
    if ((loc & 0x100) && (u16)(id - 156) < 4)
        return 1;
    if ((loc & 0x200) && (u16)(id - 196) < 4)
        return 1;
    return 0;
}

// N64 0x80126EEC: what the attempt that caught `def` becomes, by where both stand: on a ladder
// 180 / 181; from the apron 127 / 128 (a slumped defender on its side, or from behind); floor to
// apron 156 / 157 (flags78 0x20000), floor to the ring 134 / 135 (0x40000); the cage wall 205 /
// 206; else the apron sides mark flags78 0x400 / 0x200, a defender slumped in the corner 0x100 (dir copied),
// then the running grapple 160 / 161 (0x40) or the lock-up 81 / 82 (0x800; the defender's flags78
// copied without 0x04, its flags80 0x20 cleared).
void grapple_start_pair(Wrestler *att, Wrestler *def) {
    if (def->location & 8) {
        wrestler_pair_set_state_move(att, state::LadderPulledOff, def, state::LadderFallOffAlternate);
        return;
    }
    if (att->location & 2) {
        s32 apron = 0;
        if (def->frame_flags & 6)
            apron = !(def->flags8a & 1);
        else if (att->flags78 & 0x80)
            apron = 1;
        if (apron) {
            wrestler_pair_set_state_move(att, state::RearLiftAttacker, def, state::RearLiftDefender);
            def->flags78 = att->flags78 & 0xFFFFFFFBu;
            return;
        }
    }
    if (att->location & 4) {
        if (def->location & 2) {
            att->flags78 |= 0x20000;
            wrestler_pair_set_state_move(att, state::GrappleFromFloorToApronAttacker, def, state::GrappleFromFloorToApronDefender);
            def->flags78 = att->flags78;
            return;
        }
        if (def->location & 0x3C1) {
            att->flags78 |= 0x40000;
            wrestler_pair_set_state_move(att, state::GrappleFromFloorAttacker, def, state::GrappleFromFloorDefender);
            def->flags78 = att->flags78;
            return;
        }
    }
    if ((att->location & 1) && (def->location & 0x400)) {
        wrestler_pair_set_state_move(att, state::CageYankAttacker, def, state::CageYankDefender);
        return;
    }
    if (att->location & 2)
        att->flags78 |= 0x400;
    if (def->location & 2)
        att->flags78 |= 0x200;
    u32 f = att->flags78;
    if (!(f & 0x400) && (def->frame_flags & 6)) {
        att->flags78 = f | 0x100;
        att->dir = def->dir;
    }
    f = att->flags78;
    att->flags78 = f & 0xFFFFF7FFu;
    s32 lockup;
    if (f & 0x708) {
        lockup = 1;
    } else {
        att->flags78 = (f & 0xFFFFF7FFu) | 0x800;
        lockup = !(f & 0x40);
    }
    if (lockup) {
        wrestler_pair_set_state_move(att, state::LockupAttacker, def, state::LockupDefender);
        def->flags78 = att->flags78 & 0xFFFFFFFBu;
        def->flags80 &= 0xFFDF;
    } else {
        wrestler_pair_set_state_move(att, state::RunningGrappleAttacker, def, state::RunningGrappleDefender);
    }
}

// N64 0x801269DC: what an attempt that reached `def` does. From behind (relative_position 0x08)
// flags78 0x80. A defender swinging a weapon (27) loses 10 spirit. No grapple on a lying wrestler,
// one held without flags80 0x200, or one staying down; it drops its weapon. A defender in the air
// is caught (pair 136 / 137) when the attacker is in the ring and not running. Not from a grab
// (0x08): the L evade 107 within 4 frames deflects (108 / 109, or 91 / 92 with move 90 against a
// running grapple), as does the timed R press (kind 3); two attempts meeting (def in 0x50 step 4
// before frame 3) clash with 0x200000; the timed A press (kind 4) joins with 0x200000; a defender
// in 52 with frame_flags 0x08 in the ring gets 64 / 65; 129 and a locked-up defender refuse; the
// double team may take over; else grapple_start_pair. A grab (0x08): the move pair 91 / 92, or the
// three-way 131 / 132 / 133 with a second attacker (0x1000). Returns 1 when the attempt is done
// with (the pair set).
s32 grapple_attempt_resolve(Wrestler *att, Wrestler *def) {
    if (relative_position(att, def) & 8)
        att->flags78 |= 0x80;
    if (def->state == state::Taunt)
        wrestler_add_spirit(def, -10);
    u16 f80 = def->flags80;
    if ((f80 & 8) || ((f80 & 0xC0) && !(f80 & 0x200)) || (def->stay_flags & 5))
        return 0;
    weapon_release(def, 0);
    if ((def->act_flags & 0xF) || (def->state == state::ApronPartnerWaiting && def->state_step == 3) ||
        (!(def->flags80 & 1) && (u32)(def->state - state::TurnbuckleDiveMoves256And257) < 5)) {
        if ((att->location & 1) && !(att->flags78 & 0x40)) {
            att->flags78 = (def->act_flags & 2) ? 0x8000000 : 0;
            wrestler_pair_set_state_move(att, state::CatchFlyerAttacker, def, state::CatchFlyerDefender);
            return 1;
        }
        return 0;
    }
    u32 f = att->flags78;
    if (f & 8) {
        if (f & 0x80)
            return 0;
        if (!(f & 0x1000)) {
            wrestler_pair_set_state_move(att, state::CatchWhippedRunnerAttacker, def, state::CatchWhippedRunnerDefender);
            return 0;
        }
        Wrestler *third = &g_wrestlers[att->slot_d];
        if (third->state == state::GrappleAttempt && third->state_step == 4) {
            wrestler_set_state_free(att, state::DoubleTeamFirstAttacker);
            wrestler_set_state_free(third, state::DoubleTeamPartner);
            wrestler_set_state_free(def, state::DoubleTeamVictim);
            att->flags80 |= 1;
            third->flags80 |= 1;
            def->flags80 |= 1;
            att->flags88 |= 6;
            third->flags88 |= 2;
            def->flags88 |= 2;
            att->pair_slot = third->slot;
            att->pair_slot2 = def->slot;
            att->opponent = def->slot;
            third->opponent = def->slot;
            third->slot_d = att->slot;
            def->opponent = att->slot;
            def->pair_other = att->slot;
            def->slot_d = third->slot;
        }
        return 0;
    }
    s32 st;
    if (!(def->flags_e9 & 1)) {
        if (!(f & 0x80)) {
            if (def->state == state::GrappleEvade) {
                if (!(att->location & def->location & 0x3C5) || (s16)def->count_timer > 4)
                    return 0;
                if (f & 0x40) {
                    wrestler_pair_set_state_move(def, state::CatchWhippedRunnerAttacker, att, state::CatchWhippedRunnerDefender);
                    wrestler_start_move(def, def->slot, 90);
                    return 0;
                }
                wrestler_pair_set_state_move(def, state::GrappleDeflectAttacker, att, state::GrappleDeflectDefender);
                return 0;
            }
            if (def->state != state::LockupDefender && timed_press_check(def, 0, 0, 3)) {
                wrestler_pair_set_state_move(def, state::GrappleDeflectAttacker, att, state::GrappleDeflectDefender);
                return 0;
            }
        }
        u32 f2 = att->flags78;
        if (!(f2 & 0xC0) && (att->location & def->location & 0x3C5)) {
            if (def->state == state::GrappleAttempt && def->state_step == 4 && def->anim_frame < 3) {
                att->flags78 = f2 | 0x200000;
                grapple_start_pair(att, def);
                return 1;
            }
            if (timed_press_check(def, 0, 0, 4)) {
                st = def->state;
                if (st != state::LockupDefender) {
                    att->flags78 |= 0x200000;
                    grapple_start_pair(att, def);
                    return 1;
                }
                goto checks;
            }
        }
    }
    st = def->state;
    if (st != state::LockupDefender) {
        if (def->flags80 & 1)
            return 0;
        st = def->state;
    }
checks:
    if (st == state::CarryAttacker)
        return 0;
    if (st == state::HeldUpsideDown) {
        if ((att->location & def->location & 1) && (def->frame_flags & 8))
            wrestler_pair_set_state_move(att, state::YankDownAttacker, def, state::YankDownDefender);
        return 0;
    }
    if (st == state::LockupDefender&& (def->flags78 & 0x200000))
        return 0;
    if ((att->location & 4) && (def->location & 2) && def->state != state::LockupDefender && !(att->flags78 & 0x40)) {
        grapple_start_pair(att, def);
        return 1;
    }
    if (!(att->flags_e9 & 1) && !(att->flags78 & 0x40) && grapple_double_team_check(att, def))
        return 1;
    if (def->state == state::LockupDefender)
        return 0;
    grapple_start_pair(att, def);
    return 1;
}

// N64 0x80126384: the wrestler a grapple attempt reaches this frame (-1 none), inside the hit
// window: a grab (flags78 0x08) takes any candidate within 90 in front (a sixth of a turn); a
// running reach (0x40) the move's joints against the target's joint 1 by the summed radii; else
// (skipping one up a ladder past rung 3) a point ahead of the body (its hand reach: the hands'
// distance halved, from the shoulders' distance) against the target's joints 3 and 1 (radius x
// 0.8 for joint 1), both at a 35-unit hand reach (0x20) or the 45-unit floor reach at a raised
// target; in the ring a target up on the cage wall below 320 only.
s32 grapple_attempt_target(Wrestler *w) {
    if (w->anim_frame < w->anim_player[AP_HIT_FROM] || w->anim_player[AP_HOLD_FRAME] < w->anim_frame)
        return -1;
    s32 cand[11];
    hit_candidates(w, 0, 0, cand);
    u32 f = w->flags78;
    if (f & 8) {
        for (s32 i = 0; cand[i] != -1; i++) {
            s32 t = cand[i];
            if (t >= 100)
                continue;
            const Wrestler *o = &g_wrestlers[t];
            if (!((f64)dist_to(&w->point_120, &o->point_120) <= 90.0))
                continue;
            s32 a = angle_to(&w->point_120, &o->point_120);
            if (abs_i(angle_diff(w->facing, a)) < 227)
                return t;
        }
        return -1;
    }
    if (f & 0x40) {
        for (s32 i = 0; cand[i] != -1; i++) {
            s32 t = cand[i];
            if (t >= 100)
                continue;
            const Wrestler *o = &g_wrestlers[t];
            for (s32 r = 0; r < 3; r++) {
                s32 j = mem_s8(w->move + 13 + r);
                f32 lim = mem_f32(0x8016B728 + 112u * (u32)w->slot + 4u * (u32)j) + mem_f32(0x8016B72C + 112u * (u32)t);
                if (dist_to(wrestler_joint(w, j - 1), &o->point_120) <= lim)
                    return t;
            }
        }
        return -1;
    }
    for (s32 i = 0; cand[i] != -1; i++) {
        s32 t = cand[i];
        if (t >= 100)
            continue;
        const Wrestler *o = &g_wrestlers[t];
        const Vec3 *pa, *pb;
        f32 reach;
        s32 two = 0;
        if ((w->location & 4) && (o->location & 0x3C1)) {
            if (o->location & 8)
                continue;
            pa = wrestler_joint(w, 16);
            pb = wrestler_joint(w, 18);
            reach = 45.0f;
            two = 1;
        } else {
            if ((o->location & 8) && o->ladder_rung >= 4)
                continue;
            if (f & 0x20) {
                pa = wrestler_joint(w, 16);
                pb = wrestler_joint(w, 17);
                reach = 35.0f;
                two = 1;
            }
        }
        if (two) {
            if (dist_to(pa, &o->point_120) <= reach)
                return t;
            if (dist_to(pb, &o->point_120) <= reach)
                return t;
            continue;
        }
        const Vec3 *hand = wrestler_joint(w, 16), *shoulder = wrestler_joint(w, 8);
        f32 half = (f32)((f64)dist_to(hand, shoulder) * 0.5);
        f32 ahead = dist_to(shoulder, &w->point_120) - half;
        Vec3 off = {0.0f, 0.0f, ahead};
        Vec3 p;
        point_at_offset(&w->point_120, &off, w->facing, &p);
        p.y = shoulder->y;
        f32 d = dist_to(&p, wrestler_joint(o, 2));
        if (d <= mem_f32(0x8016B734 + 112u * (u32)t) + half) {
            if (!(w->location & 1) || !(o->location & 0x400) || (f64)o->pos.y < 320.0)
                return t;
        }
        d = dist_to(&p, &o->point_120);
        if ((f64)d <= (f64)mem_f32(0x8016B72C + 112u * (u32)t) * 0.8 + (f64)half) {
            if (!(w->location & 1) || !(o->location & 0x400) || (f64)o->pos.y < 320.0)
                return t;
        }
    }
    return -1;
}

// N64 0x80125D44: what the attacker plays when the attempt starts: from the apron flags78 0x20,
// on the apron line, 11902 / 11903 (strong); in the ring at a slumped opponent (flags80 0x20) in
// the same place: draped (its flags86 0x220) the grab 94 (0x400000), 88 / 92 (strong), 86 / 90
// (+ 4 with flags86 0x40) with flags78 0x08; from behind (facings within 20 degrees, no weapon)
// 86 / 88 with 0x08; else the reach 11892 (11896 in finisher mode) + 1 strong, or 11901 sliding on
// the floor.
void grapple_attempt_start_anim(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    if (w->location & 2) {
        w->flags78 |= 0x20;
        apron_line_pos(w, &w->pos);
        wrestler_set_anim(w, 0x2E7E | ((w->flags78 & 4) ? 1 : 0), 2);
        return;
    }
    if ((a->flags80 & 0x20) && (w->location & a->location & 0x3C5)) {
        if (a->flags86 & 0x220) {
            w->flags78 |= 8;
            s32 move;
            if (w->flags78 & 0x400000)
                move = 94;
            else if (w->flags78 & 4)
                move = 0x58 | ((a->flags86 & 0x40) ? 4 : 0);
            else
                move = (a->flags86 & 0x40) ? 90 : 86;
            wrestler_start_move(w, w->slot, move);
            wrestler_set_anim(w, anim_style(w, 0), 3);
            return;
        }
        if (!(a->flags_ef & 1) && abs_i(angle_diff(w->facing, a->facing)) >= 1821) {
            w->flags78 |= 8;
            wrestler_start_move(w, w->slot, (w->flags78 & 4) ? 88 : 86);
            wrestler_set_anim(w, anim_style(w, 0), 3);
            return;
        }
    }
    if (grapple_attempt_slide(ctx)) {
        wrestler_set_anim(w, 11901, 3);
        return;
    }
    s32 anim = (w->flags78 & 1) ? 11896 : 11892;
    anim += (w->flags78 & 4) ? 1 : 0;
    wrestler_set_anim(w, anim, 3);
}

// N64 0x80125900: the grapple attempt (state 0x50). Steps 0-2: the input history cleared, flags78
// from the finisher mode, aux_timer counting; with flags8a 0x80 (from state 27) the slide and
// bounds until the animation ends (on the apron, then 11794); else walking and turning toward the
// opponent; the press kind: B becomes the strike 34 (step 1), a hold sets flags78 0x04 (strong)
// and either way step 3. Step 3: drops the weapon, starts the reach (grapple_attempt_start_anim).
// Step 4: turns toward the opponent (34 a frame) through the hit window, grapple_attempt_target
// and grapple_attempt_resolve; in the ring the slide (2) and bounds (unless sliding, 11901); a
// running attempt moves to step 5 after the hold frame; move_event. Step 7 (the grab miss) and 8
// (the grab 0x68 / 0x69 by the side) start their animations and go to step 4.
void state_50_grapple_attempt(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        input_history_clear(ctx->slot);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1:
        w->flags78 = 0;
        if (w->flags_ee & 4)
            w->flags78 |= 1;
        w->aux_timer = 0;
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 2: {
        w->aux_timer = (u16)(w->aux_timer + 1);
        if (w->flags8a & 0x80) {
            if (w->location & 2)
                w->flags8a |= 0x800;
            wrestler_slide(w, 4.0f);
            keep_in_bounds_self(w, 4.0f, 0, 0xFFFFE);
            if (w->anim_frames_left <= 0) {
                w->flags8a &= 0xFF7F;
                if (w->flags8a & 0x800) {
                    apron_line_pos(w, &w->pos);
                    wrestler_set_anim(w, 11794, 2);
                    return;
                }
            }
        }
        if (!(w->flags8a & 0x80)) {
            walk_step(ctx, ctx->buttons, 2);
            if (!(w->location & 2))
                wrestler_turn_toward(w, w->facing_to_opponent, 341, 512);
        }
        input_press_kind(ctx->slot, &w->press_kind);
        if (!grapple_attempt_double(ctx) && (w->press_kind & 3)) {
            if (w->press_kind & 8) {
                wrestler_set_state_now(w, state::Strike);
                w->state_step = 1;
            } else {
                if (w->press_kind & 1)
                    w->flags78 |= 4;
                wrestler_set_step(w, 3);
            }
        }
        return;
    }
    case 3:
        weapon_release(w, 0);
        grapple_attempt_start_anim(ctx);
        w->state_step = w->state_step + 1;
        return;
    case 4: {
        if (!(w->flags78 & 0x68) && !(w->anim_player[AP_HOLD_FRAME] < w->anim_frame))
            wrestler_turn_toward(w, w->facing_to_opponent, 34, 0);
        s32 t = grapple_attempt_target(w);
        if (t != -1 && grapple_attempt_resolve(w, &g_wrestlers[t]))
            return;
        if (!(w->location & 2)) {
            wrestler_slide(w, 2.0f);
            if (w->anim_id != 11901)
                keep_in_bounds_self(w, 1.0f, 0, 2);
        }
        if ((w->flags78 & 0x40) && w->anim_player[AP_HOLD_FRAME] < w->anim_frame)
            w->state_step = w->state_step + 1;
        move_event(ctx, 0);
        return;
    }
    case 5:
        wrestler_slide(w, 2.0f);
        keep_in_bounds_self(w, 1.0f, 0, 2);
        move_event(ctx, 0);
        return;
    case 7:
        w->flags78 = 2064;
        if (w->flags_ee & 4)
            w->flags78 |= 5;
        wrestler_set_anim(w, 11894, 3);
        w->state_step = 4;
        return;
    case 8: {
        s32 rel = relative_position(w, ctx->rec_a);
        wrestler_start_move(w, ctx->slot, 0x68 | ((rel & 8) ? 1 : 0));
        s32 anim = 11904;
        if ((w->location & ctx->rec_a->location & 0x3C5) && !(ctx->rec_a->frame_flags & 0x19))
            anim = anim_style(w, 0);
        wrestler_set_anim(w, anim, 3);
        w->flags78 = 2112;
        w->state_step = 4;
        return;
    }
    default:
        return;
    }
}

// N64 0x80127390: the lock-up's attacker (state 0x51). Step 0 starts the pose (grapple_anim_
// defender); step 1 slides into place, then step 2 (8 for a clash, 4 straight to the move for the
// running / whipped kinds); step 2 keeps the pair record (distance 73.5 / 53.5, the pair angle),
// walks (grapple_walk) and runs the clock (grapple_timer: 1 a press, step 3; 2 the time-out, step
// 7 with state_flags 0x01); steps 3-4 read the move (lockup_input: 2 back to step 2, 3 the
// reversal (state_flags 0x04, the opponent's style animation), 4 the walk round (step 10), else a
// move: a corner-slumped opponent's pull-in 12099 / 12101 first (step 5 waits), then step 6 starts the
// move's animation and state 0x55); step 7 follows the pair until the break animation ends, then
// 12531 (step 9: move_event with the end pose) or the end pose; step 8 the clash: damage from the
// generic move 0, 11910; steps 10-11 the walk round (11929 / 11933) and back to step 2 with the
// sides swapped and a fresh time limit.
void state_51_grapple_attacker(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    w->flags8a |= 0x2000;
    switch (w->state_step) {
    case 0:
        w->flags_f0 &= 0xF0;
        grapple_anim_defender(w, ctx->rec_a);
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1: {
        if (!lockup_slide_in(ctx))
            return;
        u32 f = w->flags78;
        if (f & 0x200000)
            w->state_step = 8;
        else if ((f & 0x240) == 0x240 || (f & 0x140) == 0x140)
            w->state_step = 4;
        else
            w->state_step = w->state_step + 1;
        return;
    }
    case 2: {
        u32 pair = pair_rec(w->slot);
        if (!(w->state_counter & 1)) {
            w->state_counter |= 1;
            mem_w32(pair, 0);
            mem_w32(pair + 4, 0);
            mem_wf32(pair + 8, (w->flags78 & 0x80) ? 53.5f : 73.5f);
            mem_w32(pair + 16, (u32)angle_add(w->facing, 2048));
            w->aux_timer = 0;
        }
        grapple_walk(ctx, pair);
        s32 r = grapple_timer(ctx);
        if (r == 1) {
            w->state_step = w->state_step + 1;
        } else if (r == 2) {
            w->state_step = 7;
            w->state_flags |= 1;
        }
        return;
    }
    case 3:
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 4: {
        w->state_flags |= 2;
        s32 v = lockup_input(ctx);
        if (v) {
            if (v == 2) {
                w->state_flags &= 0xFFFD;
                w->state_step = w->state_step - 2;
            }
            if (v == 3) {
                w->state_flags |= 4;
                wrestler_set_anim(w, anim_style(ctx->rec_a, 1), 1);
            }
            if (v == 4) {
                w->state_step = 10;
                w->state_flags |= 8;
            }
            return;
        }
        if ((w->flags78 & 0x100) && (mem_u32(w->move + 0x20) & 0x100000)) {
            wrestler_set_anim(w, (w->flags78 & 0x80) ? 12101 : 12099, 1);
            w->state_step = w->state_step + 1;
        } else {
            w->state_step = 6;
        }
        return;
    }
    case 5:
        if (w->anim_frames_left != 1)
            return;
        w->state_step = w->state_step + 1;
        return;
    case 6:
        wrestler_set_anim(w, anim_style(w, 0), (w->flags78 & 0x180) ? 1 : 3);
        wrestler_set_state(w, state::MoveAttacker);
        return;
    case 7:
        if (!(w->flags78 & 0x700))
            pair_follow(w, ctx->rec_a, nullptr, 1.0f, 0, 2);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        wrestler_unpair(w);
        if (w->flags78 & 0x200) {
            wrestler_set_anim(w, 12531, 3);
            w->state_step = 9;
        } else {
            wrestler_settle_pose(w);
        }
        return;
    case 8:
        if (w->anim_frames_left <= 0) {
            wrestler_set_generic_move(w, 0);
            hit_move_copy(w, w);
            apply_damage(ctx->rec_a, w);
            anim_end_apply(w);
            wrestler_set_anim(w, 11910, 1);
        }
        return;
    case 9:
        move_event(ctx, 0x200);
        return;
    case 10: {
        wrestler_set_anim(w, 0x2E99 | ((w->flags78 & 0x80) ? 4 : 0), 2);
        u16 sc = w->state_counter;
        w->state_flags &= 0xFFF5;
        sc &= 0xFFFE;
        if (sc & 4)
            sc = (sc & 0xFFF6) | 8;
        else
            sc = (sc & 0xFFFA) | 4;
        w->state_counter = sc;
        w->state_step = w->state_step + 1;
        return;
    }
    case 11:
        pair_follow(w, ctx->rec_a, nullptr, 1.0f, 0, 2);
        if (w->anim_frames_left > 0)
            return;
        anim_end_apply(w);
        if (w->flags78 & 0x80) {
            w->flags78 &= 0xFFFFFF7Fu;
            wrestler_set_anim(w, 11905, 1);
        } else {
            w->flags78 |= 0x80;
            wrestler_set_anim(w, 11909, 1);
        }
        w->count_timer = (u16)lockup_time_limit(w, ctx->rec_a);
        w->state_step = 2;
        return;
    default:
        return;
    }
}

// N64 0x801278C4: the lock-up's defender (state 0x52), following the attacker's steps: step 0
// the pose (grapple_defender_anim); step 1 the reversal roll (grapple_defender_input), the slide
// in, then step 2 (8 for a clash); step 2 the roll, the pair bounds in a lock-up, the attacker's
// state_flags: 0x01 its break animation + 1 (step 7), 0x08 the walk round (step 9), 0x02 step 4;
// step 4 when the attacker has its move (its state_flags 0x02): a reversal (0x04) swaps the pair
// into 0x55 / 0x56 with the defender leading, else the pull-in 12100 / 12102 (step 5 waits) or
// step 6: the move's animation (the attacker's style + 1) and state 0x56; step 7 follows the pair
// through the break and move_event; step 8 the clash's end: back into the lock-up from behind with
// the sides swapped; steps 9-10 the walk round.
void state_52_grapple_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    w->flags8a |= 0x2000;
    switch (w->state_step) {
    case 0:
        w->flags_f0 &= 0xF0;
        grapple_defender_anim(w, a);
        w->count_timer = 0;
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 1:
        grapple_defender_input(w, a, ctx->pressed);
        w->count_timer = (u16)(w->count_timer + 1);
        if (!lockup_slide_in_defender(w, a))
            return;
        if (a->flags78 & 0x200000)
            w->state_step = 8;
        else
            w->state_step = w->state_step + 1;
        return;
    case 2: {
        if (a->flags78 & 0x800)
            w->dpad_held = keep_in_bounds_pair(w, a, nullptr, 4.0f, 0xFFFFE);
        grapple_defender_input(w, a, ctx->pressed);
        w->count_timer = (u16)(w->count_timer + 1);
        if (a->state_flags & 1) {
            wrestler_set_anim(w, a->anim_id + 1, 1);
            w->state_step = 7;
        }
        u16 sf = a->state_flags;
        if (sf & 8)
            w->state_step = 9;
        else if (sf & 2)
            wrestler_set_step(w, 4);
        return;
    }
    case 3:
        w->state_step = w->state_step + 1;
        [[fallthrough]];
    case 4:
        if (a->state == state::LockupAttacker && !(a->state_flags & 2)) {
            w->state_step = w->state_step - 2;
            return;
        }
        if (a->state_flags & 4) {
            wrestler_set_anim(w, anim_style(w, 0), 1);
            wrestler_set_state(w, state::MoveAttacker);
            wrestler_set_state(a, state::MoveDefender);
            wrestler_unpair(a);
            w->flags88 |= 4;
            w->pair_slot = ctx->slot_a;
            a->pair_other = ctx->slot;
            w->flags78 = a->flags78;
            return;
        }
        if ((a->flags78 & 0x100) && (mem_u32(a->move + 0x20) & 0x100000)) {
            wrestler_set_anim(w, 0x2F44 | ((a->flags78 & 0x80) ? 2 : 0), 1);
            w->state_step = w->state_step + 1;
        } else {
            w->state_step = 6;
        }
        return;
    case 5:
        if (w->anim_frames_left == 1)
            w->state_step = w->state_step + 1;
        return;
    case 6:
        wrestler_set_anim(w, anim_style(a, 1), (a->flags78 & 0x180) ? 1 : 3);
        wrestler_set_state(w, state::MoveDefender);
        return;
    case 7:
        if (!(a->flags78 & 0x700))
            pair_follow(w, a, nullptr, 1.0f, 0, 2);
        move_event(ctx, 0);
        return;
    case 8:
        if (w->anim_frames_left <= 0) {
            anim_end_apply(w);
            wrestler_set_anim(w, 11909, 1);
            w->flags_f0 &= 0xF0;
            a->flags_f0 &= 0xF0;
            wrestler_pair_set_state_move(w, state::LockupAttacker, a, state::LockupDefender);
            w->state_step = 2;
            a->state_step = 2;
            a->count_timer = 0;
            a->flags78 &= 0x804;
            w->flags78 = a->flags78 | 0x80;
            w->count_timer = (w->flags78 & 4) ? 210 : 150;
        }
        return;
    case 9:
        wrestler_set_anim(w, a->anim_id + 1, 3);
        w->state_step = w->state_step + 1;
        return;
    case 10:
        pair_follow(w, a, nullptr, 1.0f, 0, 2);
        if (w->anim_frames_left <= 0) {
            anim_end_apply(w);
            w->count_timer = 0;
            w->flags_f0 &= 0xF0;
            wrestler_set_anim(w, a->anim_id + 1, 1);
            w->state_step = 2;
        }
        return;
    default:
        return;
    }
}

// N64 0x8012CA7C: keeps a wrestler in bounds the way its state's row of the table 0x8014D70C
// (state, partner's state, third's state, scale, joints, own scale; -1 ends it) says: with the
// partner (or third) in the row's state pair_follow at the row's scale, else keep_in_bounds_self at
// its own scale. Returns what they return, 0 for no row.
u16 state_bounds(Wrestler *w, Wrestler *other, Wrestler *other2, u16 mask) {
    for (u32 row = 0x8014D70C;; row += 20) {
        s16 st = mem_s16(row);
        if (st == -1)
            return 0;
        if (st != w->state)
            continue;
        if (other2 && mem_s16(row + 4) != other2->state)
            other2 = nullptr;
        if (mem_s16(row + 2) != other->state)
            other = nullptr;
        if (!other && !other2)
            return keep_in_bounds_self(w, mem_f32(row + 16), mask, mem_u32(row + 12));
        return pair_follow(w, other, other2, mem_f32(row + 8), mask, mem_u32(row + 12));
    }
}

// N64 0x800ED5AC: the follow-up of a move with record flags 0x1800: on a raised place the
// attacker starts 287 and the defender its variant (move_anim_offset); elsewhere the
// defender gets the variant and the pair goes to 0x59 / 0x5A. Returns 1 for the latter.
s32 move_follow_up(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (!(mem_u32(w->move + 0x20) & 0x1800))
        return 0;
    Wrestler *o = ctx->rec_a;
    if (w->location & 0x3C4) {
        wrestler_start_move(w, ctx->slot, 287);
        wrestler_start_move(o, w->slot, move_anim_offset(w, o, 0));
        s32 mode = (w->flags78 & 0x180) ? 1 : 3;
        wrestler_set_anim(w, anim_style(w, 0), mode);
        wrestler_set_anim(o, anim_style(w, 1), mode);
        return 0;
    }
    wrestler_start_move(o, w->slot, move_anim_offset(w, o, 0));
    wrestler_pair_set_state_move(w, state::MoveFollowUpWalkAttacker, o, state::MoveFollowUpDefender);
    return 1;
}

// N64 0x800EE1E4: a finisher landing on a breakable table (location 0x40 / 0x100 / 0x200 with
// its g_tables_intact bit) at the player's frame 0x15 breaks it; nothing elsewhere.
void finisher_effects(Wrestler *w) {
    if (w->anim_player[0x15] == 0 || w->anim_frame != w->anim_player[0x15])
        return;
    if (((w->location & 0x40) && (mem_u8(0x8016B928) & 4)) || ((w->location & 0x100) && (mem_u8(0x8016B928) & 8)) ||
        ((w->location & 0x200) && (mem_u8(0x8016B928) & 0x10)))
        NM_UNPORTED("finisher_effects: a table breaking");
}

// N64 0x8014291C: a move that throws an object (the player's byte 0x14) spawns it at the player's
// frame 0x15; nothing for other moves.
void object_spawn_for_move(Wrestler *w, Wrestler *other, s32 mode) {
    if (mode != 0)
        return;
    if (w->anim_player[0x14] == 0)
        return;
    if (w->anim_frame != w->anim_player[0x15])
        return;
    NM_UNPORTED("object_spawn_for_move: a thrown object");
}

// N64 0x80142AE4 (the TOML name is a call-graph match; it is a texture stamp): 7 frames after the
// player's frame 0x15 of a move with byte 0x14 set, stamps the texture class on the opponent
// (rendering: nothing for the oracle).
void move_texture_stamp(Wrestler *w, Wrestler *other) {
    (void)w;
    (void)other;
}

// N64 0x800ECEEC: what a grapple move chains into when it ends: with end pose 6 and no follow-up
// both go straight back into the lock-up from behind (0x51 / 0x52 at step 2, flags78 0x880 plus
// the strong bit, 11909 / 11910; 1); the follow-up chains (record flags 0x1E00, the follow-up
// move: a second move, the counter 0x64, the rear states) are not ported. 0 for a move without a
// follow-up.
s32 move_chain_check(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    w->flags_f0 &= 0xF8;
    o->flags_f0 &= 0xF8;
    s16 follow = mem_s16(w->move + 4);
    if (w->anim_player[AP_END_POSE] == 6 && follow == 0) {
        anim_end_apply(w);
        anim_end_apply(o);
        wrestler_set_state(w, state::LockupAttacker);
        w->state_step = 2;
        w->flags78 = (o->flags78 & 4) | 0x880;
        w->count_timer = (w->flags78 & 4) ? 210 : 150;
        wrestler_set_anim(w, 11909, 1);
        wrestler_set_state(o, state::LockupDefender);
        o->state_step = 2;
        o->count_timer = 0;
        o->flags78 = 0;
        wrestler_set_anim(o, 11910, 1);
        return 1;
    }
    if (mem_u32(w->move + 0x20) & 0x1E00) {
        // The move ends in a pin (0x4A / 0x4B, record flag 0x200) or a hold (0x48 / 0x49).
        anim_end_height(w);
        anim_end_height(o);
        o->move_link = (s16)mem_u16(0x8008FA70 + 1096u * (u32)(s8)w->move_slot + 2u * (u32)(u16)w->move_num);
        if (mem_u32(w->move + 0x20) & 0x200)
            wrestler_pair_set_state_now(w, state::PinFromMoveAttacker, o, state::PinFromMoveDefender);
        else
            wrestler_pair_set_state_now(w, state::GroundHoldAttacker, o, state::GroundHoldDefender);
        stats_count(w, o, 0);
        return 1;
    }
    if (follow == 0)
        return 0;
    // The follow-up, move +4. 186 / 188 / 190 knock it back (0x23 step 1), 192-198 send it off (0x07 step 1).
    s32 m = follow;
    if (m == 186 || m == 188 || m == 190 || m == 192 || m == 194 || m == 196 || m == 198) {
        w->pair_slot = -1;
        w->flags88 &= 0xFFFB;
        anim_end_apply(w);
        wrestler_set_state_now(w, m <= 190 ? state::StrongStrike : state::RunningShoulderBlock);
        w->state_step = 1;
        return 1;
    }
    anim_end_apply(w);
    anim_end_apply(o);
    if ((u32)(m - 180) < 24) {
        // A counter: the bit 0x64 / 0x65 reads (183 / 184 0x01, 187..203 0x02..0x200).
        u32 bits = 0;
        if (m == 183 || m == 184)
            bits = 1;
        else if (m >= 187 && m <= 203 && (m & 1))
            bits = 1u << ((m - 185) / 2);
        w->counter_bits = bits;
        wrestler_pair_set_state_move(w, state::StrikeCounterAttacker, o, state::StrikeCounterDefender);
        return 1;
    }
    if ((u32)(m - 108) < 16) {
        wrestler_start_move(w, w->slot, m);
        wrestler_set_anim(w, anim_style(w, 0), 1);
        wrestler_set_anim(o, anim_style(w, 1), 1);
        w->flags78 |= 0x100;
        if (reversal_check(o, w), o->flags_f0 & 2)
            wrestler_start_move(o, w->slot, move_anim_offset(w, o, 0));
        wrestler_pair_set_state_move(w, state::MoveAttacker, o, state::MoveDefender);
        return 1;
    }
    // 73-76 and 77-80 are sets to pick from at random.
    if ((u32)(m - 73) < 4)
        m = rand_pool_next() % 4 + 73;
    else if ((u32)(m - 77) < 4)
        m = rand_pool_next() % 4 + 77;
    wrestler_start_move(w, w->slot, m);
    bool reversible = (mem_u32(o->attrs + 0x34) & 0x1000) != 0;
    if ((u32)(m - 73) >= 8) {
        if (reversible) {
            if (!(mem_u32(w->move + 0x20) & 0x2000))
                wrestler_start_move(w, w->slot, 159);
            if (reversal_check(o, w), o->flags_f0 & 2) {
                if (w->move_num == 159)
                    wrestler_start_move(o, o->slot, 160);
                else
                    wrestler_start_move(o, w->slot, move_anim_offset(w, o, 0));
            }
            wrestler_set_anim(w, anim_style(w, 0), 1);
            wrestler_set_anim(o, anim_style(w, 1), 1);
            wrestler_pair_set_state_move(w, state::ChainMoveAttacker, o, state::ChainMoveDefender);
            return 1;
        }
    } else if (reversible) {
        if (!(mem_u32(w->move + 0x20) & 0x2000))
            wrestler_start_move(w, w->slot, 161);
        if (reversal_check(o, w), o->flags_f0 & 2) {
            s32 r = w->move_num == 161 ? 162 : ((w->move_num >= 77) | 0x54);
            wrestler_start_move(o, o->slot, r);
            wrestler_set_anim(w, anim_style(o, 1), 1);
            wrestler_set_anim(o, anim_style(o, 0), 1);
            wrestler_pair_set_state_move(o, state::CarryMoveAttacker, w, state::CarryMoveDefender);
            return 1;
        }
    }
    wrestler_set_anim(w, anim_style(w, 0), 1);
    wrestler_set_anim(o, anim_style(w, 1), 1);
    wrestler_pair_set_state_move(w, state::CarryMoveAttacker, o, state::CarryMoveDefender);
    return 1;
}

// The place bits a move's record sets by the player's end place (byte 6: 1 ring, 2 floor, 3
// apron) while the pair moves across the ropes (flags78 0x600), with pair_follow at scale 6 past
// the player's frame 0x15, else in the ring at scale 4 keeping flags80.
void move_across_ropes_follow(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    u8 place = w->anim_player[AP_END_PLACE];
    if (place == 1) {
        w->location = (w->location & 0xF800) | 1;
        place = w->anim_player[AP_END_PLACE];
    }
    if (place == 3)
        w->location = (w->location & 0xF800) | 2;
    if (w->anim_player[AP_END_PLACE] == 2)
        w->location = (w->location & 0xF800) | 4;
    if (!(w->anim_frame < w->anim_player[0x15])) {
        pair_follow(w, ctx->rec_a, nullptr, 6.0f, 0, 2);
    } else {
        u16 f80 = w->flags80;
        w->location = (w->location & 0xF800) | 1;
        pair_follow(w, ctx->rec_a, nullptr, 4.0f, 0, 2);
        w->flags80 = f80;
    }
}

// N64 0x800E8F1C: a grapple move's attacker (state 0x55): the follow-up (move_follow_up) first;
// else the bounds (across the ropes by the move's places, else state_bounds); the defender's
// reversal (its flags_f0 0x02 at the player's frame 0x10) switches to the opponent's style
// animation (flags_f0 0x01); the finisher effects, flags8c 0x400 on the opponent at the last frame
// of a move with record flag 0x80000 (no aftermath), the thrown object, the texture stamp, then
// move_event 0x2480 (the damage to the opponent, chains, the statistics, the end pose).
void state_55_move_attacker(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    if (move_follow_up(ctx))
        return;
    if (!(w->flags78 & 0x100)) {
        if (w->flags78 & 0x600)
            move_across_ropes_follow(ctx);
        else
            state_bounds(w, o, nullptr, 0);
    }
    if (!(o->flags_f0 & 1) && w->anim_frame == w->anim_player[0x10] && (o->flags_f0 & 2)) {
        w->flags_f0 |= 1;
        wrestler_set_anim(w, anim_style(o, 1), 1);
        return;
    }
    finisher_effects(w);
    if (w->anim_frames_left == 1 && (mem_u32(w->move + 0x20) & 0x80000))
        o->flags8c |= 0x400;
    object_spawn_for_move(w, o, 0);
    move_texture_stamp(w, o);
    move_event(ctx, 0x2480);
}

// N64 0x800E9174: a grapple move's defender (state 0x56): the bounds as the attacker's; when the
// attacker took the reversal (its flags_f0 0x01) the defender becomes the attacker: its own style
// animation, the attacker's flags78, a statistic, and the pair 0x55 / 0x56 the other way round;
// else the finisher effects and, when the animation ends, the time down (escape_time op 2), the
// end pose, and the aftermath (bleeding / TKO) unless flags8c 0x400.
void state_56_move_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *a = ctx->rec_a;
    if (!(a->flags78 & 0x100)) {
        if (a->flags78 & 0x600)
            move_across_ropes_follow(ctx);
        else
            state_bounds(w, a, nullptr, 0);
    }
    if (a->flags_f0 & 1) {
        wrestler_set_anim(w, anim_style(w, 0), 1);
        w->flags78 = a->flags78;
        stats_count(w, a, 1);
        wrestler_pair_set_state_move(w, state::MoveAttacker, a, state::MoveDefender);
        return;
    }
    finisher_effects(w);
    if (w->anim_frames_left <= 0) {
        w->escape = (s16)escape_time(w, nullptr, 2);
        w->fall_chance = 0;
        anim_end_apply(w);
        wrestler_settle_pose(w);
        if (!(w->flags8c & 0x400))
            move_aftermath_check(a, a, w, 2);
        w->flags8c &= 0xFBFF;
    }
}


// ---- the Irish whip ----

// N64 0x8011B168: the whip direction 0-7 (0x8014D238: even the four sides, odd the corners). At an
// opponent slumped in a corner (flags78 0x100) the corner opposite its dir; otherwise the d-pad in
// the input entry's +0x0A word (dpad_dir8 - 1), or with none the side nearest the whipper's back. A
// choice within 768 of the whipper's facing becomes the side nearest its back (or, for a corner, the
// corner nearest behind).
s32 whip_direction(WrestlerCtx *ctx) {
    const Wrestler *s = ctx->self;
    s32 d;
    s32 best_side = 0, best_corner = 0;
    bool searched = false;
    if (s->flags78 & 0x100) {
        d = 2 * ((ctx->rec_a->dir + 2) & 3) + 1;
    } else {
        searched = true;
        s32 bd = 2048;
        for (s32 i = 0; i < 8; i += 2) {
            s32 a = abs_i(angle_diff(angle_add(s->facing, 2048), mem_s32(0x8014D238 + 4u * (u32)i)));
            if (a < bd) {
                bd = a;
                best_side = i;
            }
        }
        bd = 2048;
        for (s32 i = 1; i < 8; i += 2) {
            u32 c = 0x8014D1A8 + 12u * (u32)(i / 2);
            Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
            s32 back = angle_add(s->facing, 2048);
            s32 a = abs_i(angle_diff(back, angle_to(&s->point_120, &corner)));
            if (a < bd) {
                bd = a;
                best_corner = i;
            }
        }
        s32 pad = dpad_dir8(ctx->in_0a);
        d = pad ? pad - 1 : best_side;
    }
    if (abs_i(angle_diff(mem_s32(0x8014D238 + 4u * (u32)d), s->facing)) < 768) {
        if (!searched)
            NM_UNPORTED("whip_direction: a corner whip in front (the original reads unset registers)");
        d = (d & 1) ? best_corner : best_side;
    }
    return d;
}

// N64 0x8011B320: the whip's target (1 when it found one; then the whipper's animation is set). In
// the ring, strong (flags78 0x04), toward a corner (odd d) faced within 768 and nearer than 100: the
// opponent gets state_flags 0x04, the whipper's dir the corner, animation 11999. Otherwise a move of
// 100 along d against the walls (move_hits_segment) at a wall whose normal is within 135 degrees of
// the facing: in a cage match with flags78 0x10000000 the cage whip (the opponent's grab_zone_info,
// state_flags 0x10, animation 12823); a wall with +0x28 0x100 (the ropes) state_flags 0x08 and 11999.
s32 whip_target(WrestlerCtx *ctx, s32 d) {
    Wrestler *s = ctx->self;
    Wrestler *o = ctx->rec_a;
    if ((s->location & 1) && (s->flags78 & 4) && (d & 1)) {
        s32 c = d / 2;
        if (abs_i(angle_diff(mem_s32(0x8014D208 + 4u * (u32)c), s->facing)) < 768) {
            u32 a = 0x8014D1A8 + 12u * (u32)c;
            Vec3 corner = {mem_f32(a), mem_f32(a + 4), mem_f32(a + 8)};
            if ((f64)dist_to(&s->pos, &corner) < 100.0) {
                o->state_flags |= 4;
                s->dir = (s16)c;
                wrestler_set_anim(s, 11999, 2);
                return 1;
            }
        }
    }
    Vec3 move;
    vec_from_angle(mem_s32(0x8014D238 + 4u * (u32)d), 100.0f, &move);
    if (!(u16)move_hits_segment(s, &move))
        return 0;
    Vec3 f;
    vec_from_angle(s->facing, 1.0f, &f);
    Vec3 n = {mem_f32(s->zone_info + 24), mem_f32(s->zone_info + 28), mem_f32(s->zone_info + 32)};
    if (!(-0.707106781 < (f64)vec_normalize_two(&f, &n)))
        return 0;
    if ((g_match_type & 0x10000) && (s->flags78 & 0x10000000)) {
        o->grab_zone_info = s->zone_info;
        o->state_flags |= 0x10;
        wrestler_set_anim(s, 12823, 2);
        return 1;
    }
    if (!(mem_u16(s->zone_info + 0x28) & 0x100))
        return 0;
    o->grab_zone_info = s->zone_info;
    o->state_flags |= 8;
    wrestler_set_anim(s, 11999, 2);
    return 1;
}

// N64 0x8011B524: puts the runner at its point_120 moved (1.43, 0, 27.77) along its facing, and the
// whipper 73.5 in front of it.
void whip_place_runner(WrestlerCtx *ctx) {
    Wrestler *o = ctx->rec_a;
    Vec3 off = {1.43f, 0.0f, 27.77f};
    point_at_offset(&o->point_120, &off, o->facing, &o->pos);
    Vec3 off2 = {0.0f, 0.0f, 73.5f};
    point_at_offset(&o->pos, &off2, o->facing, &ctx->self->pos);
}

// N64 0x8011A580: state 0x0B, the Irish whip (the whipper; the opponent runs in 0x0C). Step 0: the
// whip set-up (flags78 0x1000000 / 0x2000000 when the whipper / the whipped lacks attrs +0x34
// 0x2000, the latter dropped for a weak hold on a non-finisher-mode opponent; the opponent's
// flags86 0x21, 0x40 for a strong hold; the distance kept in +0x2D8), animation 11981 (11991 from a
// slumped opponent), the direction (whip_direction) and target (whip_target: step 4, the whip into
// the corner or the ropes). With 0x2000000 the reversal animation 11989 / 11993 and step 7. Else a
// corner from the ring sets the opponent's flags86 0x08 (to the corner); a hold with flags78
// 0x10000000 the paired move 194 / 195; else a turn toward the direction over the animation; the
// opponent's dir the side. Step 1: turning together (the opponent kept at the stored distance),
// then the release (11983, 11987 for 0x1000000; 11986 when the opponent has flags_f0 0x04). Step 2
// the follow-through; step 3 the reversed whip (the roles swap). Steps 4-6: the slide into place
// (pair_scripted_place, pair_slide_step) and the follow (pair_follow while the opponent has
// state_flags 0x18); step 7 the reversal's follow.
void state_0B_whip_attacker(WrestlerCtx *ctx) {
    Wrestler *s = ctx->self;
    Wrestler *o = ctx->rec_a;
    auto bounds = [&]() {
        if (!(s->flags78 & 0x100))
            keep_in_bounds_pair(s, o, nullptr, 1.0f, 2);
    };
    auto hold_distance = [&](Wrestler *a, Wrestler *b) {
        Vec3 off = {0.0f, 0.0f, s->hold_dist};
        Vec3 at, d;
        point_at_offset(&b->pos, &off, b->facing, &at);
        at.y = a->pos.y;
        vec_sub(&a->pos, &at, &d);
        wrestler_translate(a, &d);
    };
    switch (s->state_step) {
    case 0: {
        s->flags_f0 &= ~4u;
        s->flags8a &= ~2u;
        o->flags8a &= ~2u;
        if (!(mem_u32(s->attrs + 0x34) & 0x2000))
            s->flags78 |= 0x1000000;
        if (!(mem_u32(o->attrs + 0x34) & 0x2000))
            s->flags78 |= 0x2000000;
        if ((s->flags78 & 1) && !(o->flags_ee & 4))
            s->flags78 &= 0xFDFFFFFF;
        o->flags86 = 33;
        if (s->flags78 & 4)
            o->flags86 |= 0x40;
        s->hold_dist = dist_to(&s->pos, &o->pos);
        wrestler_set_anim(s, (s->flags78 & 0x100) ? 11991 : 11981, 2);
        s32 d = whip_direction(ctx);
        if (whip_target(ctx, d)) {
            s->state_step = 4;
            return;
        }
        if (s->flags78 & 0x2000000) {
            wrestler_set_anim(s, (s->flags78 & 0x100) ? 11993 : 11989, 2);
            s->state_step = 7;
            return;
        }
        if ((d & 1) && (s->location & 1)) {
            o->flags86 &= ~1u;
            o->flags86 |= 8;
        } else {
            if ((s->flags78 & 0x10000000) && !(o->flags86 & 8)) {
                s->dir = (s16)(d / 2);
                wrestler_pair_set_state_move(s, state::CageWhipAttacker, o, state::CageWhipDefender);
                return;
            }
            s32 t = angle_diff(mem_s32(0x8014D238 + 4u * (u32)d), angle_add(s->facing, 2048));
            s->turn_step = t / (s32)s->anim_player[4];
        }
        o->dir = (s16)(d / 2);
        s->state_step++;
        return;
    }
    case 1:
        if (!(s->flags78 & 0x1000000)) {
            if (o->flags86 & 8) {
                u32 c = 0x8014D1A8 + 12u * (u32)o->dir;
                Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
                s32 t = angle_diff(angle_to(&o->point_120, &corner), o->facing);
                s32 n = s->anim_frames_left ? s->anim_frames_left : 1;
                s->turn_step = t / n;
            }
            s->facing = angle_add(s->facing, s->turn_step);
            o->facing = angle_add(o->facing, s->turn_step);
            hold_distance(s, o);
        }
        if (s->anim_frames_left > 0) {
            bounds();
            return;
        }
        if (s->flags78 & 0x100)
            whip_place_runner(ctx);
        if (o->flags_f0 & 4) {
            wrestler_set_anim(s, 11986, 0);
            return;
        }
        wrestler_set_anim(s, (s->flags78 & 0x1000000) ? 11987 : 11983, 0);
        s->state_step++;
        bounds();
        return;
    case 2:
        if (!(s->flags78 & 0x100))
            keep_in_bounds_pair(s, o, nullptr, 1.0f, 2);
        move_event(ctx, 0);
        return;
    case 3:
        if (s->anim_frames_left <= 0) {
            wrestler_set_generic_move(s, 4);
            hit_move_copy(s, s);
            spirit_exchange(s, o, s);
            s->facing = angle_add(s->facing, 2048);
            o->facing = angle_add(o->facing, 2048);
            whip_place_runner(ctx);
            s32 anim = 11983;
            if (o->flags78 & 0x1000000) {
                wrestler_set_generic_move(s, 5);
                anim = 11987;
            }
            wrestler_set_anim(s, anim, 0);
            s->state_step--;
        }
        bounds();
        return;
    case 4:
        s->hold_dist = dist_to(&s->pos, &o->pos);
        pair_scripted_place(s, o);
        s->state_step++;
        [[fallthrough]];
    case 5:
        if (pair_slide_step(s, nullptr))
            s->state_step++;
        o->facing = angle_add(s->facing, 2048);
        hold_distance(o, s);
        [[fallthrough]];
    case 6:
        if (o->state_flags & 0x18)
            pair_follow(s, o, nullptr, 4.0f, 0, 2);
        move_event(ctx, 128);
        return;
    case 7:
        pair_follow(s, o, nullptr, 1.0f, 0, 2);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}


// N64 0x8011AC94: state 0x0C, the whipped wrestler (the whipper is in 0x0B). Step 0: the whipper's
// animation + 1; into a corner, the ropes or a cage wall (state_flags 0x1C) step 4, a reversal
// (whipper's flags78 0x2000000) step 6, else 1. Step 1: with the whipper's hand released, either
// the reversed whip (flags_f0 0x04: animation 11985, the roles swap into 0x0B / 0x0C at step 3,
// copying flags78 0x100, flags86 and dir to the new whipper) or the next animation (step 2). Step 2:
// the stumble (animation 11988: kept in bounds, event 594) or, the animation over, the run (state 3
// at step 2 with step_vec at walk speed along the facing, rebound_count 0). Step 3 the reversed
// whip's wait. Step 4: into the corner / ropes / wall: at the animation's impact frame (anim_player
// +16) with flags_f0 0x04 the reversal there (the roles swap at steps 6 / 5, animations 12826 / 12825
// at a cage wall, else 12001 / 12002), else step 5: the follow (pair_follow while state_flags 0x18),
// the impact sound on frame 29 (109 the second time, state_counter 0x01): 7 into the corner, else
// the wall's kind, and the events 2657 / 2658 (cage) or 2645 / 2646 (strong / weak whip). Step 6:
// the reversal's follow (event 73).
void state_0C_whip_runner(WrestlerCtx *ctx) {
    Wrestler *s = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (s->state_step) {
    case 0:
        wrestler_set_anim(s, o->anim_id + 1, 2);
        if (s->state_flags & 0x1C)
            s->state_step = 4;
        else if (o->flags78 & 0x2000000)
            s->state_step = 6;
        else
            s->state_step++;
        return;
    case 1:
        if (!(o->flags78 & 0x100))
            keep_in_bounds_pair(s, o, nullptr, 1.0f, 2);
        if (s->anim_frames_left > 0)
            return;
        if (s->flags_f0 & 4) {
            wrestler_set_anim(s, 11985, 0);
            wrestler_pair_set_state_move(s, state::IrishWhipAttacker, o, state::IrishWhipRunner);
            s->state_step = 3;
            o->state_step = 3;
            s->flags78 = o->flags78 & 0x100;
            o->flags86 = s->flags86;
            o->dir = s->dir;
        } else {
            wrestler_set_anim(s, o->anim_id + 1, 0);
            s->state_step++;
        }
        return;
    case 2:
        if (s->anim_id == 11988) {
            keep_in_bounds_pair(s, o, nullptr, 4.0f, 0xFFFFE);
            move_event(ctx, 594);
            return;
        }
        if (!(o->flags78 & 0x100))
            keep_in_bounds_pair(s, o, nullptr, 1.0f, 2);
        if (s->anim_frames_left > 0)
            return;
        vec_from_angle(s->facing, walk_speed(s, 8), &s->step_vec);
        wrestler_set_state_now(s, state::Running);
        s->state_step = 2;
        s->rebound_count = 0;
        return;
    case 3:
        if (!(s->flags78 & 0x100))
            keep_in_bounds_pair(s, o, nullptr, 1.0f, 2);
        if (s->anim_frames_left > 0)
            return;
        wrestler_set_anim(s, o->anim_id + 1, 0);
        s->state_step--;
        return;
    case 4:
        if (s->anim_frame == s->anim_player[16] && (s->flags_f0 & 4)) {
            wrestler_set_generic_move(s, 4);
            hit_move_copy(s, s);
            spirit_exchange(s, o, s);
            u16 sf = s->state_flags;
            wrestler_pair_set_state_move(s, state::IrishWhipAttacker, o, state::IrishWhipRunner);
            s->state_step = 6;
            o->state_step = 5;
            s->flags78 = o->flags78;
            o->state_flags = sf;
            if (o->state_flags & 0x18)
                o->grab_zone_info = s->grab_zone_info;
            o->state_counter |= 1;
            if (o->state_flags & 0x10) {
                wrestler_set_anim(s, 12826, 0);
                wrestler_set_anim(o, 12825, 0);
            } else {
                wrestler_set_anim(s, 12001, 3);
                wrestler_set_anim(o, 12002, 3);
            }
            return;
        }
        [[fallthrough]];
    case 5: {
        if (s->state_flags & 0x18)
            pair_follow(s, o, nullptr, 4.0f, 0, 0xFFFFE);
        s32 ev;
        if (s->state_flags & 0x10) {
            ev = (s->state_counter & 1) ? 2658 : 2657;
        } else {
            if (s->anim_frame == ((s->state_counter & 1) ? 109 : 29))
                host_sfx_slot(s->slot, (s->state_flags & 4) ? 7 : (s16)zone_kind(s->grab_zone_info));
            ev = (o->flags78 & 4) ? 2645 : 2646;
        }
        move_event(ctx, (u32)ev);
        return;
    }
    case 6:
        pair_follow(s, o, nullptr, 1.0f, 0, 2);
        move_event(ctx, 73);
        return;
    default:
        return;
    }
}

// N64 0x800E9680: state 0x5F, a move from a carry or pick-up, the attacker (paired with 0x60).
// Unless a follow-up move takes over (move_follow_up): kept in bounds against the opponent, on the
// last frame a move with record flag 0x80000 gives the opponent flags8c 0x400, the move's objects and
// texture stamp, the finisher's effects, and the animation's events (0x2480).
void state_5F_carry_move(WrestlerCtx *ctx) {
    if (move_follow_up(ctx))
        return;
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    state_bounds(w, o, nullptr, 0);
    if (w->anim_frames_left == 1 && (mem_u32(w->move + 0x20) & 0x80000))
        o->flags8c |= 0x400;
    object_spawn_for_move(w, o, 0);
    move_texture_stamp(w, o);
    finisher_effects(w);
    move_event(ctx, 0x2480);
}

// N64 0x8012C044: wrestler_move_step(w, none, scale, 0, slot, 1): a slide that ignores the wrestler
// in `slot` (the one being carried or held).
s32 wrestler_slide_with(Wrestler *w, s32 slot, f32 scale) { return wrestler_move_step(w, nullptr, scale, 0, slot, 1); }

// N64 0x8011A2E4: one frame of walking while holding the opponent (`held`, kept `dist` in front):
// the d-pad moves the holder at walk speed (class 0x20): on the apron (flags78 0x20) only along its
// side (0x80146590 by ring_zone & 1: x for even sides, z for odd), else in the eight directions
// (diagonals x 0.707). The 4.0 slide ignoring the held one, which follows by the same x / z, the pair
// kept in bounds; off the apron A / B turn the pair by +-34 and the holder is put back `dist` in front
// of the held one.
void hold_for_partner_walk(Wrestler *w, Wrestler *held, u16 buttons, f32 dist) {
    Vec3 start = w->pos;
    Vec3 d = {0.0f, 0.0f, 0.0f};
    if (w->flags78 & 0x20) {
        s32 odd = w->ring_zone & 1;
        u16 mask = mem_u16(0x80146590 + 4u * (u32)odd) | mem_u16(0x80146592 + 4u * (u32)odd);
        if (buttons & mask) {
            f32 v = walk_speed(w, 0x20);
            if (odd)
                d.z = (buttons & 0x800) ? v : -v;
            else
                d.x = (buttons & 0x100) ? v : -v;
        }
    } else if (buttons & 0xF00) {
        f32 v = walk_speed(w, 0x20);
        if (buttons & 0x800)
            d.z = v;
        if (buttons & 0x100)
            d.x = v;
        if (buttons & 0x400)
            d.z = -v;
        if (buttons & 0x200)
            d.x = -v;
        if (!(dpad_dir8(buttons) & 1)) {
            d.x = (f32)((f64)d.x * 0.707106781);
            d.z = (f32)((f64)d.z * 0.707106781);
        }
    }
    wrestler_translate(w, &d);
    wrestler_slide_with(w, held->slot, 4.0f);
    Vec3 moved = {w->pos.x - start.x, 0.0f, w->pos.z - start.z};
    wrestler_translate(held, &moved);
    keep_in_bounds_pair(w, held, nullptr, 2.0f, 2);
    if (w->flags78 & 0x20)
        return;
    s32 turn = (buttons & 0x8000) ? 34 : 0;
    if (buttons & 0x4000)
        turn = -34;
    w->facing = angle_add(w->facing, turn);
    held->facing = w->facing;
    Vec3 off = {0.0f, 0.0f, dist}, at, back;
    point_at_offset(&held->pos, &off, angle_add(held->facing, 2048), &at);
    at.y = w->pos.y;
    vec_sub(&w->pos, &at, &back);
    wrestler_translate(w, &back);
}

// N64 0x80119E00: state 0x7F, the lift from behind (paired with 0x80): step 0 flags80 0x01, animation
// 12641 (12635 from the apron, flags78 0x20; with flags78 0x180 the slide into place first). Step 1:
// when the lift is up, the hold distance (0x80159240 by slot) and the carry animation 12643 / 12637.
// Step 2: off the apron flags80 0x240; while the opponent is held (0x80) the walk
// (hold_for_partner_walk), R or an interruption ends it (state_flags 0x01, then 0x04 and the step
// back), else unpaired and on. Step 3: the end animation (12639 from the apron; 12647, or 12645 /
// 12647 by state_flags 0x01 with the opponent still held), then the events with flags80 0x01.
void state_7F_rear_lift(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    u32 hold_at = 0x80159240 + 4u * (u32)ctx->slot;
    switch (w->state_step) {
    case 0:
        w->flags80 |= 1;
        if ((w->flags78 & 0x180) == 0x180)
            NM_UNPORTED("state_7F_rear_lift: the slide into place (pair_scripted_place, state 0x7F)");
        wrestler_set_anim(w, (w->flags78 & 0x20) ? 12635 : 12641, 2);
        w->state_step++;
        [[fallthrough]];
    case 1:
        if (w->state_counter & 0x20)
            pair_slide_step(w, nullptr);
        if (w->anim_frames_left > 0)
            return;
        mem_wf32(hold_at, dist_to(&w->pos, &o->pos));
        wrestler_set_anim(w, (w->flags78 & 0x20) ? 12637 : 12643, 0);
        w->state_step++;
        return;
    case 2:
        if (!(w->flags78 & 0x20))
            w->flags80 |= 0x240;
        if (o->state != state::RearLiftDefender) {
            wrestler_unpair(w);
            w->state_step++;
            return;
        }
        hold_for_partner_walk(w, o, ctx->buttons, mem_f32(hold_at));
        if ((ctx->pressed & 0x10) || match_can_interrupt())
            w->state_flags |= 1;
        if (w->state_flags & 3) {
            w->state_flags |= 4;
            wrestler_set_step(w, -1);
        }
        return;
    case 3: {
        s32 anim = 12639;
        if (!(w->flags78 & 0x20)) {
            anim = 12647;
            if (o->state == state::RearLiftDefender)
                anim = (w->state_flags & 1) ? 12645 : 12647;
        }
        wrestler_set_anim(w, anim, 0);
        w->state_step++;
        [[fallthrough]];
    }
    case 4:
        w->flags80 |= 1;
        keep_in_bounds_self(w, 2.0f, 0, 2);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}

// N64 0x800E9724: state 0x60, the receiver of a move from a carry or pick-up (paired with 0x5F):
// kept in bounds against the attacker; on the last frame the end pose, settled, and (unless the move
// gave flags8c 0x400) the move's aftermath; then the finisher's effects.
void state_60_carry_move_defender(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    state_bounds(w, ctx->rec_a, nullptr, 0);
    if (w->anim_frames_left == 1) {
        anim_end_apply(w);
        wrestler_settle_pose(w);
        if (!(w->flags8c & 0x400))
            move_aftermath_check(ctx->rec_a, ctx->rec_a, w, 2);
        w->flags8c &= ~0x400u;
    }
    finisher_effects(w);
}

// N64 0x8011A06C: state 0x80, lifted from behind (paired with 0x7F). Step 0: from the apron (the
// lifter's flags78 0x20) the slide into place, else the struggle 12642 and step 2. Step 2: when the
// lift is up, the lifter's animation + 1, the escape time (escape_time op 5, 1.5 times against a strong
// hold) and the mash count reset. Step 3: off the apron flags80 0x280; while the lifter holds (0x7F):
// its state_flags 0x04 steps back, else mashing (recover_tick) breaks out (escape 0 with the stick
// held, else the lifter's state_flags 0x02), kept in bounds off the apron; when it lets go, on. Step 4:
// the end animation (12640 from the apron; 12648, 12646 when the lifter has state_flags 0x01), then
// the events with flags80 0x01.
void state_80_carried_escape(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    Wrestler *o = ctx->rec_a;
    switch (w->state_step) {
    case 0:
        if (o->flags78 & 0x20)
            NM_UNPORTED("state_80_carried_escape: the apron lift's slide (pair_scripted_place, state 0x80)");
        wrestler_set_anim(w, 12642, 6);
        w->state_step = 2;
        return;
    case 1:
        if (pair_slide_step(w, nullptr))
            w->state_step++;
        return;
    case 2: {
        if (w->anim_frames_left > 0)
            return;
        wrestler_set_anim(w, o->anim_id + 1, 0);
        s32 e = escape_time(w, o, 5);
        w->escape = (s16)e;
        if (o->flags78 & 4)
            w->escape = (s16)(e + (s16)e / 2);
        mash_count(w, 0, 0);
        w->state_step++;
        return;
    }
    case 3:
        if (!(o->flags78 & 0x20))
            w->flags80 |= 0x280;
        if (o->state != state::RearLiftAttacker) {
            w->state_step++;
            return;
        }
        if (o->state_flags & 4) {
            wrestler_set_step(w, -1);
        } else if (recover_tick(w, ctx->pressed)) {
            if (ctx->in_byte)
                w->escape = 0;
            else
                o->state_flags |= 2;
        }
        if (!(o->flags78 & 0x20))
            keep_in_bounds_pair(w, o, nullptr, 2.0f, 2);
        return;
    case 4: {
        s32 anim = 12640;
        if (!(o->flags78 & 0x20)) {
            anim = 12648;
            if (o->state == state::RearLiftAttacker && (o->state_flags & 1))
                anim = 12646;
        }
        wrestler_set_anim(w, anim, 0);
        w->state_step++;
        [[fallthrough]];
    }
    case 5:
        w->flags80 |= 1;
        keep_in_bounds_self(w, 2.0f, 0, 2);
        move_event(ctx, 0);
        return;
    default:
        return;
    }
}


// N64 0x800E93D8: state 0x5B, catching a whipped runner: step 0 the catch (the move after move_num,
// its animation); step 1 the finisher effects and the events (0x2480); the pair's bounds every frame.
void state_5B_counter_runner(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_start_move(w, ctx->slot, w->move_num + 1);
        wrestler_set_anim(w, anim_style(w, 0), 3);
        w->state_step++;
        break;
    case 1:
        finisher_effects(w);
        move_event(ctx, 0x2480);
        break;
    default:
        break;
    }
    state_bounds(ctx->self, ctx->rec_a, nullptr, 0);
}

// N64 0x800E9480: state 0x5C, the runner caught: step 0 the catcher's move as the defender (anim_style
// 1) and the slide to it (pair_scripted_place); then the slide, the finisher effects and the events
// (0x1A00); the pair's bounds every frame.
void state_5C_runner_countered(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    switch (w->state_step) {
    case 0:
        wrestler_set_anim(w, anim_style(ctx->rec_a, 1), 2);
        pair_scripted_place(w, ctx->rec_a);
        w->state_step++;
        [[fallthrough]];
    case 1:
        pair_slide_step(w, nullptr);
        finisher_effects(w);
        move_event(ctx, 0x1A00);
        break;
    default:
        break;
    }
    state_bounds(ctx->self, ctx->rec_a, nullptr, 0);
}

}  // namespace nm

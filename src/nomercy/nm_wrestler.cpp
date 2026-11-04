// The wrestler update: the update passes, the per-slot tick (input, CPU, state handler, animation
// end), wrestler_update and the small per-frame helpers it runs, and the state setters.
#include "nm_funcs.h"

namespace nm {

// ---- state setters ----

// N64 0x800EB5E0
void wrestler_set_state(Wrestler *w, s32 state) {
    if (w->state != state::SettleAfterAnimation) {
        w->prev_state = w->state;
        w->prev_step = w->state_step;
    }
    w->state = state;
    w->state_step = 0;
    w->state_counter = 0;
    w->state_flags = 0;
    w->state_timer = 0;
    w->call_flags = 0;
    w->stay_flags = 0;
    w->bounds_override = 0;
    w->flags88 = (w->flags88 & 0xFFFE) | 0x10;
    w->flags8c &= 0xFFEF;
    w->flags8e &= 0xFFBE;
    w->flags_f7 &= 0xFE;
    w->flags80 |= 0x01;
    w->flags8a &= 0xFFDF;
    if (state == 0x01 || state == 0x31 || state == 0x18 || state == 0x32)
        w->flags8c &= 0xCFFF;
    if (state != 0x31)
        w->flags8c &= 0xF7FF;
}

// N64 0x800EB6D4: also runs the new state again this frame.
void wrestler_set_state_now(Wrestler *w, s32 state) {
    wrestler_set_state(w, state);
    w->flags88 |= 0x01;
}

// N64 0x800EB760
void wrestler_unpair(Wrestler *w) {
    w->pair_slot = -1;
    w->pair_slot2 = -1;
    w->flags88 &= 0xFFFB;
}

// N64 0x800EB704
void wrestler_set_state_free(Wrestler *w, s32 state) {
    wrestler_set_state(w, state);
    wrestler_unpair(w);
}

// N64 0x800EB730
void wrestler_set_state_free_now(Wrestler *w, s32 state) {
    wrestler_set_state_free(w, state);
    w->flags88 |= 0x01;
}

// N64 0x800EBA84: turns the facing toward `angle` by at most `step`; on arrival sets `flag` in
// flags8a and returns 1.
s32 wrestler_turn_toward(Wrestler *w, s32 angle, s32 step, u16 flag) {
    s32 d = angle_diff(angle, w->facing);
    if (abs_i(d) < step) {
        w->facing = angle;
        w->flags8a |= flag;
        return 1;
    }
    w->facing = angle_add(w->facing, d >= 0 ? step : -step);
    return 0;
}

// ---- animation end ----

// N64 0x80130170
s32 pose_lying(const Wrestler *w) { return (u32)(w->anim_player[AP_END_POSE] - 20) < 28; }

// N64 0x80130180: for an animation ending lying down (poses 20-47), the side (flags8a 0x01 on odd
// poses) and the slouch (flags80 0x10 for poses 44 / 45).
s32 pose_is_lying(Wrestler *w) {
    u8 pose = w->anim_player[AP_END_POSE];
    if (!((u32)(pose - 20) < 28))
        return 0;
    if (pose & 1)
        w->flags8a |= 0x01;
    else
        w->flags8a &= 0xFFFE;
    if ((u32)(w->anim_player[AP_END_POSE] - 44) < 2)
        w->flags80 |= 0x10;
    else
        w->flags80 &= 0xFFEF;
    return 1;
}

// N64 0x801300F0: moves the wrestler to the animation's end place.
void anim_end_location(Wrestler *w) {
    switch (w->anim_player[AP_END_PLACE]) {
    case 1: w->location = (w->location & 0xF800) | 0x001; break;
    case 2: w->location = (w->location & 0xF800) | 0x004; break;
    case 3: w->location = (w->location & 0xF800) | 0x002; break;
    case 6: w->location = (w->location & 0xF800) | 0x400; break;
    case 7: w->location = (w->location & 0xFBE0) | 0x020; break;
    default: break;
    }
}

// ---- input ----

// N64 0x80132940: applies the slot's button configuration (9 bytes at 0x800A8760 + 9 * pad) to
// one input word: each configured button found is replaced by the action it is set to.
void input_remap(s32 pad, u16 *word) {
    u16 out = *word & 0x1FC0;
    u16 seen = 0;
    u16 bit = 0;
    for (s32 i = 0; i < 8; i++) {
        switch (mem_u8(0x800A8760 + 9 * pad + i)) {
        case 0: bit = 0x8000; break;
        case 1: bit = 0x4000; break;
        case 2: bit = 0x0020; break;
        case 3: bit = 0x0010; break;
        case 4: bit = 0x0008; break;
        case 5: bit = 0x0004; break;
        case 6: bit = 0x0002; break;
        case 7: bit = 0x0001; break;
        case 8: bit = 0x2000; break;
        default: break;  // keeps the previous button, as the original does
        }
        seen |= bit;
        if (*word & bit) {
            *word &= ~bit;
            out |= mem_u16(0x8014DC30 + 2 * i);
        }
    }
    if (*word & ~seen & 0xE03F)
        out |= mem_u16(0x8014DC40);
    *word = out;
}

// N64 0x80132A48: shifts the slot's button histories and fills the combined words.
void input_history_push(s32 slot) {
    InputSlot *in = &g_input[slot];
    for (s32 i = 8; i >= 0; i--) {
        in->held_hist[i + 1] = in->held_hist[i];
        in->pressed_hist[i + 1] = in->pressed_hist[i];
    }
    in->held_hist[0] = in->held;
    in->pressed_hist[0] = in->pressed;
    u16 prev_held = in->held_hist[1];
    u16 prev_pressed = in->pressed_hist[1];
    in->held_prev = prev_held;
    in->held2 = in->held_hist[0] | prev_held;
    in->pressed2 = in->pressed_hist[0] | prev_pressed;
}

// ---- small per-frame helpers ----

// N64 0x800ED9C4: 1 while play is stopped for everyone (the match is over, a cut-scene, a special
// state 0xBE / 0xD0 / 0xD1 under way).
s32 match_can_interrupt() {
    if (g_match_flags[1] & 0x40)
        return 1;
    if (g_match_flags[0] & 0x02)
        return 1;
    if ((mem_u16(A_RUNIN_FLAGS) & 0x800) && !(g_match_type & 0x21000))
        return 1;
    if (any_wrestler_flag8c_8000())
        return 1;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1)
            continue;
        if ((u32)(w->state - state::DragAttacker) < 2 || w->state == state::WalkInToStart)
            return 1;
    }
    return 0;
}

// N64 0x800EE3B0: the fall while flags8a 0x8000 (through a table, off the turnbuckle ...).
void airborne_fall_step(Wrestler *w) {
    if (!(w->flags8a & 0x8000))
        return;
    if (w->anim_player[AP_END_PLACE] == 7) {
        f32 low = 10000.0f;
        for (s32 i = 1; i < 20; i++) {
            const Vec3 *j = wrestler_joint(w, i - 1);
            if (j->y < low)
                low = j->y;
        }
        if (low < absf(w->fall_vy))
            w->pos.y = w->pos.y - low;
        else
            w->pos.y = w->pos.y + w->fall_vy;
        w->fall_vy = w->fall_vy + w->fall_accel;
        return;
    }
    if (w->flags8a & 0x40)
        return;
    f32 y = w->pos.y + w->fall_vy;
    f32 vy = w->fall_vy + w->fall_accel;
    w->pos.y = y;
    w->fall_vy = vy;
    if ((f64)w->pos.y <= 0.0) {
        w->pos.y = 0.0f;
        w->flags8a &= 0x7FFF;
    }
}

// N64 0x80122AB0: op 0 holds the hit count for 60 frames; op 1 counts down, then clears it.
void hits_taken_decay(Wrestler *w, s32 op) {
    u32 a = 0x80159270 + 4 * w->slot;
    if (op == 0) {
        mem_w32(a, 60);
    } else if (op == 1) {
        s32 t = mem_s32(a);
        if (t > 0)
            mem_w32(a, (u32)(t - 1));
        else
            w->hits_taken = 0;
    }
}

// N64 0x8011D100: the attitude meter. 0 restart the drift, 1 the 120-frame drift toward 50 (or -8
// while hiding outside), 2 the meter modes' countdown, 3 shorten a mode, 4 empty, 5 the taunt
// (SPECIAL -> finisher mode; 1 when it happened), 7 reset to 50.
s32 spirit_update(Wrestler *w, s32 op) {
    switch (op) {
    case 0:
        w->spirit_timer = 120;
        break;
    case 1:
        if (w->flags_ee & 7)
            return 0;
        if (w->location & 0x400)
            return 0;
        if (--w->spirit_timer > 0)
            return 0;
        w->spirit_timer = 120;
        if (!(w->location & 3) && (g_wrestlers[w->opponent].location & 3)) {
            w->spirit -= 8;
            if (w->spirit > 0)
                return 0;
            w->spirit = 1;
            break;
        }
        if (w->spirit >= 51)
            w->spirit = w->spirit - 1;
        else if (w->spirit != 50)
            w->spirit = w->spirit < 31 ? w->spirit + 3 : w->spirit + 2;
        break;
    case 2:
        if (!(w->flags_ee & 7))
            return 0;
        if (--w->spirit_timer > 0) {
            if (!(w->flags_ee & 2) || w->spirit >= 100)
                return 0;
            w->spirit_timer = 120;
            w->flags_ee = 0;
            break;
        }
        if (w->flags_ee & 1)
            w->spirit = 40;
        if (w->flags_ee & 2)
            w->spirit = 70;
        if (w->flags_ee & 4)
            w->spirit = 60;
        w->spirit_timer = 120;
        w->flags_ee = 0;
        break;
    case 3:
        if (!(w->flags_ee & 6))
            return 0;
        w->spirit_timer -= 60;
        break;
    case 4:
        w->spirit_timer = 300;
        w->spirit = 0;
        w->flags_ee = 1;
        break;
    case 5:
        if (!(w->flags_ee & 2))
            return 0;
        w->spirit_timer = 600;
        w->flags_ee = 4;
        return 1;
    case 7:
        w->spirit = 50;
        w->spirit_timer = 120;
        w->flags_ee = 0;
        break;
    default:
        break;
    }
    return 0;
}

// N64 0x80137F24: counts frames while idle_flags 0x01 is set; past the limit sets 0x02.
void idle_timer(Wrestler *w) {
    if (!(w->idle_flags & 1)) {
        w->idle_flags &= 0xFD;
        w->idle_count = 0;
        return;
    }
    w->idle_count++;
    if (mem_s16(0x8014DCA4 + 2 * mem_s8(0x800BD929)) < w->idle_count)
        w->idle_flags |= 0x02;
}

// N64 0x80137F84
void tag_rules_check(Wrestler *w) {
    if ((g_match_type & 0x2000) && !(g_match_rules & 0x40) && (w->flags_e9 & 0x10))
        w->tag_state++;
}

// N64 0x8011CCF0: op 0 stops the flash, op 1 counts it down (flags_ee 0x20 while it runs).
void hit_flash(Wrestler *w, s32 op) {
    if (op == 0) {
        w->hit_flash_timer = 0;
        return;
    }
    if (op != 1)
        return;
    w->flags_ee &= 0xDF;
    if (--w->hit_flash_timer >= 0)
        w->flags_ee |= 0x20;
    else
        w->hit_flash_timer = 0;
}

// N64 0x8011D7EC: a fresh run-in (flags_f1 0x02) is no longer fresh once it is hurt.
void run_in_fresh_clear(Wrestler *w) {
    if (!(w->flags_e9 & 2) || !(w->flags_f1 & 2) || w->health >= 249)
        return;
    if ((g_match_type & 0x400000) && !(w->flags_e9 & 8))
        return;
    w->flags_f1 &= 0xFD;
}

// N64 0x8012C244: per-step automatic movement of a state (flags table 0x8014D3C0): when the bit
// of the state step is in `flags`, the push against the other bodies with the speed 0x8014D268[kind]
// (none for kind -1); in the lock-up 0x51 / 0x52 (flags78 0x600 without 0x100) the mask 768 or
// 3072 by the zone bit of whoever leads. flags8a 0x2000 (no push) when the opponent is in state
// `step_from` and the animation is past the player's byte 0x11, or when lying (unless flags
// 0x4000000), or with flags 0x8000000.
void state_move_step(WrestlerCtx *ctx, s32 step_from, s32 kind, u32 flags) {
    Wrestler *w = ctx->self;
    u32 bit = 1;
    for (s32 i = 0; i < w->state_step; i++)
        bit <<= 1;
    if (!(flags & bit))
        return;
    u32 mask = 0;
    if (w->state == state::LockupAttacker) {
        u32 f = w->flags78;
        if (!(f & 0x100) && (f & 0x600)) {
            s32 lead = (f & 0x200) ? (ctx->rec_a->joint_zone & 1) : (w->joint_zone & 1);
            mask = lead ? 3072 : 768;
        }
    }
    if (w->state == state::LockupDefender) {
        u32 f = ctx->rec_a->flags78;
        if (!(f & 0x100) && (f & 0x600)) {
            s32 lead = (f & 0x200) ? (w->joint_zone & 1) : (ctx->rec_a->joint_zone & 1);
            mask = lead ? 3072 : 768;
        }
    }
    if (ctx->rec_a->state == step_from && !(w->anim_player[AP_PAIR_FRAME] < w->anim_frame)) {
        w->flags8a |= 0x2000;
        return;
    }
    if (!(flags & 0x4000000)) {
        if (pose_lying(w))
            w->flags8a |= 0x2000;
        if (flags & 0x8000000)
            w->flags8a |= 0x2000;
    }
    if (kind != -1)
        wrestler_move_step(w, nullptr, mem_f32(0x8014D268 + 4u * (u32)kind), mask, -1, 1);
}

// N64 0x8012C164
void state_move_step_auto(WrestlerCtx *ctx) {
    Wrestler *w = ctx->self;
    if (w->flags8c & 0x200)
        return;
    s32 state = w->state;
    u32 flags = mem_u32(0x8014D3C0 + 4 * state);
    s32 from = (s32)flags < 0 ? state - 1 : -1;
    if (flags & 0x40000000)
        from = state + 1;
    s32 kind = (flags & 0x20000000) ? 3 : -1;
    if (flags & 0x10000000)
        kind = 1;
    if (flags & 0x04000000) {
        if (!(w->anim_player[AP_HOLD_FRAME] < w->anim_frame)) {
            kind = 1;
        } else {
            kind = 3;
            u8 pose = w->anim_player[AP_END_POSE];
            if ((u32)(pose - 20) < 28 || pose == 2)
                w->flags8a |= 0x6000;
        }
    }
    state_move_step(ctx, from, kind, flags);
}

// ---- the update ----

// N64 0x800E5890: one slot's state handler, with the context and the per-frame clearing.
void wrestler_update(s32 slot) {
    WrestlerCtx *c = &g_wrestler_ctx;
    Wrestler *self = &g_wrestlers[slot];
    c->slot = slot;
    c->slot_a = self->opponent;
    c->slot_b = self->partner;
    c->slot_c = self->pair_other;
    c->slot_d = self->slot_d;
    c->self = self;
    c->rec_a = &g_wrestlers[0] + c->slot_a;
    c->rec_b = &g_wrestlers[0] + c->slot_b;
    c->rec_c = &g_wrestlers[0] + c->slot_c;
    c->rec_d = &g_wrestlers[0] + c->slot_d;
    InputSlot *in = &g_input[slot];
    c->buttons = in->held;
    c->pressed = in->pressed;
    c->in_0a = in->held2;
    c->in_0c = in->pressed2;
    c->in_0e = in->held_prev;
    c->in_s16_a = in->stick_x;
    c->in_s16_b = in->stick_y;
    c->in_byte = (s8)in->stick_pushed;
    self->partner_flags80 = g_wrestlers[self->partner].flags80;
    self->state_timer++;
    self->idle_flags &= 0xE7;
    self->flags80 &= 0xF810;
    self->location &= 0xE7FF;
    self->flags88 &= 0xFFFE;
    self->flags_ee &= 0xEF;
    self->flags86 &= 0xDFFF;
    self->flags_f7 &= 0xF3;
    self->flags8a &= 0x94FF;
    self->flags8c &= 0xFF97;
    self->flags8e &= 0xFFC1;
    self->frame_flags = 0;
    self->dpad_held = 0;
    self->act_flags = 0;
    self->contact = 0;
    self->_unk068 = -1;
    g_frame_bits |= mem_u32(A_SLOT_BITS + 4 * c->slot);
    if (c->buttons & 0x2000)
        self->flags8c |= 0x08;
    if (match_can_interrupt()) {
        self->flags80 |= 0x01;
        c->buttons = 0;
        c->pressed = 0;
        c->in_byte = 0;
    }
    if (self->flags_f1 & 1)
        self->flags80 |= 0x01;
    airborne_fall_step(self);
    self->flags8a &= 0xFFBF;
    self->ring_zone = ring_zone_of(&self->pos);
    self->joint_zone = ring_zone_of(&self->point_120);
    hits_taken_decay(self, 1);
    spirit_update(self, 2);
    idle_timer(self);
    tag_rules_check(self);
    hit_flash(self, 1);
    run_in_fresh_clear(self);
    timed_press_check(self, c->buttons, c->pressed, 0);
    state_move_step_auto(c);
    self->opp_distance = dist_to(&self->point_120, &c->rec_a->point_120);
    self->facing_to_opponent = angle_to(&self->point_120, &c->rec_a->point_120);
    state_handler(self->state)(c);
}

// N64 0x80118368: one slot's frame: its input (pad or CPU), the state handler (run again while
// flags88 0x01 asks), then the animation-end bookkeeping.
void wrestler_tick(s32 slot) {
    Wrestler *w = &g_wrestlers[slot];
    if (w->roster_id == -1)
        return;
    InputSlot *in = &g_input[slot];
    if (!(g_match_type & 0x2000000)) {
        s32 pad = mem_s32(A_SLOT_PAD + 4 * slot);
        const Pad *p = &g_pads[pad];
        in->held = p->held;
        in->pressed = p->pressed;
        input_remap(pad, &in->held);
        input_remap(pad, &in->pressed);
        in->stick_x = p->stick_x;
        in->stick_y = p->stick_y;
        // Z with a C button: switch the slot between the player and the CPU.
        s32 toggle = (p->pressed & 0x2000) ? (p->held & 0xF) != 0 : 0;
        if (toggle) {
            u32 type = g_match_type;
            if ((type & 0x1000) && !(mem_u16(0x8016B9CC) & 1) && (!(w->flags_e9 & 3) || (type & 0x400000))) {
                toggle = 0;
                type = g_match_type;
            }
            if (type & 0x3A000)
                toggle &= -(s32)(mem_u16(0x8016B9CC) & 1);
            if ((type & 0x4000) && !(mem_u16(0x8016B9CC) & 1)) {
                if (type & 0x600000)
                    toggle = 0;
                else if (g_match_rules & 0x100000)
                    toggle = 0;
            }
            if (toggle) {
                if (w->flags_e9 & 0x08) {
                    w->flags_e9 &= 0xF7;
                } else {
                    w->flags_e9 |= 0x08;
                    mem_w32(A_AI_BLOCKS + 176 * slot, 0);
                }
                if (g_match_type & 0x4000)
                    mem_w8(0x800A3DC0 + 28 * slot + ((w->flags_e9 & 0x08) ? 1 : 0), 1);
            }
        }
    }
    if (w->flags_e9 & 0x08) {
        if (w->state == state::RunIn) {
            in->held = 0;
            in->pressed = 0;
            in->stick_x = 0;
            in->stick_y = 0;
        } else {
            do
                ai_update((s8)slot);
            while (w->flags88 & 1);
            in->held = mem_u16(A_AI_BLOCKS + 176 * slot + 0x0C);
            in->pressed = mem_u16(A_AI_BLOCKS + 176 * slot + 0x0E);
            in->stick_x = mem_s8(A_AI_BLOCKS + 176 * slot + 0x12);
            in->stick_y = mem_s8(A_AI_BLOCKS + 176 * slot + 0x13);
        }
    }
    if (abs_i(in->stick_x) < 41 && abs_i(in->stick_y) < 41)
        in->stick_pushed = 0;
    else
        in->stick_pushed = 1;
    input_history_push(slot);
    do
        wrestler_update(slot);
    while (w->flags88 & 1);

    s32 frame = w->anim_frame;
    if (frame == 0)
        frame = 1;
    u16 f80 = w->flags80;
    if (f80 & 1) {
        if (!(frame < w->anim_player[AP_FRAMES_B]) && w->anim_id != 0x30E7 && w->anim_id != 0x31C9) {
            w->flags80 = f80 | 8;
            pose_is_lying(w);
            anim_end_location(w);
        }
    } else {
        if (!(frame < w->anim_player[AP_FRAMES_B]) && w->anim_id != 0x30E7 && w->anim_id != 0x31C9) {
            w->flags80 = f80 | 8;
            pose_is_lying(w);
            anim_end_location(w);
        }
        u16 f = w->flags80;
        if (!(f & 8) && !(frame < w->anim_player[AP_ACTIVE_FROM]) && !(w->anim_player[AP_ACTIVE_TO] < frame))
            w->flags80 = f | 1;
    }
    w->flags88 |= 2;
}

// N64 0x80117F28: op 0 resets (start slot, state history); op 1 runs the frame's update passes.
void wrestlers_update_all(s32 op) {
    if (op == 0) {
        g_update_first_slot = 0;
        for (s32 i = 0; i < 4; i++)
            for (s32 k = 15; k >= 0; k--)
                g_state_history[i][k] = 0;
        return;
    }
    if (op != 1)
        return;
    for (s32 i = 0; i < 4; i++)
        g_wrestlers[i].flags88 &= 0xFFED;
    wrestler_joints_update();
    // Pass 1: wrestlers in a paired move (flags88 0x04), each followed by its partners.
    s32 s = g_update_first_slot;
    do {
        Wrestler *w = &g_wrestlers[s];
        if ((w->flags88 & 4) && !(w->flags88 & 2)) {
            wrestler_tick(s);
            for (s32 k = 0; k < 2; k++) {
                s32 p = (&w->pair_slot)[k];
                if (p != -1 && !(g_wrestlers[p].flags88 & 2))
                    wrestler_tick(p);
            }
        }
        s = (s + 1) & 3;
    } while (s != g_update_first_slot);
    // Pass 2: everyone not done yet.
    do {
        if (!(g_wrestlers[s].flags88 & 2))
            wrestler_tick(s);
        s = (s + 1) & 3;
    } while (s != g_update_first_slot);
    // Passes 3 and 4: wrestlers whose state asked to run again (flags88 0x01), around the objects.
    do {
        if (g_wrestlers[s].flags88 & 1)
            wrestler_tick(s);
        s = (s + 1) & 3;
    } while (s != g_update_first_slot);
    if (g_match_flags[0] & 8)
        objects_update();
    s = g_update_first_slot;
    do {
        if (g_wrestlers[s].flags88 & 1)
            wrestler_tick(s);
        s = (s + 1) & 3;
    } while (s != g_update_first_slot);
    g_update_first_slot = (s + 1) & 3;

    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        if (w->flags8a & 0x200)
            wrestler_turn_toward(w, angle_to(&w->point_120, &g_wrestlers[w->opponent].point_120), 341, 0);
    }
    objects_cleanup();
    objects_wrestler_contact();
    for (s32 i = 0; i < 4; i++)
        g_cpu_flags[i] = (g_wrestlers[i].flags_e9 & 8) ? 0 : 1;
    wrestlers_ground_contact();
    replay_record();
    // The state history: each wrestler's last 16 states other than standing and 0x30.
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        s32 st = w->state;
        if (st == state::SettleAfterAnimation || st == state::Standing)
            continue;
        if (st == g_state_history[i][0] && !(w->flags88 & 0x10))
            continue;
        for (s32 k = 15; k > 0; k--)
            g_state_history[i][k] = g_state_history[i][k - 1];
        g_state_history[i][0] = (s16)w->state;
    }
}

// N64 0x8012FEDC: copies each wrestler's posed joints from its model block into the record, and
// point_120 from joint 0 plus the model's offset, halved.
void wrestler_joints_update() {
    for (s32 i = 0; i < 4; i++) {
        Wrestler *w = &g_wrestlers[i];
        const ModelBlock *m = &g_models[i];
        for (s32 j = 1; j < 20; j++)
            *wrestler_joint(w, j - 1) = m->joint[j - 1].pt_a_world;  // joint 1 lands in point_120, overwritten below
        const Vec3 *a = &m->joint[0].pt_a_world, *b = &m->joint[21].pt_a_world;  // joint 1 and node 22
        w->point_120.x = (f32)((f64)(a->x + b->x) * 0.5);
        w->point_120.y = (f32)((f64)(a->y + b->y) * 0.5);
        w->point_120.z = (f32)((f64)(a->z + b->z) * 0.5);
    }
}

}  // namespace nm

// The CPU opponent: its per-frame update (ai_update), perception, the situation classes and the
// mode bookkeeping. The modes themselves (g_ai_modes, 145 entries) and the deciders are not
// decompiled yet: ai_mode_run names the mode it would run. Read from the code and asm (2026-11-03),
// not trace-checked. See _re_docs/individual_games/wwf_no_mercy/ai.md.
#include "nm_funcs.h"
#include <cstdio>
#include <cstring>

namespace nm {

namespace {

// The AI block (g_ai_blocks 0x801596C0, 176 bytes per slot), as far as this file uses it.
enum : u32 {
    AI_MODE = 0x00,        // s32: the mode (index into g_ai_modes)
    AI_STEP = 0x04,        // s32: the mode's step
    AI_TIMER = 0x08,       // s32: the mode's timer
    AI_HELD = 0x0C,        // u16: buttons held this frame (copied into the slot's input)
    AI_PRESSED = 0x0E,     // u16: buttons pressed this frame
    AI_STICK_X = 0x12,     // s8: the control stick
    AI_STICK_Y = 0x13,     // s8
    AI_LEVEL = 0x1C,       // s32: CPU level
    AI_MODE_TIMER = 0x18,  // s32: a mode's own timer
    AI_SKILL = 0x20,       // s32: skill 10-990 (ai_perceive's accuracy)
    AI_SIT_SELF = 0x24,    // s32: its own situation (ai_situation)
    AI_REGIONS = 0x28,     // f32[5]: its own body regions, perceived
    AI_HEALTH = 0x3C,      // s16: its own health, perceived
    AI_SPIRIT = 0x3E,      // s16: its own spirit, perceived
    AI_QUAD_SELF = 0x40,   // s16: its own ring quarter (ring_quadrant)
    AI_ZONE_SELF = 0x42,   // s16: its own ring zone (ring_zone_of)
    AI_LAST_STATE = 0x44,  // s16: its own state last seen by ai_choose_mode's focus count
    AI_SIT_OPP = 0x48,     // s32: the opponent's situation
    AI_OPP_HEALTH = 0x4C,  // s16: the opponent's health, perceived
    AI_OPP_SPIRIT = 0x4E,  // s16: the opponent's spirit, perceived
    AI_OPP_DOWN = 0x50,    // s16: how long the opponent will stay down, perceived
    AI_QUAD_OPP = 0x52,    // s16: the opponent's ring quarter
    AI_ZONE_OPP = 0x54,    // s16: the opponent's ring zone
    AI_OPP_STATE = 0x56,   // s16: the opponent's state when the mode was set
    AI_FOCUS_COUNT = 0x5E, // s16: hurt states left before a following wrestler re-focuses (0-7)
    AI_APRON_TIME = 0x58,  // s32: frames the legal man has been on the apron
    AI_RUN_TIME = 0x5C,    // s16: frames running without a target (state 0x03 without flags86 0x28, or 0x04)
    AI_DIST_P = 0x60,      // f32: the distance to the opponent, perceived
    AI_ANGLE = 0x64,       // s32: the angle to the opponent (point_120 to point_120)
    AI_DIST = 0x68,        // f32: the distance to the opponent
    AI_CORNERS = 0x6C,     // f32[4]: distances to the four ring corners
    AI_BODY_DIST = 0x7C,   // f32: the distance to the opponent's body (as if lying) + 90
    AI_FLAGS = 0x80,       // u8: 0x01 / 0x02 mode flags, 0x10 both have the 0x2000 trait, 0x20 an illegal man in tag, 0x40 over-the-top rules
    AI_WHERE = 0x82,       // u16: the where-class, both wrestlers' places (bits below; 0x1E00 kept)
    AI_MOVESET = 0x88,     // u32: the slot's move set (g_movesets 0x800AA090 + 19728 x slot)
    AI_LISTS = 0x8C,       // u32: the slot's 136-byte block at 0x8016B350
    AI_HEIGHT_DIFF = 0x90, // f32: |self.point_120.y - opponent.point_120.y|, perceived
};

enum : u32 {
    A_AI_SELF = 0x8016B8E8,        // Wrestler *: the CPU being updated
    A_AI_WRESTLERS = 0x8016B8EC,   // Wrestler *: g_wrestlers
    A_AI_OPP = 0x80159618,         // Wrestler *: its opponent
    A_AI_PARTNER = 0x8016B92C,     // Wrestler *: its partner
    A_AI_PARTNER_OPP = 0x8016B71C, // Wrestler *: the partner's opponent
    A_AI_OPP_PARTNER = 0x8016B720, // Wrestler *: the opponent's partner
    A_AI_BLOCK = 0x8015A6F0,       // u8 *: the CPU's AI block
    A_AI_SELF_POINT = 0x80159674,  // Vec3: a copy of its point_120
    A_AI_OPP_POINT = 0x8016B9C0,   // Vec3: a copy of the opponent's point_120
    A_MOVESETS = 0x800AA090,       // g_movesets: 548 move records (36 bytes) per slot
    A_AI_LIST_BLOCKS = 0x8016B350, // 136 bytes per slot
    A_AI_TABLES = 0x80145298,      // g_ai_mode_tables: weighted (threshold, mode, flags) lists
    A_AI_TABLES_B = 0x801452AC,    // the list used unless trait +39 is 2
    A_AI_STUCK_MODE = 0x801452C4,  // s16[4]: the mode the stuck-mode watchdog last saw
    A_AI_STUCK_TIME = 0x801452CC,  // s16[4]: frames it has been running
    A_AI_FLOOR_TIME = 0x80145210,  // u8[4][3]: minutes, seconds, frames on the floor
    A_AI_MODE_HISTORY = 0x8014521C, // s16[4][10]: the last ten modes
    A_STATE_HISTORY = 0x8016B5A0,  // s16[4][16]: recent states
    A_RING_CORNERS = 0x8014D1A8,   // Vec3[4]
};

// The ten personality bytes of the attribute record copied each frame (trait levels the
// deciders index chance arrays with; ai.md "Personality traits").
struct TraitCopy {
    u32 attr_off;
    u32 global;
};
const TraitCopy kTraits[10] = {
    {25, 0x80159624}, {26, 0x80159510}, {30, 0x80162A78}, {31, 0x8015A6F4}, {34, 0x80159980},
    {35, 0x8016B590}, {36, 0x8016B9E1}, {37, 0x8016B93C}, {38, 0x8016B9E0}, {39, 0x8016B9CE},
};

Wrestler *g_ai_self;
Wrestler *g_ai_opp;
u32 g_ai_blk;  // N64 address of the CPU's AI block

u32 blk(u32 off) { return g_ai_blk + off; }

}  // namespace

namespace {
// ---- forward declarations (functions defined further down) ----
void ai_set_mode_b(s32 mode);
void ai_try_grapple(s32 kind);
u16 ai_dir8_to_target();
s32 ai_chance_by_opp_health();
s32 ai_faced_by(const Wrestler *a, const Wrestler *b);
void ai_decide_engage_chance();
s32 ai_trait_s16(u32 off, s32 trait);
f32 ai_trait_f32(u32 off, s32 trait);
bool ai_losing_boost();
void ai_decide_close();
void ai_decide_opponent_focused();
void ai_decide_far();
void ai_decide_far_b();
void ai_decide_close_b();
s32 ai_decide_vs_down();
s32 ai_try_corner_climb();
s32 ai_list_pick_c(s32 slot);
s32 ai_list_pick_d(s32 slot);
s32 ai_finisher_check();
s32 ai_decide_finisher();
void ai_decide_move_list();
s32 ai_decide_vs_down_b();
void ai_decide_special();
s32 ai_list_pick_a(s32 slot);
s32 ai_list_pick_b(s32 slot);
s32 ai_try_pick_up();
void ai_decide_vs_lying_b();
s32 ai_decide_vs_lying_c();
void ai_decide_vs_lying();
s32 ai_tag_partner_check();
s32 ai_tag_help_check();
s32 ai_tag_reach_check();
s32 ai_tag_reach();
s32 ai_tag_extra();
s32 ai_decide_tag();
s32 ai_wants_tag_out(const Wrestler *w);
s32 ai_vs_runner_armed();
s32 ai_decide_ropes();
void ai_decide_wait_or_engage();
void ai_decide_in_ring_main();
s32 apron_walk_dpad(s32 slot);
s32 ai_trait_flag2();
s32 ai_apron_walk();
void ai_decide_apron_side();
void ai_decide_apron_vs_ring();
void ai_decide_apron_vs_apron();
s32 ai_decide_apron_jump();
void ai_decide_apron_vs_floor();
s32 ai_decide_floor_timeout();
s32 ai_decide_floor_b();
void ai_decide_floor();
s32 ai_list_filter(s32 count, s32 *fav, s32 *rest, u32 list);
s32 ai_decide_ring_edge(s32 kind);
s32 ai_list_pick_floor(s32 slot, u32 count_at, u32 list, s32 fav_chance, s32 climb_move);
s32 ai_list_pick_floor_a(s32 slot);
s32 ai_list_pick_floor_b(s32 slot);
void ai_decide_ring_vs_floor();
void ai_decide_floor_vs_ring();
s32 ai_decide_change_area();
s32 zone_is_turnbuckle_a(const Wrestler *w);
s32 zone_is_turnbuckle_b(const Wrestler *w);
void ai_decide_vs_cage_wall();
void ai_decide_cage_wall_vs_ring();
void ai_decide_both_cage_wall();
void ai_decide_both_raised();
void ai_decide_self_raised();
void ai_decide_opponent_raised();
s32 ai_ladder_climbers();
void ai_think();
void ai_decide_backstage();
void ai_set_mode_arg(s32 mode, s16 arg);
s32 ai_third_party_active();
f32 ai_side_distance(s32 side, const Vec3 *p);
s32 ai_side_target(s32 *out);
s32 ai_line_up(s32 *out);
s32 ai_try_ground_move(s32 kind);
s32 ai_try_run_at_dazed_opponent();
s32 ai_try_line_up_on_lying_opponent();
s32 ai_finisher_kind();
s32 ai_decide_vs_down_far(s32 slot);
s32 ai_decide_vs_down_near(s32 slot);
s32 ai_tag_position(u16 loc, s32 quad, s32 zone);
s32 ai_partner_busy();
s32 ai_partner_distance();
s32 ai_partner_mode(s32 kind);
s32 ai_evade_direction();
s32 ai_block_timer();
s32 nearest_segment_along(const Wrestler *w, const Vec3 *move, f32 *dist, s32 *seg);
s32 ai_arena_target();
void ai_others_dirs(s32 out[2]);
s32 ai_advantage(s32 kind);
s32 ai_chance_by_advantage();
s32 ai_lockup_free_dir();
s32 ai_lockup_dir_move();
s32 ai_lockup_ropes();
s32 ai_lockup_finisher();
s32 ai_lockup_whip_check();
s32 ai_lockup_tag_check();
s32 ai_lockup_special();
void ai_mode_22_strike();
s32 ai_evade_timer();
void ai_mode_42_evade_grapple();
void ai_mode_55_wait_long();
void ai_mode_105_running();
// ---- end of forward declarations ----
}  // namespace

namespace {

}  // namespace

// N64 0x800FD6DC: the CPU's noisy reading of a value: the error e = value x (1 - skill / 1000),
// at most value x cap; the result value + e x r / 1000 - e / 2 for a random r (rand_pool_next),
// not below 0. A skill-1000 CPU reads exactly. Doubles where the original converts.
f32 ai_perceive(f32 value, f32 cap) {
    f32 e = value * (f32)(1.0 - (f64)mem_s32(blk(AI_SKILL)) * 0.001);
    f32 m = value * cap;
    if (m < e)
        e = m;
    s32 r = rand_pool_next();
    f64 ed = (f64)e;
    f32 out = (f32)((f64)value + ed * 0.001 * (f64)r - ed * 0.5);
    if ((f64)out < 0.0)
        out = 0.0f;
    return out;
}

// N64 0x800FD128: which quarter of the ring a wrestler's point_120 is in: x > 0: 0 (z > 0) or 1;
// x <= 0: 3 (z > 0) or 2.
s32 ring_quadrant(const Wrestler *w) {
    if (w->point_120.x > 0.0f)
        return w->point_120.z <= 0.0f ? 1 : 0;
    return w->point_120.z > 0.0f ? 3 : 2;
}

// N64 0x800FCEB4: sets the CPU's mode: step and timer 0, the opponent's state noted, mode flags
// 0x03 cleared and 0x01 set.
void ai_set_mode(s32 mode) {
    mem_w32(blk(AI_MODE), (u32)mode);
    mem_w32(blk(AI_STEP), 0);
    mem_w32(blk(AI_TIMER), 0);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & 0xFC);
    mem_w16(blk(AI_OPP_STATE), (u16)g_ai_opp->state);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 1);
}

// N64 0x800FCF74: ai_set_mode when the mode differs.
void ai_set_mode_if_new(s32 mode) {
    if (mem_s32(blk(AI_MODE)) != mode)
        ai_set_mode(mode);
}

// N64 0x800F0DA4: the slot's last ten modes: a new non-zero mode is pushed in front.
void ai_mode_history_push(s32 slot) {
    u32 h = A_AI_MODE_HISTORY + 20u * (u32)slot;
    s32 mode = mem_s32(blk(AI_MODE));
    if (mode == ai_mode::Think || mode == mem_s16(h))
        return;
    for (s32 i = 9; i > 0; i--)
        mem_w16(h + 2u * (u32)i, mem_u16(h + 2u * (u32)(i - 1)));
    mem_w16(h, (u16)mode);
}

// N64 0x800F0D04: the time the CPU has spent on the floor (location 0x04) and not out of the fall,
// as minutes / seconds / frames bytes; cleared otherwise.
void ai_floor_timer(s32 slot) {
    u32 t = A_AI_FLOOR_TIME + 3u * (u32)slot;
    if (!(g_ai_self->location & 4) || (g_ai_self->end_flags & 1)) {
        mem_w8(t, 0);
        mem_w8(t + 1, 0);
        mem_w8(t + 2, 0);
        return;
    }
    mem_w8(t + 2, (u8)(mem_u8(t + 2) + 1));
    if (mem_s8(t + 2) >= 30) {
        mem_w8(t + 2, 0);
        mem_w8(t + 1, (u8)(mem_u8(t + 1) + 1));
    }
    if (mem_s8(t + 1) >= 60) {
        mem_w8(t + 1, 0);
        mem_w8(t, (u8)(mem_u8(t) + 1));
    }
}

// N64 0x800F0E78: how often `value` is among the first n (at most 16) entries of the slot's state
// history (0x8016B5A0); with mode 1 the count's sign bit (always 0), other modes 0.
s32 ai_history_count(s32 slot, s16 value, s32 n, s32 mode) {
    if (n >= 17)
        n = 16;
    s32 count = 0;
    for (s32 i = 0; i < n; i++)
        count += mem_s16(A_STATE_HISTORY + 32u * (u32)slot + 2u * (u32)i) == value;
    if (mode == 0)
        return count;
    return mode == 1 ? (s32)((u32)count >> 31) : 0;
}

// N64 0x800FD040: while the CPU has no mode, a weighted pick from a list of (threshold, mode,
// flags) u16 triples ended by -1: the first entry whose threshold reaches a random 0-999, or the
// terminator's mode; the entry's flags byte goes into mode flags 0x03.
void ai_pick_mode_weighted_idle(u32 table) {
    if (mem_s32(blk(AI_MODE)) != ai_mode::Think)
        return;
    s32 r = rand_pool_next();
    while (mem_s16(table) != -1 && mem_s16(table) < r)
        table += 6;
    ai_set_mode(mem_s16(table + 2));
    mem_w8(blk(AI_FLAGS), (u8)((mem_u8(blk(AI_FLAGS)) & 0xFC) | mem_u8(table + 5)));
}

// N64 0x800FD188: the CPU's perceived values: own health (cap 0.1) and spirit (0.1), the
// opponent's health (0.3) and spirit (0.2), the distance (0.2) and height difference (0.1), its
// own five regions (0.1), and how long the opponent will stay down (its escape counter, 0.3; in
// states 0x18 / 0x31 / 0x32 / 0x34; 60 in the TKO check 0x2E; else 0). Then the where-class
// (AI block +0x82, bits 0x1E00 kept): self in the ring: 0x01 opponent in the ring, 0x2000 on the
// cage wall (0x400), 0x02 on the apron, else 0x04; self on the apron: 0x08 / 0x10 / 0x20 (ring /
// apron / else); on the floor: 0x40 / 0x80 (ring / apron); on the cage wall: 0x4000 / 0x8000 (ring /
// wall); any other pairing 0x100. Mode flags: 0x10 when
// both have trait attrs +0x34 0x2000 (or only this one in finisher mode), 0x20 for an illegal man in
// a tag match, 0x40 under over-the-top elimination (type 0x4000 with rule 0x400).
void ai_update_perception() {
    Wrestler *s = g_ai_self;
    Wrestler *o = g_ai_opp;
    mem_w16(blk(AI_HEALTH), (u16)(s32)ai_perceive((f32)s->health, 0.1f));
    mem_w16(blk(AI_OPP_HEALTH), (u16)(s32)ai_perceive((f32)o->health, 0.3f));
    mem_w16(blk(AI_SPIRIT), (u16)(s32)ai_perceive((f32)s->spirit, 0.1f));
    mem_w16(blk(AI_OPP_SPIRIT), (u16)(s32)ai_perceive((f32)o->spirit, 0.2f));
    mem_wf32(blk(AI_DIST_P), ai_perceive(mem_f32(blk(AI_DIST)), 0.2f));
    mem_wf32(blk(AI_HEIGHT_DIFF), ai_perceive(mem_f32(blk(AI_HEIGHT_DIFF)), 0.1f));
    for (s32 k = 0; k < 5; k++)
        mem_wf32(blk(AI_REGIONS) + 4u * (u32)k, ai_perceive(s->region[k], 0.1f));
    switch (o->state) {
    case state::SlumpedInCorner:
    case state::LyingDown:
    case state::Dazed:
    case state::HeldUpsideDown:
        mem_w16(blk(AI_OPP_DOWN), (u16)(s32)ai_perceive((f32)o->escape, 0.3f));
        break;
    case state::TechnicalKnockoutStagger:
        mem_w16(blk(AI_OPP_DOWN), 60);
        break;
    default:
        mem_w16(blk(AI_OPP_DOWN), 0);
        break;
    }
    mem_w16(blk(AI_WHERE), mem_u16(blk(AI_WHERE)) & 0x1E00);
    u16 sl = s->location, ol = o->location;
    u16 where = 0x100;
    if (sl & 1) {
        if (ol & 1)
            where = 0x01;
        else if (ol & 0x400)
            where = 0x2000;
        else
            where = (ol & 2) ? 0x02 : 0x04;
    } else if (sl & 2) {
        if (ol & 1)
            where = 0x08;
        else
            where = (ol & 2) ? 0x10 : 0x20;
    } else if (sl & 4) {
        if (ol & 1)
            where = 0x40;
        else if (ol & 2)
            where = 0x80;
    } else if (sl & 0x400) {
        if (ol & 1)
            where = 0x4000;
        else if (ol & 0x400)
            where = 0x8000;
    }
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & ~0x10);
    mem_w16(blk(AI_WHERE), mem_u16(blk(AI_WHERE)) | where);
    u8 f = 0;
    if (mem_u32(s->attrs + 0x34) & 0x2000) {
        f = (u8)((s->flags_ee << 2) & 0x10);
        if (mem_u32(o->attrs + 0x34) & 0x2000)
            f = 0x10;
    }
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | f);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & ~0x20);
    if ((g_match_type & 0x2000) && !(g_match_rules & 0x40) && !(s->flags_e9 & 0x10))
        mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 0x20);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & ~0x40);
    if ((g_match_type & 0x4000) && (g_match_rules & 0x400))
        mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 0x40);
}

// N64 0x800FCBB0: a wrestler's situation class for the CPU (ai.toml AiSituation) from its state,
// step and flags; state 0x30 keeps `prev`, other unlisted states give 0. flags8c 0x20 makes it 22
// and 0x40 23 (checked last). `self` is the CPU (a strike or grapple attempt by itself reads as
// its own action from the first step).
s32 ai_situation(const Wrestler *w, const Wrestler *self, s32 prev) {
    s32 c = (w->state == state::SettleAfterAnimation) ? prev : 0;
    switch (w->state) {
    case state::Standing: case state::ApronCornerWalk: case state::CarryAttacker:
        c = 1;
        break;
    case state::Running: case state::RunnerHopOver:
        if (!(w->flags86 & 0x20))
            c = 2;
        else if (w->slot == self->slot || !(w->flags86 & 8))
            c = 3;
        else
            c = (w->flags86 & 0x40) ? 31 : 30;
        break;
    case state::RunningRopeRebound:
        c = (w->flags86 & 0x20) ? 3 : 4;
        break;
    case state::RunningRButtonMove:
        c = 5;
        break;
    case state::RunningShoulderBlock: case state::RunningWeaponStrike: case state::CornerReboundStrike: case state::ApronStrikeIntoRing: case state::ApronStrikeDownToFloor: case state::ApronStrikeAlongApron: case state::StrongStrike: case state::StrikeAtReturningWhippedRunner:
    case state::GetUpAttack: case state::WeaponSwingLight: case state::WeaponSwingHard: case state::BreakUpPinOrHold: case state::RunningBreakUpPinOrHold: case state::StrikeAtOpponentSlumpedInCorner: case state::StrikeAtOpponentHeldUpsideDown: case state::LadderTopStrike:
        c = 8;
        break;
    case state::ApronDodge: case state::Blocking: case state::BlockHit: case state::GrappleEvade:
        c = 14;
        break;
    case state::TurnbucklePerch:
        if (w->act_flags & 0xB)
            c = 25;
        break;
    case state::IrishWhipAttacker: c = 28; break;
    case state::IrishWhipRunner: c = 29; break;
    case state::RopesLockupPushFromRingAttacker: case state::RopesLockupPushFromApronAttacker: c = 32; break;
    case state::RopesLockupPushFromRingDefender: case state::RopesLockupPushFromApronDefender: c = 33; break;
    case state::RunIntoCorner: case state::RunKnockoutFall: case state::KnockedDown: c = 46; break;
    case state::SlumpedInCorner: case state::HeldUpsideDown: c = 20; break;
    case state::Taunt: case state::CornerTaunt: c = 43; break;
    case state::DuckingRunner: c = 58; break;
    case state::Strike:
        if (w->slot == self->slot || w->state_step >= 2)
            c = 8;
        break;
    case state::WeaponGroundAttack: case state::BareHandedGroundAttack: c = 9; break;
    case state::TechnicalKnockoutStagger: case state::PinDefender: case state::GroundGrappleDefender: case state::GroundHoldDefender: case state::PinFromMoveDefender: case 0x53: case 0x54: c = 7; break;
    case state::LyingDown:
        // Lying: steps 0-3 (the fall and the count) 15, the later steps 19.
        if (w->state_step < 4)
            c = 15;
        else if ((u32)(w->state_step - 4) < 3)
            c = 19;
        break;
    case state::Dazed: c = 16; break;
    case state::OffBalance: case state::GrappleDeflectDefender: c = 18; break;
    case state::KnockedBack: c = 17; break;
    case state::FlipDownedOpponentAttacker: c = 53; break;
    case state::PickUpDownedOpponentAttacker: c = 54; break;
    case state::CornerPlaceOnTopAttacker: c = 21; break;
    case state::CoverFromStanding: case state::PinAttacker: case state::PinFromMoveAttacker: c = 56; break;
    case state::GroundGrappleAttacker: case state::GroundHoldAttacker: c = 57; break;
    case state::CrouchAfterGetUp: c = 47; break;
    case state::GetUp: c = 24; break;
    case state::GrappleAttempt:
        if (w->slot == self->slot) {
            c = 11;
        } else {
            switch (w->state_step) {
            case state::PreBell: case state::Standing: case state::ApronCornerWalk: case state::Running: c = 1; break;
            case state::RunningRopeRebound: case state::RunningShoulderBlock: case state::RunningWeaponStrike: c = 11; break;
            case state::HitByRunner: c = 17; break;
            default: break;
            }
        }
        break;
    case state::LockupAttacker: c = 13; break;
    case state::LockupDefender:
        if (w->state_step == 2)
            c = 6;
        break;
    case state::MoveAttacker: case state::ChainMoveAttacker: case state::CarryMoveAttacker: c = 44; break;
    case state::MoveDefender: case state::ChainMoveDefender: case state::CarryMoveDefender: c = 45; break;
    case state::MoveFollowUpDefender: case state::CatchWhippedRunnerDefender: c = 12; break;
    case state::RunningGroundAttack: c = 10; break;
    case state::HitWhileDown: c = 15; break;
    case state::CaughtLegHolder: c = 34; break;
    case state::CaughtLegHeld: c = 35; break;
    case state::DropDown: case state::RunOffEdge: c = 49; break;
    case state::ClimbOnto: case state::RunOntoRaisedPlace: c = 48; break;
    case state::KnockedOverTopRope:
        if ((mem_u8(blk(AI_FLAGS)) & 0x40) && (w->location & 2))
            c = 41;
        break;
    case state::RunningMoveOffTheSide: case state::RopeTaunt: case state::ApronDiveMove271: case state::ApronDiveMove272: case state::ApronToRingMove: case state::RunningFlyingMove: c = 26; break;
    case state::ApronMove: case state::AerialAttackAtDownedOpponent: c = 27; break;
    case state::TurnbuckleDiveMoves256And257: case state::TurnbuckleDiveMoves263And264: case state::TurnbuckleDiveMoves261And262: case state::TurnbuckleDiveMove279: case state::TurnbuckleDiveMove280: c = 25; break;
    case state::RearLiftAttacker: c = 39; break;
    case state::RearLiftDefender: c = 40; break;
    case state::CarryDefender: c = 38; break;
    case state::CrowdWeaponGrab: c = 50; break;
    case state::SpinDazedOpponentAttacker: c = 51; break;
    case state::LeapfrogRunner: case state::SideStepRunner: c = 42; break;
    case state::RunIn: c = 52; break;
    case state::RunToSidePoint: c = 55; break;
    case state::CageTopClimbOver: c = 60; break;
    case state::CageHang: c = 59; break;
    case state::CageClimbStart: c = 61; break;
    default:
        break;
    }
    if (w->flags8c & 0x20)
        c = 22;
    if (w->flags8c & 0x40)
        return 23;
    return c;
}

// ---- the choice (mode 0) ----

namespace {

// The CPU's own copy of its point_120 and its opponent's (ai_update stores them).
Vec3 ai_self_point() {
    return {mem_f32(A_AI_SELF_POINT), mem_f32(A_AI_SELF_POINT + 4), mem_f32(A_AI_SELF_POINT + 8)};
}
Vec3 ai_opp_point() {
    return {mem_f32(A_AI_OPP_POINT), mem_f32(A_AI_OPP_POINT + 4), mem_f32(A_AI_OPP_POINT + 8)};
}

// Whether some other wrestler in play in a multi-wrestler line-up (flags_e9 0x02) is also marked
// flags_f1 0x02: the loop ai_choose_mode runs twice; 1 when none is.
bool ai_no_other_marked() {
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *o = &g_wrestlers[i];
        if (i == g_ai_self->slot || o->roster_id == -1 || !(o->flags_e9 & 2))
            continue;
        if (o->flags_f1 & 2)
            return false;
    }
    return true;
}

// The skill a chance roll against a wrestler uses: the full skill when the opponent is down (its
// situation 15, 16 or 19), else half.
s32 ai_skill_vs_down() {
    s32 sit = mem_s32(blk(AI_SIT_OPP));
    s32 skill = mem_s32(blk(AI_SKILL));
    return (sit >= 15 && (sit < 17 || sit == 19)) ? skill : skill / 2;
}

// The slot of the CPU's teammate for the double-team checks: in team play (type 0x1000) slot ^ 2,
// in tag (0x2000) the partner; slot 0 otherwise.
s32 ai_teammate_slot() {
    s32 t = 0;
    if (g_match_type & 0x1000)
        t = (s16)(g_ai_self->slot ^ 2);
    if (g_match_type & 0x2000)
        t = g_wrestlers[g_ai_self->partner].slot;
    return t;
}

}  // namespace

void ai_press_focus_switch();
void ai_pick_mode_weighted(u32 table);

namespace {

// The CPU holding a weapon fights with it by trait +38: 3 always, 2 only with both outside
// (where-class 0x100).
bool ai_armed_style() {
    if (!(g_ai_self->flags_ef & 1))
        return false;
    s8 t = mem_s8(kTraits[8].global);
    return t == 3 || (t == 2 && (mem_u16(blk(AI_WHERE)) & 0x100));
}

// The skill roll against an opponent attempting something (situation 8 striking or 11 grappling): a
// grapple attempt scales it by 1.2 when running or strong (flags78 0x44), else 0.8. On success the
// close-quarters deciders: ai_decide_close for a grapple, ai_decide_opponent_focused for a strike.
bool ai_counter_roll() {
    s32 chance = mem_s32(blk(AI_SKILL));
    if (mem_s32(blk(AI_SIT_OPP)) == 11)
        chance = (s32)((f64)chance * ((g_ai_opp->flags78 & 0x44) ? 1.2 : 0.8));
    if (!chance_check(chance))
        return false;
    if (mem_s32(blk(AI_SIT_OPP)) == 11)
        ai_decide_close();
    else
        ai_decide_opponent_focused();
    return true;
}

}  // namespace

// N64 0x800F1EA4: the shared standing decision. Near (perceived distance under 170): armed (trait
// +38, ai_armed_style): an opponent in the ropes' bounce (flags80 0x01) mode 32; a skill roll: the
// opponent down (15) 48 when its body is nearer than it (+0x7C < +0x68) else 22, otherwise 70% mode
// 9 else 6; failing that a second roll 41 else 32. Unarmed, by the opponent's situation: running
// (2) nothing; striking / grappling (8 / 11) the counter roll, else a skill roll for the list
// 0x801452F4 or mode 41; down (15) ai_decide_special; lying (16, 19, 22, 23) ai_decide_vs_lying;
// 17 ai_decide_far; 18 ai_decide_far_b; 24 ai_decide_close_b; 45 mode 32; anything else: the
// opponent on the apron the list 0x80145330, bouncing (flags80 0x01) 32, else
// ai_decide_engage_chance. Far: the opponent bouncing: a skill roll 48 else 32; armed: down (15) 48,
// a skill roll 10, else the list 0x80145318; unarmed, a skill roll by situation: 2 nothing; 8 / 11
// the counter roll; 15 / 16 / 19 SPECIAL (flags_ee 0x02) 50, else in the ring ai_decide_vs_down_b
// or 48 (then on to the grapple roll below); 17 / 22 / 23 ai_decide_far; 18 _far_b; 24 _close_b;
// otherwise the apron list 0x80145330 when the opponent is on the apron. Last: a skill roll, the
// trait +37 chance (0x80145354) and 10% try a grapple (ai_try_grapple(1)); else the list 0x80145348
// in a meter mode (flags_ee 0x06) or 0x80145318.
void ai_decide_standing() {
    const Wrestler *o = g_ai_opp;
    s32 skill = mem_s32(blk(AI_SKILL));
    if ((f64)mem_f32(blk(AI_DIST_P)) < 170.0) {
        if (ai_armed_style()) {
            if (o->flags80 & 1) {
                ai_set_mode(ai_mode::WaitAndWatch);
                return;
            }
            if (chance_check(skill)) {
                if (mem_s32(blk(AI_SIT_OPP)) == 15)
                    ai_set_mode(mem_f32(blk(AI_BODY_DIST)) < mem_f32(blk(AI_DIST)) ? ai_mode::WalkToDownedOpponent : ai_mode::StrikeDownedOpponent);
                else
                    ai_set_mode(chance_check(700) ? ai_mode::StrikeThenFollowUp : ai_mode::Strike);
                return;
            }
            ai_set_mode(chance_check(mem_s32(blk(AI_SKILL))) ? ai_mode::WalkTowardOpponent : ai_mode::WaitAndWatch);
            return;
        }
        switch (mem_s32(blk(AI_SIT_OPP))) {
        case 2:
            return;
        case 8:
        case 11:
            if (ai_counter_roll())
                return;
            if (chance_check(mem_s32(blk(AI_SKILL))))
                ai_pick_mode_weighted(0x801452F4);
            else
                ai_set_mode(ai_mode::WalkTowardOpponent);
            return;
        case 15: ai_decide_special(); return;
        case 16: case 19: case 22: case 23: ai_decide_vs_lying(); return;
        case 17: ai_decide_far(); return;
        case 18: ai_decide_far_b(); return;
        case 24: ai_decide_close_b(); return;
        case 45: ai_set_mode(ai_mode::WaitAndWatch); return;
        default:
            if (mem_u16(blk(AI_WHERE)) & 2)
                ai_pick_mode_weighted(0x80145330);
            else if (o->flags80 & 1)
                ai_set_mode(ai_mode::WaitAndWatch);
            else
                ai_decide_engage_chance();
            return;
        }
    }
    if (o->flags80 & 1) {
        ai_set_mode(chance_check(skill) ? ai_mode::WalkToDownedOpponent : ai_mode::WaitAndWatch);
        return;
    }
    if (ai_armed_style()) {
        if (mem_s32(blk(AI_SIT_OPP)) == 15) {
            ai_set_mode(ai_mode::WalkToDownedOpponent);
            return;
        }
        if (chance_check(mem_s32(blk(AI_SKILL))))
            ai_set_mode(ai_mode::EvadeAndCounter);
        else
            ai_pick_mode_weighted(0x80145318);
        return;
    }
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        switch (mem_s32(blk(AI_SIT_OPP))) {
        case 2:
            return;
        case 8:
        case 11:
            if (ai_counter_roll())
                return;
            if (mem_u16(blk(AI_WHERE)) & 2) {
                ai_pick_mode_weighted(0x80145330);
                return;
            }
            break;
        case 15: case 16: case 19:
            if (g_ai_self->flags_ee & 2) {
                ai_set_mode(ai_mode::ApronWaitAndTaunt);
                return;
            }
            if ((g_ai_self->location & 1) && ai_decide_vs_down_b())
                return;
            ai_set_mode(ai_mode::WalkToDownedOpponent);
            break;
        case 17: case 22: case 23: ai_decide_far(); return;
        case 18: ai_decide_far_b(); return;
        case 24: ai_decide_close_b(); return;
        default:
            if (mem_u16(blk(AI_WHERE)) & 2) {
                ai_pick_mode_weighted(0x80145330);
                return;
            }
            break;
        }
    }
    if (chance_check(mem_s32(blk(AI_SKILL))) &&
        chance_check(mem_s16(0x80145354 + 2u * (u32)(s32)mem_s8(kTraits[7].global))) && chance_check(100)) {
        ai_try_grapple(1);
        return;
    }
    ai_pick_mode_weighted((g_ai_self->flags_ee & 6) ? 0x80145348 : 0x80145318);
}


// N64 0x800F0E0C: 1 when `mode` is among the first n (at most 10) of the CPU's last ten modes
// (0x8014521C, ai_mode_history_push). Renamed 2026-11-03 from ai_move_listed: the list holds modes.
s32 ai_mode_recent(s32 mode, s32 n) {
    if (n >= 11)
        n = 10;
    u32 h = A_AI_MODE_HISTORY + 20u * (u32)g_ai_self->slot;
    for (s32 i = 0; i < n; i++)
        if (mem_s16(h + 2u * (u32)i) == mode)
            return 1;
    return 0;
}

// N64 0x800F1740: the decision with both in the ring (where-class 0x01), or the opponent on the apron
// (0x02). In order: the tag deciders (ai_decide_tag; the tag-out wish: mode 61). With the opponent on
// the apron: over-the-top rules and more than 170 away: mode 10; otherwise (not under rule 0x400) a
// 20% roll, then the chance by trait +38 (0x801452E0) to knock it off (mode 36, unless 33 / 34 ran in
// the last ten modes), else more than 170 away mode 10. By the opponent's situation: diving (25 /
// 26) ai_decide_wait_or_engage; in the corner (20) or rebounding (30) ai_decide_in_ring_main; on the
// ropes (38) ai_decide_ropes when it acts; carried (40): beyond 170 ai_try_grapple(1), within 130
// the chance by trait +37 (0x801452D4) for mode 9 else 1, between them 48. The opponent on the apron
// and the CPU in finisher mode: beyond 130 mode 48, else 2. A runner (ai_vs_runner_armed). Then
// helping a strong grapple: in team / tag matches the teammate, in others any wrestler, in the ring
// in a strong grapple attempt (0x50, flags78 0x04; not both in finisher mode): on the same opponent
// within 160 (sharing a place): mode 1; on another wrestler within 160 of it (sharing a place): by a
// skill roll C-right. In a cage match (type 0x10000), with the opponent down and its own spirit 71+,
// a time-down threshold by the opponent's health (0x801452EC by health / 50, at most 5) below its
// perceived time down: mode 127 (unless 19 ran in the last five). Then ai_decide_vs_down, against a
// finisher-mode opponent ai_decide_change_area, and ai_decide_standing.
void ai_decide_in_ring() {
    Wrestler *s = g_ai_self;
    if (ai_decide_tag())
        return;
    if (ai_wants_tag_out(s)) {
        ai_set_mode(ai_mode::ApronWalkTowardPartner);
        return;
    }
    f64 dist = (f64)mem_f32(blk(AI_DIST_P));
    if (mem_u16(blk(AI_WHERE)) & 2) {
        if ((mem_u8(blk(AI_FLAGS)) & 0x40) && dist > 170.0) {
            ai_set_mode(ai_mode::EvadeAndCounter);
            return;
        }
        if (!(g_match_rules & 0x400) && chance_check(200)) {
            if (chance_check(mem_s16(0x801452E0 + 2u * (u32)(s32)mem_s8(kTraits[8].global))) && !ai_mode_recent(ai_mode::HeadForRing, 10) &&
                !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
                ai_set_mode(ai_mode::LeaveRingThroughRopes);
                return;
            }
            if (dist > 170.0) {
                ai_set_mode(ai_mode::EvadeAndCounter);
                return;
            }
        }
    }
    s32 osit = mem_s32(blk(AI_SIT_OPP));
    if ((u32)(osit - 25) < 2) {
        ai_decide_wait_or_engage();
        return;
    }
    if (osit == 20 || osit == 30) {
        ai_decide_in_ring_main();
        return;
    }
    if (osit == 38 && ai_decide_ropes())
        return;
    dist = (f64)mem_f32(blk(AI_DIST_P));
    if (mem_s32(blk(AI_SIT_OPP)) == 40) {
        if (dist > 170.0) {
            ai_try_grapple(1);
            return;
        }
        if (dist < 130.0) {
            ai_set_mode(chance_check(mem_s16(0x801452D4 + 2u * (u32)(s32)mem_s8(kTraits[7].global))) ? ai_mode::StrikeThenFollowUp : ai_mode::EngageNearbyOpponent);
            return;
        }
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    if ((mem_u16(blk(AI_WHERE)) & 2) && (s->flags_ee & 4)) {
        ai_set_mode(dist > 130.0 ? ai_mode::WalkToDownedOpponent : ai_mode::GrappleApproach);
        return;
    }
    if (ai_vs_runner_armed())
        return;
    Vec3 sp = ai_self_point();
    bool done_help = false;
    if (g_match_type & 0x3000) {
        const Wrestler *m = &g_wrestlers[ai_teammate_slot()];
        if (m->roster_id != -1 && (m->location & 1) && m->state == state::GrappleAttempt && !(m->flags_ee & s->flags_ee & 4) &&
            (m->flags78 & 4)) {
            if (s->opponent == m->opponent) {
                Vec3 op = ai_opp_point();
                if ((f64)dist_to(&sp, &op) < 160.0) {
                    ai_set_mode(ai_mode::EngageNearbyOpponent);
                    return;
                }
            } else if ((f64)dist_to(&sp, &m->point_120) < 160.0 && chance_check(ai_skill_vs_down())) {
                ai_press_focus_switch();
                return;
            }
        }
        done_help = true;
    }
    for (s32 i = 0; i < 4 && !done_help; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if ((w->flags_ee & s->flags_ee & 4) || w->roster_id == -1 || w->slot == s->slot || (w->end_flags & 1) ||
            w->state != state::GrappleAttempt || !(w->flags78 & 4))
            continue;
        if (w->opponent == s->opponent) {
            if (!(s->location & g_ai_opp->location & 0x7FF))
                continue;
            Vec3 op = ai_opp_point();
            if ((f64)dist_to(&sp, &op) < 160.0) {
                ai_set_mode(ai_mode::EngageNearbyOpponent);
                return;
            }
            continue;
        }
        const Wrestler *t = &g_wrestlers[w->opponent];
        if ((f64)dist_to(&sp, &t->point_120) >= 160.0 || !(s->location & t->location & 0x7FF))
            continue;
        if (chance_check(ai_skill_vs_down())) {
            ai_press_focus_switch();
            return;
        }
    }
    if (g_match_type & 0x10000) {
        s32 o2 = mem_s32(blk(AI_SIT_OPP));
        if (o2 >= 15 && (o2 < 17 || o2 == 19) && mem_s16(blk(AI_SPIRIT)) >= 71) {
            s32 k = (s16)(mem_s16(blk(AI_OPP_HEALTH)) / 50);
            if (k >= 6)
                k = 5;
            if (mem_s8(0x801452EC + (u32)k) < mem_s16(blk(AI_OPP_DOWN)) && !ai_mode_recent(ai_mode::WalkToDownedOpponentAndPickUp, 5)) {
                ai_set_mode(ai_mode::WalkToEdgeAndClimbOutVariant1);
                return;
            }
        }
    }
    if (ai_decide_vs_down())
        return;
    if ((g_ai_opp->flags_ee & 4) && ai_decide_change_area())
        return;
    ai_decide_standing();
}

// N64 0x800F559C: presses C-right (AI block +0x0E = 1: switch the focused opponent,
// opponent_switch_nearest) when the CPU can: standing (0x01), perched on the corner in the air (0x0A
// with act_flags 0x02), or crouched after getting up (0x4F step 2). Renamed 2026-11-03 from
// ai_can_act. (The original leaves its return register unset; nothing reads it.)
void ai_press_focus_switch() {
    const Wrestler *s = g_ai_self;
    bool can = (s->state == state::Standing) || (s->state == state::TurnbucklePerch && (s->act_flags & 2)) || (s->state == state::GetUp && s->state_step == 2);
    if (can)
        mem_w16(blk(AI_PRESSED), 1);
}

// N64 0x800FCB5C: 1 when exactly two wrestlers are out of the fall (end_flags 0x01) and the CPU is
// not one of them.
s32 ai_one_fall_left() {
    s32 out = 0;
    for (s32 i = 0; i < 4; i++)
        out += g_wrestlers[i].end_flags & 1;
    return (!(g_ai_self->end_flags & 1) && out == 2) ? 1 : 0;
}

// N64 0x80142CB4: the loose objects (g_objects 0-6 in use, flags 0x04 without 0x4000) as (index,
// distance from p) pairs, nearest first; out has room for 8, unused entries keep index -1.
void objects_near(const Vec3 *p, s32 idx[8], f32 dist[8]) {
    for (s32 i = 0; i < 8; i++)
        idx[i] = -1;
    s32 n = 0;
    for (s32 i = 0; i < 7; i++) {
        u32 o = 0x8016AE10 + 180u * (u32)i;
        if (mem_s32(o + 4) == -1)
            continue;
        u16 f = mem_u16(o + 0x20);
        if (!(f & 4) || (f & 0x4000))
            continue;
        Vec3 pos = {mem_f32(o + 0x44), mem_f32(o + 0x48), mem_f32(o + 0x4C)};
        idx[n] = i;
        dist[n] = dist_to(p, &pos);
        n++;
    }
    for (s32 i = 0; i < n - 1; i++) {
        for (s32 j = i + 1; j < n; j++) {
            if (dist[j] < dist[i]) {
                s32 ti = idx[j];
                f32 td = dist[j];
                idx[j] = idx[i];
                dist[j] = dist[i];
                idx[i] = ti;
                dist[i] = td;
            }
        }
    }
}

// N64 0x800FCAFC: 1 when the nearest loose object (objects_near from the CPU's pos) lies within 100
// and is not object 6.
s32 ai_objects_near() {
    s32 idx[8];
    f32 dist[8];
    objects_near(&g_ai_self->pos, idx, dist);
    if (idx[0] == -1 || !((f64)dist[0] < 100.0))
        return 0;
    return idx[0] != 6 ? 1 : 0;
}

// N64 0x800FCFA4: a weighted pick: (threshold, mode, flags) u16 triples ended by -1; the first whose
// threshold reaches a random 0-999 (or the terminator) sets the mode, its flags byte into mode flags
// 0x03.
void ai_pick_mode_weighted(u32 table) {
    s32 r = rand_pool_next();
    while (mem_s16(table) != -1 && mem_s16(table) < r)
        table += 6;
    ai_set_mode(mem_s16(table + 2));
    mem_w8(blk(AI_FLAGS), (u8)((mem_u8(blk(AI_FLAGS)) & 0xFC) | mem_u8(table + 5)));
}

// N64 0x800FD828: the decision for a wrestler that follows another (flags_e9 0x01; a run-in ally):
// standing, it takes its partner as its opponent when its own opponent is more than 200 away; on the
// floor (location 0x04) it heads up the entrance way (mode 71) when its point is 530 or more down
// the z axis, else to the ring (72) when its ring zone is below 2 (slot 2) or 2 and above (others).
void ai_decide_ring_simple() {
    Wrestler *s = g_ai_self;
    if (s->state != state::Standing)
        return;
    if ((s->flags_e9 & 1) && s->opponent != s->partner && (f64)mem_f32(blk(AI_DIST)) > 200.0) {
        s->opponent = s->partner;
        return;
    }
    if (!(s->location & 4))
        return;
    if ((f64)mem_f32(A_AI_SELF_POINT + 8) >= 530.0) {
        ai_set_mode(ai_mode::WalkDownTheRing);
        return;
    }
    s16 zone = mem_s16(blk(AI_ZONE_SELF));
    if (s->slot == 2 ? zone < 2 : zone >= 2)
        ai_set_mode(ai_mode::WalkToRingCentre);
}

// N64 0x800F0EF0: the CPU's choice (mode 0). In order:
// 1. For a following wrestler (flags_e9 0x01) when no other line-up wrestler is marked: in a hurt
//    state (0x15, 0x16, 0x2D, 0x47, 0x49, 0x4B, 0x56, 0x5A, 0x5C, 0x5E, 0x60, 0x63) each new one
//    counts the +0x5E counter down; below 0 it sets focus flag 0x04 and re-rolls 0-7. With focus 0x08
//    it keeps on while its opponent is within 300 and shares a place with it, else both focus bits
//    clear and it stops; with 0x04 it looks for a wrestler working on it (slot ^ 1's opposite, then
//    | 2) within 120: already its opponent → focus 0x08; else C-right (ai_press_focus_switch). Then
//    ring arenas: ai_decide_ring_simple and stop; elsewhere C-right when its opponent is its partner.
// 2. The same for a line-up wrestler (flags_e9 0x02, not itself flags_f1 0x02).
// 3. Not standing (situation 1 / 43): running 151+ frames without a target presses (+0x0E
//    0x0F00: all four d-pad bits); on a ladder (location 0x08) mode 136 (138 at the top, 0x10).
// 4. Standing: +0x8016B718[slot] = 1 when the opponent looks weak (health < 30, spirit < 50) and the
//    per-slot word 0x8016B580 is set, else 0. In the partner's hold (partner_flags80 0x80) in state
//    0x81 it presses R and stops; in 0x81 otherwise it skips the deciders. Then by the where-class:
//    0x01 / 0x02 ai_decide_in_ring, 0x04 _ring_vs_floor, 0x20 _apron_vs_floor, 0x08 _apron_vs_ring,
//    0x40 / 0x80 _floor_vs_ring, 0x10 _apron_vs_apron, 0x2000 _vs_cage_wall, 0x4000
//    _cage_wall_vs_ring, 0x8000 _both_cage_wall (backstage arenas: ai_decide_backstage); the raised
//    places (location 0x3C0) for either; ai_think in type 0x100000; mode 144 in elimination when two
//    are out and its opponent is one of them. In ring arenas where-class 0x100: ai_decide_floor.
// 5. Unless rule 0x2000 (DQ), with the opponent down (situation 15-20) and an object near: C-left
//    held and pressed (pick it up).
void ai_choose_mode() {
    Wrestler *s = g_ai_self;
    Wrestler *o = g_ai_opp;
    if ((s->flags_e9 & 1) && ai_no_other_marked()) {
        switch (s->state) {
        case state::RunIntoCorner: case state::RunKnockoutFall: case state::KnockedDown: case state::GroundGrappleDefender: case state::GroundHoldDefender: case state::PinFromMoveDefender:
        case state::MoveDefender: case state::MoveFollowUpDefender: case state::CatchWhippedRunnerDefender: case state::ChainMoveDefender: case state::CarryMoveDefender: case state::HitWhileDown:
            if (!(mem_u8(blk(AI_FLAGS)) & 4) && mem_s16(blk(AI_LAST_STATE)) != s->state) {
                mem_w16(blk(AI_LAST_STATE), (u16)s->state);
                s16 left = (s16)(mem_s16(blk(AI_FOCUS_COUNT)) - 1);
                mem_w16(blk(AI_FOCUS_COUNT), (u16)left);
                if (left < 0) {
                    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 4);
                    s32 r = rand_pool_next();
                    mem_w16(blk(AI_FOCUS_COUNT), (u16)(r % 8));
                }
            }
            break;
        default:
            mem_w16(blk(AI_LAST_STATE), (u16)s->state);
            break;
        }
        u8 f = mem_u8(blk(AI_FLAGS));
        if (f & 8) {
            Vec3 sp = ai_self_point(), op = ai_opp_point();
            if ((f64)dist_to(&sp, &op) > 300.0 || !(s->location & o->location & 0x7FF)) {
                mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & 0xF3);
                return;
            }
        } else {
            if (f & 4) {
                Vec3 sp = ai_self_point();
                s32 cand = !(s->slot & 1);
                bool done = false;
                for (s32 pass = 0; pass < 2 && !done; pass++) {
                    if (pass == 1)
                        cand |= 2;
                    const Wrestler *c = &g_wrestlers[cand];
                    if (c->opponent != s->slot || (f64)dist_to(&sp, &c->point_120) >= 120.0)
                        continue;
                    if (s->opponent != cand)
                        ai_press_focus_switch();
                    else
                        mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 8);
                    done = true;
                }
                if (done)
                    return;
            }
            if (mem_s32(A_ARENA) < 10) {
                ai_decide_ring_simple();
                return;
            }
            if (s->opponent == s->partner)
                ai_press_focus_switch();
        }
    }
    if ((s->flags_e9 & 2) && !(s->flags_f1 & 2) && ai_no_other_marked()) {
        if (mem_s32(A_ARENA) < 10) {
            ai_decide_ring_simple();
            return;
        }
        if (s->opponent == s->partner)
            ai_press_focus_switch();
    }
    s32 sit = mem_s32(blk(AI_SIT_SELF));
    if (sit != 1 && sit != 43) {
        if (s->state >= state::Running && s->state < state::HitByRunner && mem_s16(blk(AI_RUN_TIME)) >= 151)
            mem_w16(blk(AI_PRESSED), 0x0F00);
        if (s->location & 8)
            ai_set_mode((s->location & 0x10) ? ai_mode::OnLadderAgainstOpponent : ai_mode::StrikeFromBehind);
    } else {
        u32 weak = 0x8016B718 + (u32)s->slot;
        if (mem_s16(blk(AI_OPP_HEALTH)) < 30 && mem_s16(blk(AI_OPP_SPIRIT)) < 50 &&
            mem_s32(0x8016B580 + 4u * (u32)s->slot) != 0)
            mem_w8(weak, 1);
        else
            mem_w8(weak, 0);
        bool deciders = true;
        if (s->partner_flags80 & 0x80) {
            if (s->state == state::CarryAttacker) {
                mem_w16(blk(AI_PRESSED), 0x10);
                return;
            }
        } else if (s->state == state::CarryAttacker) {
            deciders = false;
        }
        if (deciders) {
            if (mem_s32(A_ARENA) >= 10) {
                ai_decide_backstage();
            } else {
                u16 where = mem_u16(blk(AI_WHERE));
                if (where & 0x01) ai_decide_in_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x02) ai_decide_in_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x04) ai_decide_ring_vs_floor();
                if (mem_u16(blk(AI_WHERE)) & 0x20) ai_decide_apron_vs_floor();
                if (mem_u16(blk(AI_WHERE)) & 0x08) ai_decide_apron_vs_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x40) ai_decide_floor_vs_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x80) ai_decide_floor_vs_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x10) ai_decide_apron_vs_apron();
                if (mem_u16(blk(AI_WHERE)) & 0x2000) ai_decide_vs_cage_wall();
                if (mem_u16(blk(AI_WHERE)) & 0x4000) ai_decide_cage_wall_vs_ring();
                if (mem_u16(blk(AI_WHERE)) & 0x8000) ai_decide_both_cage_wall();
            }
            if (s->location & 0x3C0) {
                if (o->location & 0x3C0)
                    ai_decide_both_raised();
                else
                    ai_decide_self_raised();
            } else if (o->location & 0x3C0) {
                ai_decide_opponent_raised();
            }
            if (g_match_type & 0x100000)
                ai_think();
            if ((g_match_type & 0x4000) && ai_one_fall_left() && (o->end_flags & 1))
                ai_set_mode(ai_mode::ChaseLastWrestler);
        }
        if (mem_s32(A_ARENA) < 10 && (mem_u16(blk(AI_WHERE)) & 0x100))
            ai_decide_floor();
    }
    if (!(g_match_rules & 0x2000)) {
        s32 osit = mem_s32(blk(AI_SIT_OPP));
        if (osit >= 15 && osit < 21 && ai_objects_near()) {
            mem_w16(blk(AI_PRESSED), 2);
            mem_w16(blk(AI_HELD), 2);
        }
    }
}

// N64 0x800FD970: mode 0: the choice every frame until a mode is set.
void ai_mode_0_think() { ai_choose_mode(); }

// N64 0x801028F0: mode 32, waiting and watching. Step 0: a wait of 8-35 frames (rand % 28 + 8),
// step 1. Step 1 (each frame): back to mode 0 when the CPU is no longer standing, the wait runs out,
// it may not act (tag rules), or the opponent's state has changed since the mode began. While the
// opponent stands or taunts (0x01, 0x1B, 0x1C) or is in the TKO check (0x2E) it just waits. While it
// runs or is whipped (0x03, 0x04, 0x0C), half the time in team / tag matches with the teammate in the
// ring: mode 143 (the double team) when the teammate works on the same opponent, else by a skill roll
// C-right to switch to the teammate's opponent. Otherwise, more than 170 away (perceived) in the ring
// or both outside (where-class 0x101): a 20% roll (0 for a weapon holder with trait +38 == 3 unless
// the opponent is down, 15) to try a grapple (ai_try_grapple(1)).
void ai_mode_32_wait() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        s32 r = rand_pool_next();
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        mem_w32(blk(AI_MODE_TIMER), (u32)(r % 28 + 8));
        return;
    }
    if (step != 1)
        return;
    if (mem_s32(blk(AI_SIT_SELF)) != 1) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
    mem_w32(blk(AI_MODE_TIMER), (u32)t);
    if (t <= 0 || !wrestler_may_act(g_ai_self) || mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    u16 ostate = mem_u16(blk(AI_OPP_STATE));
    switch (ostate) {
    case 0x01: case 0x1B: case 0x1C: case 0x2E:
        return;
    case 0x03: case 0x04: case 0x0C:
        if (chance_check(50) && (g_match_type & 0x3000)) {
            const Wrestler *mate = &g_wrestlers[ai_teammate_slot()];
            if (mate->roster_id != -1 && (mate->location & 1) && !(mate->flags_ee & g_ai_self->flags_ee & 4)) {
                if (g_ai_self->opponent == mate->opponent) {
                    ai_set_mode(ai_mode::TagAssist);
                    return;
                }
                if (chance_check(ai_skill_vs_down())) {
                    ai_press_focus_switch();
                    return;
                }
            }
        }
        break;
    default:
        break;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 170.0 && (mem_u16(blk(AI_WHERE)) & 0x101)) {
        s32 chance = 200;
        if ((g_ai_self->flags_ef & 1) && mem_s8(kTraits[8].global) == 3)
            chance = (mem_s32(blk(AI_SIT_OPP)) != 15) ? 200 : 0;
        if (chance_check(chance))
            ai_try_grapple(1);
    }
}

// ---- the modes ----

namespace {

enum : u32 {
    AI_PENDING = 0x10,               // u16: the buttons a mode will press next (or hold)
    A_DIR8_ANGLES = 0x8014D238,      // s32[8]: the angle of each of the eight directions
    A_DIR8_BUTTONS = 0x8014D258,     // u16[8]: the d-pad bits of each direction
    A_TAUNT_MOVES = 0x80145F18,      // s16[4]: the taunts by stick direction (227, 231, 229, 228)
    A_GRAPPLE_LISTS = 0x80145C54,    // the CPU's move lists by kind (8-byte entries: s32 move, u16 buttons, s8 list index)
};


// The record flags of move `m` in the CPU's move set (AI block +0x88).
u32 ai_move_flags(s32 m) { return mem_u32(mem_u32(blk(AI_MOVESET)) + 36u * (u32)m + 0x20); }

// The d-pad bits for direction d (0-7).
u16 dir8_buttons(s32 d) { return mem_u16(A_DIR8_BUTTONS + 2u * (u32)(d & 7)); }

}  // namespace

// N64 0x8012E1CC: the direction 0-7 nearest an angle.
s32 angle_dir8(s32 angle) { return ((angle + 4352) / 512) & 7; }

// N64 0x8012E1E4: the direction 0-7 from a to b.
s32 dir8_to(const Vec3 *a, const Vec3 *b) { return angle_dir8(angle_to(a, b)); }

// N64 0x80107584: how hard the CPU mashes out of a struggle: 0.75 at perceived health under 65, 0.65
// under 129, 0.55 above, plus (spirit x 16 / 100 - 8) x 0.01.
f32 ai_struggle_rate(u32 b) {
    s16 hp = mem_s16(b + AI_HEALTH);
    f32 base = (hp >= 129) ? 0.55f : ((hp < 65) ? 0.75f : 0.65f);
    s32 sp = 16 * mem_s16(blk(AI_SPIRIT)) / 100 - 8;
    return (f32)((f64)base + (f64)sp * 0.01);
}

// N64 0x8010A084: pushes the stick for a taunt: 40% of the time the first of the taunts 227 / 231 /
// 229 / 228 whose record has flag 0x8000 (a favourite), else a random one; stick up, down, right or
// left (+0x12 / +0x13 = 0 / 80). Renamed 2026-11-03 from ai_mode_11_evade (it is mode 49's taunt).
void ai_stick_taunt() {
    s32 pick = -1;
    if (chance_check(400)) {
        for (s32 i = 0; i < 4; i++) {
            if (ai_move_flags(mem_s16(A_TAUNT_MOVES + 2u * (u32)i)) & 0x8000) {
                pick = i;
                break;
            }
        }
    }
    if (pick == -1)
        pick = rand_pool_next() % 4;
    switch (pick) {
    case 0: mem_w8(blk(AI_STICK_X), 0); mem_w8(blk(AI_STICK_Y), 80); break;
    case 1: mem_w8(blk(AI_STICK_X), 0); mem_w8(blk(AI_STICK_Y), (u8)-80); break;
    case 2: mem_w8(blk(AI_STICK_X), 80); mem_w8(blk(AI_STICK_Y), 0); break;
    case 3: mem_w8(blk(AI_STICK_X), (u8)-80); mem_w8(blk(AI_STICK_Y), 0); break;
    default: break;
    }
}

// N64 0x800FE7C8: picks an entry from the CPU's move list of `kind` (0x80145C54: kind 0 the strikes
// 172-175, 1 three entries, 2 two, 3 four, 4-6 two, 7 one) and returns its buttons. With the
// weak-opponent flag (0x8016B718[slot]) the entry of the planned move (0x8016B580[slot]); else with
// flags bit 0x01 a 30% roll for the first whose record has flag 0x8000; else, or failing those, a
// random entry. Flags bit 0x02 instead takes the entry whose list slot (+6) has the lowest count in
// the 0x8016B350 block (ties 50 / 50). `out_index` gets the entry's list slot.
u16 ai_pick_grapple_move(s32 flags, s32 kind, s32 *out_index) {
    static const u32 kList[8] = {0x80145C74, 0x80145C94, 0x80145CAC, 0x80145CBC,
                                 0x80145CDC, 0x80145CEC, 0x80145CFC, 0x80145D0C};
    static const s32 kCount[8] = {4, 3, 2, 4, 2, 2, 2, 1};
    s32 k = (kind >= 1 && kind <= 7) ? kind : 0;
    u32 list = kList[k];
    s32 n = kCount[k];
    s32 slot = g_ai_self->slot;
    s32 pick = 0;
    bool weak = mem_u8(0x8016B718 + (u32)slot) != 0;
    if (weak || (flags & 1)) {
        pick = -1;
        if (weak) {
            s32 want = mem_s32(0x8016B580 + 4u * (u32)slot);
            for (s32 i = 0; i < n; i++) {
                if (mem_s32(list + 8u * (u32)i) == want) {
                    pick = i;
                    break;
                }
            }
        } else if (chance_check(300)) {
            for (s32 i = 0; i < n; i++) {
                if (ai_move_flags(mem_s32(list + 8u * (u32)i)) & 0x8000) {
                    pick = i;
                    break;
                }
            }
        }
        if (pick == -1)
            pick = rand_pool_next() % n;
    }
    if (flags & 2) {
        pick = 0;
        u32 counts = mem_u32(blk(AI_LISTS));
        for (s32 i = 1; i < n; i++) {
            s8 idx = mem_s8(list + 8u * (u32)i + 6);
            if (idx == -1)
                continue;
            s16 c = mem_s16(counts + 8u * (u32)idx + 4);
            s16 best = mem_s16(counts + 8u * (u32)mem_s8(list + 8u * (u32)pick + 6) + 4);
            if (c < best)
                pick = i;
            if (c == mem_s16(counts + 8u * (u32)mem_s8(list + 8u * (u32)pick + 6) + 4) && chance_check(500))
                pick = i;
        }
    }
    if (out_index)
        *out_index = mem_s8(list + 8u * (u32)pick + 6);
    return mem_u16(list + 8u * (u32)pick + 4);
}

namespace {

// The direction to head in (modes 41 / 45): straight there (`dir`), but more than 100 from the
// ring centre with the direction pointing outward (within 768 of the centre-to-self angle), two
// steps to one side or the other at random; returns whether it turned.
bool ai_avoid_outward(s32 *dir) {
    Vec3 sp = ai_self_point();
    if ((f64)vec2_len(sp.x, sp.z) <= 100.0)
        return false;
    s32 out = angle_dir8(vec_xz_angle(sp.x, sp.z));
    if (abs_i(angle_diff(mem_s32(A_DIR8_ANGLES + 4u * (u32)out), mem_s32(A_DIR8_ANGLES + 4u * (u32)*dir))) >= 768)
        return false;
    *dir = (*dir + (chance_check(500) ? 2 : -2)) & 7;
    return true;
}

// The chance to try a grapple from a distance: 40% / 20% (by mode), none for a weapon holder with
// trait +38 == 3 unless the opponent is down (15).
s32 ai_grapple_try_chance(s32 base) {
    if ((g_ai_self->flags_ef & 1) && mem_s8(kTraits[8].global) == 3)
        return (mem_s32(blk(AI_SIT_OPP)) != 15) ? base : 0;
    return base;
}

}  // namespace

// N64 0x80104024: mode 41, walking up to the opponent. Step 0: the direction to it; away from the
// centre and pointing outward, two steps aside for 5-36 frames; otherwise 40% straight, 60% a
// direction 2-6 steps round, for 7-38 frames (the d-pad in +0x10). Step 1: holds that d-pad; back to
// mode 0 when the time is up or the opponent's state changed; while it stands, taunts or is in the
// TKO check it just walks; otherwise more than 170 away in the ring or both outside, a 40% roll to
// try a grapple.
void ai_mode_41_approach() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        s32 d = dir8_to(&sp, &op);
        s32 t;
        if (ai_avoid_outward(&d)) {
            mem_w16(blk(AI_PENDING), dir8_buttons(d));
            t = (rand_pool_next() & 0x1F) + 5;
        } else {
            s32 off = 0;
            if (rand_pool_next() >= 400)
                off = rand_pool_next() % 5 + 2;
            mem_w16(blk(AI_PENDING), dir8_buttons(d + off));
            t = (rand_pool_next() & 0x1F) + 7;
        }
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
    mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
    mem_w32(blk(AI_MODE_TIMER), (u32)t);
    s16 noted = mem_s16(blk(AI_OPP_STATE));
    if (t <= 0 || noted != g_ai_opp->state) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (noted == 1 || noted == 27 || noted == 28 || noted == 46)
        return;
    if ((f64)mem_f32(blk(AI_DIST_P)) > 170.0 && (mem_u16(blk(AI_WHERE)) & 0x101) &&
        chance_check(ai_grapple_try_chance(400)))
        ai_try_grapple(1);
}

// N64 0x80104648: mode 45, backing off: the direction away from the opponent (turned aside like
// mode 41's when it points outward), held for 15-30 frames while standing; then mode 0.
void ai_mode_45_back_off() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        s32 d = dir8_to(&op, &sp);
        ai_avoid_outward(&d);
        u16 b = dir8_buttons(d);
        mem_w16(blk(AI_PENDING), b);
        mem_w16(blk(AI_HELD), b);
        mem_w32(blk(AI_MODE_TIMER), (u32)((rand_pool_next() & 0xF) + 15));
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    s32 t = mem_s32(blk(AI_MODE_TIMER));
    if (mem_s32(blk(AI_SIT_SELF)) == 1) {
        mem_w32(blk(AI_MODE_TIMER), (u32)(t - 1));
        if (t > 0) {
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
            return;
        }
    }
    ai_set_mode(ai_mode::Think);
}

// N64 0x80105134: mode 53, struggling while dazed, knocked back or off balance (its own situation
// 16-18): a roll on skill x the struggle rate (x 1.2 at levels 5-6 against a flags_e9 0x08 opponent
// 41+ healthier); then a skill x 1.15 roll picks the button by what the opponent does: grappling
// (11) A or L 50 / 50, striking (8) or diving (25 / 26) R, else B; failing that B. The button is
// pressed and held unless it is outside the ring (where-class 0xE0FE) with its time down (dazed,
// 16) or animation (17 / 18) under 3 frames; any other own situation ends the mode.
void ai_mode_53_react_dazed() {
    f32 rate = ai_struggle_rate(g_ai_blk);
    s32 lvl = mem_s32(blk(AI_LEVEL));
    if (lvl < 7 && lvl >= 5 && (g_ai_opp->flags_e9 & 8) &&
        mem_s16(blk(AI_OPP_HEALTH)) - mem_s16(blk(AI_HEALTH)) >= 41)
        rate = (f32)((f64)rate * 1.2);
    if (!chance_check((s32)((f32)mem_s32(blk(AI_SKILL)) * rate)))
        return;
    u16 b = 0x4000;
    if (chance_check((s32)((f64)mem_s32(blk(AI_SKILL)) * 1.15))) {
        s32 osit = mem_s32(blk(AI_SIT_OPP));
        if (osit == 11)
            b = chance_check(500) ? 0x8000 : 0x20;
        else if (osit == 8 || osit == 25 || osit == 26)
            b = 0x10;
    }
    mem_w16(blk(AI_PENDING), b);
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own < 16 || own >= 19) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (mem_u16(blk(AI_WHERE)) & 0xE0FE) {
        s32 left = (own == 16) ? g_ai_self->escape : g_ai_self->anim_frames_left;
        if (left < 3)
            return;
    }
    mem_w16(blk(AI_PRESSED), mem_u16(blk(AI_PENDING)));
    mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
}

// N64 0x800FE508: mode 6, a strike. Step 0: B (outside the ring always; armed: R under the DQ rule);
// unarmed in the ring the strike move list (kind 0; 4 / 5 against an opponent slumped in the corner,
// 5 when it hangs upside down, frame_flags 0x08). Step 1: its own strike (8) moves on to step 2, else
// mode 0; then (the same frame) step 2 once standing again: an opponent knocked back or off balance
// (17 / 18) more than 170 away (where-class 0x103) and a skill roll: ai_try_grapple(1); within 160 a
// skill roll and 25% strike again (step 0); unarmed a skill roll against a striker (8) presses R, a
// grappler (11) A or L, and against a standing or blocking opponent (1 / 14) goes to mode 1; else 0.
void ai_mode_6_strike() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        u16 b;
        if (mem_u16(blk(AI_WHERE)) & 0xE0FE)
            b = 0x4000;
        else if (!(g_ai_self->flags_ef & 1))
            b = ai_pick_grapple_move(1, mem_s32(blk(AI_SIT_OPP)) == 20 ? ((g_ai_opp->frame_flags & 8) ? 5 : 4) : 0, nullptr);
        else
            b = (g_match_rules & 0x2000) ? 0x10 : 0x4000;
        mem_w16(blk(AI_HELD), b);
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step == 1) {
        if (mem_s32(blk(AI_SIT_SELF)) == 8)
            mem_w32(blk(AI_STEP), 2);
        else
            ai_set_mode(ai_mode::Think);
    } else if (step != 2) {
        return;
    }
    if (mem_s32(blk(AI_SIT_SELF)) != 1)
        return;
    s32 osit = mem_s32(blk(AI_SIT_OPP));
    if (osit < 19 && osit >= 17 && (mem_u16(blk(AI_WHERE)) & 0x103) && (f64)mem_f32(blk(AI_DIST_P)) > 170.0 &&
        chance_check(mem_s32(blk(AI_SKILL)))) {
        ai_try_grapple(1);
        return;
    }
    s32 next = ai_mode::Think;
    if ((f64)mem_f32(blk(AI_DIST_P)) < 160.0) {
        if (chance_check(mem_s32(blk(AI_SKILL))) && chance_check(250)) {
            mem_w32(blk(AI_STEP), 0);
            return;
        }
        if (!(g_ai_self->flags_ef & 1) && chance_check(mem_s32(blk(AI_SKILL)))) {
            s32 o = mem_s32(blk(AI_SIT_OPP));
            if (o == 8)
                mem_w16(blk(AI_HELD), 0x10);
            else if (o == 11)
                mem_w16(blk(AI_HELD), chance_check(500) ? 0x8000 : 0x20);
            else
                next = (o == 1 || o == 14) ? ai_mode::EngageNearbyOpponent : ai_mode::Think;
        }
    }
    ai_set_mode(next);
}

// N64 0x80104CDC: modes 49 / 50 on the apron or standing. Step 0: the timer cleared; mode 49 (not
// in a meter mode, flags_ee 0x06, nor with trait +39 == 2) pushes the stick for a taunt
// (ai_stick_taunt); mode 50 needs SPECIAL (flags_ee 0x02) and pushes the stick up (+0x13 = 80): the
// taunt into finisher mode. Step 1: mode 50 gives up after 60 frames; back to mode 0 when the
// opponent's state changed or the CPU is no longer taunting (situation 43).
void ai_mode_49_apron_wait_taunt() {
    s32 step = mem_s32(blk(AI_STEP));
    s32 mode = mem_s32(blk(AI_MODE));
    if (step == 0) {
        mem_w32(blk(AI_MODE_TIMER), 0);
        if (mode == ai_mode::ApronWait && ((g_ai_self->flags_ee & 6) || mem_s8(kTraits[9].global) == 2)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_s32(blk(AI_MODE)) == ai_mode::ApronWaitAndTaunt) {
            if (!(g_ai_self->flags_ee & 2)) {
                ai_set_mode(ai_mode::Think);
                return;
            }
            mem_w8(blk(AI_STICK_Y), 80);
        } else {
            ai_stick_taunt();
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    }
    if (step != 1)
        return;
    if (mode == ai_mode::ApronWaitAndTaunt) {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) + 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t >= 61)
            ai_set_mode(ai_mode::Think);
    }
    if (mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state)
        ai_set_mode(ai_mode::Think);
    if (mem_s32(blk(AI_SIT_SELF)) != 43)
        ai_set_mode(ai_mode::Think);
}

// N64 0x800FFB98: mode 15, a partner's decision after a whip or run. Step 0: standing, against an
// opponent rebounding (3) step 1, running (29) wait; whipping (own 28) wait; else mode 0. Step 1: on
// the floor mode 0; both in the ring: a runner the partner can reach (ai_partner_distance) mode 16;
// in team play or under over-the-top rules, with a running-move trait (attrs +0x34 0x700000) and
// mode 114 not among the last five, a side more than 260 long and level 2+: 45% (team play: 15%, or
// 45% with attrs +0x18 == 2) mode 114; with attrs +0x38 0x80000 and a side target: mode 90 at 20% (40%
// with move 278 a favourite, 100% when it is the planned move). Then a running opponent (flags86
// 0x20): in the ring the partner mode (ai_partner_mode, by flags86 0x40), else 0.
void ai_mode_15_partner_decide() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        s32 own = mem_s32(blk(AI_SIT_SELF));
        if (own != 1) {
            if (own != 28)
                ai_set_mode(ai_mode::Think);
            return;
        }
        s32 o = mem_s32(blk(AI_SIT_OPP));
        if (o == 29)
            return;
        if (o != 3) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_STEP), 1);
        return;
    }
    if (step != 1)
        return;
    Wrestler *s = g_ai_self;
    if (s->location & 4) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (mem_u16(blk(AI_WHERE)) & 1) {
        if ((g_ai_opp->flags86 & 0x20) && ai_partner_distance()) {
            ai_set_mode(ai_mode::CatchRunnerVariant1);
            return;
        }
        if (((g_match_type & 0x1000) || (mem_u8(blk(AI_FLAGS)) & 0x40)) && (mem_u32(s->attrs + 0x34) & 0x700000) &&
            !ai_mode_recent(ai_mode::LButtonAgainstRunner, 5)) {
            Vec3 sp = ai_self_point(), op = ai_opp_point();
            s32 d = dir8_to(&sp, &op);
            if ((f64)ai_side_distance(d / 2, &sp) > 260.0 && mem_s32(blk(AI_LEVEL)) >= 2) {
                s32 c = 450;
                if (!(mem_u8(blk(AI_FLAGS)) & 0x40))
                    c = (mem_u8(s->attrs + 0x18) == 2) ? 450 : 150;
                if (chance_check(c)) {
                    ai_set_mode(ai_mode::LButtonAgainstRunner);
                    return;
                }
            }
        }
        if ((mem_u32(s->attrs + 0x38) & 0x80000) && (s8)ai_side_target(nullptr)) {
            s32 c;
            if (mem_u8(0x8016B718 + (u32)s->slot) && mem_s32(0x8016B580 + 4u * (u32)s->slot) == 278)
                c = 1000;
            else
                c = (ai_move_flags(278) & 0x8000) ? 400 : 200;
            if (chance_check(c)) {
                ai_set_mode(ai_mode::PickTargetAndRun);
                return;
            }
        }
    }
    u16 f86 = g_ai_opp->flags86;
    if (f86 & 0x20)
        ai_set_mode((mem_u16(blk(AI_WHERE)) & 1) ? ai_partner_mode((f86 & 0x40) ? 1 : 0) : ai_mode::Think);
}

// N64 0x80105354: mode 54, getting up (own situation 24). Step 0, by the get-up's step: in the crouch
// (state_step 2): with the opponent 160 or more away, in a tag match its partner in the ring within
// 130 (or in one-on-one / elimination anyone else within 160), R and C-right; else step 1. Closer,
// in the ring or both outside: R (the crouch) and, facing the opponent within 170, a skill roll by
// what it does: standing (1) B / A by the get-up strike's chance (800 if move 217 is a favourite,
// else 500) then mode 0; striking (8) L too (the roll) and step 1; grappling (11) B, blocking (14) A,
// then mode 0; down (15), in a hold, or in a strike state (flags80 0x07) step 1. Failing the roll a
// random 0-449 as standing, 450-899 adds L, 900+ releases R; step 1. Earlier steps of the get-up: a
// skill roll within 160 holds R. Not in situation 24 any more: mode 0. Step 1: standing, a skill
// roll against a striker R (again), against a grappler mode 42 or 43; else mode 0 (still getting up:
// wait).
void ai_mode_54_get_up() {
    Wrestler *s = g_ai_self;
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        s32 gs = s->state_step;
        if (gs == 2) {
            if ((f64)mem_f32(blk(AI_DIST_P)) >= 160.0) {
                Vec3 sp = ai_self_point();
                const Wrestler *oppp = &g_wrestlers[g_ai_opp->partner];
                bool help = false;
                if ((g_match_type & 0x2000) && (s->location & oppp->location & 1) &&
                    (f64)dist_to(&sp, &oppp->point_120) < 130.0)
                    help = true;
                if (!help && (g_match_type & 0xC000)) {
                    for (s32 i = 0; i < 4 && !help; i++) {
                        const Wrestler *w = &g_wrestlers[i];
                        if (w->roster_id == -1 || w->slot == s->slot || w->slot == g_ai_opp->slot)
                            continue;
                        help = (f64)dist_to(&sp, &w->point_120) < 160.0;
                    }
                }
                if (help) {
                    mem_w16(blk(AI_HELD), 0x10);
                    ai_press_focus_switch();
                } else {
                    mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                }
                return;
            }
            if (mem_u16(blk(AI_WHERE)) & 0x101) {
                mem_w16(blk(AI_HELD), 0x10);
                if (abs_i(angle_diff(s->facing, mem_s32(blk(AI_ANGLE)))) >= 170)
                    return;
                s32 strike = (ai_move_flags(217) & 0x8000) ? 800 : 500;
                auto press_mode0 = [&](u16 b) {
                    mem_w16(blk(AI_HELD), mem_u16(blk(AI_HELD)) | b);
                    ai_set_mode(ai_mode::Think);
                };
                if (chance_check(mem_s32(blk(AI_SKILL)))) {
                    switch (mem_s32(blk(AI_SIT_OPP))) {
                    case 1:
                        press_mode0(chance_check(strike) ? 0x4000 : 0x8000);
                        return;
                    case 8:
                        mem_w16(blk(AI_HELD), mem_u16(blk(AI_HELD)) | 0x20);
                        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                        break;
                    case 11: press_mode0(0x4000); return;
                    case 14: press_mode0(0x8000); return;
                    case 15:
                        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                        break;
                    default:
                        if ((g_ai_opp->flags80 & 0xC0) || (g_ai_opp->flags80 & 7))
                            mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                        break;
                    }
                } else {
                    s32 r = rand_pool_next();
                    if (r < 450) {
                        press_mode0(chance_check(strike) ? 0x4000 : 0x8000);
                        return;
                    }
                    if (r < 900)
                        mem_w16(blk(AI_HELD), mem_u16(blk(AI_HELD)) | 0x20);
                    else
                        mem_w16(blk(AI_HELD), 0);
                    mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                }
            }
        } else if (gs >= 2) {
            mem_w32(blk(AI_STEP), 1);
        } else if (chance_check(mem_s32(blk(AI_SKILL))) && (f64)mem_f32(blk(AI_DIST_P)) < 160.0) {
            mem_w16(blk(AI_HELD), 0x10);
        }
        if (mem_s32(blk(AI_SIT_SELF)) != 24)
            ai_set_mode(ai_mode::Think);
        return;
    }
    if (step != 1)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own != 1) {
        if (own != 24)
            ai_set_mode(ai_mode::Think);
        return;
    }
    if (!chance_check(mem_s32(blk(AI_SKILL)))) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if (o == 8) {
        mem_w16(blk(AI_HELD), 0x10);
        return;
    }
    ai_set_mode(o == 11 ? (chance_check(500) ? ai_mode::WaitThenGrapple : ai_mode::GrappleEvade) : ai_mode::Think);
}

// N64 0x80105AD0: mode 59, a strike from behind or the side: standing, when the facings differ by
// more than 90 degrees and the opponent is within the reach of the CPU's list block (+0x50 plus
// +0x54 x 10) presses B (step 1); the opponent no longer running or rebounding (situations 2 / 3)
// ends it; step 1 lasts while its strike (8) does.
void ai_mode_59_strike_facing() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step != 0) {
        if (step == 1 && mem_s32(blk(AI_SIT_SELF)) != 8)
            ai_set_mode(ai_mode::Think);
        return;
    }
    if (mem_s32(blk(AI_SIT_SELF)) != 1) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (abs_i(angle_diff(g_ai_self->facing, g_ai_opp->facing)) >= 1025) {
        u32 lb = mem_u32(blk(AI_LISTS));
        f64 reach = (f64)mem_f32(lb + 0x50) + (f64)mem_s16(lb + 0x54) * 10.0;
        if ((f64)mem_f32(blk(AI_DIST_P)) < reach) {
            mem_w16(blk(AI_HELD), 0x4000);
            mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        }
    }
    if ((u32)(mem_s32(blk(AI_SIT_OPP)) - 2) >= 2)
        ai_set_mode(ai_mode::Think);
}

// N64 0x800FD98C: mode 1, the grapple attempt: A, then standing within 160 a skill roll against a
// striker (8) the block (mode 46), a grappler (11) 75% the L counter (21) else 43; anything else (and
// out of reach) mode 0; while its own grapple attempt (11) runs it waits.
void ai_mode_1_engage() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        mem_w16(blk(AI_HELD), 0x8000);
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    s32 next = ai_mode::Think;
    if (own == 1) {
        if ((f64)mem_f32(blk(AI_DIST_P)) < 160.0 && chance_check(mem_s32(blk(AI_SKILL)))) {
            s32 o = mem_s32(blk(AI_SIT_OPP));
            if (o == 8)
                next = ai_mode::HoldBlock;
            else if (o == 11)
                next = chance_check(750) ? ai_mode::PressLButton : ai_mode::WaitThenGrapple;
        }
    } else if (own == 11) {
        return;
    }
    ai_set_mode(next);
}

// N64 0x800FEC98: mode 9, a strike and a follow-up. Step 0: B. Step 1, while striking (8): the next
// input: B on a raised place (where 0x80); else armed B (R under the DQ rule) pressed at once; else a
// move from the strike list (ai_pick_grapple_move kind 1; flags 1 with the weak-opponent plan, 2 with
// mode flag 0x02, else 1), held. Step 2: while striking in state 0x22 it keeps holding; standing
// again, against an opponent knocked back or off balance (17 / 18) more than 170 away a 50% roll to
// try a grapple; mode 0 otherwise.
void ai_mode_9_strike_follow() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        mem_w16(blk(AI_HELD), 0x4000);
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step == 1) {
        if (mem_s32(blk(AI_SIT_SELF)) != 8) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_u16(blk(AI_WHERE)) & 0x80) {
            mem_w16(blk(AI_PENDING), 0x4000);
        } else if (!mem_u8(0x8016B718 + (u32)g_ai_self->slot) && (g_ai_self->flags_ef & 1)) {
            mem_w16(blk(AI_HELD), (g_match_rules & 0x2000) ? 0x10 : 0x4000);
            mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
            return;
        } else {
            s32 f = mem_u8(0x8016B718 + (u32)g_ai_self->slot) ? 1 : ((mem_u8(blk(AI_FLAGS)) & 2) ? 2 : 1);
            mem_w16(blk(AI_PENDING), ai_pick_grapple_move(f, 1, nullptr));
        }
        mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    }
    if (step != 2)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own == 8) {
        if (g_ai_self->state == state::Strike)
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
        return;
    }
    if (own != 1) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if (o >= 19 || o < 17) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 170.0 && chance_check(500))
        ai_try_grapple(1);
}

namespace {

enum : u32 {
    AI_REACH = 0xA0,  // f32: the reach of the move mode 10 prepared (the list block's entry)
};


// The CPU's chance roll on skill x a rate, forced to 1000 for a losing CPU at levels 5-6 against a
// flags_e9 0x08 opponent 41+ healthier.
bool ai_mash_roll(f32 rate) {
    s32 c = (s32)((f32)mem_s32(blk(AI_SKILL)) * rate);
    s32 lvl = mem_s32(blk(AI_LEVEL));
    if (lvl < 7 && lvl >= 5 && (g_ai_opp->flags_e9 & 8) && mem_s16(blk(AI_OPP_HEALTH)) - mem_s16(blk(AI_HEALTH)) >= 41)
        c = 1000;
    return chance_check(c) != 0;
}

// The running counters of modes 3 and 10 near an opponent in a move: a skill roll, then 20% R
// (the counter), 40% L, 40% (and a failed roll) mode 119 (keep running).
void ai_run_counter() {
    if (!chance_check(mem_s32(blk(AI_SKILL)))) {
        ai_set_mode(ai_mode::KeepRunning);
        return;
    }
    s32 r = rand_pool_next();
    if (r < 200)
        mem_w16(blk(AI_HELD), 0x10);
    else if (r < 600)
        mem_w16(blk(AI_HELD), 0x20);
    else
        ai_set_mode(ai_mode::KeepRunning);
}

}  // namespace

// N64 0x8010257C: mode 30, being whipped (own situation 3): a roll on skill x the struggle rate,
// then 90%: B or A 50 / 50 (the reversal); when the whip is over, running in the ring (2) modes 107 /
// 108 50 / 50, else 0.
void ai_mode_30_whip_reverse() {
    f32 rate = ai_struggle_rate(g_ai_blk);
    if (chance_check((s32)((f32)mem_s32(blk(AI_SKILL)) * rate)) && chance_check(900))
        mem_w16(blk(AI_PRESSED), chance_check(500) ? 0x4000 : 0x8000);
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own == 3)
        return;
    s32 next = ai_mode::Think;
    if (own == 2 && (g_ai_self->location & 1))
        next = chance_check(500) ? ai_mode::RunWithMoveVariant3 : ai_mode::RunWithMoveVariant4;
    ai_set_mode(next);
}

// N64 0x80102404: mode 29, struggling in a lock-up (own situation 6): the trait +36 rate
// (0x80145ECC), raised under 128 perceived health (x 1.15, level 3+ x 1.2) and more under 65 (x 1.25,
// level 3+ x 1.3), x 1.2 for a losing CPU at levels 5-6, x the trait +37 rate (0x80145EB8), x skill:
// a roll presses A+B+L+R (0xC030). Mode 0 once out of the lock-up.
void ai_mode_29_grappled_struggle() {
    s16 hp = mem_s16(blk(AI_HEALTH));
    f32 r = mem_f32(0x80145ECC + 4u * (u32)(s32)mem_s8(kTraits[6].global));
    if (hp < 128) {
        f64 m;
        if (hp < 65)
            m = (mem_s32(blk(AI_LEVEL)) >= 3) ? 1.3 : 1.25;
        else
            m = (mem_s32(blk(AI_LEVEL)) >= 3) ? 1.2 : 1.15;
        r = (f32)((f64)r * m);
    }
    s32 lvl = mem_s32(blk(AI_LEVEL));
    if (lvl < 7 && lvl >= 5 && (g_ai_opp->flags_e9 & 8) && mem_s16(blk(AI_OPP_HEALTH)) - mem_s16(blk(AI_HEALTH)) >= 41)
        r = (f32)((f64)r * 1.2);
    f32 t37 = mem_f32(0x80145EB8 + 4u * (u32)(s32)mem_s8(kTraits[7].global));
    if (chance_check((s32)((f32)mem_s32(blk(AI_SKILL)) * (r * t37))))
        mem_w16(blk(AI_PRESSED), 0xC030);
    if (mem_s32(blk(AI_SIT_SELF)) != 6)
        ai_set_mode(ai_mode::Think);
}

// N64 0x80102660: mode 31, held (in a hold, a pin, the corner, the TKO check ...). Step 0, in the
// ground hold (0x49): the d-pad toward the ropes, away from the holder along its ring zone's axis
// (zones 0 / 2: right if the holder is to the left, else left; 1 / 3: up / down). Step 1 while held
// (situations 7, 15, 20, 38, 40, 59, 60; 61 hanging on the cage while the holder is on the wall:
// against a holder in 0xC6 only below 51 spirit, never against 0xC7): in 0x49 it crawls when the
// holder's bounds (dpad_held) allow that side (0x8014D218 by zone); and mashes A+B+L+R on skill x the
// struggle rate (1000 for a losing CPU, see ai_mash_roll). Anything else: mode 0.
void ai_mode_31_ground_hold() {
    Wrestler *s = g_ai_self;
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        if (s->state == state::GroundHoldDefender) {
            s16 z = mem_s16(blk(AI_ZONE_SELF));
            Vec3 sp = ai_self_point(), op = ai_opp_point();
            if (z == 0 || z == 2)
                mem_w16(blk(AI_PENDING), op.x < sp.x ? 0x100 : 0x200);
            else if (z == 1 || z == 3)
                mem_w16(blk(AI_PENDING), op.z < sp.z ? 0x800 : 0x400);
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    }
    if (step != 1)
        return;
    switch (mem_s32(blk(AI_SIT_SELF))) {
    case 7: case 15: case 20: case 38: case 40: case 59: case 60:
        break;
    case 61:
        if (!(g_ai_opp->location & 0x400)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (g_ai_opp->state == state::CageHang) {
            if (mem_s16(blk(AI_SPIRIT)) >= 51) {
                ai_set_mode(ai_mode::Think);
                return;
            }
        } else if (g_ai_opp->state == state::CageFallOff) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        break;
    default:
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (s->state == state::GroundHoldDefender && (mem_u16(0x8014D218 + 2u * (u32)mem_s16(blk(AI_ZONE_SELF))) & g_ai_opp->dpad_held))
        mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
    if (ai_mash_roll(ai_struggle_rate(g_ai_blk)))
        mem_w16(blk(AI_PRESSED), 0xC030);
}

// N64 0x800FDCD8: mode 3, a run and a running attack. Step 0: not against a down opponent (15) and
// in the ring or both outside: C-down (run). Step 1: rebounding (4) mode 119; running (2, 5) or its
// own grapple attempt (11) stays: at the run's step 2, within 170 a skill roll against a striker or
// grappler (8 / 11) presses L (the evade); within 160 of an opponent in a move (flags80 0x03) or down
// (15) the running counter (ai_run_counter), else A (the running grapple). Anything else: mode 0.
void ai_mode_3_run_in_and_counter() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        if (mem_s32(blk(AI_SIT_OPP)) == 15 || !(mem_u16(blk(AI_WHERE)) & 0x101)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w16(blk(AI_HELD), 4);
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own == 4) {
        ai_set_mode(ai_mode::KeepRunning);
        return;
    }
    if (own == 5 || own == 11)
        return;
    if (own != 2) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (g_ai_self->state_step != 2)
        return;
    if ((f64)mem_f32(blk(AI_DIST_P)) < 170.0 && chance_check(mem_s32(blk(AI_SKILL)))) {
        s32 o = mem_s32(blk(AI_SIT_OPP));
        if (o == 8 || o == 11) {
            mem_w16(blk(AI_HELD), 0x20);
            return;
        }
    }
    if ((f64)mem_f32(blk(AI_DIST)) < 160.0) {
        if ((g_ai_opp->flags80 & 3) || mem_s32(blk(AI_SIT_OPP)) == 15)
            ai_run_counter();
        else
            mem_w16(blk(AI_HELD), 0x8000);
    }
}

// N64 0x800FEE64: mode 10, a run with a planned attack. Step 0: C-down, and the follow-up in +0x10
// with its reach in +0xA0: on a raised place B (175); armed (no weak-opponent plan) B, or R under
// the DQ rule (the list block's +0x70); else the evade direction (ai_evade_direction) picks a running
// move (list 3, the d-pad added for its entries 5 / 6) or, with none, list 2 (flags 1 with the plan
// or mode flag 0x01, else 2), the reach from the list block entry. Step 1: rebounding (4) mode 119;
// running (2) at the run's step 2: within 170 a skill roll against a striker or grappler: armed all
// four d-pad bits pressed (0x0F00), else L; within the reach + 20 of an opponent in a move
// (flags80 0x05) the running counter, else the prepared input; 5 / 8 stay; anything else mode 0.
void ai_mode_10_evade() {
    s32 step = mem_s32(blk(AI_STEP));
    Wrestler *s = g_ai_self;
    if (step == 0) {
        mem_wf32(blk(AI_REACH), 0.0f);
        mem_w16(blk(AI_HELD), 4);
        bool weak = mem_u8(0x8016B718 + (u32)s->slot) != 0;
        if (mem_u16(blk(AI_WHERE)) & 0x80) {
            mem_w16(blk(AI_PENDING), 0x4000);
            mem_wf32(blk(AI_REACH), 175.0f);
        } else if (!weak && (s->flags_ef & 1)) {
            mem_w16(blk(AI_PENDING), (g_match_rules & 0x2000) ? 0x10 : 0x4000);
            mem_w32(blk(AI_REACH), mem_u32(mem_u32(blk(AI_LISTS)) + 0x70));
        } else {
            s32 dir = ai_evade_direction();
            s32 f = (weak || (mem_u8(blk(AI_FLAGS)) & 1)) ? 1 : 2;
            s32 idx = 0;
            if (dir == -1) {
                mem_w16(blk(AI_PENDING), ai_pick_grapple_move(f, 2, &idx));
            } else {
                mem_w16(blk(AI_PENDING), ai_pick_grapple_move(f, 3, &idx));
                if ((u32)(idx - 5) < 2)
                    mem_w16(blk(AI_HELD), mem_u16(blk(AI_HELD)) | dir8_buttons(dir));
            }
            mem_wf32(blk(AI_REACH), mem_f32(mem_u32(blk(AI_LISTS)) + 8u * (u32)idx));
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    }
    if (step != 1)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own == 4) {
        ai_set_mode(ai_mode::KeepRunning);
        return;
    }
    if (own == 5 || own == 8)
        return;
    if (own != 2) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (s->state_step != 2)
        return;
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if ((f64)mem_f32(blk(AI_DIST_P)) < 170.0 && chance_check(mem_s32(blk(AI_SKILL))) && (o == 8 || o == 11)) {
        if (s->flags_ef & 1)
            mem_w16(blk(AI_PRESSED), 0x0F00);
        else
            mem_w16(blk(AI_HELD), 0x20);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST)) < (f64)mem_f32(blk(AI_REACH)) + 20.0) {
        if (g_ai_opp->flags80 & 5)
            ai_run_counter();
        else
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
    }
}

namespace {

enum : u32 {
    AI_RETURN_MODE = 0x86,  // s16: the mode to come back to after a detour (modes 94 / 100)
    AI_JOINTS = 0x94,       // u8[6]: the opponent's joints a block watches (mode 47)
    AI_JOINT_COUNT = 0x9A,  // s16: how many
};


}  // namespace

// N64 0x80104860: mode 47, blocking a strike or a dive. Step 0: against a striker (8) or a diver (25,
// or 26 in one of the six (state, step) pairs at 0x80145ED8; others wait) a block timer
// (ai_block_timer) and the joints the opponent's move hits with (its record bytes +0x0D-+0x0F; 20
// stands for joints 9 and 17); else mode 0. Step 1 (from the same frame): the timer counts down; at
// 0 against a striker R (the block); against a diver, while every watched joint is still more than
// 50 above the CPU's joint 4 the timer waits, else R; anything else mode 0. Step 2: mode 0 when no
// longer blocking (14).
void ai_mode_47_block_dive() {
    s32 step = mem_s32(blk(AI_STEP));
    const Wrestler *o = g_ai_opp;
    if (step == 0) {
        s32 osit = mem_s32(blk(AI_SIT_OPP));
        if (osit == 26) {
            bool found = false;
            for (s32 i = 0; i < 6 && !found; i++)
                found = o->state == mem_s16(0x80145ED8 + 4u * (u32)i) && o->state_step == mem_s16(0x80145EDA + 4u * (u32)i);
            if (!found)
                return;
        } else if (osit != 25 && osit != 8) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_MODE_TIMER), (u32)ai_block_timer());
        mem_w16(blk(AI_JOINT_COUNT), 0);
        for (s32 i = 0; i < 3; i++) {
            u8 j = mem_u8(o->move + 0x0D + (u32)i);
            s16 n = mem_s16(blk(AI_JOINT_COUNT));
            if (j == 20) {
                mem_w8(blk(AI_JOINTS) + (u32)n, 9);
                n = (s16)(n + 1);
                mem_w16(blk(AI_JOINT_COUNT), (u16)n);
                mem_w8(blk(AI_JOINTS) + (u32)n, 17);
            } else {
                mem_w8(blk(AI_JOINTS) + (u32)n, j);
            }
            mem_w16(blk(AI_JOINT_COUNT), (u16)(mem_s16(blk(AI_JOINT_COUNT)) + 1));
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        step = 1;
    }
    if (step == 1) {
        s32 osit = mem_s32(blk(AI_SIT_OPP));
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (osit == 8) {
            if (t <= 0) {
                mem_w16(blk(AI_HELD), 0x10);
                mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
            }
            return;
        }
        if (osit != 25 && osit != 26) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (t > 0)
            return;
        s16 n = mem_s16(blk(AI_JOINT_COUNT));
        s32 above = 0;
        for (s32 i = 0; i < n; i++) {
            s8 j = mem_s8(blk(AI_JOINTS) + (u32)i);
            // The record's joint numbers address Wrestler +0x114 + 12 x j (j 1 is point_120); y at +4.
            f32 jy;
            std::memcpy(&jy, (const u8 *)o + 0x118 + 12 * j, 4);
            if ((f64)(f32)(jy - g_ai_self->joints[3].y) > 50.0)
                above++;
        }
        if (above == n) {
            mem_w32(blk(AI_MODE_TIMER), (u32)(mem_s32(blk(AI_MODE_TIMER)) + 1));
        } else {
            mem_w16(blk(AI_HELD), 0x10);
            mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        }
        return;
    }
    if (step == 2 && mem_s32(blk(AI_SIT_SELF)) != 14)
        ai_set_mode(ai_mode::Think);
}

// N64 0x800FDA7C: mode 2, a grapple with a detour. Step 0: against a dazed opponent (16) in the ring
// in finisher mode, facing it, with the special trait (attrs +0x3C 0x01 without 0x02): mode 100
// (coming back to this mode); otherwise, the CPU on the apron and facing it, mode 0. Against an opponent
// slumped in the corner (20): not in finisher mode a 15% roll (no weak-opponent plan) for mode 94; in
// finisher mode mode 94 when its corner special suits the opponent's side (attrs +0x3C 0x600000:
// 0x200000 face down, flags8a 0x01, or 0x400000 face up). Otherwise step 1: A. Step 2: the attempt
// (0x50) holds A; standing again: mode 1 (at step 1); anything else mode 0.
void ai_mode_2_grapple_approach() {
    s32 step = mem_s32(blk(AI_STEP));
    Wrestler *s = g_ai_self;
    if (step == 1) {
        mem_w16(blk(AI_RETURN_MODE), 0);
        mem_w16(blk(AI_HELD), 0x8000);
        mem_w32(blk(AI_STEP), 2);
        return;
    }
    if (step == 2) {
        if (s->state == state::Standing) {
            ai_set_mode(ai_mode::EngageNearbyOpponent);
            mem_w32(blk(AI_STEP), 1);
        } else if (s->state == state::GrappleAttempt) {
            mem_w16(blk(AI_HELD), 0x8000);
        } else {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    if (step != 0)
        return;
    auto detour = [&](s32 mode) {
        mem_w16(blk(AI_RETURN_MODE), (u16)mem_s32(blk(AI_MODE)));
        ai_set_mode(mode);
    };
    s32 osit = mem_s32(blk(AI_SIT_OPP));
    if (osit == 16) {
        bool tried = false;
        if ((mem_u16(blk(AI_WHERE)) & 0x101) && (s->flags_ee & 4)) {
            if (ai_faced_by(s, g_ai_opp)) {
                mem_w32(blk(AI_STEP), (u32)(step + 1));
                return;
            }
            u16 sp = mem_u16(s->attrs + 0x3C);
            if (!(sp & 2) && (sp & 1)) {
                detour(100);
                return;
            }
            tried = true;
        }
        (void)tried;
        if (!(mem_u16(blk(AI_WHERE)) & 8) || ai_faced_by(s, g_ai_opp)) {
            mem_w32(blk(AI_STEP), (u32)(step + 1));
            return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (osit == 20) {
        if (!(s->flags_ee & 4)) {
            if (!mem_u8(0x8016B718 + (u32)s->slot) && chance_check(150)) {
                detour(94);
                return;
            }
        } else {
            u32 sp = mem_u32(s->attrs + 0x3C) & 0x600000;
            if (sp == ((g_ai_opp->flags8a & 1) ? 0x200000u : 0x400000u)) {
                detour(94);
                return;
            }
        }
    }
    mem_w32(blk(AI_STEP), (u32)(step + 1));
}

// N64 0x80104BB8: mode 48, walking up to a downed opponent: the direction (ai_dir8_to_target), held
// until within reach (its body's distance + 75 when down, 15; else 120), or the opponent's state
// changes, or the CPU stops standing: then mode 0.
void ai_mode_48_approach_downed() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        mem_w16(blk(AI_PENDING), ai_dir8_to_target());
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    f64 reach = 120.0;
    if (mem_s32(blk(AI_SIT_OPP)) == 15) {
        Body b;
        lying_body_axis(g_ai_opp, &b);
        Vec3 sp = ai_self_point();
        reach = (f64)(f32)((f64)lying_body_dist(&b, &sp) + 75.0);
    }
    if ((f64)mem_f32(blk(AI_DIST)) < reach)
        ai_set_mode(ai_mode::Think);
    if (mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state)
        ai_set_mode(ai_mode::Think);
    if (g_ai_self->state != state::Standing)
        ai_set_mode(ai_mode::Think);
    mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
}

// N64 0x80103F4C: mode 40, a run: C-down (on the floor toward the opponent's zone side when the zones
// differ, 0x8014D218); while running (2) within 180 the d-pad is pressed (0x0F00: stop the run's
// steering) and mode 119; a rebound (4) mode 119; anything else mode 0.
void ai_mode_40_run() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        mem_w16(blk(AI_HELD), 4);
        if (mem_u16(blk(AI_WHERE)) & 0x100) {
            s16 oz = mem_s16(blk(AI_ZONE_OPP));
            if (mem_s16(blk(AI_ZONE_SELF)) != oz)
                mem_w16(blk(AI_HELD), (u16)(mem_u16(0x8014D218 + 2u * (u32)oz) | 4));
        }
        mem_w32(blk(AI_STEP), (u32)(step + 1));
        return;
    }
    if (step != 1)
        return;
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own != 2) {
        ai_set_mode(own == 4 ? ai_mode::KeepRunning : ai_mode::Think);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) < 180.0) {
        mem_w16(blk(AI_PRESSED), 0x0F00);
        ai_set_mode(ai_mode::KeepRunning);
    }
}

// N64 0x8010B8B0: mode 119, keep running: the d-pad pressed each frame while running (2); a rebound
// (4) keeps the mode; anything else mode 0.
void ai_mode_119_keep_running() {
    s32 own = mem_s32(blk(AI_SIT_SELF));
    if (own == 2)
        mem_w16(blk(AI_PRESSED), 0x0F00);
    else if (own != 4)
        ai_set_mode(ai_mode::Think);
}

namespace {


}  // namespace

// N64 0x8012E198: the direction 0-7 opposite an angle.
s32 angle_dir8_back(s32 angle) { return angle_dir8(angle + 2048); }

// N64 0x80101774: the CPU's lock-up move list (pairs of s16 move, u16 buttons) and its length, by
// the lock-up: in the corner (flags78 0x100) front 0x80145E20 / strong 0x80145E28, rear 0x80145E30 /
// 0x80145E38 (two each); front weak 0x80145D9C (8), strong 0x80145DCC (8) or 0x80145DEC (9, with
// attrs +0x34 0x02); rear 0x80145DBC / strong 0x80145E10 (4). Renamed 2026-11-03 from
// ai_lockup_wait_time.
s32 ai_lockup_move_list(u32 *list) {
    u32 f = g_ai_self->flags78;
    bool rear = (f & 0x80) != 0, strong = (f & 4) != 0;
    s32 n;
    if (f & 0x100) {
        *list = rear ? (strong ? 0x80145E38 : 0x80145E30) : (strong ? 0x80145E28 : 0x80145E20);
        n = 2;
    } else if (!rear) {
        if (!strong) {
            *list = 0x80145D9C;
            n = 8;
        } else if (mem_u32(g_ai_self->attrs + 0x34) & 2) {
            *list = 0x80145DEC;
            n = 9;
        } else {
            *list = 0x80145DCC;
            n = 8;
        }
    } else {
        *list = strong ? 0x80145E10 : 0x80145DBC;
        n = 4;
    }
    return n;
}

// N64 0x80101590: mode 26, the lock-up move. Step 0 (still in the lock-up, 13): the planned move's
// buttons with the weak-opponent plan, else 30% the first favourite (record flag 0x8000), else a
// random entry of the list (ai_lockup_move_list) into +0x10. Step 1: pressed and held while the
// lock-up lasts; then mode 0.
void ai_mode_26_lockup_press_move() {
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 1) {
        if (mem_s32(blk(AI_SIT_SELF)) == 13) {
            mem_w16(blk(AI_PRESSED), mem_u16(blk(AI_PENDING)));
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
            return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (step == 2) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (step != 0)
        return;
    u32 list = 0;
    s32 n = ai_lockup_move_list(&list);
    if (mem_s32(blk(AI_SIT_SELF)) != 13) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    s32 slot = g_ai_self->slot;
    s32 pick = -1;
    if (mem_u8(0x8016B718 + (u32)slot)) {
        for (s32 i = 0; i < n; i++)
            if (mem_s16(list + 4u * (u32)i) == mem_s32(0x8016B580 + 4u * (u32)slot)) {
                pick = i;
                break;
            }
    } else if (chance_check(300)) {
        for (s32 i = 0; i < n; i++)
            if (ai_move_flags(mem_s16(list + 4u * (u32)i)) & 0x8000) {
                pick = i;
                break;
            }
    }
    if (pick == -1)
        pick = rand_pool_next() % n;  // n is never 0 (the original traps a division by 0)
    mem_w16(blk(AI_PENDING), mem_u16(list + 4u * (u32)pick + 2));
    mem_w32(blk(AI_STEP), 1);
}

// N64 0x80102C4C: modes 33 / 34, back into the ring from outside (33: onto the apron and through the
// ropes with C-up; 34: from the apron up the corner, C-down). An illegal man in tag (mode flag 0x20)
// whose apron side is not open (apron_walk_dpad) stops on the apron (mode 0), or turns 34 into 33.
// Step 0: standing (blocking 14 waits; anything else mode 0), not out of the fall: armed it drops
// the weapon with R unless trait +38 is 3; step 1. Step 1: every 32 frames the d-pad toward the ring
// centre (angle_dir8_back of its position); 34 adds C-down and goes to step 2; 33 presses C-up and
// holds the d-pad while standing, giving up (mode 0) on the floor against an opponent working on it
// that dives (25 / 26), is within 170 and on the apron striking (state 0x20), or strikes / grapples
// (8 / 11) nearby; climbing (48) 35% another C-up for a legal man in 33, else step 3. Step 2 (34
// running): at the run's step 2 within 170 a skill roll against a striker or grappler presses L, else
// the d-pad + C-down; a rebound (4) mode 119; 5 waits; climbing (48 / 49) step 3. Step 3 (climbing):
// within 160 of a striker / grappler a skill roll for step 4 (defend) else step 5. Step 4: R (on the
// apron, or against a striker), L against a grappler. Step 5: waits while climbing; mode 0 after.
void ai_mode_33_apron() {
    Wrestler *s = g_ai_self;
    s32 mode = mem_s32(blk(AI_MODE));
    if ((mem_u8(blk(AI_FLAGS)) & 0x20) && !apron_walk_dpad(s->slot)) {
        if (s->location & 2) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mode == ai_mode::HeadForRingAlternate) {
            ai_set_mode(ai_mode::HeadForRing);
            return;
        }
    }
    s32 own = mem_s32(blk(AI_SIT_SELF));
    switch (mem_s32(blk(AI_STEP))) {
    case 0:
        if (own != 1) {
            if (own != 14)
                ai_set_mode(ai_mode::Think);
            return;
        }
        if (s->end_flags & 1) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_MODE_TIMER), 0);
        if ((s->flags_ef & 1) && !(s->flags_ef & 0x20) && mem_s8(kTraits[8].global) != 3) {
            mem_w16(blk(AI_HELD), 0x10);
            return;
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    case 1: {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t <= 0) {
            if (mode < ai_mode::DropOffApron && mode >= ai_mode::HeadForRing)
                mem_w16(blk(AI_PENDING), dir8_buttons(angle_dir8_back(vec_xz_angle(s->pos.x, s->pos.z))));
            if (mem_s32(blk(AI_MODE)) == ai_mode::HeadForRingAlternate) {
                mem_w16(blk(AI_HELD), (u16)(mem_u16(blk(AI_PENDING)) | 4));
                mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
                return;
            }
            mem_w32(blk(AI_MODE_TIMER), 32);
        }
        own = mem_s32(blk(AI_SIT_SELF));
        if (own == 1) {
            mem_w16(blk(AI_PRESSED), 8);
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
            if ((s->location & 4) && g_ai_opp->opponent == s->slot) {
                s32 o = mem_s32(blk(AI_SIT_OPP));
                if ((u32)(o - 25) < 2) {
                    ai_set_mode(ai_mode::Think);
                    return;
                }
                if ((f64)mem_f32(blk(AI_DIST_P)) < 170.0) {
                    if (g_ai_opp->state == state::ApronStrikeDownToFloor || ((mem_u16(blk(AI_WHERE)) & 0x101) && (o == 11 || o == 8))) {
                        ai_set_mode(ai_mode::Think);
                        return;
                    }
                }
            }
        } else if (own == 48) {
            if (mem_s32(blk(AI_MODE)) == ai_mode::HeadForRing && (s->flags_e9 & 0x10) && chance_check(350))
                mem_w16(blk(AI_PRESSED), 8);
            else
                mem_w32(blk(AI_STEP), 3);
        } else {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    case 2:
        switch (own) {
        case 2: {
            s32 o = mem_s32(blk(AI_SIT_OPP));
            if (s->state_step == 2 && (f64)mem_f32(blk(AI_DIST_P)) < 170.0 && chance_check(mem_s32(blk(AI_SKILL))) &&
                (o == 8 || o == 11))
                mem_w16(blk(AI_HELD), 0x20);
            else
                mem_w16(blk(AI_HELD), (u16)(mem_u16(blk(AI_PENDING)) | 4));
            return;
        }
        case 4: ai_set_mode(ai_mode::KeepRunning); return;
        case 5: return;
        case 48: case 49:
            mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
            return;
        default: ai_set_mode(ai_mode::Think); return;
        }
    case 3:
        if (own != 48) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if ((f64)mem_f32(blk(AI_DIST_P)) < 160.0 && (mem_s32(blk(AI_SIT_OPP)) == 11 || mem_s32(blk(AI_SIT_OPP)) == 8))
            mem_w32(blk(AI_STEP), chance_check(mem_s32(blk(AI_SKILL))) ? 4 : 5);
        return;
    case 4:
        if (own != 48) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (s->location & 2) {
            mem_w16(blk(AI_HELD), 0x10);
            return;
        }
        if (mem_s32(blk(AI_SIT_OPP)) == 11)
            mem_w16(blk(AI_HELD), 0x20);
        if (mem_s32(blk(AI_SIT_OPP)) == 8)
            mem_w16(blk(AI_HELD), 0x10);
        return;
    case 5:
        if (own != 48)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

namespace {

enum : u32 {
    AI_STICK_PENDING = 0x14,  // s8: the stick y a mode will push (mode 25: 80 for a special)
};

}  // namespace

// N64 0x801009B0: mode 25, the lock-up choice (written from the pseudocode, then checked branch by
// branch against the asm, 2026-11-03). Step 0: a wait of base +- spread frames
// (by size class attrs +0x15 / 3: 0x80145D84 / 0x80145D90; 15 +- 7 against a finisher-mode
// opponent; 10% 15 +- 5). Step 1 (still locked up, 13): when the wait is over (or the weak-opponent
// plan's special, ai_lockup_special):
// - not a strong one-handed grapple (flags78 0x05): 10% L outside the rope / corner kinds (step 3,
//   the side switch); in the corner from the front L by the favourites of moves 178 / 179 / 204 (30%,
//   else 10%) then mode 0; across the ropes C-down (30%, strong 15%) or A; in the corner from behind
//   mode 26, from the front with mode flag 0x10 a 5% C-down (not strong) else 26; from the front
//   with mode flag 0x10: backstage 80% toward an arena target (ai_arena_target), the ropes
//   (ai_lockup_ropes), the corner finisher direction (ai_lockup_finisher: mode 117), 7.5% a free
//   direction, in a cage 50% C-right, 25% a direction move; then (front) strong with attrs +0x34
//   0x02 70% B, else mode 26; from behind the whip check (mode 111), the tag check (112), else 26;
// - a strong one-handed grapple: with the specials not ruled out (attrs +0x3C) 20% L (step 3); else
//   by kind the special (stick pending 80) when its bit is set (corner front 0x20 / rear 0x40, apron
//   defender 0x80, apron attacker 0x100, open: front 0x01 / rear 0x02), A otherwise on the apron,
//   A+B (the finisher input) 10% at an opponent below 10 spirit, attrs +0x34 0x02 70% B, else 26.
// Step 2: pushes the pending stick and presses / holds the pending buttons while locked up (the
// grapple attempt 32 waits). Steps 3 / 4 wait out the side switch (state_step 10 and back).
void ai_mode_25_lockup_choose() {
    Wrestler *s = g_ai_self;
    s32 step = mem_s32(blk(AI_STEP));
    auto next_step = [&]() { mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1)); };
    auto pend = [&](u16 b) {
        mem_w16(blk(AI_PENDING), b);
        next_step();
    };
    auto special = [&]() {
        mem_w8(blk(AI_STICK_PENDING), 80);
        next_step();
    };
    auto press_l = [&]() {
        mem_w16(blk(AI_PRESSED), 0x20);
        mem_w16(blk(AI_HELD), 0x20);
        mem_w32(blk(AI_STEP), 3);
    };
    switch (step) {
    case 0: {
        mem_w8(blk(AI_STICK_PENDING), 0);
        mem_w16(blk(AI_PENDING), 0);
        s32 base = 15, spread;
        if (g_ai_opp->flags_ee & 4) {
            spread = 7;
        } else {
            s32 k = mem_s8(s->attrs + 0x15) / 3;
            base = mem_s16(0x80145D84 + 2u * (u32)k);
            spread = mem_s16(0x80145D90 + 2u * (u32)k);
            if (chance_check(100)) {
                base = 15;
                spread = 5;
            }
        }
        s32 r = rand_pool_next();
        next_step();
        mem_w32(blk(AI_MODE_TIMER), (u32)(base + r % (2 * spread) - spread));
        return;
    }
    case 1: {
        if (mem_s32(blk(AI_SIT_SELF)) != 13) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t >= 0 || (mem_u8(0x8016B718 + (u32)s->slot) && ai_lockup_special()))
            return;
        u32 f = s->flags78;
        u16 sp = mem_u16(s->attrs + 0x3C);
        auto low_spirit_ab = [&]() { return chance_check(mem_s16(blk(AI_OPP_SPIRIT)) < 10 ? 100 : 0) != 0; };
        if ((f & 5) != 5) {
            if (chance_check(100) && !(s->flags78 & 0x700)) {
                press_l();
                return;
            }
            if ((s->flags78 & 0x180) == 0x100) {
                s32 c = ((ai_move_flags(178) | ai_move_flags(179) | ai_move_flags(204)) & 0x8000) ? 300 : 100;
                if (chance_check(c)) {
                    mem_w16(blk(AI_PRESSED), 0x20);
                    mem_w16(blk(AI_HELD), 0x20);
                    ai_set_mode(ai_mode::Think);
                    return;
                }
            }
            f = s->flags78;
            if (f & 0x600) {
                if (chance_check((f & 4) ? 150 : 300))
                    pend(4);
                else
                    pend(0x8000);
                return;
            }
            if (f & 0x100) {
                if (!(f & 0x80) && (mem_u8(blk(AI_FLAGS)) & 0x10) && chance_check((f & 4) ? 0 : 50)) {
                    pend(4);
                    return;
                }
                ai_set_mode(ai_mode::LockupPressMove);
                return;
            }
            if (!(f & 0x80)) {
                if (mem_u8(blk(AI_FLAGS)) & 0x10) {
                    if (mem_s32(A_ARENA) >= 10 && chance_check(800)) {
                        pend((u16)(dir8_buttons(ai_arena_target()) | 4));
                        return;
                    }
                    if (ai_lockup_ropes())
                        return;
                    bool free_try = !(s->flags78 & 4);
                    if (!free_try) {
                        s32 fin = ai_lockup_finisher();
                        if (fin != -1) {
                            mem_w16(blk(AI_PENDING), (u16)(mem_u16(0x8014D228 + 2u * (u32)fin) | 4));
                            ai_set_mode(ai_mode::LockupReact);
                            return;
                        }
                        free_try = !(s->flags78 & 4);
                    }
                    if (free_try && chance_check(75)) {
                        pend((u16)(dir8_buttons(ai_lockup_free_dir()) | 4));
                        return;
                    }
                    if ((g_match_type & 0x10000) && chance_check(500)) {
                        pend(1);
                        return;
                    }
                    if (chance_check(250)) {
                        pend((u16)(mem_u16(0x8014D218 + 2u * (u32)ai_lockup_dir_move()) | 4));
                        return;
                    }
                }
                u32 g = s->flags78;
                if (!(g & 0x80)) {
                    if ((g & 4) && (mem_u32(s->attrs + 0x34) & 2) && chance_check(700))
                        pend(0x4000);
                    else
                        ai_set_mode(ai_mode::LockupPressMove);
                    return;
                }
            }
            if (ai_lockup_whip_check())
                ai_set_mode(ai_mode::LockupWhip);
            else
                ai_set_mode(ai_lockup_tag_check() ? ai_mode::LockupWhipAlternate : ai_mode::LockupPressMove);
            return;
        }
        bool ruled_out = ((!(sp & 1) || !(sp & 2)) && (mem_u32(s->attrs + 0x3C) & 0x30000));
        if (!ruled_out && chance_check(200) && !(s->flags78 & 0x700)) {
            press_l();
            return;
        }
        u32 g = s->flags78;
        if (g & 0x100) {
            if (low_spirit_ab()) {
                pend(0xC000);
                return;
            }
            if (sp & ((s->flags78 & 0x80) ? 0x40 : 0x20))
                special();
            else
                ai_set_mode(ai_mode::LockupPressMove);
            return;
        }
        if (g & 0x200) {
            if (sp & 0x80)
                special();
            else
                pend(0x8000);
            return;
        }
        if (g & 0x400) {
            if (sp & 0x100)
                special();
            else
                pend(0x8000);
            return;
        }
        if (!(mem_u32(s->attrs + 0x34) & 2) || (g & 0x80) || !chance_check(700)) {
            if (low_spirit_ab()) {
                pend(0xC000);
                return;
            }
            if (sp & ((s->flags78 & 0x80) ? 2 : 1))
                special();
            else
                ai_set_mode(ai_mode::LockupPressMove);
            return;
        }
        pend(0x4000);
        return;
    }
    case 2: {
        s32 own = mem_s32(blk(AI_SIT_SELF));
        if (own == 13) {
            mem_w8(blk(AI_STICK_Y), mem_u8(blk(AI_STICK_PENDING)));
            mem_w16(blk(AI_PRESSED), mem_u16(blk(AI_PENDING)));
            mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
        } else if (own != 32) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    case 3:
        if (mem_s32(blk(AI_SIT_SELF)) != 13) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (s->state_step >= 10)
            next_step();
        return;
    case 4:
        if (mem_s32(blk(AI_SIT_SELF)) != 13) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (s->state_step < 10)
            mem_w32(blk(AI_STEP), 0);
        return;
    default:
        return;
    }
}

namespace {

enum : u32 {
    AI_DETOUR = 0x9C,  // s32: mode 39 is walking round a wall
};

}  // namespace

// N64 0x80103AA8: mode 39, heading for the opponent or a weapon on the floor. Step 0 (standing): when
// a wall lies between it and the opponent (nearest_segment_along) a detour: toward the centre along
// x and along z (300 units), keeping the axis with the longer free run, for r & 0x1D frames; else the
// direction to the target (ai_dir8_to_target) for r & 0x16 frames. Step 1 (standing, while the time
// lasts): within 180 (no detour) or the opponent's state changing ends it; on the floor walking
// straight into a wall ends it; in a ring-steps zone (46-48, 51-53, 188-195; not under the DQ rule)
// it heads for the zone's centre and presses C-left (take the steps); the d-pad is held, both on
// the floor toward the opponent's zone side; a detour, or backstage / on a raised place, adds C-up.
void ai_mode_39_fetch_weapon() {
    Wrestler *s = g_ai_self;
    s32 step = mem_s32(blk(AI_STEP));
    if (step == 0) {
        if (mem_s32(blk(AI_SIT_SELF)) != 1) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w16(blk(AI_HELD), 0);
        Vec3 v = {g_ai_opp->pos.x - s->pos.x, 0.0f, g_ai_opp->pos.z - s->pos.z};
        f32 dx = 0.0f, dz = 0.0f;
        s32 seg = 0;
        if (nearest_segment_along(s, &v, &dx, &seg)) {
            mem_w16(blk(AI_PENDING), 0);
            v.z = 0.0f;
            if (s->pos.x < 0.0f) {
                v.x = 300.0f;
                mem_w16(blk(AI_PENDING), 0x100);
            } else {
                v.x = -300.0f;
                mem_w16(blk(AI_PENDING), 0x200);
            }
            if (!nearest_segment_along(s, &v, &dx, &seg))
                dx = 300.0f;
            v.x = 0.0f;
            if (s->pos.z < 0.0f) {
                v.z = 300.0f;
                mem_w16(blk(AI_PENDING), mem_u16(blk(AI_PENDING)) | 0x800);
            } else {
                v.z = -300.0f;
                mem_w16(blk(AI_PENDING), mem_u16(blk(AI_PENDING)) | 0x400);
            }
            if (!nearest_segment_along(s, &v, &dz, &seg))
                dz = 300.0f;
            mem_w16(blk(AI_PENDING), mem_u16(blk(AI_PENDING)) & (dz < dx ? 0x300 : 0xC00));
            mem_w32(blk(AI_MODE_TIMER), (u32)(rand_pool_next() & 0x1D));
            mem_w32(blk(AI_DETOUR), 1);
        } else {
            mem_w16(blk(AI_PENDING), ai_dir8_to_target());
            mem_w32(blk(AI_MODE_TIMER), (u32)(rand_pool_next() & 0x16));
            mem_w32(blk(AI_DETOUR), 0);
        }
        mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1));
        return;
    }
    if (step != 1)
        return;
    s32 t = 0;
    if (mem_s32(blk(AI_SIT_SELF)) == 1) {
        t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
    }
    if (mem_s32(blk(AI_SIT_SELF)) != 1 || t <= 0) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) < 180.0 && !mem_s32(blk(AI_DETOUR)))
        ai_set_mode(ai_mode::Think);
    if (mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state)
        ai_set_mode(ai_mode::Think);
    if ((s->location & 4) && !mem_s32(blk(AI_DETOUR)) && s->dpad_held) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (!(g_match_rules & 0x2000) && s->zone_info) {
        u16 id = mem_u16(s->zone_info + 0x24);
        if ((u32)(id - 46) < 3 || (u32)(id - 51) < 3 || (u32)(id - 188) < 8) {
            Vec3 a = {s->pos.x, 0.0f, s->pos.z};
            Vec3 c = {(f32)(mem_s16(s->zone_info) + mem_s16(s->zone_info + 6)) * 0.5f, 0.0f,
                      (f32)(mem_s16(s->zone_info + 4) + mem_s16(s->zone_info + 10)) * 0.5f};
            mem_w16(blk(AI_PENDING), dir8_buttons(dir8_to(&a, &c)));
            mem_w16(blk(AI_PRESSED), mem_u16(blk(AI_PRESSED)) | 2);
        }
    }
    mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
    if (mem_u16(blk(AI_WHERE)) & 0x100) {
        if (!mem_s32(blk(AI_DETOUR))) {
            s16 oz = mem_s16(blk(AI_ZONE_OPP));
            if (mem_s16(blk(AI_ZONE_SELF)) != oz)
                mem_w16(blk(AI_HELD), mem_u16(0x8014D218 + 2u * (u32)oz));
        }
    }
    if (!mem_s32(blk(AI_DETOUR)))
        return;
    if (mem_s32(A_ARENA) >= 10 || (s->location & 0x40))
        mem_w16(blk(AI_PRESSED), mem_u16(blk(AI_PRESSED)) | 8);
}

namespace {

// N64 0x800FCEFC: ai_set_mode, but mode flag 0x02 instead of 0x01 (ai_try_grapple's second kind).
void ai_set_mode_b(s32 mode) {
    mem_w32(blk(AI_MODE), (u32)mode);
    mem_w32(blk(AI_STEP), 0);
    mem_w32(blk(AI_TIMER), 0);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & 0xFC);
    mem_w16(blk(AI_OPP_STATE), (u16)g_ai_opp->state);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) | 2);
}

// N64 0x800F2968: the CPU tries a running grapple: in the ring (or both outside), unarmed, with a
// running-attack trait (attrs +0x38 0x1800000), by its favourites (any of the four moves at
// 0x801453B4: 80%; of the two at 0x801453BC: 20%; both: 50%; neither: trait +34's 0x801453C0) the run
// with a planned attack (mode 10; kind bit 0x02 sets it with mode flag 0x02), else a plain run (3);
// armed, outside, or without the trait: mode 10.
void ai_try_grapple(s32 kind) {
    if (!(mem_u16(blk(AI_WHERE)) & 0x101) || (g_ai_self->flags_ef & 1) || !(mem_u32(g_ai_self->attrs + 0x38) & 0x1800000)) {
        ai_set_mode(ai_mode::EvadeAndCounter);
        return;
    }
    s32 fav = 0;
    for (s32 i = 0; i < 4; i++)
        if (ai_move_flags(mem_s16(0x801453B4 + 2u * (u32)i)) & 0x8000) {
            fav = 1;
            break;
        }
    for (s32 i = 0; i < 2; i++)
        if (ai_move_flags(mem_s16(0x801453BC + 2u * (u32)i)) & 0x8000) {
            fav += 2;
            break;
        }
    s32 c = (fav == 1) ? 800 : (fav == 2) ? 200 : (fav == 3) ? 500 : mem_s16(0x801453C0 + 2u * (u32)(s32)mem_s8(kTraits[4].global));
    if (!chance_check(c)) {
        ai_set_mode(ai_mode::RunInAndCounter);
        return;
    }
    if (kind & 2)
        ai_set_mode_b(ai_mode::EvadeAndCounter);
    else
        ai_set_mode(ai_mode::EvadeAndCounter);
}

// N64 0x800FD934: the d-pad bits toward the opponent (the CPU's point copies).
u16 ai_dir8_to_target() {
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    return dir8_buttons(dir8_to(&sp, &op));
}

// N64 0x800F28E0: a roll on the opponent's perceived health (clamped 100-250) x 0.004 x 100 x 10:
// 40-100% (more likely against a healthier opponent).
s32 ai_chance_by_opp_health() {
    s32 h = mem_s16(blk(AI_OPP_HEALTH));
    if (h >= 251)
        h = 250;
    if (h < 100)
        h = 100;
    f32 v = (f32)((f64)((f32)h * 0.004f) * 100.0);
    return chance_check((s32)(v * 10.0f)) ? 1 : 0;
}

// N64 0x800FD694: 1 when b faces a (b's facing within 90 degrees of the direction from b toward a:
// |angle_diff(angle a -> b, b's facing)| >= 1024). Renamed 2026-11-03 from ai_angle_off_facing.
s32 ai_faced_by(const Wrestler *a, const Wrestler *b) {
    return abs_i(angle_diff(angle_to(&a->point_120, &b->point_120), b->facing)) >= 1024 ? 1 : 0;
}

// N64 0x800F25D4: the standing engage roll. Chance A = skill x the trait +37 rate (0x801453A0) x 1.2,
// x 1.75 in finisher mode, x 1.5 with SPECIAL, x 1.25 against an opponent on the apron under the
// over-the-top rules, x 1.15 / x 1.3 on the floor with trait +38 2 / 3, x 1.5 when the opponent is not
// facing it; chance B = trait +34's 0x80145394 (+50 against an opponent on the apron); an opponent in
// a hold forces A 100 and B 1000. Passing A then B: a strike (6 if ai_chance_by_opp_health, else 9);
// A only: the grapple modes (1 if the health roll, else 2); failing A: in a meter mode 48 on another A
// roll else 39, otherwise 41 / 32 on skill. Under over-the-top rules with the opponent on the apron
// only A then B (6 / 9) or 1, else nothing.
void ai_decide_engage_chance() {
    u8 ee = g_ai_self->flags_ee;
    s32 a = (s16)(s32)((f64)((f32)mem_s32(blk(AI_SKILL)) * mem_f32(0x801453A0 + 4u * (u32)(s32)mem_s8(kTraits[7].global))) * 1.2);
    if (ee & 4)
        a = (s32)((f64)a * 1.75);
    if (ee & 2)
        a = (s32)((f64)a * 1.5);
    if ((mem_u16(blk(AI_WHERE)) & 2) && (mem_u8(blk(AI_FLAGS)) & 0x40))
        a = (s32)((f64)a * 1.25);
    if (g_ai_self->location & 4) {
        if (mem_s8(kTraits[8].global) == 2)
            a = (s32)((f64)a * 1.15);
        if (mem_s8(kTraits[8].global) == 3)
            a = (s32)((f64)a * 1.3);
    }
    if (!ai_faced_by(g_ai_self, g_ai_opp))
        a = (s32)((f64)a * 1.5);
    s32 b = mem_s16(0x80145394 + 2u * (u32)(s32)mem_s8(kTraits[4].global));
    if (mem_u16(blk(AI_WHERE)) & 2)
        b += 50;
    if (g_ai_opp->flags80 & 0xC0) {
        a = 100;
        b = 1000;
    }
    bool apron_rule = (mem_u8(blk(AI_FLAGS)) & 0x40) && (mem_u16(blk(AI_WHERE)) & 2);
    if (!apron_rule) {
        s32 m;
        if (chance_check(a)) {
            if (chance_check(b))
                m = ai_chance_by_opp_health() ? ai_mode::Strike : ai_mode::StrikeThenFollowUp;
            else
                m = ai_chance_by_opp_health() ? ai_mode::EngageNearbyOpponent : ai_mode::GrappleApproach;
        } else if (g_ai_self->flags_ee & 6) {
            m = chance_check(a) ? ai_mode::WalkToDownedOpponent : ai_mode::FetchWeapon;
        } else {
            m = chance_check(mem_s32(blk(AI_SKILL))) ? ai_mode::WalkTowardOpponent : ai_mode::WaitAndWatch;
        }
        ai_set_mode(m);
        return;
    }
    if (chance_check(a))
        ai_set_mode(chance_check(b) ? (ai_chance_by_opp_health() ? ai_mode::Strike : ai_mode::StrikeThenFollowUp) : ai_mode::EngageNearbyOpponent);
}

}  // namespace

namespace {

// The AI's trait tables (offsets from g_ai_mode_tables, 0x80145298) by a trait level.
s32 ai_trait_s16(u32 off, s32 trait) { return mem_s16(0x80145298 + off + 2u * (u32)(s32)mem_s8(kTraits[trait].global)); }
f32 ai_trait_f32(u32 off, s32 trait) { return mem_f32(0x80145298 + off + 4u * (u32)(s32)mem_s8(kTraits[trait].global)); }

// The "losing CPU" condition of several deciders: levels 5-6 against a flags_e9 0x08 opponent 41+
// healthier (perceived).
bool ai_losing_boost() {
    s32 lvl = mem_s32(blk(AI_LEVEL));
    return lvl < 7 && lvl >= 5 && (g_ai_opp->flags_e9 & 8) && mem_s16(blk(AI_OPP_HEALTH)) - mem_s16(blk(AI_HEALTH)) >= 41;
}

// N64 0x800F6D30: close quarters when the opponent is working on the CPU (its opponent is the
// CPU). A running opponent (flags78 0x40) within 170 on a skill roll: by the trait +36 x +37 chance
// (tables +944 / +924) the counter mode 42, else 21. Otherwise by the perceived distance: beyond 200
// 30% ai_try_grapple(2), else the list +744; 175-200 the trait chance picks list +820, else +776;
// within 175 the losing CPU list +904, else a roll on (skill + own spirit) x 1.2 x the trait +37 rate x
// the trait +36 value: list +884, else +864.
void ai_decide_close() {
    if (g_ai_opp->opponent != g_ai_self->slot)
        return;
    s32 tc = (s32)((f32)ai_trait_s16(944, 6) * ai_trait_f32(924, 7));
    if ((g_ai_opp->flags78 & 0x40) && chance_check(mem_s32(blk(AI_SKILL))) && (f64)mem_f32(blk(AI_DIST_P)) < 170.0) {
        ai_set_mode(chance_check(tc) ? ai_mode::GrappleEvade : ai_mode::PressLButton);
        return;
    }
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    u32 list;
    if (d > 200.0) {
        if (chance_check(300)) {
            ai_try_grapple(2);
            return;
        }
        list = 744;
    } else if (d > 175.0) {
        list = chance_check(tc) ? 820 : 776;
    } else if (ai_losing_boost()) {
        list = 904;
    } else {
        s32 c = (s32)((f64)(mem_s32(blk(AI_SKILL)) + mem_s16(blk(AI_SPIRIT))) * 1.2 * (f64)ai_trait_f32(924, 7)) *
                ai_trait_s16(944, 6);
        list = chance_check(c) ? 884 : 864;
    }
    ai_pick_mode_weighted(0x80145298 + list);
}

// N64 0x800F6FA4: when the opponent works on the CPU. Against a running shoulder block (state 0x07)
// the trait +35 chance (+1028; x 1.3 below 100 health, else x 1.15): block (47), else 46. Beyond 200:
// the trait +35 x +37 chance (+1036 / +1008) the list +976, else 20% ai_try_grapple(2), else +952.
// Closer: 101+ healthier (not level 6) a 60% wait (32); then a roll on (skill + own spirit) x 1.2 x the
// trait +37 rate (x 1.2 against 35 / 38, x 1.35 against 39; 1000 for a losing CPU): the trait +35 chance
// (0x8014569C against an armed opponent, else +1036) to block (47), else 46.
void ai_decide_opponent_focused() {
    const Wrestler *o = g_ai_opp;
    if (o->opponent != g_ai_self->slot)
        return;
    if (o->state == state::RunningShoulderBlock) {
        f64 m = (mem_s16(blk(AI_OPP_HEALTH)) < 100) ? 1.3 : 1.15;
        ai_set_mode(chance_check((s32)((f64)ai_trait_s16(1028, 5) * m)) ? ai_mode::BlockDive : ai_mode::HoldBlock);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 200.0) {
        if (chance_check((s32)((f32)ai_trait_s16(1036, 5) * ai_trait_f32(1008, 7))))
            ai_pick_mode_weighted(0x80145298 + 976);
        else if (chance_check(200))
            ai_try_grapple(2);
        else
            ai_pick_mode_weighted(0x80145298 + 952);
        return;
    }
    if (mem_s32(blk(AI_LEVEL)) != 6 && mem_s16(blk(AI_HEALTH)) - mem_s16(blk(AI_OPP_HEALTH)) >= 101 && chance_check(600)) {
        ai_set_mode(ai_mode::WaitAndWatch);
        return;
    }
    s32 c = (s32)((f64)(mem_s32(blk(AI_SKILL)) + mem_s16(blk(AI_SPIRIT))) * 1.2 * (f64)ai_trait_f32(1008, 7));
    if (o->state == state::WeaponSwingLight || o->state == state::StrongStrike)
        c = (s32)((f64)c * 1.2);
    else if (o->state == state::WeaponSwingHard)
        c = (s32)((f64)c * 1.35);
    if (ai_losing_boost())
        c = 1000;
    if (!chance_check(c))
        return;
    s32 b = (o->flags_ef & 1) ? mem_s16(0x8014569C + 2u * (u32)(s32)mem_s8(kTraits[5].global)) : ai_trait_s16(1036, 5);
    ai_set_mode(chance_check(b) ? ai_mode::BlockDive : ai_mode::HoldBlock);
}

// N64 0x800F7318: with SPECIAL the taunt (50); beyond 170 a skill roll to try a grapple, else walk
// up (48); 160-170 walk up; closer the trait +34 chance (x 10, +1068) picks list +1044, else +1056.
void ai_decide_far() {
    if (g_ai_self->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if (d > 170.0) {
        if (chance_check(mem_s32(blk(AI_SKILL))))
            ai_try_grapple(1);
        else
            ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    if (d >= 160.0) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    ai_pick_mode_weighted(0x80145298 + (chance_check(10 * ai_trait_s16(1068, 4)) ? 1044u : 1056u));
}

}  // namespace

namespace {

// N64 0x800F7400: ai_decide_far with a skill roll first (failing it: walk up, 48): with SPECIAL the
// taunt (50); beyond 170 a skill roll to try a grapple, else 48; 160-170 48; closer the trait +34
// chance (x 10, +1104) picks list +1080, else +1092.
void ai_decide_far_b() {
    if (g_ai_self->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    if (!chance_check(mem_s32(blk(AI_SKILL)))) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if (d > 170.0) {
        if (chance_check(mem_s32(blk(AI_SKILL))))
            ai_try_grapple(1);
        else
            ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    if (d > 160.0) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    ai_pick_mode_weighted(0x80145298 + (chance_check(10 * ai_trait_s16(1104, 4)) ? 1080u : 1092u));
}

// N64 0x800F7508: with SPECIAL the taunt (50); else a skill roll for list +1136 within 160, +1116
// beyond (nothing when it fails).
void ai_decide_close_b() {
    if (g_ai_self->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    u32 list = ((f64)mem_f32(blk(AI_DIST_P)) <= 160.0) ? 1136u : 1116u;
    if (chance_check(mem_s32(blk(AI_SKILL))))
        ai_pick_mode_weighted(0x80145298 + list);
}

// N64 0x800F7DB0: the far decision (1 when it chose, only beyond 200). Beyond 250 the trait +39
// chance (0x801457B0) to wait / taunt (49). An opponent down (15, 16, 19), or the CPU in a meter mode:
// the size-class chance (0x801457A8 by attrs +0x15 / 3) to run at it (40), else walk up (48). In a tag
// match after the first minute, both legal and in the ring, the opponent's partner on the apron and
// the opponent in its own corner's quarter: a skill roll and the size chance + 450: 75%
// ai_try_grapple(1), else run (40). Then ai_decide_vs_down_b; against a finisher-mode opponent walk
// up (41); beyond 250 in the ring the size chance / 5 to run (40); else the list 0x80145794.
s32 ai_decide_vs_down() {
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if (!(d > 200.0))
        return 0;
    if (d > 250.0 && chance_check(mem_s16(0x801457B0 + 2u * (u32)(s32)mem_s8(kTraits[9].global)))) {
        ai_set_mode(ai_mode::ApronWait);
        return 1;
    }
    s32 size = mem_s16(0x801457A8 + 2u * (u32)(mem_s8(g_ai_self->attrs + 0x15) / 3));
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if ((u32)(o - 15) < 2 || o == 19 || (g_ai_self->flags_ee & 6)) {
        ai_set_mode(chance_check(size) ? ai_mode::Run : ai_mode::WalkToDownedOpponent);
        return 1;
    }
    const Wrestler *opp = g_ai_opp;
    const Wrestler *opp_partner = &g_wrestlers[opp->partner];
    if (g_match_clock[0] > 0 && (g_match_type & 0x2000) && !(g_match_rules & 0x40) && (mem_u16(blk(AI_WHERE)) & 1) &&
        (opp_partner->location & 2) && (g_ai_self->flags_e9 & 0x10) && (opp->flags_e9 & 0x10) &&
        mem_s16(blk(AI_QUAD_OPP)) == ((opp->slot & 2) ? 0 : 2) && chance_check(mem_s32(blk(AI_SKILL))) &&
        chance_check(size + 450)) {
        if (chance_check(750))
            ai_try_grapple(1);
        else
            ai_set_mode(ai_mode::Run);
        return 1;
    }
    if (ai_decide_vs_down_b())
        return 1;
    if (opp->flags_ee & 4) {
        ai_set_mode(ai_mode::WalkTowardOpponent);
        return 1;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 250.0 && (mem_u16(blk(AI_WHERE)) & 1) && chance_check((s16)(size / 5))) {
        ai_set_mode(ai_mode::Run);
        return 1;
    }
    ai_pick_mode_weighted(0x80145794);
    return 1;
}

}  // namespace

namespace {


// N64 0x800F87A8: a move on a downed opponent (1 when it chose): a skill roll; then by the opponent's
// health (perceived; under 100 the traits +31 / +26, else +30 / +25, for down 15 / dazed or lying 16 /
// 19) a roll of 50 per mille (500 with that trait at 2); then in finisher mode with
// the ground special (attrs +0x3C 0x400 down, 0x200 dazed / lying) the partner's help
// (ai_try_corner_climb), else a move from the lists (ai_list_pick_c / _d).
s32 ai_decide_vs_down_b() {
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return 0;
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if (o != 15 && o != 16 && o != 19)
        return 0;
    s32 trait;
    if (mem_s16(blk(AI_OPP_HEALTH)) < 100)
        trait = (o == 15) ? 3 : 1;  // +31 / +26
    else
        trait = (o == 15) ? 2 : 0;  // +30 / +25
    if (!chance_check(mem_s8(kTraits[trait].global) == 2 ? 500 : 50))
        return 0;
    u16 sp = mem_u16(g_ai_self->attrs + 0x3C);
    if (o == 15) {
        if ((g_ai_self->flags_ee & 4) && (sp & 0x400) && ai_try_corner_climb())
            return 1;
        return ai_list_pick_c(g_ai_self->slot) ? 1 : 0;
    }
    if ((g_ai_self->flags_ee & 4) && (sp & 0x200) && ai_try_corner_climb())
        return 1;
    return ai_list_pick_d(g_ai_self->slot) ? 1 : 0;
}

// N64 0x800F89D0: against a downed opponent (15). With the finish plan's flag (flags8c 0x04, outside
// type 0x100000) ai_finisher_check first. Not in finisher mode: an opponent staying down (perceived
// time down 61+) or in the TKO check (0x2E): the trait +39 chance (0x80145814; halved with the
// weak-opponent plan) to taunt (49; in team play with both in the ring 5% mode 113). Against the TKO
// check (not in finisher mode, outside type 0x100000) the trait +37 chance (0x80145808) for mode 55.
// Then in the ring with the plan ai_decide_finisher; with SPECIAL the taunt (50); else the move list
// (ai_decide_move_list).
void ai_decide_special() {
    Wrestler *s = g_ai_self;
    if ((s->flags8c & 4) && !(g_match_type & 0x100000) && ai_finisher_check())
        return;
    bool tko_roll = (s->flags_ee & 4) != 0;
    if (!tko_roll && (mem_s16(blk(AI_OPP_DOWN)) >= 61 || g_ai_opp->state == state::TechnicalKnockoutStagger)) {
        s32 c = mem_s16(0x80145814 + 2u * (u32)(s32)mem_s8(kTraits[9].global));
        if (mem_u8(0x8016B718 + (u32)s->slot))
            c = (s16)c / 2;
        if (chance_check(c)) {
            if ((g_match_type & 0x1000) && (mem_u16(blk(AI_WHERE)) & 1) && chance_check(50))
                ai_set_mode(ai_mode::SpecialMove);
            else
                ai_set_mode(ai_mode::ApronWait);
            return;
        }
        tko_roll = true;
    }
    if (tko_roll && g_ai_opp->state == state::TechnicalKnockoutStagger && !(g_match_type & 0x100000) && !(s->flags_ee & 4) &&
        chance_check(mem_s16(0x80145808 + 2u * (u32)(s32)mem_s8(kTraits[7].global)))) {
        ai_set_mode(ai_mode::LongWait);
        return;
    }
    if ((mem_u16(blk(AI_WHERE)) & 1) && mem_u8(0x8016B718 + (u32)s->slot) && ai_decide_finisher())
        return;
    if (s->flags_ee & 2)
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
    else
        ai_decide_move_list();
}

}  // namespace

namespace {


// N64 0x800F9E40: against a lying or dazed opponent (16, 19, 22, 23). In the ring with the finish
// plan ai_decide_vs_lying_c first. With SPECIAL the taunt (50). Not in finisher mode with the
// opponent staying down (perceived 81+ frames) the trait +39 chance (0x8014585C) to taunt (49; in team
// play in the ring 5% mode 113). Otherwise lying (19) more than 130 away walk up (48), else
// ai_decide_vs_lying_b.
void ai_decide_vs_lying() {
    Wrestler *s = g_ai_self;
    if ((mem_u16(blk(AI_WHERE)) & 1) && mem_u8(0x8016B718 + (u32)s->slot) && ai_decide_vs_lying_c())
        return;
    if (s->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    if (!(s->flags_ee & 4) && mem_s16(blk(AI_OPP_DOWN)) >= 81 &&
        chance_check(mem_s16(0x8014585C + 2u * (u32)(s32)mem_s8(kTraits[9].global)))) {
        if ((g_match_type & 0x1000) && (mem_u16(blk(AI_WHERE)) & 1) && chance_check(50))
            ai_set_mode(ai_mode::SpecialMove);
        else
            ai_set_mode(ai_mode::ApronWait);
        return;
    }
    if (mem_s32(blk(AI_SIT_OPP)) == 19 && (f64)mem_f32(blk(AI_DIST)) > 130.0)
        ai_set_mode(ai_mode::WalkToDownedOpponent);
    else
        ai_decide_vs_lying_b();
}

// N64 0x800F8F48: a move on a downed opponent from the CPU's lists, after a skill roll. Which list:
// 40% the trait +30 (health 100+) or +31 (below) level, else a random 0-3; on the floor list 2 is
// re-rolled to 0, 1 or 3. In finisher mode against an opponent below 10 spirit with list 1 and under
// 90 frames of the mode left: 10% mode 23, 30% list b; with 150+ frames left or no finisher
// (ai_finisher_check): with 301+ frames, list 2 and the ground special (attrs +0x3C 0x400) a 20% roll
// (45% with move 263 a favourite) for mode 65; a pick-up (ai_try_pick_up); then (and outside finisher
// mode) unless ai_finisher_check: list 0-3 = ai_list_pick_a / _b / _c / ai_try_pick_up.
void ai_decide_move_list() {
    Wrestler *s = g_ai_self;
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return;
    s32 v;
    if (mem_s16(blk(AI_OPP_HEALTH)) >= 100)
        v = chance_check(400) ? mem_s8(kTraits[2].global) : rand_pool_next() % 4;
    if (mem_s16(blk(AI_OPP_HEALTH)) < 100)
        v = chance_check(400) ? mem_s8(kTraits[3].global) : rand_pool_next() % 4;
    if ((s->location & 4) && v == 2) {
        s32 r = rand_pool_next() % 3;
        v = (r == 1) ? 1 : (r == 2) ? 3 : 0;
    }
    bool lists = true;
    if (s->flags_ee & 4) {
        bool skip = mem_s16(blk(AI_OPP_SPIRIT)) >= 10 || v != 1 || s->spirit_timer >= 90;
        if (!skip) {
            if (chance_check(100)) {
                ai_set_mode(ai_mode::GrappleUnlessPickUpMove);
                return;
            }
            if (chance_check(300) && ai_list_pick_b(s->slot))
                return;
        }
        if (s->spirit_timer < 150 && ai_finisher_check())
            return;
        if (s->spirit_timer >= 301 && v == 2 && (mem_u16(s->attrs + 0x3C) & 0x400) &&
            chance_check((ai_move_flags(263) & 0x8000) ? 450 : 200)) {
            ai_set_mode(ai_mode::MeetRunner);
            return;
        }
        if (ai_try_pick_up())
            return;
    }
    if (!lists || ai_finisher_check())
        return;
    switch (v) {
    case 0: ai_list_pick_a(s->slot); break;
    case 1: ai_list_pick_b(s->slot); break;
    case 2: ai_list_pick_c(s->slot); break;
    case 3: ai_try_pick_up(); break;
    default: break;
    }
}

}  // namespace

namespace {


// N64 0x800F4B30: the tag deciders in order (1 when one acted): ai_tag_partner_check,
// ai_tag_help_check, ai_tag_reach_check then ai_tag_reach, and in type 0x8000 ai_tag_extra.
s32 ai_decide_tag() {
    if (ai_tag_partner_check() || ai_tag_help_check())
        return 1;
    if (ai_tag_reach_check() && ai_tag_reach())
        return 1;
    if (!(g_match_type & 0x8000))
        return 0;
    return ai_tag_extra() ? 1 : 0;
}

// N64 0x800FB4C0: whether the legal man w (the CPU, in the ring, tag rules) wants to tag out, with
// its partner ready on the apron (idle_flags 0x18): a 30% roll when it has lost 51+ health since it
// came in (tag_health), or after 2,700 frames (90 seconds) in with the partner on the apron; only
// below 90 perceived spirit, not (50%) while the opponent is down, and with the partner in its team's
// own quarter of the ring (ring_quadrant: 2 / 3 for slots 0 / 1, 0 / 1 for slots 2 / 3).
s32 ai_wants_tag_out(const Wrestler *w) {
    const Wrestler *p = &g_wrestlers[g_ai_self->partner];
    if (!(w->location & 1) || !(w->flags_e9 & 0x10) || !(g_match_type & 0x2000) || (g_match_rules & 0x40))
        return 0;
    if (!(p->idle_flags & 0x18))
        return 0;
    s32 want = 0;
    if (abs_i(g_ai_self->tag_health - w->health) >= 51)
        want = chance_check(300) ? 1 : 0;
    else if (g_ai_self->tag_state >= 2701 && p->roster_id != -1 && (p->location & 2) && chance_check(300))
        want = 1;
    if (mem_s16(blk(AI_SPIRIT)) >= 90)
        return 0;
    s32 o = mem_s32(blk(AI_SIT_OPP));
    if (o >= 15 && (o < 17 || o == 19) && chance_check(500))
        return 0;
    s32 q = ring_quadrant(p);
    bool own_quarter = (p->slot & 2) ? !(q & 2) : (q & 2) != 0;
    return own_quarter ? want : 0;
}

// N64 0x800F2B04: against an armed opponent running (2) at the CPU (its facing within 170 of the
// direction to the CPU): a skill roll for mode 95, else the list 0x801453CC; 1 when it acted.
s32 ai_vs_runner_armed() {
    if (!(g_ai_opp->flags_ef & 1) || mem_s32(blk(AI_SIT_OPP)) != 2)
        return 0;
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    if (abs_i(angle_diff(angle_to(&op, &sp), g_ai_opp->facing)) >= 170)
        return 0;
    if (chance_check(mem_s32(blk(AI_SKILL))))
        ai_set_mode(ai_mode::ReplyToRunner);
    else
        ai_pick_mode_weighted(0x801453CC);
    return 1;
}

// N64 0x800F7AAC: against an opponent on the ropes (38), 1 when it acted: in the ring, outside the
// over-the-top rules, with the rope-move trait (attrs +0x38 0x400000) 50% mode 67; else, the opponent
// within the list block's reach (+0x58) + 50 of the corner of its quarter, mode 66 with the corner
// trait for the opponent's place (0x100000 in the ring, 0x200000 on the floor).
s32 ai_decide_ropes() {
    u32 tr = mem_u32(g_ai_self->attrs + 0x38);
    if ((mem_u16(blk(AI_WHERE)) & 1) && !(g_match_rules & 0x400) && (tr & 0x400000) && chance_check(500)) {
        ai_set_mode(ai_mode::RunAtCarriedOpponent);
        return 1;
    }
    u32 c = A_RING_CORNERS + 12u * (u32)mem_s16(blk(AI_QUAD_OPP));
    Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
    Vec3 op = ai_opp_point();
    if (!((f64)dist_to(&op, &corner) < (f64)mem_f32(mem_u32(blk(AI_LISTS)) + 0x58) + 50.0))
        return 0;
    tr = mem_u32(g_ai_self->attrs + 0x38);
    if (((g_ai_opp->location & 1) && (tr & 0x100000)) || ((g_ai_opp->location & 4) && (tr & 0x200000))) {
        ai_set_mode(ai_mode::MeetRunnerAlternate);
        return 1;
    }
    return 0;
}

}  // namespace

namespace {

// N64 0x800F7BFC: against a diving opponent (25 / 26) working on the CPU: a failed skill roll waits
// (55). More than 380 away the trait +39 chance (0x80145784) to taunt (49), else 55. Closer: on the
// apron 56; a following wrestler (flags_e9 0x01) a skill roll to back off (45), else 86; against a
// perched opponent (0x0A) in the ring: within 200 of the corner it perches on, 57; at its step 3, 58;
// else (and for a perched opponent not in the air, act_flags 0x0B) the trait +35 chance (0x8014578C)
// to block (47), else 86.
void ai_decide_wait_or_engage() {
    const Wrestler *o = g_ai_opp;
    Wrestler *s = g_ai_self;
    if (o->opponent != s->slot)
        return;
    s32 m = ai_mode::LongWait;
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        if ((f64)mem_f32(blk(AI_DIST_P)) > 380.0) {
            m = chance_check(mem_s16(0x80145784 + 2u * (u32)(s32)mem_s8(kTraits[9].global))) ? ai_mode::ApronWait : ai_mode::LongWait;
        } else {
            bool in_air = true;
            if (o->state == state::TurnbucklePerch)
                in_air = (o->act_flags & 0xB) != 0;
            if (s->location & 2) {
                m = ai_mode::DodgeDive;
            } else if (s->flags_e9 & 1) {
                m = chance_check(mem_s32(blk(AI_SKILL))) ? ai_mode::BackAway : ai_mode::CircleThenBlock;
            } else {
                bool done = false;
                if (o->state == state::TurnbucklePerch && (s->location & 1)) {
                    if ((f64)mem_f32(blk(AI_CORNERS) + 4u * (u32)o->dir) < 200.0) {
                        m = ai_mode::WalkOutOfCorner;
                        done = true;
                    } else if (o->state_step == 3) {
                        m = ai_mode::WalkOutOfCornerAlternate;
                        done = true;
                    }
                }
                if (!done) {
                    m = ai_mode::CircleThenBlock;
                    if (in_air && chance_check(mem_s16(0x8014578C + 2u * (u32)(s32)mem_s8(kTraits[5].global))))
                        m = ai_mode::BlockDive;
                }
            }
        }
    }
    ai_set_mode(m);
}

// N64 0x800F75A8: against an opponent in the corner (20) or rebounding (30). With SPECIAL at a
// cornered opponent the taunt (50). With the weak-opponent plan, by the plan's target bits (AI block
// +0x84): 0x2000 (a corner move; not at an opponent hung upside down, frame_flags 0x08; with 0x08 only
// in finisher mode): 0x400 mode 51; 0x100: beyond 170 run (40), beyond 130 walk up (48), else the
// grapple (1 or, with 0x4000, 2) when the opponent lies the way 0x10 wants (face down with it, face
// up without), else the special 94; without 0x2000 (and 0x08 only in finisher mode): 40 / 48 by
// distance, else 20. Otherwise: armed with trait +38 == 3: beyond 170 a skill roll for list 0x80145734
// else 39, within 160 list 0x8014571C, between 39; against a rebound (30) the trait +37 chance
// (0x80145764) for 39, else list 0x80145734; not in finisher mode (or at a hanging opponent): a
// staying-down opponent (91+) below 40 health with the trait +39 chance (0x8014577C) taunt (49);
// beyond 170 a skill roll picks list 0x80145734 or 0x80145740; beyond 130 48; at a hanging opponent
// the trait +37 time (0x80145758) at least its time down picks list 0x8014571C, else 9; otherwise by
// the same time and the trait +34 chance (0x80145770) list 0x8014571C / 0x80145728, or 9 / 2. In
// finisher mode: 40 / 48 by distance, else with a corner special (attrs +0x3C 0x600000) 2, else 30%
// 20 (otherwise 2).
void ai_decide_in_ring_main() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if (mem_s32(blk(AI_SIT_OPP)) == 20 && (s->flags_ee & 2)) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    f64 d = (f64)mem_f32(blk(AI_DIST));
    if (mem_u8(0x8016B718 + (u32)s->slot)) {
        u16 t = mem_u16(blk(0x84));
        if (t & 0x2000) {
            if (!(o->frame_flags & 8) && (!(t & 8) || (s->flags_ee & 4))) {
                if (t & 0x400) {
                    ai_set_mode(ai_mode::RunThenMove);
                    return;
                }
                if (mem_u16(blk(0x84)) & 0x100) {
                    d = (f64)mem_f32(blk(AI_DIST));
                    if (d > 170.0) {
                        ai_set_mode(ai_mode::Run);
                        return;
                    }
                    if (d > 130.0) {
                        ai_set_mode(ai_mode::WalkToDownedOpponent);
                        return;
                    }
                    bool face_down = (o->flags8a & 1) != 0;
                    if (((t & 0x10) != 0) == face_down) {
                        ai_set_mode((t & 0x4000) ? ai_mode::GrappleApproach : ai_mode::EngageNearbyOpponent);
                        return;
                    }
                    ai_set_mode(ai_mode::CornerSetUp);
                    return;
                }
            }
        } else if (!(t & 8) || (s->flags_ee & 4)) {
            d = (f64)mem_f32(blk(AI_DIST));
            ai_set_mode(d > 170.0 ? ai_mode::Run : d > 130.0 ? ai_mode::WalkToDownedOpponent : ai_mode::PickUpDownedOpponent);
            return;
        }
    }
    if ((s->flags_ef & 1) && mem_s8(kTraits[8].global) == 3) {
        d = (f64)mem_f32(blk(AI_DIST));
        if (d > 170.0) {
            if (chance_check(mem_s32(blk(AI_SKILL))))
                ai_pick_mode_weighted(0x80145734);
            else
                ai_set_mode(ai_mode::FetchWeapon);
        } else if (d < 160.0) {
            ai_pick_mode_weighted(0x8014571C);
        } else {
            ai_set_mode(ai_mode::FetchWeapon);
        }
        return;
    }
    if (mem_s32(blk(AI_SIT_OPP)) == 30) {
        if (chance_check(mem_s16(0x80145764 + 2u * (u32)(s32)mem_s8(kTraits[7].global))))
            ai_set_mode(ai_mode::FetchWeapon);
        else
            ai_pick_mode_weighted(0x80145734);
        return;
    }
    if (!(s->flags_ee & 4) || (o->frame_flags & 8)) {
        if (mem_s16(blk(AI_OPP_DOWN)) >= 91 && mem_s16(blk(AI_OPP_HEALTH)) < 40 &&
            chance_check(mem_s16(0x8014577C + 2u * (u32)(s32)mem_s8(kTraits[9].global)))) {
            ai_set_mode(ai_mode::ApronWait);
            return;
        }
        d = (f64)mem_f32(blk(AI_DIST));
        if (d > 170.0) {
            ai_pick_mode_weighted(chance_check(mem_s32(blk(AI_SKILL))) ? 0x80145734 : 0x80145740);
            return;
        }
        if (d > 130.0) {
            ai_set_mode(ai_mode::WalkToDownedOpponent);
            return;
        }
        s32 hold = mem_s16(0x80145758 + 2u * (u32)(s32)mem_s8(kTraits[7].global));
        s32 down = mem_s16(blk(AI_OPP_DOWN));
        if (o->frame_flags & 8) {
            if (hold >= down)
                ai_pick_mode_weighted(0x8014571C);
            else
                ai_set_mode(ai_mode::StrikeThenFollowUp);
            return;
        }
        s32 c = mem_s16(0x80145770 + 2u * (u32)(s32)mem_s8(kTraits[4].global));
        if (hold >= down) {
            ai_pick_mode_weighted(chance_check(c) ? 0x8014571C : 0x80145728);
            return;
        }
        ai_set_mode(chance_check(c) ? ai_mode::StrikeThenFollowUp : ai_mode::GrappleApproach);
        return;
    }
    d = (f64)mem_f32(blk(AI_DIST));
    if (d > 170.0) {
        ai_set_mode(ai_mode::Run);
        return;
    }
    if (d > 130.0) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    if ((mem_u32(s->attrs + 0x3C) & 0x600000) || !chance_check(300))
        ai_set_mode(ai_mode::GrappleApproach);
    else
        ai_set_mode(ai_mode::PickUpDownedOpponent);
}

}  // namespace

namespace {

// N64 0x80138B14: whether the wrestler in a slot may walk along the apron (1) in a tag match with
// tag rules: not the legal man, with a partner; while its partner's hold flags (partner_flags80
// 0xC0) are set only without 0x400, else when it may act (wrestler_may_act) or may stay inside.
s32 apron_walk_dpad(s32 slot) {
    const Wrestler *w = &g_wrestlers[slot];
    if (!(g_match_type & 0x2000) || (g_match_rules & 0x40) || (w->flags_e9 & 0x10) || w->partner == w->slot)
        return 0;
    if (w->partner_flags80 & 0xC0)
        return (w->partner_flags80 & 0x400) ? 0 : 1;
    if (wrestler_may_act(w))
        return 1;
    return may_stay_inside(w) ? 1 : 0;
}

// N64 0x800F64AC: 1 when the trait picked by the opponent's situation and perceived health is 2:
// against a downed opponent (15) +31 below 100 health, +30 otherwise; else +26 / +25.
s32 ai_trait_flag2() {
    s32 t;
    if (mem_s32(blk(AI_SIT_OPP)) == 15)
        t = mem_s16(blk(AI_OPP_HEALTH)) < 100 ? 3 : 2;
    else
        t = mem_s16(blk(AI_OPP_HEALTH)) < 100 ? 1 : 0;
    return mem_s8(kTraits[t].global) == 2 ? 1 : 0;
}

// N64 0x800FBEB4: 1 when the CPU (not free to walk the apron by apron_walk_dpad) stands in the
// wrong team's half of the ring sides (ring_zone 0-1 for slots 2 / 3, 2-3 for slots 0 / 1) away
// from its own corner (perceived quadrant +0x40 not 0 / not 2): it should walk back along the apron
// (mode 60).
s32 ai_apron_walk() {
    const Wrestler *s = g_ai_self;
    if (apron_walk_dpad(s->slot))
        return 0;
    s32 z = s->ring_zone;
    if (z < 0)
        return 0;
    if (z < 2)
        return (s->slot & 2) ? (mem_s16(blk(AI_QUAD_SELF)) != 0 ? 1 : 0) : 0;
    if (z < 4)
        return (s->slot & 2) ? 0 : (mem_s16(blk(AI_QUAD_SELF)) != 2 ? 1 : 0);
    return 0;
}

// N64 0x800F6524 (renamed from ai_go_to_partner): mode 65 (the C-down corner mode) when the
// opponent is within the list block's reach (+0x58) + 50 of the corner of its own quarter and has
// stayed down 61+ frames (else a 10% roll); 1 when it did. Not from the floor (mode 0), not while
// the CPU is legal with the opponent on the floor late in a count-out (under 4 counts left), not with
// the opponent's point beyond z 530, and in tag matches not while the partner is in states 66, 68, 70,
// 72 or 74.
s32 ai_try_corner_climb() {
    const Wrestler *s = g_ai_self;
    if (s->location & 4) {
        ai_set_mode(ai_mode::Think);
        return 0;
    }
    if ((s->flags_e9 & 0x10) && (g_ai_opp->location & 4) && (mem_u8(A_COUNTOUT) & 1) &&
        mem_s32(A_COUNTOUT_LEFT) < 4)
        return 0;
    if ((f64)mem_f32(A_AI_OPP_POINT + 8) > 530.0)
        return 0;
    if (g_match_type & 0x2000) {
        s32 ps = g_wrestlers[s->partner].state;
        if (ps >= 66 && ps <= 74 && !(ps & 1))
            return 0;
    }
    u32 c = A_RING_CORNERS + 12u * (u32)mem_s16(blk(AI_QUAD_OPP));
    Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
    Vec3 op = ai_opp_point();
    if (!((f64)dist_to(&op, &corner) < (f64)mem_f32(mem_u32(blk(AI_LISTS)) + 0x58) + 50.0))
        return 0;
    if (mem_s16(blk(AI_OPP_DOWN)) < 61 && !chance_check(100))
        return 0;
    ai_set_mode(ai_mode::MeetRunner);
    return 1;
}

// N64 0x800F3D70: the CPU on the apron with the walk flag (AI flags 0x20). Free to walk the apron
// (apron_walk_dpad): by the opponent's place (its partner's while the opponent is legal): in the
// ring 33 (enter) from the apron; on the floor 35 in the same zone, else 33. Otherwise a skill roll
// first looks for another wrestler in state 3 within 160 whose facing is within 284 of the
// direction: 80% (40% for the partner) mode 7. Then the apron walk (60); against an opponent down,
// dazed or lying (15, 16, 19) or beyond 250 the trait +39 chance (0x80145498) to taunt (49), else
// the list 0x80145484; within 160 with a skill roll: an opponent targeting the CPU in situation 8 /
// 11 the trait +35 / +36 chance (0x801454A8 / 0x801454A0) for 7; an opponent not on the apron
// (flags80 0x01) within 130 the trait +37 chance (0x801454B0), then the trait +34 chance
// (0x801454BC) for 6 (not with flags80 0x04), else 1 (not with 0x02).
void ai_decide_apron_side() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if (apron_walk_dpad(s->slot)) {
        const Wrestler *t = (o->flags_e9 & 0x10) ? &g_wrestlers[o->partner] : o;
        if (t->location & 1) {
            if (s->location & 2)
                ai_set_mode(ai_mode::HeadForRing);
        } else if (t->location & 4) {
            ai_set_mode(mem_s16(blk(AI_ZONE_SELF)) == ring_zone_of(&t->point_120) ? ai_mode::DropOffApron : ai_mode::HeadForRing);
        }
        return;
    }
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->slot == s->slot || w->state != state::Running)
                continue;
            Vec3 sp = ai_self_point();
            f32 out[9];
            segment_distance(&w->point_120, &sp, out);
            if (!((f64)out[4] < 160.0))
                continue;
            if (abs_i(angle_diff((s32)out[7], w->facing)) >= 284)
                continue;
            if (chance_check(w->slot == g_wrestlers[s->partner].slot ? 400 : 800)) {
                ai_set_mode(ai_mode::ShortBlockHold);
                return;
            }
        }
    }
    if (ai_apron_walk()) {
        ai_set_mode(ai_mode::WalkRandomlyByZone);
        return;
    }
    s32 so = mem_s32(blk(AI_SIT_OPP));
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if ((u32)(so - 15) < 2 || so == 19 || d > 250.0) {
        // ai_history_count with mode 1 is always 0, so a passed roll always taunts.
        if (chance_check(mem_s16(0x80145498 + 2u * (u32)(s32)mem_s8(kTraits[9].global))) &&
            !ai_history_count(o->slot, 27, 16, 1))
            ai_set_mode(ai_mode::ApronWait);
        else
            ai_pick_mode_weighted(0x80145484);
        return;
    }
    if (!(d < 160.0) || !chance_check(mem_s32(blk(AI_SKILL))))
        return;
    if (o->opponent == s->slot) {
        if (so == 8 && chance_check(mem_s16(0x801454A8 + 2u * (u32)(s32)mem_s8(kTraits[5].global)))) {
            ai_set_mode(ai_mode::ShortBlockHold);
            return;
        }
        if (so == 11 && chance_check(mem_s16(0x801454A0 + 2u * (u32)(s32)mem_s8(kTraits[6].global)))) {
            ai_set_mode(ai_mode::ShortBlockHold);
            return;
        }
    }
    if ((o->flags80 & 1) || !((f64)mem_f32(blk(AI_DIST)) < 130.0) ||
        !chance_check(mem_s16(0x801454B0 + 2u * (u32)(s32)mem_s8(kTraits[7].global))))
        return;
    if (chance_check(mem_s16(0x801454BC + 2u * (u32)(s32)mem_s8(kTraits[4].global)))) {
        if (!(o->flags80 & 4))
            ai_set_mode(ai_mode::Strike);
    } else if (!(o->flags80 & 2)) {
        ai_set_mode(ai_mode::EngageNearbyOpponent);
    }
}
// N64 0x800F356C: the CPU on the apron, its opponent in the ring (+0x82 0x08). Over-the-top rules
// send a legal man in (33); the tag deciders, the ropes (38) and the walk flag (ai_decide_apron_side)
// come first. Then by the opponent's situation: targeted while it runs or attacks (2, 8, 11) a skill
// roll for 33 (45%) or 7; against a down opponent (15, 16, 19) staying down 61+ frames the trait +39
// chance (0x8014547C) to taunt (49); against 17 / 18 within 160 the list 0x8014543C; 25 / 26
// ai_decide_wait_or_engage; else armed (trait +38 at 2 / 3) 20% round the floor (35), beyond 200 the
// +39 taunt. A keenness from ai_trait_flag2 and the distance (200-800) then rolls for a springboard
// or top-rope move: the move-set bits in attrs +0x38 / +0x3C pair a climb (ai_try_corner_climb:
// 0x40 / 0x20 / 0x2000000 / 0x4000000) with mode 67 (0x40000 / 0x20000 / 0x8000000); within 160
// the trait +37 chance (0x80145470) picks list 0x8014543C (chance 10 x spirit) or 0x80145450.
// Otherwise back in: armed 50% 35, a legal man 33, in team play 33 when anyone has flags_e9 /
// flags_f1 0x02.
void ai_decide_apron_vs_ring() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if ((g_match_rules & 0x400) && (s->flags_e9 & 0x10)) {
        ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    if (ai_tag_partner_check() || ai_tag_help_check())
        return;
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if (so == 38 && ai_decide_ropes())
        return;
    if (ai_tag_reach_check() && ai_tag_reach())
        return;
    if ((g_match_type & 0x8000) && ai_tag_extra())
        return;
    if (mem_u8(blk(AI_FLAGS)) & 0x20) {
        ai_decide_apron_side();
        return;
    }
    s32 t39 = mem_s16(0x8014547C + 2u * (u32)(s32)mem_s8(kTraits[9].global));
    bool armed = (u32)((u8)mem_s8(kTraits[8].global) - 2) < 2;  // trait +38 at 2 or 3
    s32 keen = 0;
    bool f2 = ai_trait_flag2() != 0;
    switch (so) {
    case 2: case 8: case 11:
        if (o->opponent == s->slot && chance_check(mem_s32(blk(AI_SKILL)))) {
            ai_set_mode(chance_check(450) ? ai_mode::HeadForRing : ai_mode::ShortBlockHold);
            return;
        }
        break;
    case 15: case 16: case 19:
        keen = f2 ? 800 : 300;
        if (chance_check(mem_s32(blk(AI_SKILL))) && mem_s16(blk(AI_OPP_DOWN)) >= 61 && chance_check(t39) &&
            !(s->flags_ee & 4)) {
            ai_set_mode(ai_mode::ApronWait);
            return;
        }
        break;
    case 17: case 18:
        if (chance_check(mem_s32(blk(AI_SKILL))) && (f64)mem_f32(blk(AI_DIST_P)) < 160.0) {
            ai_pick_mode_weighted(0x8014543C);
            return;
        }
        break;
    case 25: case 26:
        ai_decide_wait_or_engage();
        return;
    default:
        if (armed && chance_check(200) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
            ai_set_mode(ai_mode::DropOffApron);
            return;
        }
        if ((f64)mem_f32(blk(AI_DIST_P)) <= 200.0) {
            keen = f2 ? 400 : 200;
        } else {
            if (chance_check(t39) && !ai_mode_recent(ai_mode::ApronWait, 5)) {
                ai_set_mode(ai_mode::ApronWait);
                return;
            }
            keen = f2 ? 600 : 300;
        }
        break;
    }
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        if (so == 15) {
            if (s->flags_ee & 4) {
                if (chance_check(keen) && (mem_u16(s->attrs + 0x3C) & 0x400) && ai_try_corner_climb())
                    return;
                if (s->flags_e9 & 0x10) {
                    ai_set_mode(ai_mode::HeadForRing);
                    return;
                }
            }
            if (chance_check(mem_s32(blk(AI_SKILL))) && chance_check(keen)) {
                u32 m = mem_u32(s->attrs + 0x38);
                if ((m & 0x40040) == 0x40040) {
                    if (!chance_check(500)) {
                        ai_set_mode(ai_mode::RunAtCarriedOpponent);
                        return;
                    }
                    if (ai_try_corner_climb())
                        return;
                } else {
                    if ((m & 0x40) && ai_try_corner_climb())
                        return;
                    if (mem_u32(s->attrs + 0x38) & 0x40000) {
                        ai_set_mode(ai_mode::RunAtCarriedOpponent);
                        return;
                    }
                }
            }
            if (s->flags_e9 & 0x10) {
                ai_set_mode(ai_mode::HeadForRing);
                return;
            }
        } else {
            if (s->flags_ee & 4) {
                if (chance_check(keen)) {
                    if ((mem_u32(s->attrs + 0x3C) & 0xA000000) == 0xA000000) {
                        if (!chance_check(500)) {
                            ai_set_mode(ai_mode::RunAtCarriedOpponent);
                            return;
                        }
                        if (ai_try_corner_climb())
                            return;
                    } else {
                        if ((mem_u16(s->attrs + 0x3C) & 0x200) && ai_try_corner_climb())
                            return;
                        if (mem_u16(s->attrs + 0x3C) & 0x800) {
                            ai_set_mode(ai_mode::RunAtCarriedOpponent);
                            return;
                        }
                    }
                }
                if (so == 16) {
                    if (mem_u16(s->attrs + 0x3C) & 0x100) {
                        ai_set_mode(ai_mode::GrappleApproach);
                        return;
                    }
                } else if (s->flags_e9 & 0x10) {
                    ai_set_mode(ai_mode::HeadForRing);
                    return;
                }
            }
            if (chance_check(mem_s32(blk(AI_SKILL))) && chance_check(keen)) {
                u32 m = mem_u32(s->attrs + 0x38);
                if ((m & 0x20020) == 0x20020) {
                    if (!chance_check(500)) {
                        ai_set_mode(ai_mode::RunAtCarriedOpponent);
                        return;
                    }
                    if (ai_try_corner_climb())
                        return;
                } else {
                    if ((m & 0x20) && ai_try_corner_climb())
                        return;
                    if (mem_u32(s->attrs + 0x38) & 0x20000) {
                        ai_set_mode(ai_mode::RunAtCarriedOpponent);
                        return;
                    }
                }
            }
            if ((f64)mem_f32(blk(AI_DIST_P)) < 160.0 &&
                chance_check(mem_s16(0x80145470 + 2u * (u32)(s32)mem_s8(kTraits[7].global)))) {
                ai_pick_mode_weighted(chance_check(10 * mem_s16(blk(AI_SPIRIT))) ? 0x8014543C : 0x80145450);
                return;
            }
        }
    }
    // Back into the ring.
    if (chance_check(mem_s32(blk(AI_SKILL))) && armed && chance_check(500) && !ai_mode_recent(ai_mode::HeadForRing, 10) &&
        !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
        ai_set_mode(ai_mode::DropOffApron);
        return;
    }
    if (s->flags_e9 & 0x10)
        ai_set_mode(ai_mode::HeadForRing);
    if (g_match_type & 0x1000) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->roster_id != -1 && (w->flags_e9 & 2) && (w->flags_f1 & 2)) {
                ai_set_mode(ai_mode::HeadForRing);
                return;
            }
        }
    }
}

// N64 0x800F473C: both on the apron (+0x82 0x10). Over-the-top rules send a legal man in (33);
// then the tag deciders. On different sides of the ring (ring_zone): the trait +38 chance
// (0x80145548) to go round the floor (35, unless 33 / 34 in the last 10 modes), else a legal man
// enters (33). 170+ away: the same 35, else 15% mode 56, else 33 (unless 35 / 36 recently). Closer:
// 30% mode 6; on the apron with the walk flag the walk (60); 30% the 35 / 33 choice; 15% 56.
void ai_decide_apron_vs_apron() {
    Wrestler *s = g_ai_self;
    if ((g_match_rules & 0x400) && (s->flags_e9 & 0x10)) {
        ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    if (ai_decide_tag())
        return;
    s32 t38 = mem_s16(0x80145548 + 2u * (u32)(s32)mem_s8(kTraits[8].global));
    if (s->ring_zone != g_ai_opp->ring_zone) {
        if (chance_check(t38) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10))
            ai_set_mode(ai_mode::DropOffApron);
        else if (s->flags_e9 & 0x10)
            ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) >= 170.0) {
        if (chance_check(t38) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10))
            ai_set_mode(ai_mode::DropOffApron);
        else if (chance_check(150))
            ai_set_mode(ai_mode::DodgeDive);
        else if (!ai_mode_recent(ai_mode::DropOffApron, 10) && !ai_mode_recent(ai_mode::LeaveRingThroughRopes, 10))
            ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    if (chance_check(300)) {
        ai_set_mode(ai_mode::Strike);
        return;
    }
    if ((s->location & 2) && (mem_u8(blk(AI_FLAGS)) & 0x20) && ai_apron_walk()) {
        ai_set_mode(ai_mode::WalkRandomlyByZone);
        return;
    }
    if (chance_check(300)) {
        if (chance_check(t38) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
            ai_set_mode(ai_mode::DropOffApron);
            return;
        }
        if (!ai_mode_recent(ai_mode::DropOffApron, 10) && !ai_mode_recent(ai_mode::LeaveRingThroughRopes, 10)) {
            ai_set_mode(ai_mode::HeadForRing);
            return;
        }
    }
    if (chance_check(150))
        ai_set_mode(ai_mode::DodgeDive);
}

}  // namespace

namespace {

// N64 0x800F6798: from the apron at an opponent on the floor below the same ring side (1 when it
// set a mode). Not as the legal man under 4 counts from a count-out, nor with the opponent beyond z
// 530. The gap is the difference of the two points along the side: x on even zones, the second
// component (y, the height, as written: likely meant z) on odd ones. Against a down / dazed / lying
// opponent (15, 16, 19): beyond 300, with the opponent down longer than a tenth of the gap and the
// long-dive move bit (attrs +0x38 0x10000), mode 88; else only when it stays down longer than a
// fifth of the gap. Otherwise beyond 300 mode 88 with that bit. Mode 87 (the near dive, bit 0x8000)
// needs the opponent within 215 of the ring's centre line on x or on the second component.
s32 ai_decide_apron_jump() {
    const Wrestler *s = g_ai_self;
    if ((s->flags_e9 & 0x10) && (mem_u8(A_COUNTOUT) & 1) && mem_s32(A_COUNTOUT_LEFT) < 4)
        return 0;
    if ((f64)mem_f32(A_AI_OPP_POINT + 8) > 530.0)
        return 0;
    s16 zone = mem_s16(blk(AI_ZONE_SELF));
    if (zone != mem_s16(blk(AI_ZONE_OPP)))
        return 0;
    u32 c = (zone & 1) ? 4u : 0u;
    f32 gap = absf(mem_f32(A_AI_SELF_POINT + c) - mem_f32(A_AI_OPP_POINT + c));
    auto attr = [&]() { return mem_u32(g_ai_self->attrs + 0x38); };
    auto near_line = [&]() {
        return (f64)absf(mem_f32(A_AI_OPP_POINT)) < 215.0 || (f64)absf(mem_f32(A_AI_OPP_POINT + 4)) < 215.0;
    };
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if (so >= 15 && (so < 17 || so == 19)) {
        s32 down = mem_s16(blk(AI_OPP_DOWN));
        if ((f64)gap > 300.0) {
            if ((f64)gap * 0.1 < (f64)down && (attr() & 0x10000)) {
                ai_set_mode(ai_mode::RunningAttack);
                return 1;
            }
        } else if ((f64)gap < 150.0 && (attr() & 0x8000) && near_line()) {
            ai_set_mode(ai_mode::WalkToOpponentZoneAxis);
            return 1;
        }
        if (!((f64)gap * 0.2 < (f64)down))
            return 0;
    } else if ((f64)gap > 300.0) {
        if (attr() & 0x10000) {
            ai_set_mode(ai_mode::RunningAttack);
            return 1;
        }
    } else if ((f64)gap < 150.0) {
        if (!(attr() & 0x8000))
            return 0;
        if (near_line()) {
            ai_set_mode(ai_mode::WalkToOpponentZoneAxis);
            return 1;
        }
    }
    if ((attr() & 0x8000) && near_line()) {
        ai_set_mode(ai_mode::WalkToOpponentZoneAxis);
        return 1;
    }
    return 0;
}

// N64 0x800F312C: the CPU on the apron, its opponent on the floor (+0x82 0x20). Over-the-top rules
// send a legal man in (33); then ai_decide_tag; with the walk flag and a non-legal opponent, free to
// walk the apron: 35; the ropes (38); the apron walk (60); the legal man late in a count-out (under 5
// counts, outside type 0x4000) goes in (33). Then a skill roll (failing: LABEL_52 below). On a
// different ring side: the trait +38 chance (0x80145428, +150 under rule 0x4000) for 35, else a legal
// man (or one free to walk) 33. On the same side a dive chance (0x8014541C by +38; +200 / +300 with
// ai_trait_flag2; halved against a standing opponent, and the 35 chance too for 17 / 18), 50% the
// apron dive (ai_decide_apron_jump), else the climb (ai_try_corner_climb) with move bit 0x100
// (0x80 against an opponent not down); a lying opponent (19) may first draw the trait +39 taunt
// (0x80145434, 49, not in finisher mode). Failing those the 35 chance; LABEL_52: mode 104 after a
// recent 35 / 36, else a legal man 33.
void ai_decide_apron_vs_floor() {
    Wrestler *s = g_ai_self;
    if ((g_match_rules & 0x400) && (s->flags_e9 & 0x10)) {
        ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    if (ai_decide_tag())
        return;
    if ((mem_u8(blk(AI_FLAGS)) & 0x20) && !(g_ai_opp->flags_e9 & 0x10) && apron_walk_dpad(s->slot)) {
        ai_set_mode(ai_mode::DropOffApron);
        return;
    }
    if (mem_s32(blk(AI_SIT_OPP)) == 38 && ai_decide_ropes())
        return;
    if ((mem_u8(blk(AI_FLAGS)) & 0x20) && ai_apron_walk()) {
        ai_set_mode(ai_mode::WalkRandomlyByZone);
        return;
    }
    if (!(g_match_type & 0x4000) && (s->flags_e9 & 0x10) && (mem_u8(A_COUNTOUT) & 1) &&
        mem_s32(A_COUNTOUT_LEFT) < 5) {
        ai_set_mode(ai_mode::HeadForRing);
        return;
    }
    bool f2 = ai_trait_flag2() != 0;
    s32 t38 = mem_s8(kTraits[8].global);
    s32 round = mem_s16(0x80145428 + 2u * (u32)t38);
    if (g_match_rules & 0x4000)
        round += 150;
    s32 dive = mem_s16(0x8014541C + 2u * (u32)t38);
    bool back = true;  // LABEL_52
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        if (mem_s16(blk(AI_ZONE_SELF)) != mem_s16(blk(AI_ZONE_OPP))) {
            if (chance_check(round) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
                ai_set_mode(ai_mode::DropOffApron);
                return;
            }
            if ((s->flags_e9 & 0x10) || ((mem_u8(blk(AI_FLAGS)) & 0x20) && apron_walk_dpad(s->slot))) {
                ai_set_mode(ai_mode::HeadForRing);
                return;
            }
        } else {
            u32 climb_bit = 128;
            s32 so = mem_s32(blk(AI_SIT_OPP));
            switch (so) {
            case 15:
                if (f2)
                    dive += 200;
                climb_bit = 256;
                break;
            case 16: case 19:
                if (so == 19 && chance_check(mem_s16(0x80145434 + 2u * (u32)(s32)mem_s8(kTraits[9].global))) &&
                    !(s->flags_ee & 4)) {
                    ai_set_mode(ai_mode::ApronWait);
                    return;
                }
                if (f2)
                    dive += 300;
                break;
            case 17: case 18:
                if (f2)
                    dive += 200;
                dive /= 2;
                round /= 2;
                break;
            default:
                if (f2)
                    dive += 200;
                dive /= 2;
                break;
            }
            if (chance_check(dive)) {
                if (chance_check(500)) {
                    if (ai_decide_apron_jump())
                        return;
                } else if ((mem_u32(s->attrs + 0x38) & climb_bit) && ai_try_corner_climb()) {
                    return;
                }
            }
            if (chance_check(round) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
                ai_set_mode(ai_mode::DropOffApron);
                return;
            }
        }
    }
    (void)back;
    if (ai_mode_recent(ai_mode::DropOffApron, 10) || ai_mode_recent(ai_mode::LeaveRingThroughRopes, 10))
        ai_set_mode(ai_mode::CircleBriefly);
    else if (s->flags_e9 & 0x10)
        ai_set_mode(ai_mode::HeadForRing);
}

// N64 0x800F6154: the legal man on the floor (not finished, end_flags 0x01) in a ring arena (g_arena
// below 10) heads back in (1 when it set a mode) by the count-out setting (0x800A9462). 10 or 20
// counts: once the counts left come within 5 of the trait +38 margin (0x8014526C / 0x80145278 by
// +38, less 1), under 3 counts 34 at once, else a chance of 1000 / margin: with the opponent's point
// beyond z 530 34, else 50% 33 or 34. Off: after its time on the floor (A_AI_FLOOR_TIME) passes the
// trait +38 limit less 9 seconds (0x80145554, under rule 0x4000 0x80145564: 15 / 20 / 30 / 45
// seconds, or 30 / 45 / 60 / 90) a chance of 500 + 50 per second over: 33 at 50% (always with the
// where-class 0x100), else 34; not then with the where-class 0x100 in finisher mode.
s32 ai_decide_floor_timeout() {
    const Wrestler *s = g_ai_self;
    if (!(s->flags_e9 & 0x10) || mem_s32(A_ARENA) >= 10 || !(s->location & 4) || (s->end_flags & 1))
        return 0;
    s32 opt = mem_s8(A_COUNTOUT_OPTION);
    s32 m;
    if (opt == 1 || opt == 2) {
        u32 tab = (opt == 1) ? 0x8014526C : 0x80145278;
        s32 left = mem_s32(A_COUNTOUT_LEFT);
        s32 margin = left - (mem_s16(tab + 2u * (u32)(s32)mem_s8(kTraits[8].global)) - 1);
        if (margin >= 5)
            return 0;
        if (margin <= 0)
            margin = 1;
        if (left < 3) {
            m = ai_mode::HeadForRingAlternate;
        } else {
            if (!chance_check(1000 / margin))
                return 0;
            if ((f64)mem_f32(A_AI_SELF_POINT + 8) > 530.0)
                m = ai_mode::HeadForRingAlternate;
            else
                m = chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate;
        }
    } else if (opt == 0) {
        bool w100 = (mem_u16(blk(AI_WHERE)) & 0x100) != 0;
        if (w100 && (s->flags_ee & 4))
            return 0;
        u32 lim = ((g_match_rules & 0x4000) ? 0x80145564u : 0x80145554u) + 3u * (u32)(s32)mem_s8(kTraits[8].global);
        u32 t = A_AI_FLOOR_TIME + 3u * (u32)s->slot;
        s32 over = (mem_s8(t + 1) - mem_s8(lim + 1)) + 60 * (mem_s8(t) - mem_s8(lim));
        if (over < -9 || !chance_check(50 * over + 500))
            return 0;
        m = chance_check(w100 ? 1000 : 500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate;
    } else {
        return 0;
    }
    ai_set_mode(m);
    return 1;
}

// N64 0x800F812C: both on the floor (1 when it chose; nothing on a failed skill roll). On the same
// side beyond 250: unarmed (flags_ef 0x01) half the weapon chance (trait +38 at 0x801457CC times the
// size factor 0x801457F8 by attrs +6) for 101, else half the +39 chance (0x801457F0) for 38; then
// half the +38 chance (0x801457D8) to enter (50% 33 / 34), the +38 chance (0x801457E4) to run (40),
// else the list 0x801457B8. On different sides beyond 200: both not finished and on opposite sides
// (zone ^ 2) 41; else the same with full chances.
s32 ai_decide_floor_b() {
    const Wrestler *s = g_ai_self;
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return 0;
    s32 t38 = mem_s8(kTraits[8].global), t39 = mem_s8(kTraits[9].global);
    s16 zs = mem_s16(blk(AI_ZONE_SELF)), zo = mem_s16(blk(AI_ZONE_OPP));
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    auto weapon = [&]() {
        return (s16)(s32)((f32)mem_s16(0x801457CC + 2u * (u32)t38) *
                          mem_f32(0x801457F8 + 4u * (u32)(s32)mem_s8(s->attrs + 6)));
    };
    s32 m;
    if (zs == zo) {
        if (!(d > 250.0))
            return 0;
        if (!(s->flags_ef & 1)) {
            if (chance_check(weapon() / 2)) {
                ai_set_mode(ai_mode::WalkToArea);
                return 1;
            }
            if (!(s->flags_ef & 1) && chance_check(mem_s16(0x801457F0 + 2u * (u32)t39) / 2)) {
                ai_set_mode(ai_mode::TauntUnlessMeterMode);
                return 1;
            }
        }
        if (chance_check(mem_s16(0x801457D8 + 2u * (u32)t38) / 2))
            m = chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate;
        else if (chance_check(mem_s16(0x801457E4 + 2u * (u32)t38)))
            m = ai_mode::Run;
        else
            m = -1;
    } else {
        if (!(d > 200.0))
            return 0;
        if ((s->end_flags & g_ai_opp->end_flags & 1) && (s16)(zs ^ 2) == zo) {
            ai_set_mode(ai_mode::WalkTowardOpponent);
            return 1;
        }
        if (!(s->flags_ef & 1)) {
            if (chance_check(weapon())) {
                ai_set_mode(ai_mode::WalkToArea);
                return 1;
            }
            if (!(s->flags_ef & 1) && chance_check(mem_s16(0x801457F0 + 2u * (u32)t39))) {
                ai_set_mode(ai_mode::TauntUnlessMeterMode);
                return 1;
            }
        }
        if (chance_check(mem_s16(0x801457D8 + 2u * (u32)t38)))
            m = chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate;
        else if (chance_check(mem_s16(0x801457E4 + 2u * (u32)t38)))
            m = ai_mode::Run;
        else
            m = -1;
    }
    if (m < 0)
        ai_pick_mode_weighted(0x801457B8);
    else
        ai_set_mode(m);
    return 1;
}

// N64 0x800F49C8: the default where-class (+0x82 0x100: both on the floor, or anything else). The
// tag reach and extra checks first. Climbing out of the crowd (state 129): beyond 130 of its own
// corner, hold the d-pad toward it. Otherwise the floor time-out (ai_decide_floor_timeout), the tag
// partner and help checks, an armed runner, against a finisher-mode opponent ai_decide_change_area,
// both on the floor (ai_decide_floor_b), else the standing tree.
void ai_decide_floor() {
    if (ai_tag_reach_check() && ai_tag_reach())
        return;
    if ((g_match_type & 0x8000) && ai_tag_extra())
        return;
    if (g_ai_self->state == state::CarryAttacker) {
        s32 q = mem_s16(blk(AI_QUAD_SELF));
        if ((f64)mem_f32(blk(AI_CORNERS) + 4u * (u32)q) > 130.0) {
            Vec3 sp = ai_self_point();
            u32 c = A_RING_CORNERS + 12u * (u32)q;
            Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
            mem_w16(blk(AI_HELD), dir8_buttons(dir8_to(&sp, &corner)));
        }
        return;
    }
    if (ai_decide_floor_timeout() || ai_tag_partner_check() || ai_tag_help_check() || ai_vs_runner_armed())
        return;
    if ((g_ai_opp->flags_ee & 4) && ai_decide_change_area())
        return;
    if (ai_decide_floor_b())
        return;
    ai_decide_standing();
}

}  // namespace

namespace {

// N64 0x800FAFDC: splits a slot's move list (count entries of (s16 move, s16 flags)) into the
// favourites (flags 0x01, into fav) and the rest (into rest); returns how many are favourites.
s32 ai_list_filter(s32 count, s32 *fav, s32 *rest, u32 list) {
    s32 n = 0;
    for (s32 i = 0; i < count; i++, list += 4) {
        if (mem_u8(list + 2) & 1) {
            *fav++ = mem_s16(list);
            n++;
        } else {
            *rest++ = mem_s16(list);
        }
    }
    return n;
}

// N64 0x800F6B20: a dive from the ring at the opponent on the floor near the ring (1 when it set a
// mode): kind 1 mode 62 (move bit attrs +0x38 0x200), kind 0 63 (0x400), kind 2 64 (0x800). Not as
// the legal man under 4 counts from a count-out; the opponent within 230 of the centre line on x or
// z and short of z 530. Kinds 0 / 1 want the opponent down 31+ frames (or in situation 1), and against
// a downed one (15) the move set's entry (+0x2588 / +0x2564) without bit 0x2000000.
s32 ai_decide_ring_edge(s32 kind) {
    const Wrestler *s = g_ai_self;
    if ((s->flags_e9 & 0x10) && (mem_u8(A_COUNTOUT) & 1) && mem_s32(A_COUNTOUT_LEFT) < 4)
        return 0;
    if (!((f64)absf(mem_f32(A_AI_OPP_POINT)) < 230.0) && !((f64)absf(mem_f32(A_AI_OPP_POINT + 8)) < 230.0))
        return 0;
    if (!((f64)mem_f32(A_AI_OPP_POINT + 8) < 530.0))
        return 0;
    u32 bits = mem_u32(s->attrs + 0x38);
    s32 so = mem_s32(blk(AI_SIT_OPP));
    s32 m;
    if (kind == 2) {
        if (!(bits & 0x800))
            return 0;
        m = ai_mode::WalkToEdgeAndDiveAlternate;
    } else if (kind == 0 || kind == 1) {
        if (mem_s16(blk(AI_OPP_DOWN)) < 31 && so != 1)
            return 0;
        if (!(bits & (kind ? 0x200u : 0x400u)))
            return 0;
        if (so == 15 && (mem_u32(mem_u32(blk(AI_MOVESET)) + (kind ? 0x2564u : 0x2588u)) & 0x2000000))
            return 0;
        m = kind ? ai_mode::GoOutsideToOpponent : ai_mode::WalkToEdgeAndDive;
    } else {
        return 0;
    }
    ai_set_mode(m);
    return 1;
}

// The two floor move lists' picker (ai_list_pick_floor_a / _b): with the opponent on the floor, a
// chance (`fav_chance`) for a random favourite, else (or when it fails to act) a random other entry:
// 265 ai_decide_ring_edge(1), 266 ai_decide_ring_edge(0), 268 mode 37, `climb_move` ai_try_corner_climb.
s32 ai_list_pick_floor(s32 slot, u32 count_at, u32 list, s32 fav_chance, s32 climb_move) {
    if (!(g_ai_opp->location & 4))
        return 0;
    s32 count = mem_s16(count_at);
    if (count == 0)
        return 0;
    s32 fav[4], rest[4];
    s32 nf = ai_list_filter(count, fav, rest, list);
    s32 nr = count - nf;
    (void)slot;
    auto act = [&](s32 move) -> s32 {
        if (move == 265)
            return ai_decide_ring_edge(1);
        if (move == 266)
            return ai_decide_ring_edge(0);
        if (move == 268) {
            ai_set_mode(ai_mode::WalkToOpponent);
            return 1;
        }
        if (move == climb_move)
            return ai_try_corner_climb();
        return 0;
    };
    if (nf != 0 && chance_check(fav_chance)) {
        if (act(fav[rand_pool_next() % nf]))
            return 1;
    }
    if (nr <= 0)
        return 0;
    return act(rest[rand_pool_next() % nr]) ? 1 : 0;
}

// N64 0x800FABB4: the slot's fifth CPU list (count 0x801594E8 + 12 x slot, entries 0x8016B300 + 16 x
// slot: 264-268) at a 40% favourite chance; 264 is the corner climb. See ai_list_pick_floor.
s32 ai_list_pick_floor_a(s32 slot) {
    return ai_list_pick_floor(slot, 0x801594E8 + 12u * (u32)slot, 0x8016B300 + 16u * (u32)slot, 400, 264);
}

// N64 0x800FADC8: the sixth CPU list (count 0x801594EA + 12 x slot, entries 0x80159680 + 16 x slot)
// at 50%; 263 is the corner climb. See ai_list_pick_floor.
s32 ai_list_pick_floor_b(s32 slot) {
    return ai_list_pick_floor(slot, 0x801594EA + 12u * (u32)slot, 0x80159680 + 16u * (u32)slot, 500, 263);
}

// N64 0x800F2BBC: the CPU in the ring, its opponent on the floor (+0x82 0x04). After ai_decide_tag:
// neither legal (a tag partner pair outside), with the partner's hold flags (partner_flags80 0xC0)
// 50% 41 or 32, else 36. In a tag match with all three others on the floor (and 33 / 34 not
// recent) 36. A go-out chance (trait +38 at 0x80145400, +150 under rule 0x4000) and a dive chance
// (0x801453F4), both x1.5 in finisher mode and x0.5 against a finisher-mode opponent. The ropes (38);
// after a skill roll by the opponent's situation: 11 targeting the CPU within 160 the trait +36
// chance (0x8014540C) for 44 (else no chances); down (15) the dive chance (+200 with
// ai_trait_flag2) for list floor_a; dazed / lying (16, 19; +300) list floor_b; 17 / 18 both halved;
// else the dive halved. Then the dive chance for 30% ai_decide_ring_edge(2) or list floor_b; the
// go-out chance (zeroed when the count-out is down to its trait +38 margin) for 36 (unless 33 / 34
// recent). Otherwise the trait +39 chance (0x80145414) to taunt (49, not with SPECIAL), else the
// list 0x801453E0.
void ai_decide_ring_vs_floor() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if (ai_decide_tag())
        return;
    if (!(s->flags_e9 & 0x10) && !(o->flags_e9 & 0x10)) {
        if (s->partner_flags80 & 0xC0)
            ai_set_mode(chance_check(500) ? ai_mode::WalkTowardOpponent : ai_mode::WaitAndWatch);
        else
            ai_set_mode(ai_mode::LeaveRingThroughRopes);
        return;
    }
    if (g_match_type & 0x2000) {
        s32 out = 0;
        for (s32 i = 0; i < 4; i++)
            if (i != s->slot && (g_wrestlers[i].location & 4))
                out++;
        if (out == 3 && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
            ai_set_mode(ai_mode::LeaveRingThroughRopes);
            return;
        }
    }
    s32 t38 = mem_s8(kTraits[8].global);
    s32 go = mem_s16(0x80145400 + 2u * (u32)t38);
    if (g_match_rules & 0x4000)
        go += 150;
    s32 dive = mem_s16(0x801453F4 + 2u * (u32)t38);
    if (s->flags_ee & 4) {
        go = (s32)((f64)go * 1.5);
        dive = (s32)((f64)dive * 1.5);
    }
    if (o->flags_ee & 4) {
        go = (s32)((f64)go * 0.5);
        dive = (s32)((f64)dive * 0.5);
    }
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if (so == 38 && ai_decide_ropes())
        return;
    if (ai_history_count(s->slot, 13, 10, 1) && !(o->flags_ee & 4)) {  // mode 1: always 0
        ai_set_mode(ai_mode::LeaveRingThroughRopes);
        return;
    }
    bool f2 = ai_trait_flag2() != 0;
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        switch (so) {
        case 11:
            dive = 0;
            go = 0;
            if (o->opponent == s->slot && (f64)mem_f32(blk(AI_DIST_P)) < 160.0 &&
                chance_check(mem_s16(0x8014540C + 2u * (u32)(s32)mem_s8(kTraits[6].global)))) {
                ai_set_mode(ai_mode::RunAtOpponent);
                return;
            }
            break;
        case 15:
            if (f2)
                dive += 200;
            if (chance_check(dive) && ai_list_pick_floor_a(s->slot))
                return;
            break;
        case 16: case 19:
            if (f2)
                dive += 300;
            if (chance_check(dive) && ai_list_pick_floor_b(s->slot))
                return;
            break;
        case 17: case 18:
            if (f2)
                dive += 200;
            dive /= 2;
            go /= 2;
            break;
        default:
            if (f2)
                dive += 200;
            dive /= 2;
            break;
        }
        if (chance_check(dive) && ((chance_check(300) && ai_decide_ring_edge(2)) || ai_list_pick_floor_b(s->slot)))
            return;
        s32 opt = mem_s8(A_COUNTOUT_OPTION);
        if (opt == 1 || opt == 2) {
            s32 margin = mem_s16((opt == 1 ? 0x8014526Cu : 0x80145278u) + 2u * (u32)t38);
            if (!(mem_s32(A_COUNTOUT_LEFT) - margin > 0))
                go = 0;
        }
        if (chance_check(go) && !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
            ai_set_mode(ai_mode::LeaveRingThroughRopes);
            return;
        }
    }
    if (chance_check(mem_s16(0x80145414 + 2u * (u32)(s32)mem_s8(kTraits[9].global))) && !(s->flags_ee & 2))
        ai_set_mode(ai_mode::ApronWait);
    else
        ai_pick_mode_weighted(0x801453E0);
}

}  // namespace

namespace {

// N64 0x800F4164: the CPU on the floor, its opponent in the ring (+0x82 0x40) or on the apron (0x80).
// The floor time-out and the tag deciders first; a CPU with flags_e9 0x01 against an opponent with
// flags_e9 / flags_f1 0x02 goes in (34); against 25 / 26 ai_decide_wait_or_engage. (Two history
// checks with mode 1 are always 0.) A failed skill roll picks list 0x801454C8. The legal man armed
// with trait +38 at 3 enters at an opponent in the ring (34), and with SPECIAL 50% 33 / 34. Within
// 160: unless the opponent is down (15) or busy (flags80 0x03), the trait +38 chance (0x8014550C)
// grapples an opponent in the ring (1, unarmed) or on the apron (the trait +34 chance 0x80145524 for
// 9 against 16, else 6; failing it 1). Else by its situation the +38 chance (0x80145500): twice it
// against 15 / 16 / 19, half against others, for 50% 33 / 34; an opponent attacking the CPU (8) the
// trait +37 chance (0x80145518) for 46, else 47; else list 0x801454E8. Beyond 160: an opponent idle
// on the apron of the same side beyond 220 the +37 chance for 10; unarmed and not finished, the
// weapon chance (0x801454F4 by +38 times 0x80145538 by attrs +6; only when free to walk the apron
// with the walk flag) for 101; the +38 chance for 50% 33 / 34; unarmed the +39 chance (0x80145530)
// for 38; else list 0x801454C8.
void ai_decide_floor_vs_ring() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    auto enter = []() { ai_set_mode(chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate); };
    if (ai_decide_floor_timeout() || ai_decide_tag())
        return;
    if ((s->flags_e9 & 1) && (o->flags_e9 & 2) && (o->flags_f1 & 2)) {
        ai_set_mode(ai_mode::HeadForRingAlternate);
        return;
    }
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if ((u32)(so - 25) < 2) {
        ai_decide_wait_or_engage();
        return;
    }
    if (ai_history_count(s->slot, 15, 10, 1)) {
        enter();
        return;
    }
    if (ai_history_count(o->slot, 111, 16, 1) || ai_history_count(o->slot, 155, 16, 1)) {
        s32 r = rand_pool_next();
        if (r < 400) {
            ai_set_mode(r < 200 ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate);
            return;
        }
    }
    if (!chance_check(mem_s32(blk(AI_SKILL)))) {
        ai_pick_mode_weighted(0x801454C8);
        return;
    }
    s32 t38 = mem_s8(kTraits[8].global);
    if (s->flags_e9 & 0x10) {
        if ((s->flags_ef & 1) && t38 == 3 && (o->location & 1)) {
            ai_set_mode(ai_mode::HeadForRingAlternate);
            return;
        }
        if (s->flags_ee & 2) {
            enter();
            return;
        }
    }
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if (d < 160.0) {
        if (so != 15 && !(o->flags80 & 3) && chance_check(mem_s16(0x8014550C + 2u * (u32)t38))) {
            if (!(o->location & 1)) {
                if (!(o->location & 2))
                    return;
                if (chance_check(mem_s16(0x80145524 + 2u * (u32)(s32)mem_s8(kTraits[4].global)))) {
                    ai_set_mode(so == 16 ? ai_mode::StrikeThenFollowUp : ai_mode::Strike);
                    return;
                }
            }
            if (!(s->flags_ef & 1))
                ai_set_mode(ai_mode::EngageNearbyOpponent);
            return;
        }
        s32 c = mem_s16(0x80145500 + 2u * (u32)t38);
        if (so == 15 || so == 16 || so == 19) {
            if (chance_check(2 * c)) {
                enter();
                return;
            }
        } else if (so == 8) {
            if (o->opponent == s->slot) {
                ai_set_mode(chance_check(mem_s16(0x80145518 + 2u * (u32)(s32)mem_s8(kTraits[7].global))) ? ai_mode::HoldBlock : ai_mode::BlockDive);
                return;
            }
        } else if (chance_check(c / 2)) {
            enter();
            return;
        }
        ai_pick_mode_weighted(0x801454E8);
        return;
    }
    if (!(o->flags80 & 5) && (o->location & 2) && mem_s16(blk(AI_ZONE_SELF)) == mem_s16(blk(AI_ZONE_OPP)) &&
        d > 220.0 && chance_check(mem_s16(0x80145518 + 2u * (u32)(s32)mem_s8(kTraits[7].global)))) {
        ai_set_mode(ai_mode::EvadeAndCounter);
        return;
    }
    if (!(s->flags_ef & 1)) {
        s32 w = (s16)(s32)((f32)mem_s16(0x801454F4 + 2u * (u32)t38) *
                           mem_f32(0x80145538 + 4u * (u32)(s32)mem_s8(s->attrs + 6)));
        if (s->end_flags & 1)
            w = 0;
        if ((mem_u8(blk(AI_FLAGS)) & 0x20) && !apron_walk_dpad(s->slot))
            w = 0;
        if (chance_check(w)) {
            ai_set_mode(ai_mode::WalkToArea);
            return;
        }
    }
    if (chance_check(mem_s16(0x80145500 + 2u * (u32)t38))) {
        enter();
        return;
    }
    if (!(s->flags_ef & 1) && chance_check(mem_s16(0x80145530 + 2u * (u32)(s32)mem_s8(kTraits[9].global)))) {
        ai_set_mode(ai_mode::TauntUnlessMeterMode);
        return;
    }
    ai_pick_mode_weighted(0x801454C8);
}

}  // namespace

namespace {

// N64 0x800F2428: against a finisher-mode opponent, after a skill roll (1 when it set a mode). Both
// on the floor (where-class 0x100) on different ring sides beyond 200: unarmed the weapon chance
// (0x8014536C by trait +38 times 0x80145384 by attrs +6) for 101; the +38 chance (0x80145378, not
// after 35 / 36 in the last 5 modes) for 50% 33 / 34. Then not legal nor flags_e9 0x02 (e9 & 0x03
// clear) the trait +37 chance (0x80145360) for 25% 41, else 45.
s32 ai_decide_change_area() {
    const Wrestler *s = g_ai_self;
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return 0;
    s32 t38 = mem_s8(kTraits[8].global);
    if ((mem_u16(blk(AI_WHERE)) & 0x100) && mem_s16(blk(AI_ZONE_SELF)) != mem_s16(blk(AI_ZONE_OPP)) &&
        (f64)mem_f32(blk(AI_DIST_P)) > 200.0) {
        if (!(s->flags_ef & 1) &&
            chance_check((s16)(s32)((f32)mem_s16(0x8014536C + 2u * (u32)t38) *
                                    mem_f32(0x80145384 + 4u * (u32)(s32)mem_s8(s->attrs + 6))))) {
            ai_set_mode(ai_mode::WalkToArea);
            return 1;
        }
        if (chance_check(mem_s16(0x80145378 + 2u * (u32)t38)) && !ai_mode_recent(ai_mode::DropOffApron, 5) && !ai_mode_recent(ai_mode::LeaveRingThroughRopes, 5)) {
            ai_set_mode(chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate);
            return 1;
        }
    }
    if (!(s->flags_e9 & 3) && chance_check(mem_s16(0x80145360 + 2u * (u32)(s32)mem_s8(kTraits[7].global)))) {
        ai_set_mode(chance_check(250) ? ai_mode::WalkTowardOpponent : ai_mode::BackAway);
        return 1;
    }
    return 0;
}

}  // namespace

namespace {

// N64 0x800FBE54: 1 when the wrestler's current wall zone (zone_info) has flags +0x26 0x800 and
// +0x28 0x08 (read by the cage deciders as "at a corner of the cage").
s32 zone_is_turnbuckle_a(const Wrestler *w) {
    u32 z = w->zone_info;
    return z && (mem_u16(z + 0x26) & 0x800) && (mem_u16(z + 0x28) & 8) ? 1 : 0;
}

// N64 0x800FBE84: the same with +0x26 0x400 and +0x28 0x04.
s32 zone_is_turnbuckle_b(const Wrestler *w) {
    u32 z = w->zone_info;
    return z && (mem_u16(z + 0x26) & 0x400) && (mem_u16(z + 0x28) & 4) ? 1 : 0;
}

// N64 0x800FB64C: the CPU below an opponent up on the cage wall (+0x82 0x2000). The opponent's cage
// states: 196 (0xC4) climbing, 198 (0xC6) hanging, 204 (0xCC) idle on the wall, 207 (0xCF) its top
// attack. SPECIAL at a hanging opponent: 50. Beyond 200: a skill roll (else walk up, 48) for 121 in
// finisher mode at an opponent not climbing, else 50% 10 / 122. Closer, after a skill roll: beyond
// 160 48; with the trait +35 chance (0x80145984) and its share by +37 (x 0x8014598C) at a top attack
// 131, else list 0x80145958; at a climber 123; within 180 of its height: at an idle opponent the +35
// chance for 47 (the share) or 7; in finisher mode 128 (unless 33 recently); the trait +34 chance
// (0x8014596C) for 1; within 130 the inverse chance for 6; with 51+ spirit 128. Then the trait +37
// chance (0x80145978, +500 in finisher mode; always at a hanging opponent) for 128 with 51+ spirit
// (unless 33 recently), else 123.
void ai_decide_vs_cage_wall() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if ((s->flags_ee & 2) && o->state == state::CageHang) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 200.0) {
        s32 m = ai_mode::WalkToDownedOpponent;
        if (chance_check(mem_s32(blk(AI_SKILL)))) {
            if ((s->flags_ee & 4) && o->state != state::CageTopClimbOver)
                m = ai_mode::CageClimbFromRun;
            else
                m = chance_check(500) ? ai_mode::EvadeAndCounter : ai_mode::CageClimbFromRunAlternate;
        }
        ai_set_mode(m);
        return;
    }
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return;
    if (!((f64)mem_f32(blk(AI_DIST_P)) < 160.0)) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    s32 t34 = mem_s8(kTraits[4].global);
    s32 c35 = mem_s16(0x80145984 + 2u * (u32)(s32)mem_s8(kTraits[5].global));
    s32 share = (s32)((f32)c35 * mem_f32(0x8014598C + 4u * (u32)(s32)mem_s8(kTraits[7].global)));
    if (o->state == state::CageTopDive && chance_check(c35)) {
        if (chance_check((s16)share))
            ai_set_mode(ai_mode::BlockWhenLevel);
        else
            ai_pick_mode_weighted(0x80145958);
        return;
    }
    if (o->state == state::CageTopClimbOver) {
        ai_set_mode(ai_mode::WalkToEdgeThenCRight);
        return;
    }
    if ((f64)mem_f32(blk(AI_HEIGHT_DIFF)) < 180.0) {
        if (o->state == state::CageWallStrike && chance_check((s16)c35)) {
            ai_set_mode(chance_check((s16)share) ? ai_mode::BlockDive : ai_mode::ShortBlockHold);
            return;
        }
        if ((s->flags_ee & 4) && !ai_mode_recent(ai_mode::HeadForRing, 5)) {
            ai_set_mode(ai_mode::WalkToEdgeAndClimbOutVariant2);
            return;
        }
        if (chance_check(mem_s16(0x8014596C + 2u * (u32)t34))) {
            ai_set_mode(ai_mode::EngageNearbyOpponent);
            return;
        }
        if ((f64)mem_f32(blk(AI_DIST_P)) < 130.0 &&
            chance_check((s16)(1000 - mem_u16(0x8014596C + 2u * (u32)t34)))) {
            ai_set_mode(ai_mode::Strike);
            return;
        }
        if (mem_s16(blk(AI_SPIRIT)) >= 51) {
            ai_set_mode(ai_mode::WalkToEdgeAndClimbOutVariant2);
            return;
        }
    }
    u32 c37 = mem_u16(0x80145978 + 2u * (u32)(s32)mem_s8(kTraits[7].global));
    if (s->flags_ee & 4)
        c37 += 500;
    if (o->state != state::CageHang && (o->state == state::CageTopClimbOver || !chance_check((s16)c37))) {
        ai_set_mode(ai_mode::WalkToEdgeThenCRight);
        return;
    }
    ai_set_mode(mem_s16(blk(AI_SPIRIT)) >= 51 && !ai_mode_recent(ai_mode::HeadForRing, 5) ? ai_mode::WalkToEdgeAndClimbOutVariant2 : ai_mode::WalkToEdgeThenCRight);
}

// N64 0x800FBA08: the CPU up on the cage wall, its opponent in the ring (+0x82 0x4000). At a corner
// of the cage (zone_is_turnbuckle_a) 129; a failed skill roll or 160+ away 124 (go on along the
// wall). Within 180 of the opponent's height and 130 away, an opponent not down (outside 15-20) nor
// busy (flags80 0x05): the trait +37 chance (0x801459A0) times the +34 factor (0x801459AC) for 132.
// Beyond 130 124; at an opponent starting to climb (state 200) with the CPU below 10 spirit a skill
// roll holds A (0x8000) with no mode change; otherwise 124.
void ai_decide_cage_wall_vs_ring() {
    s32 m = ai_mode::WalkInDirectionVariant1;
    if (zone_is_turnbuckle_a(g_ai_self)) {
        m = ai_mode::WalkToEdgeAndClimbOutVariant3;
    } else if (chance_check(mem_s32(blk(AI_SKILL))) && (f64)mem_f32(blk(AI_DIST_P)) < 160.0) {
        f64 d = (f64)mem_f32(blk(AI_DIST_P));
        s32 so = mem_s32(blk(AI_SIT_OPP));
        if ((f64)mem_f32(blk(AI_HEIGHT_DIFF)) < 180.0 && d < 130.0 && (so >= 21 || so < 15) && !(g_ai_opp->flags80 & 5) &&
            chance_check((s16)(s32)((f32)mem_s16(0x801459A0 + 2u * (u32)(s32)mem_s8(kTraits[7].global)) *
                                    mem_f32(0x801459AC + 4u * (u32)(s32)mem_s8(kTraits[4].global))))) {
            m = ai_mode::StrikeFromCageWall;
        } else if ((f64)mem_f32(blk(AI_DIST_P)) < 130.0 && g_ai_opp->state == state::CageClimbStart && mem_s16(blk(AI_SPIRIT)) < 10 &&
                   chance_check(mem_s32(blk(AI_SKILL)))) {
            mem_w16(blk(AI_HELD), 0x8000);
            return;
        }
    }
    ai_set_mode(m);
}

// N64 0x800FBBC0: both up on the cage wall (+0x82 0x8000). After a skill roll (failing: the default
// below): within 130 at an idle opponent (204) the trait +35 chance (0x801459CC) for 7. A climb-off
// chance from +37 (0x801459C0) times the +34 factor (0x801459D4), +300 at a climber (196) or a
// hanging opponent with 51+ spirit, -150 below 20. On the same ring side with that chance within 250:
// within 140 and 180 of its height 132; outside 20-80 spirit: within 180 of its height 126, else 124
// below the opponent or 125. On another side, below a non-climbing opponent: 130 at the other cage
// corner kind (zone_is_turnbuckle_b), else 125. Default: 129 at a cage corner, else 124.
void ai_decide_both_cage_wall() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    s32 m = -1;
    if (chance_check(mem_s32(blk(AI_SKILL)))) {
        if ((f64)mem_f32(blk(AI_DIST_P)) < 130.0 && o->state == state::CageWallStrike &&
            chance_check(mem_s16(0x801459CC + 2u * (u32)(s32)mem_s8(kTraits[5].global)))) {
            ai_set_mode(ai_mode::ShortBlockHold);
            return;
        }
        s32 base = (s32)((f32)mem_s16(0x801459C0 + 2u * (u32)(s32)mem_s8(kTraits[7].global)) *
                         mem_f32(0x801459D4 + 4u * (u32)(s32)mem_s8(kTraits[4].global)));
        s32 c = base;
        if (o->state == state::CageTopClimbOver) {
            c = base + 300;
        } else if (o->state == state::CageHang) {
            s32 sp = mem_s16(blk(AI_SPIRIT));
            if (sp >= 51)
                c = base + 300;
            if (sp < 20)
                c -= 150;
        }
        if (s->ring_zone == o->ring_zone) {
            f64 d = (f64)mem_f32(blk(AI_DIST_P));
            if (chance_check((s16)c) && d < 250.0) {
                if (d < 140.0 && (f64)mem_f32(blk(AI_HEIGHT_DIFF)) < 180.0) {
                    m = ai_mode::StrikeFromCageWall;
                } else if ((u32)(mem_u16(blk(AI_SPIRIT)) - 20) >= 61) {
                    if (!((f64)mem_f32(blk(AI_HEIGHT_DIFF)) > 180.0))
                        m = ai_mode::WalkInDirectionVariant3;
                    else
                        m = s->point_120.y < o->point_120.y ? ai_mode::WalkInDirectionVariant1 : ai_mode::WalkInDirectionVariant2;
                }
            }
        } else if (o->state != state::CageTopClimbOver && s->point_120.y < o->point_120.y) {
            m = zone_is_turnbuckle_b(s) ? ai_mode::CUpWithDpadDown : ai_mode::WalkInDirectionVariant2;
        }
    }
    if (m < 0)
        m = zone_is_turnbuckle_a(s) ? ai_mode::WalkToEdgeAndClimbOutVariant3 : ai_mode::WalkInDirectionVariant1;
    ai_set_mode(m);
}

}  // namespace

namespace {

// N64 0x800FC5F4: both on a raised place (location 0x3C0, e.g. the announce table). In finisher mode
// within 130 the grapple (2), else walk up (48); otherwise an armed runner, against a finisher-mode
// opponent ai_decide_change_area, else the standing tree.
void ai_decide_both_raised() {
    if (g_ai_self->flags_ee & 4) {
        ai_set_mode((f64)mem_f32(blk(AI_DIST_P)) <= 130.0 ? ai_mode::GrappleApproach : ai_mode::WalkToDownedOpponent);
        return;
    }
    if (ai_vs_runner_armed())
        return;
    if ((g_ai_opp->flags_ee & 4) && ai_decide_change_area())
        return;
    ai_decide_standing();
}

// N64 0x800FC6A8: only the CPU on a raised place. As ai_decide_both_raised with mode 58 for the
// approach in finisher mode.
void ai_decide_self_raised() {
    if (g_ai_self->flags_ee & 4) {
        ai_set_mode((f64)mem_f32(blk(AI_DIST_P)) <= 130.0 ? ai_mode::GrappleApproach : ai_mode::WalkOutOfCornerAlternate);
        return;
    }
    if (ai_vs_runner_armed())
        return;
    if ((g_ai_opp->flags_ee & 4) && ai_decide_change_area())
        return;
    ai_decide_standing();
}

// N64 0x800FC75C: only the opponent on a raised place. The same as ai_decide_self_raised.
void ai_decide_opponent_raised() {
    if (g_ai_self->flags_ee & 4) {
        ai_set_mode((f64)mem_f32(blk(AI_DIST_P)) <= 130.0 ? ai_mode::GrappleApproach : ai_mode::WalkOutOfCornerAlternate);
        return;
    }
    if (ai_vs_runner_armed())
        return;
    if ((g_ai_opp->flags_ee & 4) && ai_decide_change_area())
        return;
    ai_decide_standing();
}

// N64 0x800FCA9C: 1 when any wrestler in use (slot >= 0) is on a ladder in the ring (location 0x08
// with 0x01).
s32 ai_ladder_climbers() {
    s32 mask = 0;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->slot >= 0 && (w->location & 8) && (w->location & 1))
            mask |= 1 << i;
    }
    return mask > 0 ? 1 : 0;
}

// N64 0x800FC810: the ladder match's extra decision (g_match_type 0x100000, after the where-class
// deciders). The prize is g_objects[6] (0x8016B248: holder +0x08, flags +0x20, pos +0x44). Against an opponent down, dazed or lying (15,
// 16, 19) or in situation 24: a chance of 10 x 0x80145A00 by the opponent's perceived health / 50 (at
// most 5), not after 19 in the last 5 modes: holding the prize in the ring 134; with the prize loose:
// hanging over the ring (within 30 of the centre line, flags 0x100) 135 once the opponent has been
// down 60+ frames; elsewhere, in another ring zone with the CPU out of the ring, 50% 33 / 34; else
// 133. Otherwise against an opponent on a ladder in the ring: from the ring 139, from the apron 33,
// else 34; with someone on a ladder in the ring ai_press_focus_switch.
void ai_think() {
    const Wrestler *s = g_ai_self;
    const u32 prize = 0x8016B248;  // g_objects[6]
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if (so == 15 || so == 16 || so == 19 || so == 24) {
        s32 k = (s16)(mem_s16(blk(AI_OPP_HEALTH)) / 50);
        if (k >= 6)
            k = 5;
        if (!chance_check(10 * mem_u8(0x80145A00 + (u32)k)) || ai_mode_recent(ai_mode::WalkToDownedOpponentAndPickUp, 5))
            return;
        s32 holder = mem_s32(prize + 8);
        if (holder == s->slot && (s->location & 1)) {
            ai_set_mode(ai_mode::DragOrFlipDownedOpponent);
            return;
        }
        if (holder != -1)
            return;
        Vec3 pos = {mem_f32(prize + 0x44), mem_f32(prize + 0x48), mem_f32(prize + 0x4C)};
        Vec3 axis = {0.0f, pos.y, 0.0f};
        if (!((f64)dist_to(&axis, &pos) > 30.0) && (mem_u16(prize + 0x20) & 0x100)) {
            if (mem_s16(blk(AI_OPP_DOWN)) >= 60)
                ai_set_mode(ai_mode::GoToLadderAndClimb);
            return;
        }
        if (mem_s16(blk(AI_ZONE_SELF)) != ring_zone_of(&pos) && !(s->location & 1))
            ai_set_mode(chance_check(500) ? ai_mode::HeadForRing : ai_mode::HeadForRingAlternate);
        else
            ai_set_mode(ai_mode::HeadForLadder);
        return;
    }
    u16 ol = g_ai_opp->location;
    if (ol & 8) {
        if (!(ol & 1))
            return;
        if (s->location & 1)
            ai_set_mode(ai_mode::GoForFreeLadder);
        else if (s->location & 2)
            ai_set_mode(ai_mode::HeadForRing);
        else
            ai_set_mode(ai_mode::HeadForRingAlternate);
        return;
    }
    if (ai_ladder_climbers())
        ai_press_focus_switch();
}

}  // namespace

namespace {

// N64 0x800FBF54: the decision used instead of the where-class deciders backstage (g_arena 10 or
// more). The tag decider and the tag-out wish (61) first. With where-class 0x02: AI flags 0x40 beyond
// 170 run (10); outside the over-the-top rules 20% the trait +38 chance (0x801459F4) to leave (36,
// unless 33 / 34 recently), else beyond 170 10. Then 25 / 26 ai_decide_wait_or_engage, 20 / 30
// ai_decide_in_ring_main, the ropes (38); against 40 beyond 170 ai_try_grapple(1), within 130 the trait
// +37 chance (0x801459E8) for 9 else 1, between 48; with where-class 0x02 in finisher mode within 130
// the grapple (2), else 48. Then an armed runner; then a helper check: someone (any other wrestler
// outside team / tag types, else the partner) in state 80 with flags78 0x04: on the CPU's opponent
// within 160 the grapple (1); on someone else within 160 (the partner itself, or the other's target
// sharing the CPU's place) a skill roll (halved against an opponent not down) presses C-right
// (ai_press_focus_switch). Else ai_decide_vs_down, ai_decide_change_area against a finisher-mode
// opponent, and the standing tree.
void ai_decide_backstage() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if (ai_decide_tag())
        return;
    if (ai_wants_tag_out(s)) {
        ai_set_mode(ai_mode::ApronWalkTowardPartner);
        return;
    }
    if (mem_u16(blk(AI_WHERE)) & 2) {
        if ((mem_u8(blk(AI_FLAGS)) & 0x40) && (f64)mem_f32(blk(AI_DIST_P)) > 170.0) {
            ai_set_mode(ai_mode::EvadeAndCounter);
            return;
        }
        if (!(g_match_rules & 0x400) && chance_check(200)) {
            if (chance_check(mem_s16(0x801459F4 + 2u * (u32)(s32)mem_s8(kTraits[8].global))) &&
                !ai_mode_recent(ai_mode::HeadForRing, 10) && !ai_mode_recent(ai_mode::HeadForRingAlternate, 10)) {
                ai_set_mode(ai_mode::LeaveRingThroughRopes);
                return;
            }
            if ((f64)mem_f32(blk(AI_DIST_P)) > 170.0) {
                ai_set_mode(ai_mode::EvadeAndCounter);
                return;
            }
        }
    }
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if ((u32)(so - 25) < 2) {
        ai_decide_wait_or_engage();
        return;
    }
    if (so == 20 || so == 30) {
        ai_decide_in_ring_main();
        return;
    }
    if (so == 38 && ai_decide_ropes())
        return;
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    if (mem_s32(blk(AI_SIT_OPP)) == 40) {
        if (d > 170.0)
            ai_try_grapple(1);
        else if (d < 130.0)
            ai_set_mode(chance_check(mem_s16(0x801459E8 + 2u * (u32)(s32)mem_s8(kTraits[7].global))) ? ai_mode::StrikeThenFollowUp : ai_mode::EngageNearbyOpponent);
        else
            ai_set_mode(ai_mode::WalkToDownedOpponent);
        return;
    }
    if ((mem_u16(blk(AI_WHERE)) & 2) && (s->flags_ee & 4)) {
        ai_set_mode(d > 130.0 ? ai_mode::WalkToDownedOpponent : ai_mode::GrappleApproach);
        return;
    }
    if (ai_vs_runner_armed())
        return;
    auto skill_roll = []() {
        s32 k = mem_s32(blk(AI_SIT_OPP));
        s32 c = mem_s32(blk(AI_SKILL));
        return chance_check((k >= 15 && (k < 17 || k == 19)) ? c : c / 2);
    };
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    if (!(g_match_type & 0x3000)) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if ((w->flags_ee & s->flags_ee & 4) || w->roster_id == -1 || w->slot == s->slot || (w->end_flags & 1) ||
                w->state != state::GrappleAttempt || !(w->flags78 & 4))
                continue;
            if (w->opponent == s->opponent) {
                if ((s->location & o->location & 0x7FF) && (f64)dist_to(&sp, &op) < 160.0) {
                    ai_set_mode(ai_mode::EngageNearbyOpponent);
                    return;
                }
                continue;
            }
            const Wrestler *t = &g_wrestlers[w->opponent];
            if ((f64)dist_to(&sp, &t->point_120) < 160.0 && (s->location & t->location & 0x7FF) && skill_roll()) {
                ai_press_focus_switch();
                return;
            }
        }
    } else {
        s32 k = 0;
        if (g_match_type & 0x1000)
            k = (s16)(s->slot ^ 2);
        if (g_match_type & 0x2000)
            k = g_wrestlers[s->partner].slot;
        const Wrestler *p = &g_wrestlers[k];
        if (p->roster_id != -1 && (p->location & 1) && p->state == state::GrappleAttempt && (p->flags78 & 4) &&
            !(p->flags_ee & s->flags_ee & 4)) {
            if (s->opponent == p->opponent) {
                if ((f64)dist_to(&sp, &op) < 160.0) {
                    ai_set_mode(ai_mode::EngageNearbyOpponent);
                    return;
                }
            } else if ((f64)dist_to(&sp, &p->point_120) < 160.0 && skill_roll()) {
                ai_press_focus_switch();
                return;
            }
        }
    }
    if (ai_decide_vs_down())
        return;
    if ((o->flags_ee & 4) && ai_decide_change_area())
        return;
    ai_decide_standing();
}

}  // namespace

namespace {

// N64 0x800FD0F0: ai_set_mode with an argument for the mode in AI block +0x86 (and without the mode
// flag 0x01 that ai_set_mode sets).
void ai_set_mode_arg(s32 mode, s16 arg) {
    mem_w32(blk(AI_MODE), (u32)mode);
    mem_w16(blk(AI_RETURN_MODE), (u16)arg);
    mem_w32(blk(AI_STEP), 0);
    mem_w32(blk(AI_TIMER), 0);
    mem_w16(blk(AI_OPP_STATE), (u16)g_ai_opp->state);
    mem_w8(blk(AI_FLAGS), mem_u8(blk(AI_FLAGS)) & 0xFC);
}

// N64 0x800F9C9C: 1 when another loaded wrestler (not the CPU nor its opponent) going after the same
// opponent is in state 10, 97-98, 113-119 or 121-123 (a run-in, a tag or interference).
s32 ai_third_party_active() {
    const Wrestler *s = g_ai_self;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || w->slot == s->slot || w->slot == g_ai_opp->slot || w->opponent != s->opponent)
            continue;
        s32 st = w->state;
        if (st == state::TurnbucklePerch || st == state::BareHandedGroundAttack || st == state::RunningGroundAttack || (st >= 113 && st <= 119) || (st >= 121 && st <= 123))
            return 1;
    }
    return 0;
}

// N64 0x800F9C6C: mode 19 (pick the opponent up) unless a third party is busy with it; 1 when set.
s32 ai_try_pick_up() {
    if (ai_third_party_active())
        return 0;
    ai_set_mode(ai_mode::WalkToDownedOpponentAndPickUp);
    return 1;
}

// N64 0x800F96CC: the slot's second CPU list (count 0x801594E2 + 12 x slot, entries 0x8016B940 + 32 x
// slot: moves 209-216, the strikes on a downed opponent); 1 when it set a mode. A favourite at 40%
// (always when there is nothing else), else another entry: 209 / 210 / 213 / 214 mode 22 (or walk
// up, 48, beyond the perceived reach +0x7C), 211 / 212 / 215 / 216 mode 77.
s32 ai_list_pick_a(s32 slot) {
    s32 count = mem_s16(0x801594E2 + 12u * (u32)slot);
    s32 fav[8], rest[8];
    s32 nf = ai_list_filter(count, fav, rest, 0x8016B940 + 32u * (u32)slot);
    s32 nr = count - nf;
    auto act = [](s32 move) -> s32 {
        switch (move) {
        case 209: case 210: case 213: case 214:
            ai_set_mode(mem_f32(blk(AI_BODY_DIST)) < mem_f32(blk(AI_DIST)) ? ai_mode::WalkToDownedOpponent : ai_mode::StrikeDownedOpponent);
            return 1;
        case 211: case 212: case 215: case 216:
            ai_set_mode(ai_mode::CLeftOrStrikeAtDownedOpponentVariant1);
            return 1;
        default:
            return 0;
        }
    };
    if (nf != 0 && chance_check(nr == 0 ? 1000 : 400) && act(fav[rand_pool_next() % nf]))
        return 1;
    if (nr <= 0)
        return 0;
    return act(rest[rand_pool_next() % nr]);
}

// N64 0x800F9894: the slot's first CPU list (count 0x801594E0 + 12 x slot, entries 0x8016B658 + 48 x
// slot: the ground grapples 236-250 and 155-158); 1 when it set a mode. Not while a third party is
// busy; beyond the perceived reach walk up (48). Only the entries 236+ count (all in finisher mode);
// 40% a favourite among them looked up in the pairs at 0x8014582C (move, arg) starts mode 110 with
// that arg; otherwise the ground grapple (23).
s32 ai_list_pick_b(s32 slot) {
    if (ai_third_party_active())
        return 0;
    if (mem_f32(blk(AI_BODY_DIST)) < mem_f32(blk(AI_DIST))) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return 1;
    }
    u32 list = 0x8016B658 + 48u * (u32)slot;
    u32 count_at = 0x801594E0 + 12u * (u32)slot;
    s32 n;
    if (g_ai_self->flags_ee & 4) {
        n = mem_s16(count_at);
    } else {
        n = 0;
        for (s32 i = 0; i < mem_s16(count_at); i++)
            n += mem_s16(list + 4u * (u32)i) >= 236;
    }
    s32 fav[12], rest[12];
    s32 nf = ai_list_filter(n, fav, rest, list);
    if (chance_check(400) && nf != 0) {
        s32 move = fav[rand_pool_next() % nf];
        for (s32 i = 0; i < n; i++) {
            if (move == mem_s16(0x8014582C + 4u * (u32)i)) {
                ai_set_mode_arg(ai_mode::CLeftTurningAway, mem_s16(0x8014582E + 4u * (u32)i));
                return 1;
            }
        }
    }
    ai_set_mode(ai_mode::GrappleUnlessPickUpMove);
    return 1;
}

// N64 0x800FD5FC: how far a point is from ring side 0-3 (the ring edges at +-230): 230 - z, 230 - x,
// z + 230, x + 230; 230 for any other side.
f32 ai_side_distance(s32 side, const Vec3 *p) {
    switch (side) {
    case 0: return (f32)(230.0 - (f64)p->z);
    case 1: return (f32)(230.0 - (f64)p->x);
    case 2: return (f32)((f64)p->z + 230.0);
    case 3: return (f32)((f64)p->x + 230.0);
    default: return 230.0f;
    }
}

// N64 0x800F847C: 1 when the ring side beyond the opponent (the CPU's direction to it / 2, ^ 2) is
// more than 60 + 7 x walk_speed and under 250 from the CPU: out (when given) gets the side and the
// frames to walk there ((distance - 60) / speed, as s8).
s32 ai_side_target(s32 *out) {
    f32 speed = walk_speed(g_ai_self, 8);
    f32 lo = (f32)((f64)(speed * 7.0f) + 60.0);
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    s32 side = (dir8_to(&sp, &op) / 2) ^ 2;
    f32 d = ai_side_distance(side, &sp);
    if (!(lo < d) || !((f64)d < 250.0))
        return 0;
    if (out) {
        out[0] = side;
        out[1] = (s8)(s32)(((f64)d - 60.0) / (f64)speed);
    }
    return 1;
}

// N64 0x800F8594: 1 when the CPU can line up with the opponent against a ring side (mode 89): the
// side beyond the opponent is more than 60 + 7 x walk_speed away, and from the CPU's spot on that
// side's edge (x or z at +-230, y 0) the opponent lies within 227 of the side's direction (0x8014D1E8)
// and within the list block's reach (+0x68) + 50. out (when given) gets the side and the frames to
// walk there.
s32 ai_line_up(s32 *out) {
    f32 speed = walk_speed(g_ai_self, 8);
    f32 lo = (f32)((f64)(speed * 7.0f) + 60.0);
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    s32 side = (dir8_to(&sp, &op) / 2) ^ 2;
    f32 d = ai_side_distance(side, &sp);
    if (!(lo < d))
        return 0;
    Vec3 edge = {0.0f, 0.0f, 0.0f};
    switch (side) {
    case 0: edge.x = sp.x; edge.z = 230.0f; break;
    case 1: edge.x = 230.0f; edge.z = sp.z; break;
    case 2: edge.x = sp.x; edge.z = -230.0f; break;
    default: edge.x = -230.0f; edge.z = sp.z; break;
    }
    if (abs_i(angle_diff(angle_to(&edge, &op), mem_s32(0x8014D1E8 + 4u * (u32)side))) >= 227)
        return 0;
    if (!((f64)dist_to(&edge, &op) < (f64)mem_f32(mem_u32(blk(AI_LISTS)) + 0x68) + 50.0))
        return 0;
    if (out) {
        out[0] = side;
        out[1] = (s32)(((f64)d - 60.0) / (f64)speed);
    }
    return 1;
}

// N64 0x800F9D70: in the ring against an opponent down (15) longer than a time, with a move-set bit:
// kind 1 mode 91 (45 frames, attrs +0x38 0x2000), kind 2 mode 67 (30, 0x40000), kind 0 mode 92 (60,
// 0x4000); 1 when set.
s32 ai_try_ground_move(s32 kind) {
    s32 t, m;
    u32 bit;
    switch (kind) {
    case 1: t = 45; bit = 0x2000; m = ai_mode::WalkToOpponentAndGrappleAcrossRopes; break;
    case 2: t = 30; bit = 0x40000; m = ai_mode::RunAtCarriedOpponent; break;
    case 0: t = 60; bit = 0x4000; m = ai_mode::GoToCorner; break;
    default: return 0;
    }
    if (!(mem_u16(blk(AI_WHERE)) & 1) || mem_s32(blk(AI_SIT_OPP)) != 15 || !(t < mem_s16(blk(AI_OPP_DOWN))) ||
        !(mem_u32(g_ai_self->attrs + 0x38) & bit))
        return 0;
    ai_set_mode(m);
    return 1;
}

// N64 0x800FAB0C: mode 67 against a dazed opponent (16) down 31+ frames, in the ring or on the
// apron side (where-class 0x09), with the move-set bit attrs +0x38 0x20000 (or in finisher mode
// +0x3C 0x8000000); 1 when set.
s32 ai_try_run_at_dazed_opponent() {
    const Wrestler *s = g_ai_self;
    if (!(mem_u16(blk(AI_WHERE)) & 9) || mem_s32(blk(AI_SIT_OPP)) != 16 || mem_s16(blk(AI_OPP_DOWN)) < 31)
        return 0;
    if (!((s->flags_ee & 4) && (mem_u16(s->attrs + 0x3C) & 0x800)) && !(mem_u32(s->attrs + 0x38) & 0x20000))
        return 0;
    ai_set_mode(ai_mode::RunAtCarriedOpponent);
    return 1;
}

// N64 0x800F66B8: mode 89 (run the opponent against the ropes side) in the ring when the CPU lines
// up (ai_line_up) with the move-set bit attrs +0x38 0x80000: against a lying opponent (19) after a
// skill roll, or one down longer than the trait +37 time (0x80145574); 1 when set.
s32 ai_try_line_up_on_lying_opponent() {
    const Wrestler *s = g_ai_self;
    if (!(mem_u16(blk(AI_WHERE)) & 1) || !(u8)ai_line_up(nullptr))
        return 0;
    bool lying = mem_s32(blk(AI_SIT_OPP)) == 19 && chance_check(mem_s32(blk(AI_SKILL))) &&
                 (mem_u32(s->attrs + 0x38) & 0x80000);
    bool long_down = mem_s16(0x80145574 + 2u * (u32)(s32)mem_s8(kTraits[7].global)) < mem_s16(blk(AI_OPP_DOWN)) &&
                     (mem_u32(s->attrs + 0x38) & 0x80000);
    if (!lying && !long_down)
        return 0;
    ai_set_mode(ai_mode::LineUpOnLyingOpponent);
    return 1;
}

// N64 0x800F9A70: the slot's third CPU list (count 0x801594E4 + 12 x slot, entries 0x80159630 + 16 x
// slot: 261, 269, 270, 274), not at an opponent on the floor; 1 when it acted. A favourite at 50%,
// else another: 269 ai_try_ground_move(1), 270 (0), 274 (2), 261 the corner climb.
s32 ai_list_pick_c(s32 slot) {
    if (g_ai_opp->location & 4)
        return 0;
    s32 count = mem_s16(0x801594E4 + 12u * (u32)slot);
    if (count == 0)
        return 0;
    s32 fav[4], rest[4];
    s32 nf = ai_list_filter(count, fav, rest, 0x80159630 + 16u * (u32)slot);
    s32 nr = count - nf;
    auto act = [](s32 move) -> s32 {
        switch (move) {
        case 269: return ai_try_ground_move(1);
        case 270: return ai_try_ground_move(0);
        case 274: return ai_try_ground_move(2);
        case 261: return ai_try_corner_climb();
        default: return 0;
        }
    };
    if (nf != 0 && chance_check(500) && act(fav[rand_pool_next() % nf]))
        return 1;
    if (nr <= 0)
        return 0;
    return act(rest[rand_pool_next() % nr]) ? 1 : 0;
}

// N64 0x800FA920: the slot's fourth CPU list (count 0x801594E6 + 12 x slot, entries 0x8016B620 + 12 x
// slot: 256, 273, 278), not at an opponent on the floor; 1 when it acted. A favourite at 50%, else
// another: 273 ai_try_run_at_dazed_opponent, 278 ai_try_line_up_on_lying_opponent, 256 the corner climb.
s32 ai_list_pick_d(s32 slot) {
    if (g_ai_opp->location & 4)
        return 0;
    s32 count = mem_s16(0x801594E6 + 12u * (u32)slot);
    if (count == 0)
        return 0;
    s32 fav[3], rest[3];
    s32 nf = ai_list_filter(count, fav, rest, 0x8016B620 + 12u * (u32)slot);
    s32 nr = count - nf;
    auto act = [](s32 move) -> s32 {
        switch (move) {
        case 273: return ai_try_run_at_dazed_opponent();
        case 278: return ai_try_line_up_on_lying_opponent();
        case 256: return ai_try_corner_climb();
        default: return 0;
        }
    };
    if (nf != 0 && chance_check(500) && act(fav[rand_pool_next() % nf]))
        return 1;
    if (nr <= 0)
        return 0;
    return act(rest[rand_pool_next() % nr]) ? 1 : 0;
}

// N64 0x800F95A0: whether the finisher fits a downed opponent (flags80 0x08), 1 on a passed roll. The
// kind: face down (flags8a 0x01) 3 / 7 with the CPU within 1024 of the opponent's facing (AI +0x64),
// else 1 / 5; face up 0 / 4 within, else 2 / 6 (the higher with the opponent sitting, flags80 0x10).
// The move for the kind (0x8014581C) must have the move-set flag 0x200: a roll of 500 (1000 with
// 0x8000).
s32 ai_finisher_kind() {
    const Wrestler *o = g_ai_opp;
    if (!(o->flags80 & 8))
        return 0;
    bool facing = abs_i(angle_diff(mem_s32(blk(AI_ANGLE)), o->facing)) < 1024;
    bool sit = (o->flags80 & 0x10) != 0;
    s32 k;
    if (o->flags8a & 1)
        k = facing ? (sit ? 7 : 3) : (sit ? 5 : 1);
    else
        k = facing ? (sit ? 4 : 0) : (sit ? 6 : 2);
    s32 move = mem_s16(0x8014581C + 2u * (u32)k);
    u32 f = mem_u32(mem_u32(blk(AI_MOVESET)) + 36u * (u32)move + 0x20);
    if (!(f & 0x200))
        return 0;
    return chance_check((f & 0x8000) ? 1000 : 500) ? 1 : 0;
}

// N64 0x800F9288: whether the CPU goes for its finisher (1 when it set a mode): in the ring (or on
// the floor under rule 0x4000), with rule 0x20, both legal and the opponent's perceived spirit under
// 81. Armed for it (flags8c 0x04, mode 116, or below 30 opponent health with flags8c 0x02): mode 24
// when ai_finisher_kind says it fits, else mode 110 with arg 8. Otherwise below 40 opponent health a
// roll of 100 (200 with time limit option 1) x match minutes, the opponent down 90+ frames, its
// finisher heat 30+ or in the TKO check (46), then a roll of 20 x heat + 2 x (15 or 25) x minutes
// (x1.5 with flags8c 0x01, again with the finish plan and flags8c 0x02) for the same choice.
s32 ai_finisher_check() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    u16 where = mem_u16(blk(AI_WHERE));
    if (!(where & 0x101) || (!(where & 1) && !(g_match_rules & 0x4000)) || !(g_match_rules & 0x20) ||
        !(s->flags_e9 & 0x10) || !(o->flags_e9 & 0x10) || mem_s16(blk(AI_OPP_SPIRIT)) >= 81)
        return 0;
    bool armed = (s->flags8c & 4) || mem_s32(blk(AI_MODE)) == ai_mode::RunOffRopes;
    if (mem_s16(blk(AI_OPP_HEALTH)) < 30 && (s->flags8c & 2))
        armed = true;
    if (!armed) {
        if (mem_s16(blk(AI_OPP_HEALTH)) >= 40)
            return 0;
        bool tl1 = mem_s8(A_TIME_LIMIT) == 1;
        s32 minutes = g_match_clock[0];
        if (!chance_check((tl1 ? 200 : 100) * minutes))
            return 0;
        if (!((f32)mem_s16(blk(AI_OPP_DOWN)) > 90.0f) && o->finisher_heat < 30 && o->state != state::TechnicalKnockoutStagger)
            return 0;
        s32 c = 20 * o->finisher_heat + 2 * ((tl1 ? 25 : 15) * minutes);
        if (s->flags8c & 1)
            c = (s32)((f64)c * 1.5);
        if (mem_u8(0x8016B718 + (u32)s->slot) && (s->flags8c & 2))
            c = (s32)((f64)c * 1.5);
        if (!chance_check(c))
            return 0;
    }
    if (ai_finisher_kind())
        ai_set_mode(ai_mode::Grapple);
    else
        ai_set_mode_arg(ai_mode::CLeftTurningAway, 8);
    return 1;
}

}  // namespace

namespace {

// N64 0x800FA5E0: at a downed opponent, the far choice (1 when it set a mode, except mode 6). Lying
// (19) 55. Beyond 200 a 60% roll and the trait +37 chance (0x80145864) to run at it (10). Then 6 or 9:
// healthy (100+) 50 / 50; hurt, in the ring, 25% a favourite of moves 192 / 194 / 196 / 198 (move-set
// flag 0x8000) for modes 11 / 12 / 13 / 14, else 90% 9. 9: beyond 200 walk up (48). 6: beyond 200 48,
// else mode 6, returning 0.
s32 ai_decide_vs_down_far(s32 slot) {
    (void)slot;
    if (mem_s32(blk(AI_SIT_OPP)) == 19) {
        ai_set_mode(ai_mode::LongWait);
        return 1;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 200.0 && chance_check(600) &&
        chance_check(mem_s16(0x80145864 + 2u * (u32)(s32)mem_s8(kTraits[7].global)))) {
        ai_set_mode(ai_mode::EvadeAndCounter);
        return 1;
    }
    s32 v;
    if (mem_s16(blk(AI_OPP_HEALTH)) >= 100) {
        v = chance_check(500) ? 9 : 6;
    } else {
        if ((mem_u16(blk(AI_WHERE)) & 1) && chance_check(250)) {
            u32 ms = mem_u32(blk(AI_MOVESET));
            static const s32 kMoves[4] = {192, 194, 196, 198};
            for (s32 i = 0; i < 4; i++) {
                if (mem_u32(ms + 36u * (u32)kMoves[i] + 0x20) & 0x8000) {
                    ai_set_mode(ai_mode::SideStepAndRun + i);
                    return 1;
                }
            }
        }
        v = chance_check(900) ? 9 : 6;
    }
    if (v == 9) {
        ai_set_mode((f64)mem_f32(blk(AI_DIST_P)) > 200.0 ? ai_mode::WalkToDownedOpponent : ai_mode::StrikeThenFollowUp);
        return 1;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 200.0) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return 1;
    }
    ai_set_mode(ai_mode::Strike);
    return 0;
}

// N64 0x800FA7C8: at a downed opponent, the near choice (always sets a mode; 1). Lying (19) 55.
// Healthy: 50 / 50 modes 1 / 2; hurt: in the ring 25% a favourite move 104 / 105 for modes 4 / 5,
// else 90% 2. Mode 1 beyond 160, mode 2 beyond 130: walk up (48).
s32 ai_decide_vs_down_near(s32 slot) {
    (void)slot;
    if (mem_s32(blk(AI_SIT_OPP)) == 19) {
        ai_set_mode(ai_mode::LongWait);
        return 1;
    }
    s32 v;
    if (mem_s16(blk(AI_OPP_HEALTH)) >= 100) {
        v = chance_check(500) ? 2 : 1;
    } else {
        if ((mem_u16(blk(AI_WHERE)) & 1) && chance_check(250)) {
            u32 ms = mem_u32(blk(AI_MOVESET));
            if (mem_u32(ms + 36u * 104u + 0x20) & 0x8000) {
                ai_set_mode(ai_mode::LineUpAndRunIn);
                return 1;
            }
            if (mem_u32(ms + 36u * 105u + 0x20) & 0x8000) {
                ai_set_mode(ai_mode::LineUpAndRunInAlternate);
                return 1;
            }
        }
        v = chance_check(900) ? 2 : 1;
    }
    f64 d = (f64)mem_f32(blk(AI_DIST_P));
    ai_set_mode(d > (v == 1 ? 160.0 : 130.0) ? ai_mode::WalkToDownedOpponent : v);
    return 1;
}

// N64 0x800F9FB4: at a lying or dazed opponent, after a skill roll. A choice 0-2: 40% the trait +25
// (opponent health 100+) or +26 level, else random; on the floor 2 becomes 0 or 1. In finisher mode:
// with 301+ frames of the mode left, choice 2 and the ground special (attrs +0x3C 0x2000000) a roll of
// 200 (450 with move 257 a favourite) for mode 65; else beyond 130 walk up (48), lying (19) 55, else
// the grapple (2; chance_check(0) for 1). Otherwise choice 0 ai_decide_vs_down_far, 1
// ai_decide_vs_down_near, 2 ai_list_pick_d.
void ai_decide_vs_lying_b() {
    Wrestler *s = g_ai_self;
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return;
    s32 v = 0;
    if (mem_s16(blk(AI_OPP_HEALTH)) >= 100)
        v = chance_check(400) ? mem_s8(kTraits[0].global) : rand_pool_next() % 3;
    if (mem_s16(blk(AI_OPP_HEALTH)) < 100)
        v = chance_check(400) ? mem_s8(kTraits[1].global) : rand_pool_next() % 3;
    if ((s->location & 4) && v == 2) {
        s32 r = rand_pool_next() % 2;
        if (r == 0)
            v = 0;
        else if (r == 1)
            v = 1;
    }
    if (s->flags_ee & 4) {
        if (s->spirit_timer >= 301 && v == 2 && (mem_u16(s->attrs + 0x3C) & 0x200) &&
            chance_check((mem_u32(mem_u32(blk(AI_MOVESET)) + 36u * 257u + 0x20) & 0x8000) ? 450 : 200)) {
            ai_set_mode(ai_mode::MeetRunner);
            return;
        }
        if ((f64)mem_f32(blk(AI_DIST)) > 130.0)
            ai_set_mode(ai_mode::WalkToDownedOpponent);
        else if (mem_s32(blk(AI_SIT_OPP)) == 19)
            ai_set_mode(ai_mode::LongWait);
        else
            ai_set_mode(chance_check(0) ? ai_mode::EngageNearbyOpponent : ai_mode::GrappleApproach);
        return;
    }
    switch (v) {
    case 0: ai_decide_vs_down_far(s->slot); break;
    case 1: ai_decide_vs_down_near(s->slot); break;
    case 2: ai_list_pick_d(s->slot); break;
    default: break;
    }
}

// N64 0x800FA26C: the finish plan against a lying or dazed opponent (1 when it acted). Armed: hold R
// (0x10). Lying (19): 55, beyond 130 walk up (48). SPECIAL the taunt (50). Then the plan's move
// (0x8016B580 by slot; none: 0), not with target bit 0x08 outside finisher mode: 257 (finisher mode
// only) and 256 the corner climb, 105 mode 5, 273 and 275 (finisher mode only) ai_try_run_at_dazed_opponent. Else
// beyond 130 48; by the plan's target bits (AI +0x84): 0x1000 with AI flags 0x10 the grapple (1, 2
// with 0x4000) when the opponent faces the CPU, else 100; without the flag move 192 / 194 / 196 /
// 198 / 104 modes 11 / 12 / 13 / 14 / 4, 278 ai_try_line_up_on_lying_opponent. 0x2000 with the flag 1 or 100. 0x100:
// with 0x10 (or faced) 2 / 1 by 0x4000, else 100. 0x200: 9.
s32 ai_decide_vs_lying_c() {
    Wrestler *s = g_ai_self;
    if (s->flags_ef & 1)
        mem_w16(blk(AI_HELD), 16);
    if (mem_s32(blk(AI_SIT_OPP)) == 19) {
        ai_set_mode((f64)mem_f32(blk(AI_DIST)) > 130.0 ? ai_mode::WalkToDownedOpponent : ai_mode::LongWait);
        return 1;
    }
    if (s->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return 1;
    }
    s32 plan = mem_s32(0x8016B580 + 4u * (u32)s->slot);
    if (plan == 0)
        return 0;
    if ((mem_u16(blk(0x84)) & 8) && !(s->flags_ee & 4))
        return 0;
    switch (plan) {
    case 257:
        if (!(s->flags_ee & 4))
            return 0;
        return ai_try_corner_climb() ? 1 : 0;
    case 256:
        return ai_try_corner_climb() ? 1 : 0;
    case 105:
        ai_set_mode(ai_mode::LineUpAndRunInAlternate);
        return 1;
    case 275:
        if (!(s->flags_ee & 4))
            return 0;
        return ai_try_run_at_dazed_opponent() ? 1 : 0;
    case 273:
        return ai_try_run_at_dazed_opponent() ? 1 : 0;
    default:
        break;
    }
    if ((f64)mem_f32(blk(AI_DIST)) > 130.0) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return 1;
    }
    u16 t = mem_u16(blk(0x84));
    bool flag10 = (mem_u8(blk(AI_FLAGS)) & 0x10) != 0;
    s32 m;
    if (t & 0x1000) {
        if (flag10) {
            m = ai_faced_by(s, g_ai_opp) ? ((mem_u16(blk(0x84)) & 0x4000) ? ai_mode::GrappleApproach : ai_mode::EngageNearbyOpponent) : ai_mode::RopesLButtonMove;
        } else {
            switch (mem_s32(0x8016B580 + 4u * (u32)s->slot)) {
            case 194: m = ai_mode::SideStepAndRunAlternate; break;
            case 104: m = ai_mode::LineUpAndRunIn; break;
            case 192: m = ai_mode::SideStepAndRun; break;
            case 198: m = ai_mode::LineUpAndRunInWithStrikeAlternate; break;
            case 196: m = ai_mode::LineUpAndRunInWithStrike; break;
            case 278: return ai_try_line_up_on_lying_opponent() ? 1 : 0;
            default: return 0;
            }
        }
    } else if (t & 0x2000) {
        if (!flag10)
            return 0;
        m = ai_faced_by(s, g_ai_opp) ? ai_mode::EngageNearbyOpponent : ai_mode::RopesLButtonMove;
    } else if (t & 0x100) {
        if (!(t & 0x10) && !ai_faced_by(s, g_ai_opp))
            m = ai_mode::RopesLButtonMove;
        else
            m = (mem_u16(blk(0x84)) & 0x4000) ? ai_mode::GrappleApproach : ai_mode::EngageNearbyOpponent;
    } else if (t & 0x200) {
        m = ai_mode::StrikeThenFollowUp;
    } else {
        return 0;
    }
    ai_set_mode(m);
    return 1;
}

// N64 0x800F8C04: the finish plan in the ring (1 when it acted). Armed: hold R (0x10). SPECIAL the
// taunt (50). ai_finisher_check (skipped in finisher mode with 150+ frames of the mode left). Then
// the plan's move (0x8016B580 by slot; none: 0): with target bits a pick-up (ai_try_pick_up); 262
// (finisher mode only) and 261 the corner climb; 274 ai_try_ground_move(2). Else beyond the
// perceived reach walk up (48); strikes 209-216 modes 73, 74, 78, 79, 75, 76, 80, 81; unless a third
// party is busy, the ground grapples 236-250 (even) mode 110 with arg 82-85 / 96-99 (236 not when
// its animation is style 2843 and the CPU is under 70 spirit).
s32 ai_decide_finisher() {
    Wrestler *s = g_ai_self;
    if (s->flags_ef & 1)
        mem_w16(blk(AI_HELD), 16);
    if (s->flags_ee & 2) {
        ai_set_mode(ai_mode::ApronWaitAndTaunt);
        return 1;
    }
    if (!((s->flags_ee & 4) && s->spirit_timer >= 150) && ai_finisher_check())
        return 1;
    u32 plan_at = 0x8016B580 + 4u * (u32)s->slot;
    if (mem_s32(plan_at) == 0)
        return 0;
    if (mem_u16(blk(0x84)) && ai_try_pick_up())
        return 1;
    switch (mem_s32(plan_at)) {
    case 262:
        if (!(s->flags_ee & 4))
            return 0;
        return ai_try_corner_climb() ? 1 : 0;
    case 261:
        return ai_try_corner_climb() ? 1 : 0;
    case 274:
        return ai_try_ground_move(2) ? 1 : 0;
    default:
        break;
    }
    if (mem_f32(blk(AI_BODY_DIST)) < mem_f32(blk(AI_DIST))) {
        ai_set_mode(ai_mode::WalkToDownedOpponent);
        return 1;
    }
    s32 plan = mem_s32(plan_at);
    if (plan >= 209 && plan <= 216) {
        static const s32 kStrikeModes[8] = {73, 74, 78, 79, 75, 76, 80, 81};
        ai_set_mode(kStrikeModes[plan - 209]);
        return 1;
    }
    if (ai_third_party_active())
        return 0;
    s32 arg;
    switch (mem_s32(plan_at)) {
    case 236: {
        s16 anim = mem_s16(0x8008FA70 + 1096u * (u32)s->slot + 2u * 236u);
        if (mem_s16(0x8015A700 + 2u * (u32)(s32)anim) + 13090 == 15933 && mem_s16(blk(AI_SPIRIT)) < 70)
            return 0;
        arg = 82;
        break;
    }
    case 238: arg = 83; break;
    case 240: arg = 84; break;
    case 242: arg = 85; break;
    case 244: arg = 96; break;
    case 246: arg = 97; break;
    case 248: arg = 98; break;
    case 250: arg = 99; break;
    default: return 0;
    }
    ai_set_mode_arg(ai_mode::CLeftTurningAway, (s16)arg);
    return 1;
}

}  // namespace

namespace {

// N64 0x800F4BAC: whether to switch focus to another wrestler (ai_press_focus_switch, C-right; 1 when
// it pressed). Team play (type 0x1000): the legal man at an opponent neither legal nor in the ring;
// with flags_e9 0x01 and the slot ^ 1 wrestler loaded with flags_e9 / flags_f1 0x02, not the
// opponent, and no partner hold (partner_flags80 0xC0); with flags_e9 0x02 at a non-legal opponent
// down, dazed or lying. Otherwise: type 0x20000 at an opponent with flags_e9 0x02 but not flags_f1
// 0x02. A finished CPU (end_flags 0x01) switches to anyone else unfinished or within 160; at a
// finished opponent, from the ring or beyond 160. Someone else carrying (states 0x80 / 0x82) who is
// not the partner. In types 0xC000: not with one fall left and the opponent unfinished; in 0x4000 an
// opponent in state 163 or with flags_f1 0x01; with AI flags 0x40 (not with where-class 0x02) an
// opponent on the floor. Then in types 0x8C000 a chance from the nearest other free wrestler within
// 160 (1000 unless facing the CPU, 800 when it targets the CPU, else 400; halved against a downed
// opponent), or 1000 for one on the apron within 200 with AI flags 0x40 and the CPU in the ring;
// rolled beyond 160 of the opponent.
s32 ai_tag_partner_check() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    Vec3 sp = ai_self_point();
    if (g_match_type & 0x1000) {
        bool go = false;
        if ((s->flags_e9 & 0x10) && !(o->flags_e9 & 0x10) && !(o->location & 1))
            go = true;
        if (!go && (s->flags_e9 & 1)) {
            s32 k = (s16)(s->slot ^ 1);
            const Wrestler *m = &g_wrestlers[k];
            if (m->roster_id != -1 && (m->flags_e9 & 2) && (m->flags_f1 & 2) && s->opponent != k &&
                !(s->partner_flags80 & 0xC0))
                go = true;
        }
        if (!go) {
            s32 so = mem_s32(blk(AI_SIT_OPP));
            if (!(s->flags_e9 & 2) || (o->flags_e9 & 0x10) || !(so == 15 || so == 16 || so == 19))
                return 0;
        }
        ai_press_focus_switch();
        return 1;
    }
    auto press = []() {
        ai_press_focus_switch();
        return 1;
    };
    if ((g_match_type & 0x20000) && (o->flags_e9 & 2) && !(o->flags_f1 & 2))
        return press();
    if (s->end_flags & 1) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->roster_id == -1 || w->slot == s->slot || w->slot == o->slot)
                continue;
            if ((w->end_flags & 1) || (f64)dist_to(&sp, &w->point_120) < 160.0)
                return press();
        }
        return 0;
    }
    if ((o->end_flags & 1) && (!(s->location & 4) || (f64)mem_f32(blk(AI_DIST_P)) > 160.0))
        return press();
    const Wrestler *partner = &g_wrestlers[s->partner];
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || (w->state != state::RearLiftDefender && w->state != state::CarryDefender))
            continue;
        if (w->slot == s->slot || w->slot == o->slot || w->slot == partner->slot)
            return 0;
        return press();
    }
    if (g_match_type & 0xC000) {
        if (ai_one_fall_left() && !(o->end_flags & 1))
            return 0;
        if ((g_match_type & 0x4000) && (o->state == state::RunIn || (o->flags_f1 & 1)))
            return press();
        if (mem_u8(blk(AI_FLAGS)) & 0x40) {
            if (mem_u16(blk(AI_WHERE)) & 2)
                return 0;
            if (o->location & 4)
                return press();
        }
    }
    if (!(g_match_type & 0x8C000))
        return 0;
    s32 c = 0;
    for (s32 i = 0; i < 4 && c == 0; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || w->slot == s->slot || w->slot == o->slot || (w->flags_e9 & 4) ||
            (w->flags80 & 0xC0))
            continue;
        if (w->state != state::RunIn && !(w->flags_f1 & 1) && (f64)dist_to(&sp, &w->point_120) < 160.0) {
            if (ai_faced_by(s, w))
                c = (w->opponent == s->slot) ? 800 : 400;
            else
                c = 1000;
            s32 so = mem_s32(blk(AI_SIT_OPP));
            if (so == 15 || so == 16 || so == 19)
                c /= 2;
        }
        if ((f64)dist_to(&sp, &w->point_120) < 200.0 && (mem_u8(blk(AI_FLAGS)) & 0x40) && (s->location & 1) &&
            (w->location & 2))
            c = 1000;
    }
    if ((f64)mem_f32(blk(AI_DIST_P)) > 160.0 && chance_check(c))
        return press();
    return 0;
}

// N64 0x800F51E4: whether to switch focus (ai_press_focus_switch; 1 when it pressed) to help in a
// tag match (type 0x2000), not while the opponent carries someone (0x80 / 0x82). Without the free-for-
// all tag rule (0x40): the CPU on its apron, not legal: at a legal opponent in the ring with the
// opponent's partner on the same apron side within 160 (and no partner hold); at a non-legal opponent
// unless it stands on the same apron side within 160 (then only with the partner hold). The CPU in
// the ring: legal, at an opponent on the floor while its partner is not, or a non-legal one; at a
// non-legal opponent off the ring unless its partner is on the floor; in the ring when its partner
// is too. Not legal, at a legal opponent on the floor with its partner in the ring. Then (and under
// rule 0x40) after a skill roll, in the ring with no partner hold and the opponent's partner off the
// floor, beyond 160 of the opponent: a chance from the opponent's partner within 160 (700 unless it
// faces the CPU, 500 when it targets the CPU, else 250).
s32 ai_tag_help_check() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    const Wrestler *op = &g_wrestlers[o->partner];
    if (!(g_match_type & 0x2000) || o->state == state::RearLiftDefender || o->state == state::CarryDefender)
        return 0;
    Vec3 sp = ai_self_point();
    auto press = []() {
        ai_press_focus_switch();
        return 1;
    };
    if (!(g_match_rules & 0x40)) {
        if ((s->location & 2) && !(s->flags_e9 & 0x10)) {
            if (o->flags_e9 & 0x10) {
                if ((o->location & 1) && !(s->partner_flags80 & 0xC0) && (op->location & 2) &&
                    s->ring_zone == op->ring_zone && (f64)dist_to(&sp, &op->point_120) < 160.0)
                    return press();
            } else {
                if (!(o->location & 2) || s->ring_zone != o->ring_zone || !((f64)mem_f32(blk(AI_DIST_P)) < 160.0))
                    return press();
                return (s->partner_flags80 & 0xC0) ? press() : 0;
            }
        }
        if (s->location & 1) {
            if (s->flags_e9 & 0x10) {
                if (o->location & 4) {
                    if (!(op->location & 4) || !(o->flags_e9 & 0x10))
                        return press();
                    return 0;
                }
                if (!(o->flags_e9 & 0x10)) {
                    if (!(o->location & 1))
                        return (op->location & 4) ? 0 : press();
                    if (op->location & 1)
                        return press();
                }
            } else if ((o->flags_e9 & 0x10) && (o->location & 4) && (op->location & 1)) {
                return press();
            }
        }
    }
    if (!chance_check(mem_s32(blk(AI_SKILL))) || (s->partner_flags80 & 0xC0) || !(s->location & 1) ||
        (op->location & 4))
        return 0;
    s32 c = 0;
    if ((f64)dist_to(&sp, &op->point_120) < 160.0) {
        if (ai_faced_by(s, op))
            c = (op->opponent == s->slot) ? 500 : 250;
        else
            c = 700;
    }
    if (!((f64)mem_f32(blk(AI_DIST_P)) > 160.0))
        return 0;
    return chance_check(c) ? press() : 0;
}

}  // namespace

namespace {

// N64 0x800F5614: whether the tag deciders should run (1). In a tag match (type 0x2000): always under
// the free-for-all rule (0x40) or for a man not legal. Then in team play (type 0x1000): with
// flags_e9 0x01 when anyone else loaded has flags_e9 and flags_f1 0x02; with flags_e9 0x02 when the
// CPU has flags_f1 0x02 or anyone else has both.
s32 ai_tag_reach_check() {
    const Wrestler *s = g_ai_self;
    if ((g_match_type & 0x2000) && ((g_match_rules & 0x40) || !(s->flags_e9 & 0x10)))
        return 1;
    if (!(g_match_type & 0x1000))
        return 0;
    auto other_marked = [s]() {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (i == s->slot || w->roster_id == -1 || !(w->flags_e9 & 2))
                continue;
            if (w->flags_f1 & 2)
                return true;
        }
        return false;
    };
    if ((s->flags_e9 & 1) && other_marked())
        return 1;
    if (!(s->flags_e9 & 2))
        return 0;
    if (s->flags_f1 & 2)
        return 1;
    return other_marked() ? 1 : 0;
}

// N64 0x800F609C: how the CPU relates to a wrestler's place for a tag (`loc` its location, `quad` /
// `zone` its ring quadrant and zone): 2 both in the ring. Under rule 0x4000: from the floor, at one
// also on the floor, 3 in the same zone, 4 in the same quadrant, else 1; otherwise 1. Without it: 1
// at one in the ring; both on the floor 1 in the same zone or quadrant, else 0; else 0.
s32 ai_tag_position(u16 loc, s32 quad, s32 zone) {
    u16 sl = g_ai_self->location;
    if (sl & loc & 1)
        return 2;
    if (g_match_rules & 0x4000) {
        if (!(sl & 4) || !(loc & 4))
            return 1;
        if (mem_s16(blk(AI_ZONE_SELF)) == zone)
            return 3;
        return mem_s16(blk(AI_QUAD_SELF)) == quad ? 4 : 1;
    }
    if (loc & 1)
        return 1;
    if (!(sl & loc & 4))
        return 0;
    if (mem_s16(blk(AI_ZONE_SELF)) == zone)
        return 1;
    return mem_s16(blk(AI_QUAD_SELF)) == quad ? 1 : 0;
}

// N64 0x800F5D9C: 1 when the partner, in the ring, is in the middle of a move that should not be
// broken up: state 66, or states 70 (at step 5), 85, 93, 95 with a move whose move-set flags have
// 0x600.
s32 ai_partner_busy() {
    const Wrestler *p = &g_wrestlers[g_ai_self->partner];
    if (!(p->location & 1))
        return 0;
    switch (p->state) {
    case state::CoverFromStanding:
        return 1;
    case state::GroundGrappleAttacker:
        if (p->state_step != 5)
            return 0;
        break;
    case state::MoveAttacker: case state::ChainMoveAttacker: case state::CarryMoveAttacker:
        break;
    default:
        return 0;
    }
    return (mem_u32(A_MOVESETS + 19728u * (u32)p->slot + 36u * (u32)p->move_num + 0x20) & 0x600) ? 1 : 0;
}

// N64 0x800F5770: the tag-team helper (1 when it acted). By ai_tag_position against the partner:
// 1 (apart): only while the partner or the partner's opponent is below 64 health. With the walk flag,
// not free to walk the apron and the partner busy (ai_partner_busy): on its own team's apron half,
// at an opponent (its partner while legal) on that one's own team's half, mode 118. Then with the partner's hold
// flags (partner_flags80 0xC0; with the walk flag only when free to walk): with 0x40 at an
// opponent not held (flags80 0xC0) the grapple in the ring (ai_try_grapple(1) beyond 170, else 50%
// 6 / 1); a held one with both in the ring, or an unheld one, C-right; else to the partner (in the
// ring: 33, 34 from the floor; under rule 0x4000 on the floor: 36 from the ring, 33 / 35 from the
// apron by side, else 34). 2-4 (together): with the partner hold. With 0x40 (not while the partner is
// in state 0x7F / 0x81): at an opponent held 0x80 40 for 4 else C-right; 2: in the ring or on the
// floor beyond 170 a grapple (41 when the partner is within 512 of the CPU's angle to the
// opponent), else 50% 6 / 1, elsewhere 65% 39 else 41; 4: 40 in another zone, else as 3; 3: on the
// floor beyond 170 a grapple, else 6 / 1, elsewhere 39 / 41. Without 0x40: at an opponent held 0x40
// 40 for 4, beyond 170 68 unless the opponent's partner is within 130 and 512 of the angle (6), else
// 6; not held, C-right.
s32 ai_tag_reach() {
    Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    const Wrestler *p = &g_wrestlers[s->partner];
    const Wrestler *op = &g_wrestlers[o->partner];
    s32 pzone = ring_zone_of(&p->point_120);
    s32 k = ai_tag_position(p->location, ring_quadrant(p), pzone);
    auto press = []() {
        ai_press_focus_switch();
        return 1;
    };
    auto set = [](s32 m) {
        ai_set_mode(m);
        return 1;
    };
    auto grapple_or = []() {
        if ((f64)mem_f32(blk(AI_DIST)) > 170.0) {
            ai_try_grapple(1);
            return 1;
        }
        ai_set_mode(chance_check(500) ? ai_mode::Strike : ai_mode::EngageNearbyOpponent);
        return 1;
    };
    if (k == 1) {
        if (p->health >= 64 && g_wrestlers[p->opponent].health >= 64)
            return 0;
        if ((mem_u8(blk(AI_FLAGS)) & 0x20) && !apron_walk_dpad(s->slot) && ai_partner_busy() && (s->location & 2)) {
            bool own_half = (s->slot & 2) ? !(s->ring_zone & 2) : (s->ring_zone & 2) != 0;
            if (own_half) {
                const Wrestler *t = (o->flags_e9 & 0x10) ? op : o;
                if (t->location & 2) {
                    bool its_half = (t->slot & 2) ? !(t->ring_zone & 2) : (t->ring_zone & 2) != 0;
                    if (its_half)
                        return set(118);
                }
            }
        }
        if (!(s->partner_flags80 & 0xC0))
            return 0;
        if ((mem_u8(blk(AI_FLAGS)) & 0x20) && !apron_walk_dpad(s->slot))
            return 0;
        if (s->partner_flags80 & 0x40) {
            if (!(o->flags80 & 0xC0)) {
                if (mem_u16(blk(AI_WHERE)) & 1)
                    return grapple_or();
            } else if (s->location & op->location & 1) {
                return press();
            }
        }
        if (!(o->flags80 & 0xC0))
            return press();
        if (p->location & 1)
            return set((s->location & 4) ? 34 : 33);
        if (!(g_match_rules & 0x4000) || !(p->location & 4))
            return 0;
        if (s->location & 1)
            return set(36);
        if (s->location & 2)
            return set(((pzone ^ 2) == s->ring_zone) ? 33 : 35);
        return set(34);
    }
    if (k < 2 || k >= 5)
        return 0;
    if (!(s->partner_flags80 & 0xC0))
        return 0;
    if (s->partner_flags80 & 0x40) {
        if (p->state == state::RearLiftAttacker || p->state == state::CarryAttacker)
            return 0;
        if (o->flags80 & 0x80)
            return k == 4 ? set(40) : press();
        if (k == 4 && mem_s16(blk(AI_ZONE_SELF)) != pzone)
            return set(40);
        f64 d = (f64)mem_f32(blk(AI_DIST));
        if (k == 2) {
            if (mem_u16(blk(AI_WHERE)) & 0x101) {
                if (!(d > 170.0))
                    return set(chance_check(500) ? 6 : 1);
                if (abs_i(angle_diff(angle_to(&s->point_120, &p->point_120), mem_s32(blk(AI_ANGLE)))) < 512)
                    return set(41);
                ai_try_grapple(1);
                return 1;
            }
            return set(chance_check(650) ? 39 : 41);
        }
        if (mem_u16(blk(AI_WHERE)) & 0x100)
            return grapple_or();
        return set(chance_check(500) ? 39 : 41);
    }
    if (!(o->flags80 & 0x40))
        return press();
    if (k == 4)
        return set(40);
    if ((f64)mem_f32(blk(AI_DIST)) > 170.0) {
        Vec3 sp = ai_self_point();
        if (!((f64)dist_to(&sp, &op->point_120) < 130.0) ||
            abs_i(angle_diff(angle_to(&sp, &op->point_120), mem_s32(blk(AI_ANGLE)))) >= 512)
            return set(68);
    }
    return set(6);
}

// N64 0x800F5E5C: with the opponent held (flags80 0xC0) and it or the third wrestler (the first loaded
// one that is neither) below 64 health (1 when it acted): at an opponent held 0x80, not carrying
// (0x80 / 0x82), C-right; else by ai_tag_position against the opponent: 1 to it (in the ring 33, 34
// from the floor; under rule 0x4000 on the floor 36 from the ring, 33 / 35 from the apron by side,
// else 34), 2 / 3 within 170 mode 6 else 68, 4 run (40).
s32 ai_tag_extra() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if (!(o->flags80 & 0xC0))
        return 0;
    s32 third = 0;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || w->slot == s->slot || w->slot == o->slot)
            continue;
        third = i;
        break;
    }
    if (o->health >= 64 && g_wrestlers[third].health >= 64)
        return 0;
    if (o->flags80 & 0x80) {
        if (o->state == state::RearLiftDefender || o->state == state::CarryDefender)
            return 0;
        ai_press_focus_switch();
        return 1;
    }
    s32 m;
    switch (ai_tag_position(o->location, mem_s16(blk(AI_QUAD_OPP)), mem_s16(blk(AI_ZONE_OPP)))) {
    case 1:
        if (o->location & 1) {
            m = (s->location & 4) ? ai_mode::HeadForRingAlternate : ai_mode::HeadForRing;
        } else {
            if (!(g_match_rules & 0x4000) || !(o->location & 4))
                return 0;
            if (s->location & 1)
                m = ai_mode::LeaveRingThroughRopes;
            else if (s->location & 2)
                m = ((s16)(mem_u16(blk(AI_ZONE_OPP)) ^ 2) == s->ring_zone) ? ai_mode::HeadForRing : ai_mode::DropOffApron;
            else
                m = ai_mode::HeadForRingAlternate;
        }
        break;
    case 2: case 3:
        m = (f64)mem_f32(blk(AI_DIST)) > 170.0 ? ai_mode::RunInDirection : ai_mode::Strike;
        break;
    case 4:
        m = ai_mode::Run;
        break;
    default:
        return 0;
    }
    ai_set_mode(m);
    return 1;
}

}  // namespace

namespace {

// N64 0x80102BDC: 1 in a tag match when the partner is in the ring within 100 of the CPU.
s32 ai_partner_distance() {
    const Wrestler *p = &g_wrestlers[g_ai_self->partner];
    if (!(g_match_type & 0x2000) || !(p->location & 1))
        return 0;
    return (f64)dist_to(&g_ai_self->point_120, &p->point_120) < 100.0 ? 1 : 0;
}

// N64 0x800FFE6C: the mode for meeting a runner (mode 15's choice; returns it). In finisher mode 18
// with the finish plan's move 94, or without a plan with the move-set bit attrs +0x3C 0x40000. The
// list (kind 1: 0x80145D54, else 0x80145D34: s16 move, s16 mode pairs) has 3 entries usable, in the
// ring with the side beyond the opponent within 210: 8 with the rope bits (attrs +0x38 0x1800000),
// else 7. With the plan, its move's mode. With 8 entries and AI flags 0x40: 85% a 50 / 50 of 105 and
// 106. Else 40% a random favourite (move-set flag 0x8000), else a random entry.
s32 ai_partner_mode(s32 kind) {
    const Wrestler *s = g_ai_self;
    bool plan = mem_u8(0x8016B718 + (u32)s->slot) != 0;
    u32 plan_move = 0x8016B580 + 4u * (u32)s->slot;
    if (s->flags_ee & 4) {
        if (plan ? mem_s32(plan_move) == 94 : (mem_u16(s->attrs + 0x3C) & 4) != 0)
            return ai_mode::CatchRunnerVariant3;
    }
    s32 n = 3;
    if (mem_u16(blk(AI_WHERE)) & 1) {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        f64 d = (f64)ai_side_distance((dir8_to(&sp, &op) / 2) ^ 2, &sp);
        if (mem_u32(s->attrs + 0x38) & 0x1800000)
            n = d < 210.0 ? 8 : 3;
        else
            n = d < 210.0 ? 7 : 3;
    }
    u32 list = (kind == 1) ? 0x80145D54u : 0x80145D34u;
    if (plan) {
        for (s32 i = 0; i < n; i++)
            if (mem_s32(plan_move) == mem_s16(list + 4u * (u32)i))
                return mem_s16(list + 4u * (u32)i + 2);
    }
    if (n == 8 && (mem_u8(blk(AI_FLAGS)) & 0x40) && chance_check(850))
        return chance_check(500) ? 105 : 106;
    if (chance_check(400)) {
        s16 fav[8];
        s32 nf = 0;
        u32 ms = mem_u32(blk(AI_MOVESET));
        for (s32 i = 0; i < n; i++) {
            u32 e = list + 4u * (u32)i;
            if (mem_u32(ms + 36u * (u32)(s32)mem_s16(e) + 0x20) & 0x8000)
                fav[nf++] = mem_s16(e + 2);
        }
        if (nf > 0)
            return fav[rand_pool_next() % nf];
    }
    return mem_s16(list + 4u * (u32)(rand_pool_next() % n) + 2);
}

// N64 0x800FF1B4: a direction for evading: 0 / 2 / 4 / 6 for the first of the four side angles
// (0x8014D1D8) within 170 of the CPU's angle to the opponent, else 1 / 3 / 5 / 7 for the first ring
// corner whose direction from the CPU is within 170 of it; -1 for none.
s32 ai_evade_direction() {
    s32 a = mem_s32(blk(AI_ANGLE));
    for (s32 i = 0; i < 4; i++)
        if (abs_i(angle_diff(mem_s32(0x8014D1D8 + 4u * (u32)i), a)) < 170)
            return 2 * i;
    Vec3 sp = ai_self_point();
    for (s32 i = 0; i < 4; i++) {
        u32 c = A_RING_CORNERS + 12u * (u32)i;
        Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
        if (abs_i(angle_diff(angle_to(&sp, &corner), a)) < 170)
            return 1 + 2 * i;
    }
    return -1;
}

// N64 0x80104B2C: against a strike (8) or a dive (25 / 26), after a skill roll, the frames until the
// opponent's move reaches its active window (anim_player +8 less anim_frame, at least 0); else 0.
s32 ai_block_timer() {
    s32 so = mem_s32(blk(AI_SIT_OPP));
    if (so != 8 && (so < 25 || so > 26))
        return 0;
    if (!chance_check(mem_s32(blk(AI_SKILL))))
        return 0;
    s32 t = (s32)g_ai_opp->anim_player[8] - g_ai_opp->anim_frame;
    return t < 0 ? 0 : t;
}

}  // namespace

namespace {

// N64 0x8012D82C: the nearest arena wall hit along a move from the wrestler (1 when one is hit; dist
// and seg, when given, get the distance and the wall). The start is point_120 lifted to pos.y + 10;
// on the apron pos (y + 10), with flags8a 0x800 taking point_120's x on ring sides 0 / 2 and its z on
// 1 / 3. The walls come from the place as in keep_in_bounds, except the floor list is per arena area
// (0x8014DA2C) and the 0x400 one by ring_zone; each is tested with segment_hit (kind 0).
s32 nearest_segment_along(const Wrestler *w, const Vec3 *move, f32 *dist, s32 *seg) {
    Vec3 p;
    if (w->location & 2) {
        p = w->pos;
        p.y = (f32)((f64)p.y + 10.0);
        if (w->flags8a & 0x800) {
            if (w->ring_zone == 0 || w->ring_zone == 2)
                p.x = w->point_120.x;
            else if (w->ring_zone == 1 || w->ring_zone == 3)
                p.z = w->point_120.z;
        }
    } else {
        p = w->point_120;
        p.y = (f32)((f64)w->pos.y + 10.0);
    }
    Vec3 q;
    vec_add(&p, move, &q);
    u16 loc = w->location;
    u32 set;
    if (loc & 1) {
        set = 0x80146BF0;
    } else if (loc & 2) {
        set = mem_u32(0x8014D390 + 4u * (u32)w->ring_zone);
    } else if (loc & 4) {
        s32 arena = mem_s32(A_ARENA);
        set = mem_u32(0x8014DA2C + 4u * (u32)(arena < 10 ? 0 : arena - 9));
    } else if (loc & 0x40) {
        set = 0x80147E20;
    } else if (loc & 0x80) {
        set = 0x8014BC8C;
    } else if (loc & 0x100) {
        set = 0x8014B704;
    } else if (loc & 0x400) {
        set = mem_u32(0x8014D3A0 + 4u * (u32)w->ring_zone);
    } else {
        set = 0x8014C500;
    }
    f32 best = 10000.0f;
    u32 hit = 0;
    for (u32 g = set; mem_u32(g) != 0; g += 4) {
        u32 group = mem_u32(g);
        for (u32 wall = mem_u32(group);; wall = mem_u32(wall + 0x2C)) {
            f32 d;
            if (segment_hit(&p, &q, wall, nullptr, &d, 0) && d < best) {
                best = d;
                hit = wall;
            }
            if (wall == mem_u32(group + 4))
                break;
        }
    }
    if (!hit)
        return 0;
    if (dist)
        *dist = best;
    if (seg)
        *seg = (s32)hit;
    return 1;
}

}  // namespace

namespace {

// N64 0x80101D4C: a direction 0-7 for mode 25's backstage lock-up push (mode flag 0x10). Below
// arena 21: 4 within 180 of x 0, else 3 / 5 by the side of x. Arenas 21-24: toward the arena's
// point (0x80145D74 + 12 x arena) from the CPU's pos at y 0. Others: random.
s32 ai_arena_target() {
    const Wrestler *s = g_ai_self;
    s32 arena = mem_s32(A_ARENA);
    if (arena < 21) {
        if (absf(s->pos.x) <= 180.0f)
            return 4;
        return s->pos.x < 0.0f ? 3 : 5;
    }
    if ((u32)(arena - 21) >= 4)
        return rand_pool_next() % 8;
    Vec3 from = {s->pos.x, 0.0f, s->pos.z};
    u32 t = 0x80145D74 + 12u * (u32)arena;
    Vec3 to = {mem_f32(t), mem_f32(t + 4), mem_f32(t + 8)};
    return dir8_to(&from, &to);
}

}  // namespace

namespace {

// N64 0x80101EF0: the directions 0-7 (out[0], out[1]; -1 for none) from the CPU to the first two
// other loaded wrestlers (not the opponent) sharing its place.
void ai_others_dirs(s32 out[2]) {
    const Wrestler *s = g_ai_self;
    out[0] = out[1] = -1;
    Vec3 sp = ai_self_point();
    s32 n = 0;
    for (s32 i = 0; i < 4; i++) {
        const Wrestler *w = &g_wrestlers[i];
        if (w->roster_id == -1 || w->slot == s->slot || w->slot == g_ai_opp->slot || !(s->location & w->location & 0x7FF))
            continue;
        out[n++] = (s8)angle_dir8(angle_to(&sp, &w->point_120));
        if (n >= 2)
            return;
    }
}

// N64 0x80102360: how far ahead the CPU is: (100 / 200 / 300 for kind 0 / 1 / 2, + its perceived
// spirit / 2, - the opponent's spirit / 4 and health) / 2.
s32 ai_advantage(s32 kind) {
    s32 base = kind == 1 ? 200 : 100;
    if (kind == 2)
        base = 300;
    s32 v = base + mem_s16(blk(AI_SPIRIT)) / 2;
    return (v - (mem_s16(blk(AI_OPP_SPIRIT)) / 4 + mem_s16(blk(AI_OPP_HEALTH)))) / 2;
}

// N64 0x801022F0: a 15% roll when the CPU is well ahead (ai_advantage(1) above 80) and neither
// wrestler is in finisher mode. (Its callers pass 1 or 2, which it ignores.)
s32 ai_chance_by_advantage() {
    if (!(ai_advantage(1) > 80) || (g_ai_self->flags_ee & 4) || (g_ai_opp->flags_ee & 4))
        return 0;
    return chance_check(150) ? 1 : 0;
}

// N64 0x80101E44: a free direction for the lock-up push: the first of the three by the CPU's facing
// (0x80145EA0) not toward another wrestler (ai_others_dirs), else a random even one.
s32 ai_lockup_free_dir() {
    s32 d = (s8)angle_dir8(g_ai_self->facing);
    s32 other[2];
    ai_others_dirs(other);
    u32 t = 0x80145EA0 + 3u * (u32)d;
    for (s32 i = 0; i < 3; i++) {
        s32 v = mem_s8(t + (u32)i);
        if (v != -1 && v != other[0] && v != other[1])
            return v;
    }
    return 2 * (rand_pool_next() % 4);
}

// N64 0x80101BDC: a ring side 0-3 to push the opponent toward (an index into the d-pad table
// 0x8014D218), from the three sides by the CPU's facing (0x80145E58): the first when it is the
// farthest and no other wrestler stands that way, else the second when it beats the third's
// distance (230 when the third is -1, else 0) and is clear, else the third, else random.
s32 ai_lockup_dir_move() {
    s32 other[2];
    ai_others_dirs(other);
    u32 t = 0x80145E58 + 3u * (u32)angle_dir8(g_ai_self->facing);
    s32 a = mem_s8(t), b = mem_s8(t + 1), c = mem_s8(t + 2);
    Vec3 sp = ai_self_point();
    f32 da = ai_side_distance(a, &sp);
    f32 db = ai_side_distance(b, &sp);
    f32 dc = c == -1 ? ai_side_distance(-1, &sp) : 0.0f;
    if (db < da && dc < da && other[0] != 2 * a && other[1] != 2 * a)
        return a;
    if (dc < db && other[0] != 2 * b && other[1] != 2 * b)
        return b;
    if (c != -1)
        return c;
    return rand_pool_next() % 4;
}

// N64 0x8010183C: in the lock-up, a rope or floor move (1 when it set a mode); not with flags78
// 0x780 or under the over-the-top rule. In the ring (CPU's z within 510): with a legal man in it,
// the trait +38 chance (0x80145E40, +150 under rule 0x4000; none with the finish plan or time
// limit option 1), else always: mode 102. On the floor in a ring arena with a legal man, z within
// 530: the +38 chance (0x80145E4C, halved under 0x4000) or the finish plan: mode 103.
s32 ai_lockup_ropes() {
    const Wrestler *s = g_ai_self;
    const Wrestler *o = g_ai_opp;
    if ((s->flags78 & 0x780) || (g_match_rules & 0x400))
        return 0;
    s32 t38 = mem_s8(kTraits[8].global);
    bool plan = mem_u8(0x8016B718 + (u32)s->slot) != 0;
    if (mem_u16(blk(AI_WHERE)) & 1) {
        if ((f64)mem_f32(A_AI_SELF_POINT + 8) > 510.0)
            return 0;
        s32 c = 0;
        if ((s->flags_e9 & 0x10) || (o->flags_e9 & 0x10)) {
            if (!plan && mem_s8(A_TIME_LIMIT) != 1) {
                c = mem_s16(0x80145E40 + 2u * (u32)t38);
                if (g_match_rules & 0x4000)
                    c += 150;
            }
        } else {
            c = 1000;
        }
        if (chance_check(c)) {
            ai_set_mode(ai_mode::LockupDirectionMove);
            return 1;
        }
    }
    if (!(mem_u16(blk(AI_WHERE)) & 0x100) || mem_s32(A_ARENA) >= 10 ||
        (!(s->flags_e9 & 0x10) && !(o->flags_e9 & 0x10)) || (f64)mem_f32(A_AI_SELF_POINT + 8) > 530.0)
        return 0;
    s32 c = mem_s16(0x80145E4C + 2u * (u32)t38);
    if (g_match_rules & 0x4000)
        c /= 2;
    if (chance_check(c) || plan) {
        ai_set_mode(ai_mode::CircleInRing);
        return 1;
    }
    return 0;
}

// N64 0x80101A94: for a corner finisher from the lock-up (mode 117): in the ring with attrs +0x38
// 0x1000000 and move 105's animation style 3674, the first ring corner 230-280 away whose direction
// (0x8014D208) is within 170 of the CPU's facing; -1 for none.
s32 ai_lockup_finisher() {
    const Wrestler *s = g_ai_self;
    if (!(mem_u16(blk(AI_WHERE)) & 1) || !(mem_u32(s->attrs + 0x38) & 0x1000000))
        return -1;
    s16 anim = mem_s16(0x8008FA70 + 1096u * (u32)s->slot + 2u * 105u);
    if (mem_s16(0x8015A700 + 2u * (u32)(s32)anim) + 13090 != 16764)
        return -1;
    for (s32 i = 0; i < 4; i++) {
        f64 d = (f64)mem_f32(blk(AI_CORNERS) + 4u * (u32)i);
        if (230.0 < d && d < 280.0 && abs_i(angle_diff(mem_s32(0x8014D208 + 4u * (u32)i), s->facing)) < 170)
            return i;
    }
    return -1;
}

// N64 0x80101FE8: whether to whip the opponent (mode 111), from the ring outside team play: in a tag
// match only with the partner in the ring and standing (state 1); in types 0xC000 only with a third
// wrestler (not finished, not in state 163) standing in the ring; then ai_chance_by_advantage.
s32 ai_lockup_whip_check() {
    const Wrestler *s = g_ai_self;
    if (!(s->location & 1) || (g_match_type & 0x1000))
        return 0;
    if (g_match_type & 0x2000) {
        const Wrestler *p = &g_wrestlers[s->partner];
        if (!(p->location & 1) || p->state != state::Standing)
            return 0;
    }
    if (g_match_type & 0xC000) {
        bool found = false;
        for (s32 i = 0; i < 4 && !found; i++) {
            const Wrestler *w = &g_wrestlers[i];
            if (w->roster_id == -1 || w->slot == s->slot || w->slot == g_ai_opp->slot || w->state == state::RunIn ||
                (w->flags_f1 & 1) || w->state != state::Standing)
                continue;
            found = (w->location & 1) != 0;
        }
        if (!found)
            return 0;
    }
    return ai_chance_by_advantage();
}

// N64 0x80102120: whether to drag the opponent to the partner (mode 112): not in team play nor once
// finished. In a tag match the partner must stand (state 1) in the ring with the rope bits for the
// CPU's place (attrs +0x38 0x500000 from the ring, 0x200000 from the floor). In types 0xC000 the same
// test on a third wrestler, but the original loop never advances: it only ever looks at slot 0.
// Then ai_chance_by_advantage.
s32 ai_lockup_tag_check() {
    const Wrestler *s = g_ai_self;
    if ((g_match_type & 0x1000) || (s->end_flags & 1))
        return 0;
    if (g_match_type & 0x2000) {
        const Wrestler *p = &g_wrestlers[s->partner];
        if (!(p->location & 1) || p->state != state::Standing)
            return 0;
        if ((s->location & 1) && !(mem_u32(p->attrs + 0x38) & 0x500000))
            return 0;
        if ((s->location & 4) && !(mem_u32(p->attrs + 0x38) & 0x200000))
            return 0;
    }
    if (g_match_type & 0xC000) {
        const Wrestler *w = &g_wrestlers[0];
        bool found = w->roster_id != -1 && w->slot != s->slot && w->slot != g_ai_opp->slot && w->state != state::RunIn &&
                     !(w->flags_f1 & 1) && w->state == state::Standing && (w->location & 1) &&
                     (((s->location & 1) && (mem_u32(w->attrs + 0x38) & 0x500000)) ||
                      ((s->location & 4) && (mem_u32(w->attrs + 0x38) & 0x200000)));
        if (!found)
            return 0;
    }
    return ai_chance_by_advantage();
}

// N64 0x801011E4: the weak-opponent plan's lock-up action (1 when it acted), in the ring, not with
// flags78 0x600, by the plan's target bits (AI +0x84; 0x08 only in finisher mode). 0x1000 (from the
// front, flags78 0x180 clear, with AI flags 0x10): 0x08 a push (ai_lockup_dir_move); else the hand
// must match 0x4000 (flags78 0x04), then 0xC00 mode 120, else the push. 0x2000: in the corner
// (flags78 0x100) with 0x08 and a strong grapple the plan move 138 (rear) / 128 for the special
// stick (80), without 0x08 mode 26; in the open (flags78 0x684 clear, AI flags 0x10) a push along
// a random d-pad direction (0x8014D228). Otherwise: 0x08 with a strong open grapple, the plan move 81
// (0x10, rear) / 70 (front) for the special, else L (the side switch, step 3); without 0x08 the hand
// must match 0x4000, then 0x10: L from the front, 26 from behind; else 26 from the front.
s32 ai_lockup_special() {
    Wrestler *s = g_ai_self;
    if (s->flags78 & 0x600)
        return 0;
    u16 t = mem_u16(blk(0x84));
    if ((t & 8) && !(s->flags_ee & 4))
        return 0;
    if (!(mem_u16(blk(AI_WHERE)) & 1))
        return 0;
    auto step_up = []() { mem_w32(blk(AI_STEP), (u32)(mem_s32(blk(AI_STEP)) + 1)); };
    auto push = [&](u16 b) {
        mem_w16(blk(AI_PENDING), (u16)(b | 4));
        step_up();
        return 1;
    };
    auto stick_if_plan = [&](s32 move) {
        if (mem_s32(0x8016B580 + 4u * (u32)s->slot) != move)
            return 0;
        mem_w8(blk(AI_STICK_PENDING), 80);
        step_up();
        return 1;
    };
    auto press_l = []() {
        mem_w16(blk(AI_PRESSED), 32);
        mem_w16(blk(AI_HELD), 32);
        mem_w32(blk(AI_STEP), 3);
        return 1;
    };
    auto hand_ok = [&]() { return (mem_u16(blk(0x84)) & 0x4000) ? (s->flags78 & 4) != 0 : !(s->flags78 & 4); };
    if (t & 0x1000) {
        if ((s->flags78 & 0x180) || !(mem_u8(blk(AI_FLAGS)) & 0x10))
            return 0;
        if (t & 8) {
            if (!(s->flags_ee & 4))
                return 0;
            return push(mem_u16(0x8014D218 + 2u * (u32)ai_lockup_dir_move()));
        }
        if (!hand_ok())
            return 0;
        if (mem_u16(blk(0x84)) & 0xC00) {
            ai_set_mode(ai_mode::Lockup);
            return 1;
        }
        return push(mem_u16(0x8014D218 + 2u * (u32)ai_lockup_dir_move()));
    }
    u32 f = s->flags78;
    if (t & 0x2000) {
        if (f & 0x100) {
            if (!(t & 8)) {
                ai_set_mode(ai_mode::LockupPressMove);
                return 1;
            }
            if ((f & 5) != 5)
                return 0;
            return stick_if_plan((f & 0x80) ? 138 : 128);
        }
        if ((f & 0x684) || !(mem_u8(blk(AI_FLAGS)) & 0x10))
            return 0;
        return push(mem_u16(0x8014D228 + 2u * (u32)(rand_pool_next() % 4)));
    }
    if (t & 8) {
        if ((f & 0x105) != 5)
            return 0;
        if ((t & 0x10) && (f & 0x80))
            return stick_if_plan(81);
        if (!(t & 0x10) && !(f & 0x80))
            return stick_if_plan(70);
        return press_l();
    }
    if (!hand_ok())
        return 0;
    if (mem_u16(blk(0x84)) & 0x10) {
        if (s->flags78 & 0x80) {
            ai_set_mode(ai_mode::LockupPressMove);
            return 1;
        }
        return press_l();
    }
    if (s->flags78 & 0x80)
        return 0;
    ai_set_mode(ai_mode::LockupPressMove);
    return 1;
}

}  // namespace

namespace {

// N64 0x80100624: mode 22, a strike on a downed opponent: step 0 holds B at an opponent down (15),
// else back to mode 0 (clearing AI +0x86); step 1 waits for its own strike (situation 9), else mode
// 0; step 2 when the strike is over, the mode saved in +0x86.
void ai_mode_22_strike() {
    switch (mem_s32(blk(AI_STEP))) {
    case 0:
        if (mem_s32(blk(AI_SIT_OPP)) != 15) {
            mem_w16(blk(AI_RETURN_MODE), 0);
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w16(blk(AI_HELD), 0x4000);
        mem_w32(blk(AI_STEP), 1);
        return;
    case 1:
        if (mem_s32(blk(AI_SIT_SELF)) != 9) {
            mem_w16(blk(AI_RETURN_MODE), 0);
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_STEP), 2);
        return;
    case 2:
        if (mem_s32(blk(AI_SIT_SELF)) != 9)
            ai_set_mode(mem_s16(blk(AI_RETURN_MODE)));
        return;
    default:
        return;
    }
}

// N64 0x801043B4: against an opponent starting a grapple (state 0x50), after a skill roll, the
// frames before its grab lands (anim_player +8 less anim_frame less 1, at least 0); else 0.
s32 ai_evade_timer() {
    const Wrestler *o = g_ai_opp;
    if (o->state != state::GrappleAttempt || !chance_check(mem_s32(blk(AI_SKILL))))
        return 0;
    s32 t = (s32)o->anim_player[8] - o->anim_frame - 1;
    return t < 0 ? 0 : t;
}

// N64 0x801042F4: mode 42, the grapple evade: step 0 the wait (ai_evade_timer); step 1 when it runs
// out, L; step 2 back to mode 0 unless the CPU is in the evade (0x6B).
void ai_mode_42_evade_grapple() {
    switch (mem_s32(blk(AI_STEP))) {
    case 0:
        mem_w32(blk(AI_MODE_TIMER), (u32)ai_evade_timer());
        mem_w32(blk(AI_STEP), 1);
        return;
    case 1: {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t > 0)
            return;
        mem_w16(blk(AI_HELD), 32);
        mem_w32(blk(AI_STEP), 2);
        return;
    }
    case 2:
        if (g_ai_self->state != state::GrappleEvade)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x801057BC: mode 55, a long wait: up to 150 frames while standing (situation 1), the
// opponent not dazed (16) and in the state it was in when the mode began (AI +0x56); then mode 0.
void ai_mode_55_wait_long() {
    switch (mem_s32(blk(AI_STEP))) {
    case 0:
        mem_w32(blk(AI_MODE_TIMER), 150);
        mem_w32(blk(AI_STEP), 1);
        return;
    case 1: {
        if (mem_s32(blk(AI_SIT_OPP)) != 16 && mem_s32(blk(AI_SIT_SELF)) == 1 &&
            mem_s16(blk(AI_OPP_STATE)) == g_ai_opp->state) {
            s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
            mem_w32(blk(AI_MODE_TIMER), (u32)t);
            if (t >= 0)
                return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    default:
        return;
    }
}

// N64 0x8010A1D0: modes 105-109, a run with a move at its end. Step 0: the move: 109 A with reach
// 160, the others the entry for the mode in 0x80145F20 (s16 mode, s16 list index, u16 buttons; the
// reach from the list block's entry); 105 / 106 press C-down (step 3), 107-109 press C-down with the
// d-pad side toward the opponent (0x8014D220 by dir8 / 2; step 1), or step 3 when already running.
// Step 1 waits for the run (situation 2 stays, 4 step 2, else 0); step 2 for the run's start
// (situation 2: step 3, 4 stays, else 0). Step 3 while running (2): at run step 2 within 170 a skill
// roll against a striking or grappling opponent (8 / 11) presses L, else within the reach + 100 the
// prepared buttons; a rebound (4) mode 119; situations 5 / 8 / 11 wait; others mode 0.
void ai_mode_105_running() {
    s32 mode = mem_s32(blk(AI_MODE));
    switch (mem_s32(blk(AI_STEP))) {
    case 0: {
        mem_wf32(blk(AI_REACH), 0.0f);
        if (mode == ai_mode::RunWithAButtonMove) {
            mem_w16(blk(AI_PENDING), 0x8000);
            mem_wf32(blk(AI_REACH), 160.0f);
        } else {
            for (s32 i = 0; i < 4; i++) {
                u32 e = 0x80145F20 + 6u * (u32)i;
                if (mem_s16(e) == mode) {
                    mem_wf32(blk(AI_REACH), mem_f32(mem_u32(blk(AI_LISTS)) + 8u * (u32)(s32)mem_s16(e + 2)));
                    mem_w16(blk(AI_PENDING), mem_u16(e + 4));
                    break;
                }
            }
        }
        if (mode < ai_mode::RunWithMoveVariant1)
            return;
        if (mode < ai_mode::RunWithMoveVariant3) {
            mem_w16(blk(AI_HELD), 4);
            mem_w32(blk(AI_STEP), 3);
            return;
        }
        if (mode >= ai_mode::CLeftTurningAway)
            return;
        if (g_ai_self->state == state::Running) {
            mem_w32(blk(AI_STEP), 3);
            return;
        }
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        mem_w16(blk(AI_HELD), (u16)(mem_u16(0x8014D220 + 2u * (u32)(dir8_to(&sp, &op) / 2)) | 4));
        mem_w32(blk(AI_STEP), 1);
        return;
    }
    case 1: {
        s32 sit = mem_s32(blk(AI_SIT_SELF));
        if (sit == 2)
            return;
        if (sit == 4)
            mem_w32(blk(AI_STEP), 2);
        else
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 2: {
        s32 sit = mem_s32(blk(AI_SIT_SELF));
        if (sit == 2)
            mem_w32(blk(AI_STEP), 3);
        else if (sit != 4)
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 3:
        switch (mem_s32(blk(AI_SIT_SELF))) {
        case 2: {
            if (g_ai_self->state_step != 2)
                return;
            s32 so;
            if ((f64)mem_f32(blk(AI_DIST_P)) < 170.0 && chance_check(mem_s32(blk(AI_SKILL))) &&
                ((so = mem_s32(blk(AI_SIT_OPP))) == 8 || so == 11)) {
                mem_w16(blk(AI_HELD), 32);
                return;
            }
            if ((f64)mem_f32(blk(AI_DIST)) < (f64)mem_f32(blk(AI_REACH)) + 100.0)
                mem_w16(blk(AI_HELD), mem_u16(blk(AI_PENDING)));
            return;
        }
        case 4:
            ai_set_mode(ai_mode::KeepRunning);
            return;
        case 5: case 8: case 11:
            return;
        default:
            ai_set_mode(ai_mode::Think);
            return;
        }
    default:
        return;
    }
}

}  // namespace

namespace {

inline s32 ai_step() { return mem_s32(blk(AI_STEP)); }
inline void ai_next_step() { mem_w32(blk(AI_STEP), (u32)(ai_step() + 1)); }
inline void ai_hold(u16 b) { mem_w16(blk(AI_HELD), b); }
inline void ai_press(u16 b) { mem_w16(blk(AI_PRESSED), b); }
inline s32 ai_sit_self() { return mem_s32(blk(AI_SIT_SELF)); }
inline s32 ai_sit_opp() { return mem_s32(blk(AI_SIT_OPP)); }

}  // namespace

// N64 0x801023CC: modes 27, 28, 93, 137, 140 and 142 go straight back to thinking.
void ai_mode_do_nothing() {
    ai_set_mode(ai_mode::Think);
}

// N64 0x80103A5C: Mode 38: the taunt unless the meter is in a mode or the taunt trait is 2, then think.
void ai_mode_38_evade_unless_special() {
    if (!(g_ai_self->flags_ee & 6) && mem_s8(kTraits[9].global) != 2)
        ai_stick_taunt();
    ai_set_mode(ai_mode::Think);
}

// N64 0x8010761C: Mode 71: holds d-pad down until its z is below 330.
void ai_mode_71_walk_down() {
    if ((f64)mem_f32(A_AI_SELF_POINT + 8) < 330.0)
        ai_set_mode(ai_mode::Think);
    ai_hold(0x400);
}

// N64 0x8010C434: Mode 130: standing, C-up with d-pad down; ends unless it's climbing (state 0x6F).
void ai_mode_130_c_up_down() {
    s32 st = g_ai_self->state;
    if (st == state::Standing) {
        ai_press(8);
        ai_hold(0x400);
    } else if (st != state::ClimbOnto) {
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x801050D0: Mode 52: C-left, held while approaching a downed opponent (situation 53).
void ai_mode_52_c_left() {
    if (ai_step() == 0) {
        ai_hold(2);
        ai_next_step();
    } else if (ai_step() == 1 && ai_sit_self() != 53) {
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x8010A9F4: Modes 111 / 112: in a rear lock-up (situation 13), C-down (111) or C-up (112).
void ai_mode_111_lockup_whip() {
    if (ai_sit_self() == 13 && (g_ai_self->flags78 & 0x80)) {
        u16 b = mem_s32(blk(AI_MODE)) == ai_mode::LockupWhip ? 4 : 8;
        ai_press(b);
        ai_hold(b);
    } else {
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x8010C51C: Mode 132: holds B while in the strike from the cage wall.
void ai_mode_132_strike_from_cage_wall() {
    if (ai_step() == 0) {
        ai_hold(0x4000);
        ai_next_step();
    } else if (ai_step() == 1 && g_ai_self->state != state::CageWallStrike) {
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x800FEA94: Mode 7: holds R for 1, 5, 9 or 13 frames.
void ai_mode_7_hold_r() {
    if (ai_step() == 0) {
        ai_hold(0x10);
        s32 r = rand_pool_next();
        mem_w32(blk(AI_MODE_TIMER), (u32)(r & 0xC));
        ai_next_step();
    } else if (ai_step() == 1) {
        s32 t = mem_s32(blk(AI_MODE_TIMER));
        ai_hold(0x10);
        t--;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t < 0)
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x80104428: Mode 43: waits ai_evade_timer frames, then A, and engages at its step 1.
void ai_mode_43_wait_then_grapple() {
    if (ai_step() == 0) {
        s32 t = ai_evade_timer();
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        ai_next_step();
    } else if (ai_step() == 1) {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t <= 0) {
            ai_hold(0x8000);
            ai_set_mode(ai_mode::EngageNearbyOpponent);
            mem_w32(blk(AI_STEP), 1);
        }
    }
}

// N64 0x8010C48C: Mode 131: R when the height difference is under the limit; ends once out of the block.
void ai_mode_131_block_when_level() {
    if (ai_step() == 0) {
        if ((f64)mem_f32(blk(AI_HEIGHT_DIFF)) < mem_f64(0x801506A0)) {
            ai_hold(0x10);
            ai_next_step();
        }
    } else if (ai_step() == 1 && g_ai_self->state != state::Blocking) {
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x801047C4: Mode 46: holds R while blocking (situation 14), the opponent in the same state, 3 or more frames from the
// end of its animation.
void ai_mode_46_hold_block() {
    if (ai_step() == 0) {
        ai_hold(0x10);
        ai_next_step();
    } else if (ai_step() == 1) {
        const Wrestler *o = g_ai_opp;
        if (ai_sit_self() == 14 && mem_s16(blk(AI_OPP_STATE)) == o->state && o->anim_player[AP_FRAMES] - 3 >= o->anim_frame)
            ai_hold(0x10);
        else
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x801074E4: Mode 70: its kick caught (situation 35): A or B at even odds, held.
void ai_mode_70_leg_caught() {
    if (ai_step() == 0) {
        mem_w16(blk(AI_PENDING), chance_check(500) ? 0x8000 : 0x4000);
        ai_next_step();
    } else if (ai_step() == 1) {
        if (ai_sit_self() == 35)
            ai_hold(mem_u16(blk(AI_PENDING)));
        else
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x80100478: Mode 20: R, then waits through the pick-up (situation 54); on to the mode to come back to.
void ai_mode_20_pickup() {
    switch (ai_step()) {
    case 0:
        ai_hold(0x10);
        ai_next_step();
        return;
    case 1:
        if (ai_sit_self() == 54) {
            mem_w32(blk(AI_STEP), 2);
            return;
        }
        mem_w16(blk(AI_RETURN_MODE), 0);
        ai_set_mode(ai_mode::Think);
        return;
    case 2:
        if (ai_sit_self() != 54)
            ai_set_mode(mem_s16(blk(AI_RETURN_MODE)));
        return;
    default:
        return;
    }
}

// N64 0x80109008: Mode 94: L at a slumped opponent; through situation 21, then on to the mode to come back to (the grapple at
// its step 1).
void ai_mode_94_corner_set_up() {
    switch (ai_step()) {
    case 0:
        ai_hold(0x20);
        ai_next_step();
        return;
    case 1:
        if (ai_sit_self() == 21) {
            mem_w32(blk(AI_STEP), 2);
        } else {
            mem_w16(blk(AI_RETURN_MODE), 0);
            ai_set_mode(ai_mode::Think);
        }
        return;
    case 2:
        if (ai_sit_self() != 21) {
            ai_set_mode(mem_s16(blk(AI_RETURN_MODE)));
            if (mem_u16(blk(AI_RETURN_MODE)) == ai_mode::GrappleApproach)
                mem_w32(blk(AI_STEP), 1);
        }
        return;
    default:
        return;
    }
}

// N64 0x801085D8: Mode 88: C-down; running (situation 2), within the list's reach (AI_LISTS +0x60), A (at a target on the mat,
// situation 48, the d-pad instead). Ends with situation 26 over.
void ai_mode_88_run_attack() {
    switch (ai_step()) {
    case 0:
        ai_hold(4);
        ai_next_step();
        return;
    case 1:
        if (ai_sit_self() != 2) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_f32(blk(AI_DIST_P)) < mem_f32(mem_u32(blk(AI_LISTS)) + 0x60)) {
            if (ai_sit_opp() != 48) {
                ai_press(0x8000);
                ai_hold(0x8000);
                ai_next_step();
                return;
            }
            ai_press(0xF00);
        }
        return;
    case 2:
        if (ai_sit_self() != 26)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x80100398: Mode 19: walks to a downed opponent (situation 15), R, then keeps R for the first steps of the pick-up (0x38).
void ai_mode_19_pick_up() {
    switch (ai_step()) {
    case 0:
        if (ai_sit_opp() != 15) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_f32(blk(AI_BODY_DIST)) < mem_f32(blk(AI_DIST)))
            ai_hold(ai_dir8_to_target());
        else
            mem_w32(blk(AI_STEP), 1);
        return;
    case 1:
        ai_hold(0x10);
        ai_next_step();
        return;
    case 2:
        if (g_ai_self->state != state::PickUpDownedOpponentAttacker) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (g_ai_self->state_step < 4)
            ai_hold(0x10);
        return;
    default:
        return;
    }
}

// N64 0x80105854: Mode 56: walks away from the opponent while it dives (situation 25) at the corner it is within 380 of.
void ai_mode_56_dodge_dive() {
    if (ai_step() == 0) {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        u16 b = dir8_buttons(dir8_to(&op, &sp));
        mem_w16(blk(AI_PENDING), b);
        ai_hold(b);
        ai_next_step();
    } else if (ai_step() == 1) {
        if (ai_sit_self() == 1 && 380.0 >= (f64)mem_f32(blk(AI_CORNERS) + 4u * (u32)g_ai_opp->dir) && ai_sit_opp() == 25)
            ai_hold(mem_u16(blk(AI_PENDING)));
        else
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x8010051C: Mode 21: L, then near (under 130), standing, against a grapple attempt (situation 11), a skill roll; L again at
// 100% (50% in the ring), else 42 or 43.
void ai_mode_21_counter_l() {
    if (ai_step() == 0) {
        ai_hold(0x20);
        ai_next_step();
        return;
    }
    if (ai_step() != 1)
        return;
    s32 m = ai_mode::Think;
    s32 sit = ai_sit_self();
    if (sit == 1) {
        if ((f64)mem_f32(blk(AI_DIST_P)) < 130.0 && chance_check(mem_s32(blk(AI_SKILL))) && ai_sit_opp() == 11) {
            s32 c = (mem_u16(blk(AI_WHERE)) & 0x101) ? 500 : 1000;
            if (chance_check(c))
                m = ai_mode::PressLButton;
            else
                m = c < 750 ? ai_mode::GrappleEvade : ai_mode::WaitThenGrapple;
        }
    } else if (sit == 14) {
        return;
    }
    ai_set_mode(m);
}

// Mode 115: with the opponent in situation 41, a follow-up strike timed on its recovery: walks in (past the list's
// limit) until 6 frames plus the list's +4 before its animation ends, then B, held through its strike.
void ai_mode_115_timed_follow_up_strike() {
    switch (ai_step()) {
    case 0:
        if (ai_sit_opp() != 41) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_MODE_TIMER), (u32)(g_ai_opp->anim_frames_left - 6 - mem_s16(mem_u32(blk(AI_LISTS)) + 4)));
        ai_next_step();
        return;
    case 1: {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t >= 0) {
            if (mem_f64(0x801505A8) < (f64)mem_f32(blk(AI_DIST)))
                ai_hold(ai_dir8_to_target());
        } else {
            ai_hold(0x4000);
            ai_next_step();
        }
        return;
    }
    case 2:
        if (ai_sit_self() != 8) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (g_ai_self->state == state::Strike)
            ai_hold(0x4000);
        return;
    default:
        return;
    }
}

// N64 0x80109F50: Mode 104: a short walk (0-12 frames) along or across by its ring zone, while standing.
void ai_mode_104_circle() {
    if (ai_step() == 0) {
        s32 z = mem_s16(blk(AI_ZONE_SELF));
        if (z == 0 || z == 2)
            mem_w16(blk(AI_PENDING), chance_check(500) ? 0x100 : 0x200);
        else if (z == 1 || z == 3)
            mem_w16(blk(AI_PENDING), chance_check(500) ? 0x800 : 0x400);
        s32 r = rand_pool_next();
        mem_w32(blk(AI_MODE_TIMER), (u32)(r & 0xC));
        ai_hold(mem_u16(blk(AI_PENDING)));
        ai_next_step();
    } else if (ai_step() == 1) {
        if (ai_sit_self() == 1) {
            s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
            mem_w32(blk(AI_MODE_TIMER), (u32)t);
            if (t > 0) {
                ai_hold(mem_u16(blk(AI_PENDING)));
                return;
            }
        }
        ai_set_mode(ai_mode::Think);
    }
}

// N64 0x80105BDC: Mode 60: walks 25-42 frames by ring zone and slot, while standing.
void ai_mode_60_walk_zone() {
    if (ai_step() == 0) {
        s32 z = mem_s16(blk(AI_ZONE_SELF));
        u16 b = 0;
        if (z == 0 || z == 2)
            b = (g_ai_self->slot & 2) ? 0x100 : 0x200;
        else if (z == 1 || z == 3)
            b = (g_ai_self->slot & 2) ? 0x800 : 0x400;
        mem_w16(blk(AI_PENDING), b);
        s32 r = rand_pool_next();
        ai_next_step();
        mem_w32(blk(AI_MODE_TIMER), (u32)(r % 18 + 25));
    } else if (ai_step() == 1) {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        ai_hold(mem_u16(blk(AI_PENDING)));
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t < 0 || ai_sit_self() != 1)
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x8010BCC4: Modes 121 / 122: the cage climb from the run: C-down toward the opponent's zone, running then C-up (121) or
// C-right (122) at the wall; the rebound on to step 2, the d-pad released at the run's end.
void ai_mode_121_cage_climb() {
    s32 st = g_ai_self->state;
    switch (ai_step()) {
    case 0:
        ai_press(4);
        ai_hold(mem_u16(0x8014D218 + 2u * (u32)g_ai_opp->joint_zone) | 4);
        ai_next_step();
        return;
    case 1:
        if (st == state::RunningRopeRebound) {
            mem_w32(blk(AI_STEP), 2);
            return;
        }
        if (st >= 5) {
            if (st != state::DropDown)
                ai_set_mode(ai_mode::Think);
            return;
        }
        if (st != state::Running) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_s32(blk(AI_MODE)) == ai_mode::CageClimbFromRun) {
            ai_press(8);
            ai_hold(8);
        }
        if (mem_s32(blk(AI_MODE)) == ai_mode::CageClimbFromRunAlternate) {
            ai_press(1);
            ai_hold(1);
        }
        return;
    case 2:
        if (st == state::Running)
            ai_press(0xF00);
        else if (st != state::RunningRopeRebound)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x80108A34: how long to wait before running at a runner headed for the ring side: its frames to that side past
// 100 (+18), plus its frames to come back over 460 less the reach (AI_LISTS +0x68) and 30, less the walk time and the
// list's +0x6C.
s32 ai_run_time(s32 walk_frames) {
    u32 lists = mem_u32(blk(AI_LISTS));
    s32 lead = walk_frames + mem_s16(lists + 0x6C);
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    f32 d = ai_side_distance(dir8_to(&sp, &op) / 2, &op);
    f32 speed = walk_speed(g_ai_opp, 8);
    f64 out = ((f64)d - 100.0) / (f64)speed;
    f32 reach = (f32)((f64)mem_f32(lists + 0x68) + 30.0);
    f32 back = (f32)(460.0 - (f64)reach) / speed;
    return (s32)((f32)((s32)out + 18) + back) - lead;
}

// N64 0x801088EC: Mode 90: a side of the ring to run to (ai_side_target); after ai_run_time frames with the opponent running
// (situation 3), C-down that way. A at the runner, or the d-pad released once it has stopped; a rebound keeps running.
void ai_mode_90_run_to() {
    switch (ai_step()) {
    case 0: {
        s32 out[2];
        if ((s8)ai_side_target(out)) {
            mem_w32(blk(AI_MODE_TIMER), (u32)ai_run_time(out[1]));
            mem_w16(blk(AI_PENDING), mem_u16(0x8014D218 + 2u * (u32)out[0]));
            ai_next_step();
            return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    case 1: {
        if (ai_sit_opp() != 3) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        s32 t = mem_s32(blk(AI_MODE_TIMER));
        if (t > 0) {
            mem_w32(blk(AI_MODE_TIMER), (u32)(t - 1));
        } else {
            ai_hold(mem_u16(blk(AI_PENDING)) | 4);
            ai_next_step();
        }
        return;
    }
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 4) {
            ai_set_mode(ai_mode::KeepRunning);
        } else if (sit == 26) {
            return;
        } else if (sit != 2) {
            ai_set_mode(ai_mode::Think);
        } else {
            ai_press(ai_sit_opp() == 3 ? 0x8000 : 0xF00);
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x80108238: Mode 86: a quarter turn round the opponent's facing, either way, while standing; near (under 120) off the apron a
// double skill roll goes to BlockDive.
void ai_mode_86_circle_then_block() {
    if (ai_step() == 0) {
        s32 d = angle_dir8(g_ai_opp->facing);
        d = chance_check(500) ? d + 2 : d - 2;
        mem_w16(blk(AI_PENDING), dir8_buttons(d & 7));
        ai_next_step();
        return;
    }
    if (ai_step() != 1)
        return;
    if (g_ai_self->state != state::Standing) {
        ai_set_mode(ai_mode::Think);
    } else {
        if (!(g_ai_self->location & 2) && (f64)mem_f32(blk(AI_DIST_P)) < 120.0) {
            s32 a = chance_check((s32)((f64)mem_s32(blk(AI_SKILL)) * 0.2));
            if (chance_check(a)) {
                ai_set_mode(ai_mode::BlockDive);
                return;
            }
        }
        ai_hold(mem_u16(blk(AI_PENDING)));
    }
    if (mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state)
        ai_set_mode(ai_mode::Think);
}

// N64 0x8010E3F4: Mode 144: elimination, after its opponent's fall (end_flags 0x01): runs at whoever is still in.
void ai_mode_144_chase_last() {
    const Wrestler *t = nullptr;
    if ((g_match_type & 0x4000) && (g_ai_opp->end_flags & 1)) {
        for (s32 i = 0; i < 4; i++) {
            const Wrestler *x = &g_wrestlers[i];
            if (x != g_ai_self && !(x->end_flags & 1)) {
                t = x;
                break;
            }
        }
    }
    if (!t) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (ai_step() == 0) {
        ai_hold(dir8_buttons(dir8_to(&g_ai_self->pos, &t->pos)) | 4);
        ai_next_step();
    } else if (ai_step() == 1) {
        s32 sit = ai_sit_self();
        if (sit != 2) {
            ai_set_mode(sit == 4 ? ai_mode::KeepRunning : ai_mode::Think);
            return;
        }
        if ((f64)dist_to(&g_ai_self->pos, &t->pos) < 200.0) {
            ai_press(0xF00);
            ai_set_mode(ai_mode::KeepRunning);
        }
    }
}

// N64 0x80109650: Mode 100: walks to an opponent on the ropes (within 90), L, through situation 51 (0x97), then the mode to come back
// to: the grapple at its step 1, modes 4 and 5 at step 4.
void ai_mode_100_ropes_l_move() {
    switch (ai_step()) {
    case 0:
        if ((f64)mem_f32(blk(AI_DIST)) < 90.0) {
            mem_w32(blk(AI_STEP), 1);
            return;
        }
        if (mem_s16(blk(AI_OPP_STATE)) != g_ai_opp->state) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_hold(ai_dir8_to_target());
        return;
    case 1:
        ai_hold(0x20);
        ai_next_step();
        return;
    case 2:
        if (ai_sit_self() == 51) {
            mem_w32(blk(AI_STEP), 3);
            return;
        }
        mem_w16(blk(AI_RETURN_MODE), 0);
        ai_set_mode(ai_mode::Think);
        return;
    case 3:
        if (ai_sit_self() != 51) {
            ai_set_mode(mem_s16(blk(AI_RETURN_MODE)));
            s32 r = mem_s16(blk(AI_RETURN_MODE));
            if (r == 2)
                mem_w32(blk(AI_STEP), 1);
            else if (r >= 4 && r < 6)
                mem_w32(blk(AI_STEP), 4);
        }
        return;
    default:
        return;
    }
}

// N64 0x80100850: for an opponent on the mat (flags80 0x08): the CPU's ground move from where it stands, chosen by
// the side (under 1024 of the opponent's facing), flags8a 0x01 and flags80 0x10 through 0x80145D74. 1 when the moveset
// marks that slot 0x200 or its animation is move 2843; such a move starts from a pick-up instead. Renamed from
// ai_behind_opponent.
s32 ai_ground_move_needs_pickup() {
    const Wrestler *o = g_ai_opp;
    if (!(o->flags80 & 8))
        return 0;
    bool near = abs_i(angle_diff(mem_s32(blk(AI_ANGLE)), o->facing)) < 1024;
    bool bit = (o->flags80 & 0x10) != 0;
    s32 i;
    if (o->flags8a & 1)
        i = near ? (bit ? 7 : 3) : (bit ? 5 : 1);
    else
        i = near ? (bit ? 4 : 0) : (bit ? 6 : 2);
    s32 slot = mem_s16(0x80145D74 + 2u * (u32)i);
    if (mem_u32(mem_u32(blk(AI_MOVESET)) + 36u * (u32)slot + 0x20) & 0x200)
        return 1;
    s16 anim = mem_s16(0x8008FA70 + 1096u * (u32)g_ai_self->slot + 2u * (u32)slot);
    return mem_s16(0x8015A700 + 2u * (u32)(s32)anim) + 13090 == 15933;
}

// N64 0x801006E0: Modes 23 / 24, the grapple: 23, when the ground move wants a pick-up, picks up (19). A while standing (A + B in finisher mode at a
// tired opponent, mode 23). In the grapple (situation 57): R on the floor with the count at under 3.
void ai_mode_23_grapple() {
    switch (ai_step()) {
    case 0:
        if (mem_s32(blk(AI_MODE)) == ai_mode::GrappleUnlessPickUpMove && ai_ground_move_needs_pickup()) {
            ai_set_mode(ai_mode::WalkToDownedOpponentAndPickUp);
            return;
        }
        ai_next_step();
        return;
    case 1: {
        s32 sit = ai_sit_self();
        if (sit != 1) {
            if (sit == 57)
                mem_w32(blk(AI_STEP), 2);
            else
                ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_s32(blk(AI_MODE)) == ai_mode::GrappleUnlessPickUpMove && mem_s16(blk(AI_OPP_SPIRIT)) < 10 && (g_ai_self->flags_ee & 4))
            ai_hold(0xC000);
        else
            ai_hold(0x8000);
        return;
    }
    case 2:
        if (ai_sit_self() != 57) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if ((g_ai_self->location & 4) && !(g_match_rules & 0x4000) && (mem_u8(A_COUNTOUT) & 1) && mem_s32(A_COUNTOUT_LEFT) < 3)
            ai_press(0x10);
        return;
    default:
        return;
    }
}

// N64 0x801090D4: Mode 95: the opponent running within 250: strike (6) at 30%, a chance by trait and spirit to wait else run at it
// (44); or striking: a chance by trait to wait; R as its strike window opens. Over when it does neither.
void ai_mode_95_vs_runner_reply() {
    if (ai_step() == 0) {
        s32 st = g_ai_opp->state;
        s32 t = mem_s8(0x8016B590);
        if (st == state::Running) {
            if (!((f64)mem_f32(blk(AI_DIST_P)) < 250.0))
                return;
            if (chance_check(300)) {
                ai_set_mode(ai_mode::Strike);
                return;
            }
            f64 sp = (f64)mem_s16(blk(AI_SPIRIT));
            if (!chance_check(mem_s16(0x80145F08 + 2u * (u32)t) + (s32)(sp + sp))) {
                ai_set_mode(ai_mode::RunAtOpponent);
                return;
            }
        } else {
            if (st != state::Strike) {
                ai_set_mode(ai_mode::Think);
                return;
            }
            if (!chance_check(mem_s16(0x80145F08 + 2u * (u32)t)))
                return;
        }
        ai_next_step();
    }
    if (ai_step() == 1) {
        s32 st = g_ai_opp->state;
        if (st != state::Running) {
            if (st != state::Strike) {
                ai_set_mode(ai_mode::Think);
                return;
            }
            if (g_ai_opp->anim_player[AP_HIT_FROM] - g_ai_opp->anim_frame - 1 <= 0)
                ai_hold(0x10);
        }
    }
}

// N64 0x8010D57C: Mode 136: holds d-pad up the ladder (rung under 5), B at 50% with the opponent or its partner within 80 and side-on,
// in the ring.
void ai_mode_136_strike_from_behind() {
    const Wrestler *s = g_ai_self;
    if (s->ladder_rung >= 5) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    ai_hold(0x800);
    auto near = [&](const Wrestler *x) {
        if (!(vec2_len(x->point_120.x - s->point_120.x, x->point_120.z - s->point_120.z) < 80.0f))
            return false;
        return (relative_position(s, x) & 2) && (x->location & 1) && chance_check(500);
    };
    if (near(g_ai_opp))
        ai_hold(0x4000);
    if (near(&g_wrestlers[(mem_u32(A_AI_OPP_PARTNER) - 0x80159990u) / 856u]))
        ai_hold(0x4000);
}

// N64 0x800FEB14: Mode 8: walks to a downed opponent (situation 15), then the cover: in the ring (or falls anywhere) step 4, L.
void ai_mode_8_go_pin() {
    if (ai_sit_opp() != 15) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    switch (ai_step()) {
    case 0:
        mem_w16(blk(AI_RETURN_MODE), 0);
        if (mem_f32(blk(AI_BODY_DIST)) >= mem_f32(blk(AI_DIST)))
            ai_next_step();
        else
            ai_hold(ai_dir8_to_target());
        return;
    case 1:
        if (mem_s32(blk(AI_MODE)) == ai_mode::GoPinDownedOpponent && !(mem_u16(blk(AI_WHERE)) & 1) && !(g_match_rules & 0x4000)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w32(blk(AI_STEP), 4);
        return;
    case 2:
        if (ai_sit_self() == 53)
            ai_next_step();
        else
            ai_set_mode(ai_mode::Think);
        return;
    case 3: {
        s32 sit = ai_sit_self();
        if (sit == 47)
            return;
        if (sit == 53 || sit == 1)
            ai_next_step();
        else
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 4:
        ai_hold(0x20);
        ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x8010735C: Mode 69: holding a caught kick: after a time from the style tables, A or B at even odds.
void ai_mode_69_kick_catch() {
    switch (ai_step()) {
    case 0: {
        s32 base = 15, spread = 7;
        if (!(g_ai_opp->flags_ee & 4)) {
            s32 t = mem_s8(0x8016B590);
            base = mem_s16(0x80145EF8 + 2u * (u32)t);
            spread = mem_s16(0x80145F00 + 2u * (u32)t);
            if (chance_check(100)) {
                base = 15;
                spread = 5;
            }
        }
        s32 r = rand_pool_next();
        ai_next_step();
        mem_w32(blk(AI_MODE_TIMER), (u32)(base + r % (2 * spread) - spread));
        return;
    }
    case 1: {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t < 0) {
            ai_press(chance_check(500) ? 0x8000 : 0x4000);
            ai_next_step();
        }
        return;
    }
    case 2:
        if (ai_sit_self() != 34)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x8010E26C: Mode 143: the opponent running: toward the teammate if near (under 180), engaging when within 160 of the opponent
// with the teammate in 0x50.
void ai_mode_143_tag_assist() {
    s32 st = g_ai_opp->state;
    if (st < 3 || (st >= 5 && st != state::IrishWhipRunner)) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    const Wrestler *x = &g_wrestlers[ai_teammate_slot()];
    if (x->roster_id == -1 || !(x->location & 1))
        return;
    Vec3 sp = ai_self_point();
    if (x->state == state::GrappleAttempt) {
        Vec3 op = ai_opp_point();
        if ((f64)dist_to(&sp, &op) < 160.0)
            ai_set_mode(ai_mode::EngageNearbyOpponent);
    } else if ((f64)dist_to(&sp, &x->point_120) < 180.0) {
        ai_hold(dir8_buttons(dir8_to(&sp, &x->point_120)));
    }
}

// N64 0x801044B8: Mode 44: runs (C-down) from the opponent's side through itself, turned a quarter away if that points back at the
// ring's centre from 100 out; after 8-15 frames releases, keeps running. Named RunAtOpponent from older notes.
void ai_mode_44_run_at() {
    if (ai_step() == 0) {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        s32 d = dir8_to(&op, &sp), out = d;
        if (100.0 < (f64)vec2_len(sp.x, sp.z)) {
            s32 c = angle_dir8(vec_xz_angle(sp.x, sp.z));
            if (abs_i(angle_diff(mem_s32(0x8014D238 + 4u * (u32)c), mem_s32(0x8014D238 + 4u * (u32)d))) < 768)
                out = (d + (chance_check(500) ? 2 : -2)) & 7;
        }
        ai_hold(dir8_buttons(out) | 4);
        s32 r = rand_pool_next();
        mem_w32(blk(AI_MODE_TIMER), (u32)((r & 7) + 8));
        ai_next_step();
    } else if (ai_step() == 1) {
        s32 sit = ai_sit_self();
        if (sit != 2) {
            ai_set_mode(sit == 4 ? ai_mode::KeepRunning : ai_mode::Think);
            return;
        }
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t < 0) {
            ai_press(0xF00);
            ai_set_mode(ai_mode::KeepRunning);
        }
    }
}

// N64 0x80103120: mode 35, gets down off the apron to the floor: gives up under the over-the-top rule, with the count
// nearly out, or the opponent in the ring. C-up toward the ropes' side, then at an opponent running or striking within
// 160, a skill roll: L at a run, R at a strike (always R on the floor); else waits on the apron (situation 49).
void ai_mode_35_drop_off_apron() {
    switch (ai_step()) {
    case 0: {
        if ((g_match_rules & 0x400) || ((mem_u8(A_COUNTOUT) & 1) && mem_s32(A_COUNTOUT_LEFT) < 4) || (g_ai_opp->location & 1)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_press(8);
        ai_hold(mem_u16(0x8014D218 + 2u * (u32)(s32)mem_s16(blk(AI_ZONE_SELF))));
        s32 sit = ai_sit_self();
        if (sit == 49)
            ai_next_step();
        else if (sit != 1)
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 1: {
        if (ai_sit_self() != 49) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        s32 so = ai_sit_opp();
        if ((f64)mem_f32(blk(AI_DIST_P)) < 160.0 && (so == 11 || so == 8))
            mem_w32(blk(AI_STEP), chance_check(mem_s32(blk(AI_SKILL))) ? 2 : 3);
        return;
    }
    case 2:
        if (ai_sit_self() != 49) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (g_ai_self->location & 2) {
            ai_hold(0x10);
            return;
        }
        if (ai_sit_opp() == 11)
            ai_hold(0x20);
        if (ai_sit_opp() == 8)
            ai_hold(0x10);
        return;
    case 3:
        if (ai_sit_self() != 49)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x80104E18: mode 51, the running grapple move: walks in (table 0x8014D228 by the opponent's zone) to 220, then
// C-down with a move from ai_pick_grapple_move (or, armed, B / R by rules 0x2000) and its reach. Running (step 2):
// within reach + 20 the move's button; beyond it, past 160 and a skill roll against an opponent not in 20 or 30, A / B
// to stop or R (weapon style 3 always stops). A rebound runs on (119).
void ai_mode_51_grapple_move() {
    const Wrestler *s = g_ai_self;
    switch (ai_step()) {
    case 0:
        if ((f64)mem_f32(blk(AI_DIST_P)) < 220.0)
            ai_hold(mem_u16(0x8014D228 + 2u * (u32)((s32)mem_s16(blk(AI_ZONE_OPP)) + 4)));
        else
            mem_w32(blk(AI_STEP), 1);
        return;
    case 1:
        ai_hold(4);
        if (mem_u8(0x8016B718 + (u32)s->slot) || !(s->flags_ef & 1)) {
            s32 idx;
            u16 b = ai_pick_grapple_move(1, (g_ai_opp->frame_flags & 8) ? 7 : 6, &idx);
            mem_w16(blk(AI_PENDING), b);
            mem_w32(blk(AI_REACH), mem_u32(mem_u32(blk(AI_LISTS)) + 8u * (u32)idx));
        } else {
            mem_w16(blk(AI_PENDING), (g_match_rules & 0x2000) ? 0x10 : 0x4000);
            mem_w32(blk(AI_REACH), mem_u32(mem_u32(blk(AI_LISTS)) + 0x70));
        }
        ai_next_step();
        return;
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 4) {
            ai_set_mode(ai_mode::KeepRunning);
            return;
        }
        if (sit == 5 || sit == 8)
            return;
        if (sit != 2) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (s->state_step != 2)
            return;
        if (!((f64)mem_f32(blk(AI_DIST)) < (f64)mem_f32(blk(AI_REACH)) + 20.0)) {
            s32 so = ai_sit_opp();
            if (so != 20 && so != 30 && chance_check(mem_s32(blk(AI_SKILL))) && 160.0 < (f64)mem_f32(blk(AI_DIST_P))) {
                if ((s->flags_ef & 1) && mem_s8(0x8016B9E0) == 3)
                    ai_press(0xF00);
                else if (chance_check(500))
                    ai_press(0xF00);
                else
                    ai_hold(0x10);
            }
        } else {
            ai_hold(mem_u16(blk(AI_PENDING)));
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x80105F14: on the apron, a roll of skill + spirit: at a running opponent (8) mode 46 or 47 by trait 35, at a
// striking one (11) mode 21 or 42 by trait 36. 1 when a mode was set.
s32 ai_apron_react() {
    if (!chance_check(mem_s32(blk(AI_SKILL)) + mem_s16(blk(AI_SPIRIT))))
        return 0;
    s32 so = ai_sit_opp();
    if (so == 8)
        ai_set_mode(chance_check(mem_s16(0x80145EF0 + 2u * (u32)(s32)mem_s8(0x8016B590))) ? 47 : 46);
    else if (so == 11)
        ai_set_mode(chance_check(mem_s16(0x80145EF0 + 2u * (u32)(s32)mem_s8(0x8016B9E1))) ? 42 : 21);
    else
        return 0;
    return 1;
}

// N64 0x80105D14: mode 61, walks the apron to the partner: 32 frames on one heading, then fresh. A skill roll at an
// opponent running or striking within 160 hands to ai_apron_react; C-up (the tag) within 80 of a partner standing or in
// 0x95. Over when off the apron or not standing.
void ai_mode_61_apron_walk() {
    const Wrestler *p = &g_wrestlers[(mem_u32(A_AI_PARTNER) - 0x80159990u) / 856u];
    if ((p->flags_e9 & 0x10) || apron_walk_dpad(p->slot) || !(p->location & 2)) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    Vec3 sp = ai_self_point();
    if (ai_step() == 0) {
        mem_w32(blk(AI_MODE_TIMER), 32);
        mem_w16(blk(AI_PENDING), dir8_buttons(dir8_to(&sp, &p->point_120)));
        ai_next_step();
        return;
    }
    if (ai_step() != 1)
        return;
    bool near = (f64)mem_f32(blk(AI_DIST_P)) < 160.0;
    ai_hold(mem_u16(blk(AI_PENDING)));
    if (near && chance_check(mem_s32(blk(AI_SKILL)))) {
        s32 so = ai_sit_opp();
        if ((so == 8 || so == 11) && ai_apron_react())
            return;
    }
    s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
    mem_w32(blk(AI_MODE_TIMER), (u32)t);
    if (t <= 0) {
        mem_w32(blk(AI_STEP), 0);
    } else if ((f64)dist_to(&sp, &p->point_120) < 80.0) {
        if (p->state != state::Standing && p->state != state::ApronPartnerWaiting) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_press(8);
    }
    if (g_ai_self->state != state::Standing)
        ai_set_mode(ai_mode::Think);
}

// N64 0x80109980: mode 102, the lock-up (situation 13): pushes toward its zone until it reaches the opponent's d-pad,
// sidestepping by which side of it the CPU is on; then the zone's direction and C-up (or C-right under g_match_type
// 0x10000). Over when the whip (28) ends.
void ai_mode_102_lockup_direction() {
    const Wrestler *s = g_ai_self;
    u16 toward = mem_u16(0x8014D218 + 2u * (u32)(s32)mem_s16(blk(AI_ZONE_SELF)));
    switch (ai_step()) {
    case 0:
        mem_w16(blk(AI_PENDING), toward);
        ai_next_step();
        return;
    case 1: {
        if (ai_sit_self() != 13) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        u16 pending = mem_u16(blk(AI_PENDING));
        if (pending & s->dpad_held) {
            mem_w32(blk(AI_STEP), 2);
            return;
        }
        if (!(pending & g_ai_opp->dpad_held)) {
            ai_hold(pending);
            return;
        }
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        s32 zone = mem_s16(blk(AI_ZONE_SELF));
        if (zone == 0 || zone == 2)
            mem_w16(blk(AI_PENDING), toward | (op.x < sp.x ? 0x100 : 0x200));
        else if (zone == 1 || zone == 3)
            mem_w16(blk(AI_PENDING), toward | (op.z < sp.z ? 0x800 : 0x400));
        return;
    }
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 13) {
            ai_hold(toward);
            ai_press((g_match_type & 0x10000) ? 1 : 8);
        } else if (sit != 28) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x80105934: modes 57 / 58, out of the opponent's corner (situation 25): the d-pad toward its corner (58 also
// C-down, then only that), until within 160 of it: standing to mode 1, running A / B and 119. The opponent leaving the
// corner ends it the same way, standing to Think.
void ai_mode_57_move_from_corner() {
    if (ai_step() == 0) {
        u16 b = mem_u16(0x8014D228 + 2u * (u32)(s32)g_ai_opp->dir);
        if (mem_s32(blk(AI_MODE)) == ai_mode::WalkOutOfCorner) {
            mem_w16(blk(AI_PENDING), b);
        } else {
            mem_w16(blk(AI_PENDING), 0);
            b |= 4;
        }
        ai_hold(b);
        ai_next_step();
        return;
    }
    if (ai_step() != 1)
        return;
    s32 sit = ai_sit_self();
    if (sit != 2) {
        if (sit >= 3) {
            ai_set_mode(sit == 4 ? ai_mode::KeepRunning : ai_mode::Think);
            return;
        }
        if (sit != 1 || mem_s32(blk(AI_MODE)) == ai_mode::WalkOutOfCornerAlternate) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_hold(mem_u16(blk(AI_PENDING)));
    }
    bool standing = g_ai_self->state == state::Standing;
    if (ai_sit_opp() != 25) {
        if (!standing)
            ai_press(0xF00);
        ai_set_mode(standing ? ai_mode::Think : ai_mode::KeepRunning);
        return;
    }
    if ((f64)mem_f32(blk(AI_CORNERS) + 4u * (u32)(s32)g_ai_opp->dir) < 160.0) {
        if (!standing)
            ai_press(0xF00);
        ai_set_mode(standing ? ai_mode::EngageNearbyOpponent : ai_mode::KeepRunning);
    }
}

// N64 0x8010B004: mode 116, C-down off two ropes (steps alternate running and rebounding), B within reach on the third
// run, then a finisher check after the strike (situation 10).
void ai_mode_116_run_ropes() {
    s32 sit = ai_sit_self();
    switch (ai_step()) {
    case 0:
    case 2:
        if (sit == 4)
            ai_next_step();
        else if (sit == 42 || sit == 55 || sit == 2)
            ai_hold(4);
        else
            ai_set_mode(ai_mode::Think);
        return;
    case 1:
    case 3:
        if (sit == 2) {
            ai_hold(4);
            ai_next_step();
        } else if (sit != 4) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    case 4:
        if (sit == 4) {
            ai_set_mode(ai_mode::KeepRunning);
        } else if (sit == 10) {
            mem_w32(blk(AI_STEP), 5);
        } else if (sit != 2) {
            ai_set_mode(ai_mode::Think);
        } else if (mem_f32(blk(AI_DIST)) < mem_f32(blk(AI_BODY_DIST))) {
            ai_press(0x4000);
            ai_hold(0x4000);
        }
        return;
    case 5:
        if (sit == 10 || sit == 47)
            return;
        if (sit != 1 || !ai_finisher_check())
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x8010BFC8: modes 124-126, walks along the wall with the ropes on one side: which way by the zone and the angle
// to the opponent; 124 d-pad up, 125 down, over the timer (up to 12), or at a zone's end (zone +0x26 / +0x28), or not
// standing. 126 ends at once.
void ai_mode_124_walk_dir() {
    if (ai_step() == 0) {
        bool ahead = mem_s32(blk(AI_ANGLE)) > 0;
        bool high = g_ai_self->joint_zone == 2;
        mem_w16(blk(AI_PENDING), ahead == high ? 512 : 256);
        s32 r = rand_pool_next();
        mem_w32(blk(AI_MODE_TIMER), (u32)(r & 0xC));
        ai_next_step();
        return;
    }
    if (ai_step() == 1) {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        const Wrestler *s = g_ai_self;
        if (t > 0 && s->state == state::Standing) {
            s32 mode = mem_s32(blk(AI_MODE));
            u32 z = s->zone_info;
            if (mode == ai_mode::WalkInDirectionVariant1) {
                if (!(z && (mem_u16(z + 0x26) & 0x800) && (mem_u16(z + 0x28) & 8))) {
                    ai_hold(0x800);
                    return;
                }
            } else if (mode == ai_mode::WalkInDirectionVariant2) {
                if (!(z && (mem_u16(z + 0x26) & 0x400) && (mem_u16(z + 0x28) & 4))) {
                    ai_hold(0x400);
                    return;
                }
            } else if (mode != ai_mode::WalkInDirectionVariant3) {
                return;
            }
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (ai_step() == 2) {
        ai_set_mode(ai_mode::Think);
    } else if (ai_step() == 3) {
        s32 st = g_ai_self->state;
        if (st != 2 && st != 48)
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x80108E28: mode 92, a downed opponent (situation 15): runs (C-down) to the corner of its quarter; within 60 of
// it standing, then A when it holds the way there. 27 is the next step's landing.
void ai_mode_92_go_to_corner() {
    s32 q = mem_s16(blk(AI_QUAD_OPP));
    u16 toward = mem_u16(0x8014D228 + 2u * (u32)q);
    switch (ai_step()) {
    case 0:
        mem_w16(blk(AI_PENDING), toward | 4);
        ai_hold(toward | 4);
        ai_next_step();
        return;
    case 1: {
        s32 sit = ai_sit_self();
        if (ai_sit_opp() != 15) {
            ai_set_mode(ai_mode::Think);
        } else if (sit == 4) {
            ai_set_mode(ai_mode::KeepRunning);
        } else if (sit == 42) {
            ai_hold(mem_u16(blk(AI_PENDING)));
        } else if (sit == 1) {
            u32 c = 0x8014D1A8 + 12u * (u32)q;
            Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
            Vec3 sp = ai_self_point();
            if ((f64)dist_to(&corner, &sp) < 60.0)
                ai_next_step();
            else
                ai_hold(mem_u16(blk(AI_PENDING)));
        } else if (sit != 2) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 27) {
            mem_w32(blk(AI_STEP), 3);
            return;
        }
        if (sit != 1 || ai_sit_opp() != 15) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_hold(toward);
        if (toward & g_ai_self->dpad_held)
            ai_press(0x8000);
        return;
    }
    default:
        return;
    }
}

// N64 0x8010BE00: mode 123, standing: walks to within 180 of the middle of the ring along the opponent's side, then
// C-right holding the way to that side. Over when not standing (or in 0xC8 at step 1).
void ai_mode_123_walk_to_edge_then_c_right() {
    const Wrestler *s = g_ai_self;
    if (ai_step() == 0) {
        if (s->state != state::Standing) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        Vec3 sp = ai_self_point();
        s32 zone = g_ai_opp->joint_zone;
        if (zone == 0 || zone == 2) {
            if (180.0 < (f64)sp.x)
                ai_hold(0x200);
            else if ((f64)sp.x < -180.0)
                ai_hold(0x100);
            else
                ai_next_step();
        } else if (zone == 1 || zone == 3) {
            if (180.0 < (f64)sp.z)
                ai_hold(0x400);
            else if ((f64)sp.z < -180.0)
                ai_hold(0x800);
            else
                ai_next_step();
        }
    } else if (ai_step() == 1) {
        if (s->state == state::Standing) {
            ai_press(1);
            ai_hold(mem_u16(0x8014D218 + 2u * (u32)g_ai_opp->joint_zone) | 1);
        } else if (s->state != state::CageClimbStart) {
            ai_set_mode(ai_mode::Think);
        }
    }
}

// N64 0x801097A8: mode 101, heads to an arena area by C-left along its own side (never under rules 0x2000): gives up at
// an opponent in the corner (25, 26) or on the floor within 200; beyond z 560 turns along the barrier. A chance by the
// attrs' +6 to try again once there (situation 50).
void ai_mode_101_go_to_area() {
    if (g_match_rules & 0x2000) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    const Wrestler *s = g_ai_self;
    switch (ai_step()) {
    case 0:
        mem_w16(blk(AI_PENDING), mem_u16(0x8014D218 + 2u * (u32)(s32)mem_s16(blk(AI_ZONE_SELF))));
        ai_next_step();
        return;
    case 1: {
        s32 sit = ai_sit_self();
        if (sit != 1) {
            if (sit == 50)
                mem_w32(blk(AI_STEP), 2);
            else
                ai_set_mode(ai_mode::Think);
            return;
        }
        s32 so = ai_sit_opp();
        if (so == 25 || so == 26 || ((g_ai_opp->location & 4) && (f64)mem_f32(blk(AI_DIST_P)) < 200.0)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        Vec3 sp = ai_self_point();
        if (560.0 < (f64)sp.z) {
            s32 q = mem_s16(blk(AI_QUAD_SELF));
            if (q >= 0 && q < 2)
                mem_w16(blk(AI_PENDING), 256);
            else if (q >= 2 && q < 4)
                mem_w16(blk(AI_PENDING), 512);
        }
        ai_press(2);
        ai_hold(mem_u16(blk(AI_PENDING)));
        return;
    }
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 50)
            return;
        if (sit == 1 && !(s->flags_ef & 1) && chance_check(mem_s16(0x80145F10 + 2u * (u32)(s32)mem_s8(s->attrs + 6)))) {
            mem_w32(blk(AI_STEP), 1);
            return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    }
    default:
        return;
    }
}

// N64 0x8010AA5C: mode 113, the running special (slot 232 of the move anims must be one of a few moves; never in
// special mode or trait 39 at 2): C-down to the far corner when it is over 100 away, then stick up.
void ai_mode_113_special_move() {
    if (ai_step() == 0) {
        const Wrestler *s = g_ai_self;
        s16 anim = mem_s16(0x8008FA70 + 1096u * (u32)s->slot + 2u * 232u);
        s32 m = mem_s16(0x8015A700 + 2u * (u32)(s32)anim) + 13090;
        bool ok = m == 17142 || m == 17282 || m == 17139 || m == 17140 || (m >= 17285 && m < 17287) || (m >= 17288 && m < 17290) || (m >= 17502 && m < 17505);
        if ((s->flags_ee & 2) || mem_s8(0x8016B9CE) == 2 || !ok) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_press(4);
        u32 q = mem_u16(blk(AI_QUAD_OPP));
        u32 idx = 100.0 < (f64)mem_f32(blk(AI_CORNERS) + 4u * (q ^ 2)) ? q : q ^ 1;
        ai_hold(mem_u16(0x8014D230 + 2u * idx) | 4);
        ai_next_step();
    } else if (ai_step() == 1) {
        s32 sit = ai_sit_self();
        if (sit == 4)
            ai_set_mode(ai_mode::KeepRunning);
        else if (sit == 2 || sit == 43)
            mem_w8(blk(AI_STICK_Y), 80);
        else
            ai_set_mode(ai_mode::Think);
    }
}

// N64 0x80107668: mode 72, along its side toward the corner of its team (slot 2's other way): the side's d-pad, past
// 330 the other; over at the team's own quarter.
void ai_mode_72_walk_to_centre() {
    Vec3 sp = ai_self_point();
    s32 zone = mem_s16(blk(AI_ZONE_SELF));
    s32 q = mem_s16(blk(AI_QUAD_SELF));
    u16 b;
    if (g_ai_self->slot != 2) {
        if (zone == 0 || zone == 1) {
            b = zone == 0 ? 256 : 2048;
            if (q == 0)
                ai_set_mode(ai_mode::Think);
        } else if (zone == 2) {
            b = 330.0 < (f64)sp.x ? 2048 : 256;
        } else if (zone == 3) {
            b = 330.0 < (f64)sp.z ? 256 : 2048;
        } else {
            ai_set_mode(ai_mode::Think);
            return;
        }
    } else {
        if (zone == 0) {
            b = (f64)sp.x < -330.0 ? 1024 : 512;
        } else if (zone == 1) {
            b = (f64)sp.z < -330.0 ? 512 : 1024;
        } else if (zone == 2 || zone == 3) {
            b = zone == 2 ? 512 : 1024;
            if (q == 2)
                ai_set_mode(ai_mode::Think);
        } else {
            ai_set_mode(ai_mode::Think);
            return;
        }
    }
    ai_hold(b);
}

// N64 0x80107854: modes 73-76, at a downed opponent: L-side grapple: 73 and 75 when face up, 74 and 76 when face down
// C-left then wait for it to resolve (53, 47, then standing); otherwise, the opponent still on the mat (flags80 0x08),
// 75 and 76 first get it lying by flags80 0x10 (mode 20, coming back), then B.
void ai_mode_73_c_left_at_downed_opponent() {
    const Wrestler *o = g_ai_opp;
    s32 mode = mem_s32(blk(AI_MODE));
    switch (ai_step()) {
    case 0: {
        mem_w16(blk(AI_RETURN_MODE), 0);
        bool up = o->flags8a & 1;
        if (mode == ai_mode::CLeftAtDownedOpponentVariant2 || mode == ai_mode::CLeftAtDownedOpponentVariant4)
            up = !up;
        else if (mode != ai_mode::CLeftAtDownedOpponentVariant1 && mode != ai_mode::CLeftAtDownedOpponentVariant3)
            return;
        if (up) {
            ai_hold(2);
            ai_next_step();
        } else {
            mem_w32(blk(AI_STEP), 3);
        }
        return;
    }
    case 1:
        if (ai_sit_self() == 53)
            ai_next_step();
        else
            ai_set_mode(ai_mode::Think);
        return;
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 1)
            ai_next_step();
        else if (sit != 47 && sit != 53)
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 3:
        if (!(o->flags80 & 8)) {
            ai_set_mode(ai_mode::Think);
        } else if (mode == ai_mode::CLeftAtDownedOpponentVariant1 || mode == ai_mode::CLeftAtDownedOpponentVariant2) {
            ai_next_step();
        } else if (mode == ai_mode::CLeftAtDownedOpponentVariant3 || mode == ai_mode::CLeftAtDownedOpponentVariant4) {
            if (o->flags80 & 0x10) {
                ai_next_step();
            } else {
                mem_w16(blk(AI_RETURN_MODE), (u16)mode);
                ai_set_mode(ai_mode::PickUpDownedOpponent);
            }
        }
        return;
    case 4:
        if (!(o->flags80 & 8)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_hold(0x4000);
        ai_next_step();
        return;
    case 5:
        if (ai_sit_self() != 9)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// N64 0x80100174: modes 16-18, a runner (situation 3) meeting the CPU face to face: within 150 (17: within 200) A for
// 3 (15) frames, 18 up-stick in finisher mode; then held through the catch (11).
void ai_mode_16_meet_runner() {
    switch (ai_step()) {
    case 0:
        if (ai_sit_opp() != 3)
            ai_set_mode(ai_mode::Think);
        if (abs_i(angle_diff(g_ai_self->facing, g_ai_opp->facing)) >= 1793)
            ai_next_step();
        return;
    case 1: {
        s32 mode = mem_s32(blk(AI_MODE));
        f64 d = (f64)mem_f32(blk(AI_DIST_P));
        if (mode == ai_mode::CatchRunnerVariant2) {
            if (d < 200.0) {
                mem_w32(blk(AI_MODE_TIMER), 15);
                ai_next_step();
            }
        } else if ((mode == ai_mode::CatchRunnerVariant1 || mode == ai_mode::CatchRunnerVariant3) && d < 150.0) {
            mem_w32(blk(AI_MODE_TIMER), 3);
            ai_next_step();
        }
        if (ai_sit_opp() != 3)
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 2: {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t > 0) {
            if (mem_s32(blk(AI_MODE)) == ai_mode::CatchRunnerVariant3 && (g_ai_self->flags_ee & 4))
                mem_w8(blk(AI_STICK_Y), 80);
            else
                ai_hold(0x8000);
        }
        s32 sit = ai_sit_self();
        if (sit == 11)
            ai_next_step();
        else if (sit != 1)
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 3:
        if (ai_sit_self() == 11) {
            s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
            mem_w32(blk(AI_MODE_TIMER), (u32)t);
            if (t > 0)
                ai_hold(0x8000);
        } else {
            ai_set_mode(ai_mode::Think);
        }
        return;
    default:
        return;
    }
}

// N64 0x80107584: the mash rate for struggles: 0.75 under 65 health, 0.65 under 129, else 0.55, plus spirit's share.
f32 ai_struggle_rate() {
    s32 h = mem_s16(blk(AI_HEALTH));
    f32 base = h < 129 ? (h < 65 ? 0.75f : 0.65f) : 0.55f;
    return (f32)((f64)base + (f64)(16 * mem_s16(blk(AI_SPIRIT)) / 100 - 8) * 0.01);
}

// N64 0x8010D70C: mode 138, climbing a ladder with the opponent also on one: blocks a strike (a skill roll, ai_block_timer
// frames, then R), B on a low roll; else d-pad up and the struggle's mash (always, far behind at levels 5 and 6). A at
// the top (0xBD) comes back in 0xB1.
void ai_mode_138_ladder_top() {
    s32 step = ai_step();
    if (step == 2) {
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t <= 0) {
            ai_hold(0x10);
            mem_w32(blk(AI_STEP), 0);
        }
        return;
    }
    if (step != 0 && step != 1) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    if (step == 0) {
        mem_w16(blk(AI_PENDING), 0);
        ai_next_step();
    }
    const Wrestler *s = g_ai_self;
    if (g_ai_opp->location & 0x10) {
        if (ai_sit_opp() == 8 && chance_check(mem_s32(blk(AI_SKILL)))) {
            s32 t = ai_block_timer();
            mem_w32(blk(AI_MODE_TIMER), (u32)t);
            if (t) {
                ai_next_step();
                return;
            }
        }
        if (chance_check(mem_s32(blk(AI_SKILL)) / 15)) {
            ai_hold(0x4000);
            return;
        }
    }
    ai_hold(0x800);
    s32 rate = (s32)((f32)mem_s32(blk(AI_SKILL)) * ai_struggle_rate());
    s32 level = mem_s32(blk(AI_LEVEL));
    if (level < 7 && level >= 5 && (g_ai_opp->flags_e9 & 8) && mem_s16(blk(AI_OPP_HEALTH)) - mem_s16(blk(AI_HEALTH)) >= 41)
        rate = 1000;
    if (chance_check(rate))
        ai_press(0xC030);
    if (s->state == 189 && s->state_step > 0)
        mem_w16(blk(AI_PENDING), 0x8000);
    if (s->state == 177)
        ai_hold(mem_u16(blk(AI_HELD)) | mem_u16(blk(AI_PENDING)));
}

// N64 0x8010C9D8: mode 134, standing: walks until 80 ahead of it is about the centre of the ring (within a perceived
// 20), then C-left.
void ai_mode_134_drag_or_flip() {
    switch (ai_step()) {
    case 0:
        mem_wf32(blk(AI_REACH), ai_perceive(20.0f, mem_f32(0x80145F80 + 4u * (u32)mem_s32(blk(AI_LEVEL)))));
        ai_next_step();
        return;
    case 1: {
        if (ai_sit_self() != 1) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        const Wrestler *s = g_ai_self;
        f32 a = angle_to_float(s->facing);
        f32 x = -(s->point_120.x + sinf_n64(a) * 80.0f);
        f32 z = -(s->point_120.z + cosf_n64(a) * 80.0f);
        if (vec2_len(x, z) < mem_f32(blk(AI_REACH))) {
            ai_next_step();
            return;
        }
        if (8.0f < absf(x))
            ai_hold(mem_u16(blk(AI_HELD)) | (x < 0.0f ? 0x200 : 0x100));
        if (8.0f < absf(z))
            ai_hold(mem_u16(blk(AI_HELD)) | (z < 0.0f ? 0x400 : 0x800));
        return;
    }
    case 2:
        if (ai_sit_self() == 1) {
            ai_press(2);
            ai_hold(2);
        }
        ai_set_mode(ai_mode::Think);
        return;
    default:
        ai_set_mode(ai_mode::Think);
        return;
    }
}

// N64 0x8010711C: mode 68, C-down (the way it faced when the roster's row is 118). Running: B within 150 at step 2; with
// flags86 0x02, turned back away from the partner, 119. Standing again: 20 frames a quarter round the opponent.
void ai_mode_68_run() {
    const Wrestler *s = g_ai_self;
    switch (ai_step()) {
    case 0:
        ai_hold(4);
        if (mem_s16(0x8014521E + 20u * (u32)s->slot) == 118)
            ai_hold(mem_u16(blk(AI_PENDING)) | 4);
        ai_next_step();
        return;
    case 1: {
        s32 sit = ai_sit_self();
        if (sit == 2) {
            if (s->state_step == 2 && (f64)mem_f32(blk(AI_DIST_P)) < 150.0) {
                ai_hold(0x4000);
                return;
            }
            if (s->flags86 & 2) {
                Vec3 sp = ai_self_point(), op = ai_opp_point();
                const Wrestler *p = &g_wrestlers[(mem_u32(A_AI_PARTNER) - 0x80159990u) / 856u];
                s32 d = abs_i(angle_diff(angle_to(&sp, &op), angle_to(&sp, &p->point_120)));
                if (2048 - d < 227)
                    ai_set_mode(ai_mode::KeepRunning);
            }
        } else if (sit == 1) {
            Vec3 sp = ai_self_point(), op = ai_opp_point();
            mem_w16(blk(AI_PENDING), dir8_buttons((dir8_to(&sp, &op) + 2) & 7));
            mem_w32(blk(AI_MODE_TIMER), 20);
            ai_next_step();
        } else if (sit == 4) {
            ai_set_mode(ai_mode::KeepRunning);
        } else if (sit != 8) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    case 2: {
        if (s->state != state::Standing) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        s32 t = mem_s32(blk(AI_MODE_TIMER)) - 1;
        mem_w32(blk(AI_MODE_TIMER), (u32)t);
        if (t > 0)
            ai_hold(mem_u16(blk(AI_PENDING)));
        else
            ai_set_mode(ai_mode::Think);
        return;
    }
    default:
        return;
    }
}

// N64 0x801086AC: mode 89, at a lying (19) or dazed (16) opponent: a side to line up on (ai_line_up). Lying with time to
// spare it waits, within 50 on the axis, for that many frames left; then C-down and A.
void ai_mode_89_vs_lying() {
    const Wrestler *o = g_ai_opp;
    switch (ai_step()) {
    case 0: {
        s32 out[2];
        if (!(s8)ai_line_up(out)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        mem_w16(blk(AI_PENDING), mem_u16(0x8014D218 + 2u * (u32)out[0]));
        mem_w32(blk(0x9C), (u32)out[1]);
        s32 so = ai_sit_opp();
        if (so == 16) {
            if (out[1] >= mem_s16(blk(AI_OPP_DOWN))) {
                ai_set_mode(ai_mode::Think);
                return;
            }
        } else if (so != 19) {
            ai_set_mode(ai_mode::Think);
            return;
        } else if (out[1] < o->anim_frames_left) {
            mem_w32(blk(AI_STEP), 1);
            return;
        }
        mem_w32(blk(AI_STEP), 2);
        return;
    }
    case 1: {
        s32 so = ai_sit_opp();
        if (so == 16) {
            mem_w32(blk(AI_STEP), 2);
            return;
        }
        if (so != 19) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        u16 b = mem_u16(blk(AI_PENDING));
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        f32 d = (b == 1024 || b == 2048) ? sp.x - op.x : sp.z - op.z;
        if (50.0 < (f64)absf(d)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_s32(blk(0x9C)) >= o->anim_frames_left)
            ai_next_step();
        return;
    }
    case 2: {
        s32 so = ai_sit_opp();
        if (so != 16 && so != 19) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_hold(mem_u16(blk(AI_PENDING)) | 4);
        ai_next_step();
        return;
    }
    case 3: {
        s32 sit = ai_sit_self();
        if (sit == 4)
            ai_set_mode(ai_mode::KeepRunning);
        else if (sit == 2)
            ai_press(0x8000);
        else if (sit != 26)
            ai_set_mode(ai_mode::Think);
        return;
    }
    default:
        return;
    }
}

s32 side_point(const Vec3 *p, s32 angle, Vec3 *out);

// N64 0x80106210: mode 62's way through the ropes (AI_TIMER the step; attrs +0x38 0x04): C-down along the side whose
// corner is farther from the opponent's line, then at the rebound A / B. 0 when the CPU is doing something else.
s32 ai_go_outside_steps() {
    s32 sit = ai_sit_self();
    switch (mem_s32(blk(AI_TIMER))) {
    case 0: {
        Vec3 op = ai_opp_point();
        Vec3 at;
        s32 first = (mem_s16(blk(AI_ZONE_OPP)) + 1) & 3;
        u32 c = 0x8014D1A8 + 12u * (u32)first;
        Vec3 corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
        side_point(&corner, angle_to(&corner, &op), &at);
        f32 d1 = dist_to(&corner, &at);
        s32 second = (s16)(mem_u16(blk(AI_ZONE_OPP)) ^ 2);
        c = 0x8014D1A8 + 12u * (u32)second;
        corner = {mem_f32(c), mem_f32(c + 4), mem_f32(c + 8)};
        side_point(&corner, angle_to(&corner, &op), &at);
        f32 d2 = dist_to(&corner, &at);
        ai_hold(mem_u16(0x8014D228 + 2u * (u32)(d2 < d1 ? first : second)) | 4);
        mem_w32(blk(AI_TIMER), 1);
        return 1;
    }
    case 1:
        if (sit == 2) {
            mem_w32(blk(AI_TIMER), 2);
            return 1;
        }
        if (sit == 4) {
            mem_w32(blk(AI_TIMER), 3);
            return 1;
        }
        return 0;
    case 2:
        if (sit == 2)
            return 1;
        if (sit == 1) {
            ai_next_step();
            return 1;
        }
        if (sit == 4) {
            mem_w32(blk(AI_TIMER), 3);
            return 1;
        }
        return 0;
    case 3:
        if (sit == 2) {
            ai_press(0xF00);
            return 1;
        }
        return sit == 4;
    default:
        return 1;
    }
}

// N64 0x80105FC8: mode 62, at an opponent on the floor: from its side, walks back from the ropes to 260, then C-down
// and A over; or (attrs +0x38 0x04) by ai_go_outside_steps. A rebound off the far ropes goes on to the R dive, a taunt
// at its end.
void ai_mode_62_go_outside() {
    switch (ai_step()) {
    case 0: {
        if (!(g_ai_opp->location & 4) || ai_sit_opp() == 48) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (mem_u32(g_ai_self->attrs + 0x38) & 4) {
            if (!ai_go_outside_steps())
                ai_set_mode(ai_mode::Think);
            return;
        }
        Vec3 sp = ai_self_point();
        s32 zone = mem_s16(blk(AI_ZONE_OPP));
        if ((f64)ai_side_distance(zone, &sp) > 260.0)
            ai_next_step();
        else
            ai_hold(mem_u16(0x8014D220 + 2u * (u32)zone));
        return;
    }
    case 1:
        ai_hold(4);
        ai_next_step();
        return;
    case 2: {
        s32 sit = ai_sit_self();
        if (sit == 4)
            mem_w32(blk(AI_STEP), 4);
        else if (sit == 26)
            ai_next_step();
        else if (sit == 2)
            ai_hold(0x8000);
        else
            ai_set_mode(ai_mode::Think);
        return;
    }
    case 3:
        if (ai_sit_self() != 26)
            ai_set_mode(ai_mode::Think);
        return;
    case 4: {
        s32 sit = ai_sit_self();
        if (sit == 2) {
            ai_hold(0x10);
            ai_press(0x10);
        } else if (sit == 5) {
            if (mem_s8(0x8016B9CE) != 2 && !(g_ai_self->flags_ee & 2))
                ai_stick_taunt();
        } else if (sit != 4 && sit != 43) {
            ai_set_mode(ai_mode::Think);
        }
        return;
    }
    default:
        return;
    }
}

// N64 0x8010AC3C: mode 114, an L sidestep at a runner: picks a side (with AI_FLAGS 0x40 and that side near, 70% the
// d-pad away from it). Standing, once the runner has turned in within 260: L, with the side by attrs +0x34. A hold-off
// (58, 47) ends in mode 15.
void ai_mode_114_l_vs_runner() {
    const Wrestler *s = g_ai_self;
    switch (ai_step()) {
    case 0: {
        Vec3 sp = ai_self_point(), op = ai_opp_point();
        s32 side = (dir8_to(&sp, &op) / 2) ^ 2;
        f32 d = ai_side_distance(side, &sp);
        mem_w32(blk(0x9C), mem_u16(0x8014D218 + 2u * (u32)side));
        if ((mem_u8(blk(AI_FLAGS)) & 0x40) && (f64)d < 200.0 && chance_check(700))
            mem_w16(blk(AI_PENDING), (u16)mem_s32(blk(0x9C)));
        else
            mem_w16(blk(AI_PENDING), 0);
        ai_next_step();
        return;
    }
    case 1:
        if (ai_sit_self() != 1) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if (abs_i(angle_diff(s->facing, g_ai_opp->facing)) >= 1025 && (f64)mem_f32(blk(AI_DIST_P)) < 260.0) {
            ai_hold(0x20);
            if (mem_u32(s->attrs + 0x34) & 0x100000)
                ai_hold((u16)mem_s32(blk(0x9C)) | 0x20);
            if ((mem_u32(s->attrs + 0x34) & 0x400000) && chance_check(500))
                ai_hold(mem_u16(blk(AI_HELD)) | (u16)mem_s32(blk(0x9C)));
            ai_next_step();
            return;
        }
        if ((u32)(ai_sit_opp() - 2) < 2) {
            if (!(mem_u16(blk(AI_PENDING)) & s->dpad_held))
                ai_hold(mem_u16(blk(AI_PENDING)));
            return;
        }
        ai_set_mode(ai_mode::Think);
        return;
    case 2:
        if (ai_sit_self() == 58)
            mem_w32(blk(AI_STEP), 3);
        else
            ai_set_mode(ai_mode::Think);
        return;
    case 3: {
        s32 sit = ai_sit_self();
        if (sit != 47 && sit != 58)
            ai_set_mode(ai_mode::PartnerDecision);
        return;
    }
    default:
        return;
    }
}

// N64 0x8010838C: mode 87, in the opponent's zone, on the apron: walks until within 30 of it on the side's axis, then
// (an opponent not bouncing, not in 48) A with the d-pad toward it, and waits out the leap (situation 26).
void ai_mode_87_walk_to_opponent_zone_axis() {
    s32 zone = mem_s16(blk(AI_ZONE_SELF));
    if (zone != mem_s16(blk(AI_ZONE_OPP)) || zone < 0 || zone > 3) {
        ai_set_mode(ai_mode::Think);
        return;
    }
    Vec3 sp = ai_self_point(), op = ai_opp_point();
    bool x_axis = zone == 0 || zone == 2;
    f32 own = x_axis ? sp.x : sp.z;
    f32 other = x_axis ? op.x : op.z;
    f32 d = absf(own - other);
    switch (ai_step()) {
    case 0:
        mem_w32(blk(AI_STEP), 1);
        return;
    case 1:
        if (g_ai_self->state != state::Standing) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        if ((f64)d < 30.0) {
            mem_w32(blk(AI_STEP), 2);
            return;
        }
        if (x_axis)
            ai_hold(other < own ? 512 : 256);
        else
            ai_hold(other < own ? 1024 : 2048);
        return;
    case 2:
        if (g_ai_opp->flags80 & 1)
            return;
        if (ai_sit_opp() == 48 || !(g_ai_self->location & 2)) {
            ai_set_mode(ai_mode::Think);
            return;
        }
        ai_press(0x8000);
        ai_hold(mem_u16(0x8014D218 + 2u * (u32)(s32)mem_s16(blk(AI_ZONE_OPP))));
        ai_next_step();
        return;
    case 3:
        if (ai_sit_self() != 26)
            ai_set_mode(ai_mode::Think);
        return;
    default:
        return;
    }
}

// The mode table g_ai_modes (0x80145A10, 145 entries): the modes decompiled so far; any other stops a
// replay, naming the mode.
void ai_mode_run(s32 mode) {
    switch (mode) {
    case ai_mode::CLeftAtDownedOpponentVariant1: ai_mode_73_c_left_at_downed_opponent(); return;
    case ai_mode::CLeftAtDownedOpponentVariant2: ai_mode_73_c_left_at_downed_opponent(); return;
    case ai_mode::CLeftAtDownedOpponentVariant3: ai_mode_73_c_left_at_downed_opponent(); return;
    case ai_mode::CLeftAtDownedOpponentVariant4: ai_mode_73_c_left_at_downed_opponent(); return;
    case ai_mode::CatchRunnerVariant1: ai_mode_16_meet_runner(); return;
    case ai_mode::CatchRunnerVariant2: ai_mode_16_meet_runner(); return;
    case ai_mode::CatchRunnerVariant3: ai_mode_16_meet_runner(); return;
    case ai_mode::OnLadderAgainstOpponent: ai_mode_138_ladder_top(); return;
    case ai_mode::DragOrFlipDownedOpponent: ai_mode_134_drag_or_flip(); return;
    case ai_mode::RunInDirection: ai_mode_68_run(); return;
    case ai_mode::LineUpOnLyingOpponent: ai_mode_89_vs_lying(); return;
    case ai_mode::GoOutsideToOpponent: ai_mode_62_go_outside(); return;
    case ai_mode::LButtonAgainstRunner: ai_mode_114_l_vs_runner(); return;
    case ai_mode::WalkToOpponentZoneAxis: ai_mode_87_walk_to_opponent_zone_axis(); return;
    case ai_mode::WalkOutOfCorner: ai_mode_57_move_from_corner(); return;
    case ai_mode::WalkOutOfCornerAlternate: ai_mode_57_move_from_corner(); return;
    case ai_mode::RunOffRopes: ai_mode_116_run_ropes(); return;
    case ai_mode::WalkInDirectionVariant1: ai_mode_124_walk_dir(); return;
    case ai_mode::WalkInDirectionVariant2: ai_mode_124_walk_dir(); return;
    case ai_mode::WalkInDirectionVariant3: ai_mode_124_walk_dir(); return;
    case ai_mode::GoToCorner: ai_mode_92_go_to_corner(); return;
    case ai_mode::WalkToEdgeThenCRight: ai_mode_123_walk_to_edge_then_c_right(); return;
    case ai_mode::WalkToArea: ai_mode_101_go_to_area(); return;
    case ai_mode::SpecialMove: ai_mode_113_special_move(); return;
    case ai_mode::WalkToRingCentre: ai_mode_72_walk_to_centre(); return;
    case ai_mode::DropOffApron: ai_mode_35_drop_off_apron(); return;
    case ai_mode::RunThenMove: ai_mode_51_grapple_move(); return;
    case ai_mode::ApronWalkTowardPartner: ai_mode_61_apron_walk(); return;
    case ai_mode::LockupDirectionMove: ai_mode_102_lockup_direction(); return;
    case ai_mode::CageClimbFromRun: ai_mode_121_cage_climb(); return;
    case ai_mode::CageClimbFromRunAlternate: ai_mode_121_cage_climb(); return;
    case ai_mode::PickTargetAndRun: ai_mode_90_run_to(); return;
    case ai_mode::CircleThenBlock: ai_mode_86_circle_then_block(); return;
    case ai_mode::ChaseLastWrestler: ai_mode_144_chase_last(); return;
    case ai_mode::RopesLButtonMove: ai_mode_100_ropes_l_move(); return;
    case ai_mode::GrappleUnlessPickUpMove: ai_mode_23_grapple(); return;
    case ai_mode::Grapple: ai_mode_23_grapple(); return;
    case ai_mode::ReplyToRunner: ai_mode_95_vs_runner_reply(); return;
    case ai_mode::StrikeFromBehind: ai_mode_136_strike_from_behind(); return;
    case ai_mode::GoPinDownedOpponent: ai_mode_8_go_pin(); return;
    case ai_mode::HoldCaughtKick: ai_mode_69_kick_catch(); return;
    case ai_mode::TagAssist: ai_mode_143_tag_assist(); return;
    case ai_mode::RunAtOpponent: ai_mode_44_run_at(); return;
    case ai_mode::DoNothing27: ai_mode_do_nothing(); return;
    case ai_mode::DoNothing28: ai_mode_do_nothing(); return;
    case ai_mode::DoNothing93: ai_mode_do_nothing(); return;
    case ai_mode::DoNothing137: ai_mode_do_nothing(); return;
    case ai_mode::DoNothing140: ai_mode_do_nothing(); return;
    case ai_mode::DoNothing142: ai_mode_do_nothing(); return;
    case ai_mode::TauntUnlessMeterMode: ai_mode_38_evade_unless_special(); return;
    case ai_mode::WalkDownTheRing: ai_mode_71_walk_down(); return;
    case ai_mode::CUpWithDpadDown: ai_mode_130_c_up_down(); return;
    case ai_mode::PressCLeftAtDownedOpponent: ai_mode_52_c_left(); return;
    case ai_mode::LockupWhip: ai_mode_111_lockup_whip(); return;
    case ai_mode::LockupWhipAlternate: ai_mode_111_lockup_whip(); return;
    case ai_mode::StrikeFromCageWall: ai_mode_132_strike_from_cage_wall(); return;
    case ai_mode::ShortBlockHold: ai_mode_7_hold_r(); return;
    case ai_mode::WaitThenGrapple: ai_mode_43_wait_then_grapple(); return;
    case ai_mode::BlockWhenLevel: ai_mode_131_block_when_level(); return;
    case ai_mode::HoldBlock: ai_mode_46_hold_block(); return;
    case ai_mode::BreakOutOfCaughtKick: ai_mode_70_leg_caught(); return;
    case ai_mode::PickUpDownedOpponent: ai_mode_20_pickup(); return;
    case ai_mode::CornerSetUp: ai_mode_94_corner_set_up(); return;
    case ai_mode::RunningAttack: ai_mode_88_run_attack(); return;
    case ai_mode::WalkToDownedOpponentAndPickUp: ai_mode_19_pick_up(); return;
    case ai_mode::DodgeDive: ai_mode_56_dodge_dive(); return;
    case ai_mode::PressLButton: ai_mode_21_counter_l(); return;
    case ai_mode::TimedFollowUpStrike: ai_mode_115_timed_follow_up_strike(); return;
    case ai_mode::CircleBriefly: ai_mode_104_circle(); return;
    case ai_mode::WalkRandomlyByZone: ai_mode_60_walk_zone(); return;
    case ai_mode::Think: ai_mode_0_think(); return;
    case ai_mode::EngageNearbyOpponent: ai_mode_1_engage(); return;
    case ai_mode::GrappleApproach: ai_mode_2_grapple_approach(); return;
    case ai_mode::RunInAndCounter: ai_mode_3_run_in_and_counter(); return;
    case ai_mode::Strike: ai_mode_6_strike(); return;
    case ai_mode::StrikeThenFollowUp: ai_mode_9_strike_follow(); return;
    case ai_mode::EvadeAndCounter: ai_mode_10_evade(); return;
    case ai_mode::PartnerDecision: ai_mode_15_partner_decide(); return;
    case ai_mode::StrikeDownedOpponent: ai_mode_22_strike(); return;
    case ai_mode::LockupChooseAction: ai_mode_25_lockup_choose(); return;
    case ai_mode::LockupPressMove: ai_mode_26_lockup_press_move(); return;
    case ai_mode::StruggleWhileHeldInLockup: ai_mode_29_grappled_struggle(); return;
    case ai_mode::ReverseWhip: ai_mode_30_whip_reverse(); return;
    case ai_mode::GroundHold: ai_mode_31_ground_hold(); return;
    case ai_mode::WaitAndWatch: ai_mode_32_wait(); return;
    case ai_mode::HeadForRing: case ai_mode::HeadForRingAlternate: ai_mode_33_apron(); return;
    case ai_mode::FetchWeapon: ai_mode_39_fetch_weapon(); return;
    case ai_mode::Run: ai_mode_40_run(); return;
    case ai_mode::WalkTowardOpponent: ai_mode_41_approach(); return;
    case ai_mode::GrappleEvade: ai_mode_42_evade_grapple(); return;
    case ai_mode::BackAway: ai_mode_45_back_off(); return;
    case ai_mode::BlockDive: ai_mode_47_block_dive(); return;
    case ai_mode::WalkToDownedOpponent: ai_mode_48_approach_downed(); return;
    case ai_mode::ApronWait: case ai_mode::ApronWaitAndTaunt: ai_mode_49_apron_wait_taunt(); return;
    case ai_mode::ReactToDazedOpponent: ai_mode_53_react_dazed(); return;
    case ai_mode::GetUp: ai_mode_54_get_up(); return;
    case ai_mode::LongWait: ai_mode_55_wait_long(); return;
    case ai_mode::StrikeWhenOpponentFacesAway: ai_mode_59_strike_facing(); return;
    case ai_mode::RunWithMoveVariant1: case ai_mode::RunWithMoveVariant2: case ai_mode::RunWithMoveVariant3: case ai_mode::RunWithMoveVariant4: case ai_mode::RunWithAButtonMove: ai_mode_105_running(); return;
    case ai_mode::KeepRunning: ai_mode_119_keep_running(); return;
    default: break;
    }
    static char msg[48];
    std::snprintf(msg, sizeof msg, "the CPU's mode %d (g_ai_modes)", (int)mode);
    unported("ai_mode_run", msg);
}

// N64 0x800F03F0: the CPU's frame (from wrestler_tick, before the state handler). It sets the AI
// globals (self, opponent, partner, the partner's opponent, the opponent's partner, the AI block),
// clears the pad it will write, notes its move set, the distance, angle and height difference to
// the opponent, perceives (ai_update_perception), classifies both wrestlers (ai_situation), and
// measures the four corners, the opponent's body (+90), both ring quarters and zones; copies both
// point_120 and the ten personality bytes; clears its flags88 0x01. Then the overrides:
// - an opponent taunting (0x1B / 0x1C), with the CPU not level 6, having a partner, in the ring or
//   on the floor near it (where-class 0x10B), its health 51+ and not 100 under the opponent's, the
//   opponent's 31+ with spirit under 90, and the opponent's last 16 states holding fewer than three
//   0x1B and three 0x1C: a weighted pick (trait +39 == 2: 0x80145298, else 0x801452AC) when idle;
// - an opponent in the TKO check (0x2E) and down (flags80 0x08) reads as 15 (not for a CPU-limited
//   one, flags_e9 0x01);
// - running without a target for 151 frames: back to mode 0;
// - by its own situation: 3 → mode 30, 6 → 29, 7 / 15 / 20 / 38 / 40 / 59-61 → 31, LOCKUP 13 → 25
//   (unless already in 26, 102 / 103, 111 / 112, 117, 120), 16-18 → 53, 24 → 54 (not in type
//   0x100000), 25 → 65 at step 4 (unless in 65 / 66), 28 → 15 (unless in 117), 34 → 69, 35 → 70;
// - the opponent hanging on the ropes from the apron (41) → 115; on the cage top (0xA5) with the
//   opponent lying in step-2 flags (state 0x31, state_counter 0x02) → 116;
// - the legal man on the apron for 151 frames: mode 0;
// - the floor timer; a watchdog that resets a mode that has run 300 frames; the mode history; then
//   the mode runs.
void ai_update(s8 slot) {
    Wrestler *s = &g_wrestlers[slot];
    Wrestler *o = &g_wrestlers[s->opponent];
    Wrestler *p = &g_wrestlers[s->partner];
    g_ai_self = s;
    g_ai_opp = o;
    g_ai_blk = A_AI_BLOCKS + 176u * (u32)slot;
    mem_w32(A_AI_SELF, wrestler_n64_addr(s));
    mem_w32(A_AI_PARTNER, wrestler_n64_addr(p));
    mem_w32(A_AI_WRESTLERS, wrestler_n64_addr(&g_wrestlers[0]));
    mem_w32(A_AI_BLOCK, g_ai_blk);
    mem_w32(A_AI_OPP, wrestler_n64_addr(o));
    mem_w32(A_AI_PARTNER_OPP, wrestler_n64_addr(&g_wrestlers[p->opponent]));
    mem_w32(A_AI_OPP_PARTNER, wrestler_n64_addr(&g_wrestlers[o->partner]));
    mem_w8(blk(AI_STICK_X), 0);
    mem_w16(blk(AI_HELD), 0);
    mem_w16(blk(AI_PRESSED), 0);
    mem_w8(blk(AI_STICK_Y), 0);
    mem_w32(blk(AI_MOVESET), A_MOVESETS + 19728u * (u32)s->slot);
    mem_w32(blk(AI_LISTS), A_AI_LIST_BLOCKS + 136u * (u32)s->slot);
    mem_wf32(blk(AI_DIST), dist_to(&s->point_120, &o->point_120));
    mem_w32(blk(AI_ANGLE), (u32)angle_to(&s->point_120, &o->point_120));
    mem_wf32(blk(AI_HEIGHT_DIFF), absf(s->point_120.y - o->point_120.y));
    ai_update_perception();
    mem_w32(blk(AI_SIT_SELF), (u32)ai_situation(s, s, mem_s32(blk(AI_SIT_SELF))));
    mem_w32(blk(AI_SIT_OPP), (u32)ai_situation(o, s, mem_s32(blk(AI_SIT_OPP))));
    for (s32 k = 0; k < 4; k++) {
        Vec3 c = {mem_f32(A_RING_CORNERS + 12u * (u32)k), mem_f32(A_RING_CORNERS + 12u * (u32)k + 4),
                  mem_f32(A_RING_CORNERS + 12u * (u32)k + 8)};
        mem_wf32(blk(AI_CORNERS) + 4u * (u32)k, dist_to(&s->point_120, &c));
    }
    Body b;
    lying_body_axis(o, &b);
    mem_wf32(blk(AI_BODY_DIST), (f32)((f64)lying_body_dist(&b, &s->point_120) + 90.0));
    mem_w16(blk(AI_QUAD_SELF), (u16)ring_quadrant(s));
    mem_w16(blk(AI_QUAD_OPP), (u16)ring_quadrant(o));
    mem_w16(blk(AI_ZONE_SELF), (u16)ring_zone_of(&s->point_120));
    mem_w16(blk(AI_ZONE_OPP), (u16)ring_zone_of(&o->point_120));
    for (s32 i = 0; i < 3; i++) {
        mem_wf32(A_AI_SELF_POINT + 4u * (u32)i, (&s->point_120.x)[i]);
        mem_wf32(A_AI_OPP_POINT + 4u * (u32)i, (&o->point_120.x)[i]);
    }
    for (const TraitCopy &t : kTraits)
        mem_w8(t.global, mem_u8(s->attrs + t.attr_off));
    s->flags88 &= 0xFFFE;

    if (o->state == state::Taunt || o->state == state::CornerTaunt) {
        s16 hp = mem_s16(blk(AI_HEALTH)), ohp = mem_s16(blk(AI_OPP_HEALTH));
        if (mem_s32(blk(AI_LEVEL)) != 6 && s->opponent != s->partner && (mem_u16(blk(AI_WHERE)) & 0x10B) &&
            hp - ohp >= -99 && hp >= 51 && ohp >= 31 && mem_s16(blk(AI_OPP_SPIRIT)) < 90 &&
            ai_history_count(o->slot, 27, 16, 0) < 3 && ai_history_count(o->slot, 28, 16, 0) < 3)
            ai_pick_mode_weighted_idle(mem_s8(kTraits[9].global) == 2 ? A_AI_TABLES : A_AI_TABLES_B);
    } else if (o->state == state::TechnicalKnockoutStagger && !(s->flags_e9 & 1) && (o->flags80 & 8)) {
        mem_w32(blk(AI_SIT_OPP), 15);
    }

    if ((s->state == state::Running && !(s->flags86 & 0x28)) || s->state == state::RunningRopeRebound) {
        s16 t = (s16)(mem_s16(blk(AI_RUN_TIME)) + 1);
        mem_w16(blk(AI_RUN_TIME), (u16)t);
        if (t >= 151)
            ai_set_mode(ai_mode::Think);
    } else {
        mem_w16(blk(AI_RUN_TIME), 0);
    }

    s32 mode = mem_s32(blk(AI_MODE));
    switch (mem_s32(blk(AI_SIT_SELF))) {
    case 3: ai_set_mode(ai_mode::ReverseWhip); break;
    case 6: ai_set_mode(ai_mode::StruggleWhileHeldInLockup); break;
    case 7: case 15: case 20: case 38: case 40: case 59: case 60: case 61:
        ai_set_mode_if_new(ai_mode::GroundHold);
        break;
    case 13:
        if (mode >= ai_mode::SpecialMove) {
            if (mode != ai_mode::LockupReact && mode != ai_mode::Lockup)
                ai_set_mode_if_new(ai_mode::LockupChooseAction);
        } else if (mode < ai_mode::LockupWhip && mode != ai_mode::LockupPressMove) {
            if (mode < ai_mode::LockupPressMove || mode >= ai_mode::CircleBriefly || mode < ai_mode::LockupDirectionMove)
                ai_set_mode_if_new(ai_mode::LockupChooseAction);
        }
        break;
    case 16: case 17: case 18: ai_set_mode_if_new(ai_mode::ReactToDazedOpponent); break;
    case 24:
        if (!(g_match_type & 0x100000))
            ai_set_mode_if_new(ai_mode::GetUp);
        break;
    case 25:
        if ((u32)(mode - ai_mode::MeetRunner) >= 2) {
            ai_set_mode(ai_mode::MeetRunner);
            mem_w32(blk(AI_STEP), 4);
        }
        break;
    case 28:
        if (mode != ai_mode::LockupReact)
            ai_set_mode_if_new(ai_mode::PartnerDecision);
        break;
    case 34: ai_set_mode_if_new(ai_mode::HoldCaughtKick); break;
    case 35: ai_set_mode_if_new(ai_mode::BreakOutOfCaughtKick); break;
    default: break;
    }
    if (mem_s32(blk(AI_SIT_OPP)) == 41)
        ai_set_mode_if_new(ai_mode::TimedFollowUpStrike);
    if (s->state == state::RunToSidePoint && (mem_u16(blk(AI_WHERE)) & 1) && o->state == state::LyingDown && (o->state_counter & 2))
        ai_set_mode_if_new(ai_mode::RunOffRopes);
    if ((s->flags_e9 & 0x10) && (s->location & 2)) {
        s32 t = mem_s32(blk(AI_APRON_TIME)) + 1;
        mem_w32(blk(AI_APRON_TIME), (u32)t);
        if (t >= 151) {
            mem_w32(blk(AI_APRON_TIME), 0);
            ai_set_mode(ai_mode::Think);
        }
    } else {
        mem_w32(blk(AI_APRON_TIME), 0);
    }
    ai_floor_timer(s->slot);
    u32 last = A_AI_STUCK_MODE + 2u * (u32)s->slot;
    u32 time = A_AI_STUCK_TIME + 2u * (u32)s->slot;
    if (mem_s16(last) == mem_s32(blk(AI_MODE))) {
        s16 t = (s16)(mem_s16(time) + 1);
        mem_w16(time, (u16)t);
        if (t >= 300) {
            ai_set_mode_if_new(ai_mode::Think);
            mem_w16(last, (u16)mem_s32(blk(AI_MODE)));
            mem_w16(time, 0);
        }
    } else {
        mem_w16(last, (u16)mem_s32(blk(AI_MODE)));
        mem_w16(time, 0);
    }
    ai_mode_history_push(s->slot);
    ai_mode_run(mem_s32(blk(AI_MODE)));
}

}  // namespace nm

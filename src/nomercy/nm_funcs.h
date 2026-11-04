// Every decompiled function, by file, with the original's names (see _re_docs/symbols/nomercy/).
#pragma once
#include "nm_math.h"
#include "nm_state_ids.h"
#include "nm_ai_mode_ids.h"
#include "nm_state.h"

namespace nm {

// Wrestler.anim_player bytes (anim_player_set copies them from the animation's file 703 record).
enum {
    AP_FRAMES = 4,          // frame count (anim_frame_count)
    AP_LOOP = 5,            // record byte 1 bit 7: the animation starts over when it runs out
    AP_END_PLACE = 6,       // where the animation ends: 1 ring, 2 floor, 3 apron, 6 cage wall, 7 on top
    AP_END_POSE = 7,        // the pose class it ends in (anim_pose_class)
    AP_HIT_FROM = 8,        // record byte 3: first frame a strike can hit (strike_hit_test), the strike turns toward the opponent before it
    AP_HOLD_FRAME = 9,      // record byte 4; also the last frame a strike can hit
    AP_ACTIVE_FROM = 0x0E,  // record byte 7: first frame of the active window
    AP_ACTIVE_TO = 0x0F,    // record byte 8: last frame of the active window
    AP_PAIR_FRAME = 0x11,   // record byte 10
    AP_FRAMES_B = 0x14,     // record byte 17: the frame at which the animation counts as ended
    AP_REPLAY_FRAME = 0x1A, // record byte 15 (or the frame count): 15 frames after it a replay may start
};

// A body for the wrestler-against-wrestler push (move_step_collide): an ellipse rx by rz round
// `center`, turned by `angle`.
struct Body {
    f32 rx, ry, rz;
    Vec3 center;
    s32 angle;
};
// One collision candidate: a slot (or 100 + an object), the angle and xz distance to it, its body.
struct Contact {
    s32 slot;
    s32 angle;
    f32 dist;
    Body body;
};
static_assert(sizeof(Contact) == 40, "Contact is the original's 40-byte entry");

// point_120 is joint 0 of the record's joint list, joints[] the rest.
inline Vec3 *wrestler_joint(Wrestler *w, s32 k) { return k == 0 ? &w->point_120 : &w->joints[k - 1]; }
inline const Vec3 *wrestler_joint(const Wrestler *w, s32 k) { return k == 0 ? &w->point_120 : &w->joints[k - 1]; }

typedef void (*StateHandler)(WrestlerCtx *);
StateHandler state_handler(s32 state);  // g_state_handlers (0x80144CF0)

// ---- host side (nm_host.cpp): presentation and the parts not decompiled yet ----
void host_hud(const char *what, s32 a, s32 b);       // HUD calls (ignored by the oracle)
void host_sfx(s32 id);                               // sfx_play (0x800173C0)
void host_sfx_slot(s32 slot, s32 id);                // sfx_play_slot (0x80017780)
void host_rom_read(u32 rom_addr, u8 *dst, u32 n);   // the PI DMA of anim_stream_fetch (0x80011340)
void host_rope_push(s32 kind, s32 side, f32 along);  // ring_rope_push (0x800225BC), rendering
void host_anim_event_special(s32 slot, s32 kind);   // anim_event_special (0x800172D4), rendering
void set_unported_hook(void (*hook)(const char *func, const char *what));

// ---- nm_rng.cpp ----
u32 rng_main();
s32 rng_match();
s32 rand_pool_next();
s32 chance_check(s32 n);

// ---- nm_frame.cpp ----
void match_flags_clear(s32 *p, s32 a, s32 b);
s32 clock_tick(s32 *c, s32 frames_per_second);
s32 any_wrestler_flag8c_8000();
s32 run_in_try(s32 slot, s32 side);
void run_in_check();
void pin_count_reset(s32 slot);
void referee_count_tick();
void damage_history_tick();
s32 match_clock_tick();
s32 countout_tick(s32 op);
s32 match_result(u32 code, s32 arg);
s32 match_end(u32 cause);
s32 result_stats_record(const s32 *winners, const s32 *losers, u32 code);
s32 elimination_record(s32 slot);
s32 tag_partner_takes_over(s32 slot);
void match_record_fall(const s32 *winners, const s32 *losers, u32 code, s32 record_stats);
s32 match_end_start(s32 winner, s32 loser, u32 code);
s32 match_events();
void match_tick();

// ---- nm_wrestler.cpp ----
void wrestler_set_state(Wrestler *w, s32 state);
void wrestler_set_state_now(Wrestler *w, s32 state);
void wrestler_unpair(Wrestler *w);
void wrestler_set_state_free(Wrestler *w, s32 state);
void wrestler_set_state_free_now(Wrestler *w, s32 state);
s32 wrestler_turn_toward(Wrestler *w, s32 angle, s32 step, u16 flag);
s32 pose_lying(const Wrestler *w);
s32 pose_is_lying(Wrestler *w);
void anim_end_location(Wrestler *w);
void input_remap(s32 pad, u16 *word);
void input_history_push(s32 slot);
s32 match_can_interrupt();
void airborne_fall_step(Wrestler *w);
void hits_taken_decay(Wrestler *w, s32 op);
s32 spirit_update(Wrestler *w, s32 op);
void idle_timer(Wrestler *w);
void tag_rules_check(Wrestler *w);
void hit_flash(Wrestler *w, s32 op);
void run_in_fresh_clear(Wrestler *w);
s32 timed_press_check(Wrestler *w, u16 held, u16 pressed, s32 op);
void state_move_step(WrestlerCtx *ctx, s32 step_from, s32 kind, u32 flags);
void state_move_step_auto(WrestlerCtx *ctx);
void wrestler_update(s32 slot);
void wrestler_tick(s32 slot);
void wrestlers_update_all(s32 op);
void wrestler_joints_update();

// ---- nm_anim.cpp ----
s32 anim_frame_count(s32 anim);
s32 anim_pose_class(s32 anim);
void anim_player_set(u8 *p, s16 anim);
void wrestler_anim_invalidate(Wrestler *w);
void wrestler_set_anim(Wrestler *w, s32 anim, s32 mode);
void wrestler_restart_anim(Wrestler *w, s32 anim, s32 mode);
void model_pose_in_place(Wrestler *w, Vec3 *lean, s32 mode);
s32 head_look_update(s32 slot, Vec3 *lean);
void wrestlers_ground_contact();

// ---- nm_strike.cpp ----
s32 angle_to_side(s32 angle);
void input_history_clear(s32 slot);
void input_press_kind(s32 slot, u8 *out);
void wrestler_start_move(Wrestler *w, s32 from, s32 move);
void wrestler_set_generic_move(Wrestler *w, s32 move);
s32 anim_style(const Wrestler *w, s32 variant);
void wrestler_set_step(Wrestler *w, s32 step);
void hit_move_copy(const Wrestler *src, Wrestler *dst);
void pair_link_clear_others(const Wrestler *w);
void wrestler_pair_set_state(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b);
void wrestler_pair_set_state_move(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b);
s32 wrestler_health_eff(const Wrestler *w);
void wrestler_add_spirit(Wrestler *w, s32 n);
void spirit_exchange(Wrestler *att, Wrestler *def, const Wrestler *rec);
void damage_history_add(Wrestler *w, s32 damage, s32 mode);
void region_add(Wrestler *w, s32 region, f32 d);
s32 apply_damage(Wrestler *w, Wrestler *d);
s32 chance_roll(Wrestler *w, Wrestler *opp, s32 kind);
void stats_count(const Wrestler *w, const Wrestler *d, s32 kind);
void strike_dq_check(Wrestler *w, const Wrestler *d);
void relative_facing(const Wrestler *w, Wrestler *d, s32 from_behind);
s32 strike_reaction_anim(const Wrestler *w, s32 fallback);
s32 strike_counter_check(Wrestler *d, Wrestler *w);
s32 move_aftermath_check(const Wrestler *w, Wrestler *att, Wrestler *d, s32 how);
void hit_candidates(const Wrestler *w, s32 obj, s32 kind, s32 *out);
s32 strike_hit_test(Wrestler *w, s32 obj, s32 kind);
void strike_land(Wrestler *w, s32 target, u32 flags);
void strike_keep_in_bounds(Wrestler *w);
void strike_slide(Wrestler *w);
s32 running_ground_attack_check(Wrestler *w, Wrestler *opp, f32 reach, s32 *angle, s32 mode);
s32 dazed_turn_target(const Wrestler *w, s32 *angle);
void strike_across_ropes(WrestlerCtx *ctx);
s32 input_cup_opponent(WrestlerCtx *ctx);
s32 input_r(WrestlerCtx *ctx);
s32 input_l(WrestlerCtx *ctx);
s32 input_leapfrog(WrestlerCtx *ctx);
s32 input_stick(WrestlerCtx *ctx);
s32 input_a(WrestlerCtx *ctx);
s32 input_c_left_held(WrestlerCtx *ctx);
s32 dpad_side(u16 buttons);
void taunt_hold_opponent(WrestlerCtx *ctx);
s32 apron_line_snap(Wrestler *w);
void taunt_stir_check(Wrestler *w, Wrestler *o);
s32 zone_kind(u32 zone);
void apron_line_pos(const Wrestler *w, Vec3 *out);
s32 knockdown_dir_from_zone(Wrestler *w);
void wrestler_settle_pose(Wrestler *w);

// ---- nm_grapple.cpp ----
u16 relative_position(const Wrestler *w, const Wrestler *other);
void slide_vector(Wrestler *w, const Vec3 *from, const Vec3 *to, s32 frames);
s32 lockup_time_limit(const Wrestler *w, const Wrestler *other);
u16 keep_in_bounds_pair(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, u32 joints);
u16 keep_in_bounds_pair_c(Wrestler *w, Wrestler *other, f32 scale, Vec3 *pts3, u32 joints);
s32 cover_side_pick(const Wrestler *rec, s32 angle);
s32 ground_approach_plan(WrestlerCtx *ctx, const Vec3 *target, s32 advance);
u16 pair_follow(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, u16 mask, u32 joints);
void grapple_anim_defender(Wrestler *w, Wrestler *other);
void grapple_defender_anim(Wrestler *w, Wrestler *att);
s32 lockup_slide_in(WrestlerCtx *ctx);
s32 lockup_slide_in_defender(Wrestler *w, const Wrestler *att);
void grapple_walk(WrestlerCtx *ctx, u32 pair);
s32 grapple_timer(WrestlerCtx *ctx);
void grapple_defender_input(Wrestler *d, Wrestler *att, u16 pressed);
s32 lockup_input(WrestlerCtx *ctx);
s32 grapple_double_team_check(Wrestler *att, Wrestler *def);
s32 grapple_attempt_double(WrestlerCtx *ctx);
s32 grapple_attempt_slide(WrestlerCtx *ctx);
void grapple_start_pair(Wrestler *att, Wrestler *def);
s32 grapple_attempt_resolve(Wrestler *att, Wrestler *def);
s32 grapple_attempt_target(Wrestler *w);
void grapple_attempt_start_anim(WrestlerCtx *ctx);

u16 state_bounds(Wrestler *w, Wrestler *other, Wrestler *other2, u16 mask);
s32 move_follow_up(WrestlerCtx *ctx);
void finisher_effects(Wrestler *w);
void object_spawn_for_move(Wrestler *w, Wrestler *other, s32 mode);
void move_texture_stamp(Wrestler *w, Wrestler *other);
s32 move_chain_check(WrestlerCtx *ctx);
s32 move_allowed(const Wrestler *w, const Wrestler *opp, s32 move);
s32 lockup_place_move(WrestlerCtx *ctx);

// ---- nm_tag.cpp ----
s32 wrestler_slide_b(Wrestler *w, f32 scale);
void pair_scripted_place(Wrestler *w, Wrestler *other);
s32 pair_slide_step(Wrestler *w, Wrestler *other);
void wrestler_move_pos_to_pelvis(Wrestler *w);
void place_from_zone(Wrestler *w, s32 frames, s32 angle, f32 dist);
s32 apron_side_ok(const Wrestler *w);
s32 dist_between(const Wrestler *a, const Wrestler *b);
void tag_exchange(Wrestler *a, Wrestler *b, s32 at_step2);
s32 tag_try_partner(WrestlerCtx *ctx);
s32 climb_place_free(s32 side);
void place_from_zone_pair(Wrestler *w, Wrestler *other);
void turnbuckle_check(WrestlerCtx *ctx, s32 moving);
s32 special_move_kind(Wrestler *w, Wrestler *o, s32 kind);
void aerial_height(Wrestler *w, s32 always);
void aerial_step(Wrestler *w, Wrestler *o, s32 aim);
void special_move_land(Wrestler *w);
s32 illegal_man_leave(Wrestler *w);
void partner_in_move(Wrestler *w);
s32 dpad_to_side(u16 buttons);
void tag_in(Wrestler *w);
s32 may_stay_inside(const Wrestler *w);
s32 walk_to_angle(WrestlerCtx *ctx, s32 angle);
s32 idle_check(Wrestler *w);
s32 input_tag(WrestlerCtx *ctx);

// ---- nm_hold.cpp ----
void ref_count_reset(s32 kind, s32 count);
void fall_count_add(Wrestler *w, s32 slot);
void ground_hold_crawl(Wrestler *held, Wrestler *holder, u16 buttons);

// ---- nm_down.cpp ----
s32 escape_time(Wrestler *w, Wrestler *other, s32 op);
s32 mash_count(Wrestler *w, u16 pressed, s32 op);
s32 recover_tick(Wrestler *w, u16 pressed);
s32 down_count_allowed(const Wrestler *w);
s32 down_count_others(const Wrestler *w);
void anim_is_dazed_like(Wrestler *w);
s32 get_up_anim(const Wrestler *w);
void get_up_finish(WrestlerCtx *ctx);
s32 damage_history_ratio(const Wrestler *w, s32 *hits, s32 *ratio);
s32 anim_event_frame(s16 anim, s32 kind);
void wrestler_pair_set_state_now(Wrestler *w, s32 state_a, Wrestler *other, s32 state_b);
s32 input_c_left(WrestlerCtx *ctx);
s32 input_stir(Wrestler *w, Wrestler *opp, u16 held);

// ---- nm_model.cpp (main segment: the model posing) ----
void model_animate(s32 slot, s32 anim, s32 frame, Vec3 *pos, s32 facing, const Vec3 *lean, s8 mode, s32 tilt_a,
                   s32 tilt_b);

// ---- nm_bounds.cpp ----
f32 plane_distance(const Vec3 *origin, const Vec3 *n, const Vec3 *p);
f32 vec_normalize_two(const Vec3 *a, const Vec3 *b);
s32 quad_contains_point(u32 wall, const Vec3 *p);
s32 segment_hit(const Vec3 *from, const Vec3 *to, u32 wall, Vec3 *push, f32 *dist, s32 kind);
u16 move_hits_segment(Wrestler *w, const Vec3 *move);
s32 run_step_corner(u32 wall, const Vec3 *center, f32 radius, f32 *depth);
u16 keep_in_bounds(Wrestler *w, Wrestler *other, Wrestler *other2, f32 scale, const u8 *until, Vec3 *pts3, Vec3 *pt,
                   u16 mask, u32 joints, s32 apply);
u16 keep_in_bounds_self(Wrestler *w, f32 scale, u16 mask, u32 joints);
f32 segment_distance(const Vec3 *a, const Vec3 *b, f32 out[9]);
f32 lying_body_dist(Body *b, const Vec3 *point);
void lying_body_axis(Wrestler *w, Body *out);
void move_step_collide(Wrestler *w, Contact entries[11]);
s32 wrestler_move_step(Wrestler *w, Wrestler *other, f32 scale, u32 mask, s32 exclude, s32 mode);
s32 wrestler_slide(Wrestler *w, f32 scale);
void wrestler_translate(Wrestler *w, const Vec3 *d);
s32 wrestler_translate_pushed(Wrestler *w, const Vec3 *d, s32 exclude);

// ---- nm_walk.cpp ----
f32 wrestler_region_condition(const Wrestler *w, s32 region);
f32 walk_speed(Wrestler *w, u32 flags);
s32 dpad_dir8(u16 buttons);
s32 side_input_check(const Wrestler *w, u16 dpad);
s32 walk_anim_step(const Wrestler *w);
s32 anim_style_variant(const Wrestler *w);
s32 wrestler_move_anim(const Wrestler *w, s32 carry);
s32 anim_pick(WrestlerCtx *ctx, s32 kind, s32 angle);
s32 object_sfx(s32 slot, s32 obj, s32 kind);
void object_underfoot(Wrestler *w);
u16 walk_step(WrestlerCtx *ctx, u16 buttons, s32 flags);

// ---- nm_stand.cpp ----
s32 wrestler_may_act(const Wrestler *w);
s32 stand_flags_update(Wrestler *w);
s32 illegal_check(Wrestler *w);
void illegal_man_check(Wrestler *w);
void wrestler_add_health(Wrestler *w, s32 amount, s32 mode);
void health_regen(Wrestler *w, s32 op);
void weapon_release(Wrestler *w, s32 how);

// ---- nm_object.cpp ----
// What object_model_update reads and writes back: a position and four angles (x, y, z, yaw).
struct ObjectPose {
    Vec3 pos;
    s32 angle[4];
};
void object_model_update(s32 obj, s32 holder, s32 type, ObjectPose *io);
void object_pose_in_place(s32 obj);
void object_clear(s32 obj);
void objects_probe_points();
s32 object_free_slot();
s32 weapon_type_allowed(s32 type);
s32 weapon_pick_type(Wrestler *w, s32 how);
s32 object_hit_check(s32 obj);
f32 weapon_tip_point(const Wrestler *w, s32 obj, Vec3 *out);
MoveRecord move_record_at(u32 a);
void object_fly(s32 obj, Vec3 *out);
void ladder_object_update(s32 obj);
s32 finisher_full_spirit_roll(const Wrestler *w);
void object_hand_over(Wrestler *w, Wrestler *from);
Vec3 ladder_point(u32 obj_addr, f32 x, f32 z);
void object_contact_faces(s32 obj, u32 out);
s32 object_at_point(const Vec3 *p, f32 radius);
void object_set_motion(s32 obj, s32 kind, const Vec3 *dir);
void ladder_camera(s32 mode, s32 kind);
s32 ladder_pose(Wrestler *w);
f32 arena_area_lookup(f32 x, f32 z, u32 *area);
u16 dist_to_ring_point(const Vec3 *n);
s32 may_leave_ring(Wrestler *w);
s32 climb_check(const Wrestler *w, const Wrestler *o);
s32 whip_direction(WrestlerCtx *ctx);
s32 apron_grapple_start(const Wrestler *w, const Wrestler *o);
s32 dpad_diagonal_quadrant(u16 buttons);
s32 wrestler_move_step_b(Wrestler *w, u16 mask, f32 scale);
s32 opponent_switch_nearest(Wrestler *w, u16 pressed);
s32 nearest_corner(const Vec3 *p, s32 kind);
s32 corner_free(const Wrestler *w);
s32 input_weapon_grab(WrestlerCtx *ctx);
s32 may_act_b(const Wrestler *w);

// ---- nm_run.cpp ----
s32 move_event(WrestlerCtx *ctx, u32 bits);
void anim_end_apply(Wrestler *w);
void anim_end_height(Wrestler *w);

// ---- nm_replay_rec.cpp ----
void replay_record();

// ---- not decompiled yet (nm_todo.cpp: each stops a replay when reached) ----
void ai_update(s8 slot);
void objects_update();
void objects_cleanup();
void objects_wrestler_contact();

}  // namespace nm

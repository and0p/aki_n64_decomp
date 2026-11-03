/*
 * WWF No Mercy (US, Rev 1) - structures recovered from the original game.
 *
 * Reverse-engineering reference only, not the port's own types. Pointers are 32-bit on
 * the N64; the IDA build adds __ptr32 to every pointer automatically.
 *
 * Only the basic types for now: the World Tour / Revenge structs are not copied in, because
 * their layouts differ between the games. Add each struct here once its layout is read from
 * No Mercy's own code. Field comments follow ../wcw/types.h ("?" marks a guess).
 */
typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;
typedef unsigned int u32; typedef int s32; typedef float f32;

struct Vec3 { f32 x, y, z; };
struct Vec4 { f32 x, y, z, w; };

/* Move record, 36 bytes (g_movesets: 548 per slot; the generic table at 0x800A6040). Offsets from the
 * code that reads w->move (+0x310) and w->hit_move (+0x320), 2026-11-02. */
struct MoveRecord {
    u16  _unk00;              /* always 0 in file 495 (padding) */
    s16  place;               /* +0x02: read by dive_aim and move_metrics_measure (a distance / placement value?) */
    s16  follow_up;           /* +0x04: follow-up move number (move_follow_up, move_chain_check: 0 = none) */
    u8   damage;              /* +0x06: base damage D (apply_damage) */
    s8   spirit_att;          /* +0x07: spirit for the attacker (spirit_exchange) */
    s8   spirit_def;          /* +0x08: spirit for the defender */
    s8   bleed_class;         /* +0x09: move_aftermath_check / chance_roll kind 5: bleeding (World Tour's f07) */
    s8   tko_class;           /* +0x0A: move_aftermath_check: the TKO stagger (World Tour's f08) */
    s8   region;              /* +0x0B: body region hit (4 = none) */
    s8   hit_kind;            /* +0x0C: hit kind, indexes the defender's defence attrs +0x0F (apply_damage); state 0x49 */
    s8   test_kind;           /* +0x0D: strike_hit_test kind: picks the joint list (0x80146844) and the reach rules */
    u8   _unk0e[2];           /* 1-21 (mostly 1) in file 495; no reader found in the match code (2026-11-02) */
    s8   aim;                 /* +0x10: dive_aim, state 0x5A */
    s8   reaction;            /* +0x11: reaction kind 0-6 (strike_land's switch, the TKO check's table, 0x2D, 0x90) */
    u8   region_damage[5];    /* +0x12: damage to each of the five regions */
    s8   fall;                /* +0x17: read by strike_land, state_2D_knocked_down and pair_scripted_place */
    s8   special_spirit;      /* +0x18: spirit from a special move's landing (special_move_land) */
    u8   _unk19[2];           /* 0 / 2 / 3 and 1 / 3 / 5 in file 495; no reader found in the match code */
    u8   alt_damage;          /* +0x1B: damage used instead when def +0x31E matches move_check */
    s16  move_check;          /* +0x1C */
    u8   _unk1e[2];           /* always 0 (padding) */
    u32  flags;               /* +0x20: 0x01 reversible, 0x2000 not reversible by 159 / 161, 0x8000 (flags8c 0x01; preferred by the CPU), 0x600 'partner busy', 0x8000000 damage does not lower health_max; 0x10, 0x80000, 0x1000000 tested by states */
};

struct Wrestler {             /* 0x358 bytes; g_wrestlers[4] at 0x80159990 */
    s32  pair_other;          /* slot of the wrestler it is paired with in a move; set by wrestler_pair_set_state (0 at the start) */
    s32  slot_d;              /* ? a slot index, copied to WrestlerCtx.slot_d */
    s32  opponent;            /* slot it is working on (tag: the other team's legal man) */
    s32  partner;             /* tag partner's slot; its own slot in singles */
    s32  runner_slot;         /* +0x10: the runner it leapfrogs (0x9E) or dodges (0x9F): those states read that slot's record; state_01_stand resets it to -1 (code 2026-11-02) */
    s32  state;               /* action state, index into g_state_handlers */
    s32  state_step;          /* step within the state */
    s32  prev_state;          /* the state before this one */
    s32  prev_step;           /* the step it left prev_state at */
    s32  facing_to_opponent;  /* ? set every frame from point_120 of itself and of the opponent */
    s32  run_dir_copy;        /* +0x28: the run's direction, copied by state_03_run when the run starts (code 2026-11-02) */
    s32  lockup_facing;       /* +0x2C: the facing a lock-up turns to (grapple_anim_defender / grapple_defender_anim); lockup_slide_in sets facing to it when the slide ends */
    s32  pair_slot;           /* the paired wrestler's slot while a pair move runs; -1 after wrestler_unpair */
    s32  pair_slot2;          /* ? also set to -1 by wrestler_unpair */
    s32  damage_hist_pos;     /* +0x38: the current entry (0-29) of the damage history (+0x344): damage_history_tick moves it on once a second and clears that entry (code 2026-11-02) */
    s32  held_obj;            /* index into g_objects of the object held: 4 / 5 are the two ring steps (input_weapon_grab sets them directly), a crowd weapon gets a free record (1 for the first after a thrown one, RAM 2026-11-01) */
    s32  ladder_side;         /* +0x40: which side of the ladder it climbs (0 / 1), set with held_obj by input_ladder_climb */
    s32  contact;             /* +0x44: what it is touching this frame: 100 + object index for an object (input_ladder_climb), cleared by wrestler_update */
    s32  ladder_rung;         /* +0x48: the rung it is on while climbing a ladder: + 1 at the end of each step up (state_AC_ladder_up), read by the ladder top / knocked-off states and chance_roll (code 2026-11-02) */
    s32  apron_hold;          /* +0x4C: frames the d-pad has pushed along the apron (input_apron_dpad: at 9 the corner walk 0x02 starts; cleared otherwise) (code 2026-11-02) */
    s32  dive_hit;            /* +0x50: what a dive landed on: a slot, 100 + an object, or -1 (state_79_dive) */
    s32  idle_count;          /* +0x54: frames idle_timer has counted while idle_flags 0x01 is set; past the per-difficulty limit (0x8014DCA4[0x800BD929]) it sets idle_flags 0x02; cleared when 0x01 is not set (code 2026-11-02) */
    u8   _unk058[8];
    s32  ring_zone;           /* ring_zone_of(&pos), set by wrestler_update every frame */
    s32  joint_zone;          /* +0x64: ring_zone_of(point_120), set by wrestler_update every frame (code 2026-11-02) */
    s32  _unk068;             /* set to -1 by wrestler_update every frame */
    s32  dq_victim;           /* +0x6C: under g_match_type 0x80000 strike_dq_check stores here the slot (+0x9E) of the wrestler it hit illegally (and sets flags8c 0x1000 on itself, 0x2000 on the victim) instead of disqualifying it; match_end credits that slot with the DQ fall (code 2026-11-02) */
    u32  strike_flags;        /* +0x70: the incoming strike's flags: 0x100600 unblockable (0x600 a weapon), 0x40 with location 0x10, 0x800 / 0x8000 / 0x80 pick falls in state_2D_knocked_down */
    u32  hit_flags;           /* +0x74: how the last hit landed, set by strike_land; 0x100000-0x1000000 pick the reaction (5 kinds), 0x2000000 its mirror, 0x10 / 0x01 / 0x8000 / 0x80 / 0x20 / 0x04 other falls (state_2D_knocked_down, strike_reaction_anim) */
    u32  flags78;             /* grapple and situation bits (grapple_start_pair, grapple_attempt_resolve, state_50): 0x04 strong grapple (A held), 0x40 running, 0x80 rear, 0x100 opponent slumped in the corner (its frame_flags 0x02 / 0x04, state 0x18; read as staggered until 2026-11-03), 0x200 / 0x400 defender / attacker on the apron, 0x800 normal standing grapple, 0x20000 / 0x40000 floor to apron / ring, 0x8000 / 0x8000000 tested by 0x84 / 0x89; copied to the opponent when a grapple starts */
    u32  counter_bits;        /* +0x7C: strike_counter_check's bit for the countered move (0x01 the kick ... 0x4000 move 73); read by the counter states 0x64 / 0x66 */
    u16  flags80;             /* flag words +0x80-+0x8E: wrestler_update clears some bits of each every frame. flags80: 0x01 in a grapple or move, 0x08 down, 0x40 / 0x80 ground hold attacker / held, 0x10 down sitting slouched (cleared when a hit lays it flat) (RAM) */
    u16  location;            /* where it is: 0x01 ring, 0x02 apron, 0x04 floor, 0x40 / 0x80 / 0x100 / 0x200 at one of four raised places (state_6F picks one from flags84 bits 0x01 / 0x02 / 0x10 / 0x20; 0x40, 0x100 and 0x200 are the three breakable tables, intact bits 0x04 / 0x08 / 0x10 of g_tables_intact, 0x80 has no such check), 0x20 up on top of it (anim_end_location place 7 keeps the place bits and adds 0x20; RAM read 0x40 beside the announce table and 0x60 on it); place codes from anim_end_location: 1 ring, 2 floor, 3 apron, 6 0x400, 7 on top; bits 0xF800 survive those changes; 0x08 on a ladder (set by 0xAA, the climb, cleared by 0xAB); 0x10 set by 0xAF / cleared by 0xB0; 0x800 and 0x1000 are per-frame: wrestler_update clears both every frame and the state handler sets them again (0x800 by 0x0A, perched on the turnbuckle, and 0x95, the partner on the apron; 0x1000 by the whip 0x12 / 0x14, the shove 0x0E, the cheap shot 0x6E and the chain moves 0x87 / 0x9D; read by the collision and landing code) (code); 0x400 up on the cage wall (the cage family 0xC2-0xCF, from Andrew's viewer tags: climbing, hanging, attacks from the top; anim_end_location place 6), probably also a turnbuckle */
    u16  state_counter;       /* ? zeroed by wrestler_set_state */
    u16  flags86;
    u16  flags88;             /* bit 2: in the match (wrestlers_update_all), 1 / 2 pick the update pass; 0x01 set by wrestler_set_state_now (rerun now?), 0x02 by the pair setters, 0x04 while paired (cleared by wrestler_unpair), 0x10 set on every state change */
    u16  flags8a;             /* ? RAM: 0x0300 standing, 0x2000 in a grapple / move or on the table, 0x8000 while falling through the table, 0x6000 getting up */
    u16  flags8c;             /* ? 0x80 on the waiting partners on the apron */
    u16  flags8e;
    u16  saved_end_flags;     /* +0x90: end_flags saved by match_end before it clears them (read by the run-in) */
    u16  tko_flags;           /* +0x92: set by move_aftermath_check, read by state_2E_tko_check and animation frame events */
    u16  end_flags;           /* +0x94: why this wrestler lost a fall, read by match_end: 0x08 pinned (count of 3), 0x10 out over the top: set by state_01_stand when it stands on the floor under g_match_rules 0x400 (the battle royal / Rumble rule; also sets flags80 0x01 and drops the input), result 0x80000 in match_end, 0x20 submitted (ground hold 0x49), 0x40 TKO, 0x100 last one standing: set on itself by state_01_stand when it has flags_f1 0x10 (entered under elimination rules or by a run-in) and every other slot is on the floor with end_flags 0x01, without flags_f1 0x01 and not in state 0xA3 (code; the bell sounds), so the flags also mark a winner, 0x200 a fall called by the guest referee, 0x400 disqualified (strike_dq_check, guest_referee_call), 0x800 lost the ladder match (the opponent grabbed the prize, state 0xBD), 0x1000 the opponent escaped the cage (set by state_C5_cage_escape, code); 0x01 / 0x04 by match_record_fall */
    u16  partner_flags80;     /* copy of the partner's flags80, made by wrestler_update */
    u16  dpad_held;           /* +0x98: cleared every frame; state_01_stand stores walk_step's result here: the d-pad direction bits (0xF00) of the walls keep_in_bounds pushed the wrestler off (wall +0x26), so the place checks (climbing, apron moves, weapons, the cage) only run against a wall (code 2026-11-02); weapon grabs and AI modes read it */
    u16  climb_target;        /* +0x9A: ? the raised place / climb target, written and read by 0x6E, 0x6F, 0x9A, 0x71 and AI mode 47 */
    u16  state_flags;         /* zeroed by wrestler_set_state; per-state flag bits (0x02, 0x04, 0x10 in state_69) */
    s16  slot;                /* its own slot index (the pair setters copy it into the partner's pair_other / opponent); also indexes the last-animation table at 0x801593A0 */
    s16  roster_id;           /* who is in this slot (-1 = empty); run_in_try copies a side's ally_id here (0x0601 for P2's ally, RAM) */
    s16  anim_frames_left;    /* ? countdown; 48 functions test "<= 0", often with state_step, to end a step */
    s16  anim_frame;          /* ? frames into the animation; handlers compare it with frame numbers (14, 45) */
    s16  anim_id;             /* current animation (archive file number, e.g. 12557); written by wrestler_set_anim */
    u16  state_timer;         /* frames in the current state: zeroed by wrestler_set_state, incremented by wrestler_update */
    s16  health;              /* 0 to health_max (255 at the start); changed by wrestler_add_health. Thresholds in code: 0 rolls a run-in for its side, < 49 gains 3 more spirit per exchange, < 50 the referee's count calls a sound, raised to 60 after a fall (fall_count_add), < 100 weakened hit reactions (+14: the slouched fall), AI checks < 64 and < 128 */
    s16  health_max;          /* +0xAC: wrestler_add_health caps health here */
    s16  spirit;              /* +0xAE: the attitude meter, 0-100 (wrestler_add_spirit, spirit_update): at 100 SPECIAL for 600 frames, at 0 empty for 300 frames */
    s16  spirit_timer;        /* +0xB0: frames left in the current meter mode (flags_ee & 7), or until the next drift step (120) */
    u16  count_timer;         /* countdown: the lock-up time limit (grapple_timer; R resets it to 0, i.e. lets go), the kick-catch window, 60 in state_2E_tko_check */
    s16  escape;              /* +0xB4: frames left before getting up (state_31_down, from escape_time op 0) or escaping a pin / hold (op 1); -1 a frame, -1 more per mash (recover_tick); 15 once the wrestler has lost the fall */
    s16  rebound_count;       /* +0xB6: ? set by the rope rebound 0x04, the whip runner 0x0C and the run's frame event; cleared by wrestler_reset */
    s16  run_timer;           /* +0xB8: ? the run's frame event uses it; standing and 0x31 clear it */
    s16  pin_count;           /* pins taken this match: state_45_pin_defender adds 1 after each escape_time op 1, wrestler_reset clears it; each one adds 2 frames to the next pin's escape time (code) */
    s16  run_counter;         /* +0xBC: ? the run states (0x03, 0x04, 0xA5) count it; standing and 0x31 set -1 */
    s16  finisher_heat;       /* +0xBE: set to 180 when a finisher lands (escape_time case 0); escape_time looks it up in 0x801461E4 */
    s16  hit_flash_timer;     /* +0xC0: hit_flash counts it down (flags_ee 0x20 while it runs), set by wrestler_add_health */
    u8   _unk0c2[2];
    u32  pickup_anims;        /* +0xC4: state_38_pickup_attacker's place in its animation row, an N64 address (0x8014DC50 / 0x8014DC78; read as a timer until 2026-11-03) */
    s16  regen_timer;         /* +0xC8: health_regen's frame counter (90, or 60) */
    s16  down_hit_count;      /* +0xCA: hits taken while down (state_63), cleared by wrestler_settle_pose / wrestler_reset */
    s16  dir;                 /* ? direction index into the angle table at 0x8014D1D8 */
    s16  down_count;          /* +0xCE: knockdowns so far: state_31_down adds one each time; each one takes 80 frames off the time down (escape_time op 0) */
    u16  aux_timer;           /* +0xD0: a frame counter states use for their own purposes: d-pad held in a lock-up (grapple_walk switches to walking at 7), the walk-in countdown in 0xBE, the referee count in 0xA6 */
    s16  slide_frames;        /* +0xD2: state_BD_ladder_grab_prize uses it as the mash count for the prize (40 - spirit / 5); else: frames of the slide into a lock-up (6; grapple_anim_defender / grapple_defender_anim), counted by count_timer in lockup_slide_in; the cage and ladder grabs too */
    u8   _unk0d4[2];
    s16  move_link;           /* +0xD6: ? written by the pin states, move_chain_check and move_aftermath_check */
    s16  ally_id;             /* roster id of the wrestler who runs in for this one (run_in_try) */
    s16  run_target;          /* +0xDA: the target set by run_inputs for state 0x71 */
    s16  tag_state;           /* +0xDC: tag_rules_check / tag_in / ai_wants_tag_out */
    s16  tag_health;          /* +0xDE: health when the wrestler last came in (set with health at the start, and by tag_in); sub_800FB4C0 compares it with health (a drop of 51 or more). Named 2026-11-03 (was _unk0de) */
    s16  climb_counter;       /* +0xE0: C-up / cage climb counter (input_c_up, state_C4_cage_climb, walk_move) */
    u8   end_pose;            /* +0xE2: pose class the last move or animation ended in; state_30_settle switches on it to pick the next state */
    u8   hits_taken;          /* +0xE3: hits taken recently (strike_land adds, hits_taken_decay lowers; reset when down); the CPU's focus switch reads it */
    u8   fall_chance;         /* +0xE4: knockdown chance level for chance_roll kinds 16-18 (100 x this); set by the rope / corner / held states */
    u8   down_hit_kind;       /* +0xE5: state_63_hit_while_down */
    u8   ai_e6;               /* +0xE6: read by ai_decide_in_ring / ai_decide_backstage */
    u8   weakest_region;      /* +0xE7: the weakest body region (apply_damage); picks hurt animations (anim_pick_hurt) */
    u8   run_in_e8;           /* +0xE8: read by state_A3_run_in */
    u8   flags_e9;            /* 0x08 CPU-controlled (Z + a C button toggles it, wrestler_tick); 0x02 slot may take a run-in (run_in_try); bits 1 / 2 / 4 tested by stand (0x03) and 0x00; RAM: 0x10 the two wrestlers, 0x0A the spare slots; in a guest-referee match 0x18 the wrestlers and 0x04 the guest referee's slot (2) */
    u8   flags_ea;            /* ? 0x80 tested on rec_c by the pin defender, 0x08 by 0x73 */
    u8   ground_side;         /* +0xEB: where a ground grapple is taken (head / legs, face up / down): 0x46, ground_grapple_start, the stir input */
    u8   frame_flags;         /* +0xEC: cleared by wrestler_update every frame; states set bits: 0x01 slumped in the corner (0x18; also 0x2D), 0x02 / 0x04 more corner-slump steps (0x18 sets 0x03 / 0x05), 0x08 held upside down (0x34) or knocked down (0x2D), 0x10 on the ropes (0x17). The corner set-up (0x3C) needs 0x01 without 0x02 (code 2026-11-02) */
    u8   press_kind;          /* +0xED: input_press_kind result: 0x01 held (strong), 0x02 tapped, 0x04 A, 0x08 B, 0x10 A+B held, 0x20 with a d-pad direction */
    u8   flags_ee;            /* meter mode: 0x01 empty (spirit 0 for 300 frames, then 40), 0x02 SPECIAL (at 100, 600 frames, then 70), 0x04 finisher mode (a taunt while SPECIAL, spirit_update op 5: 600 frames, damage x1.2, health_eff reads health_max, then 60); 0x10 hit this frame (cleared every frame), 0x20 the hit flash: set with hit_flash_timer = 30 by wrestler_add_health mode 0 on a loss, kept by hit_flash while the timer runs (code 2026-11-02) */
    u8   flags_ef;            /* ? 0x01 holding a weapon (the strike states pick weapon swings 0x26 / 0x27 on it) */
    u8   flags_f0;            /* ? bits 1 / 2 tested on the opponent by grapple states 0x55 / 0x56 / 0x5D */
    u8   flags_f1;            /* ? 0x08 set when this wrestler's ally has run in */
    u8   idle_flags;          /* +0xF2: idle_timer, stand_flags_update, tag checks */
    u8   bonus_flags;         /* +0xF3: bit 0x04 faster regen (60 frames) and spirit bonuses (spirit_exchange); move_aftermath_check */
    u8   act_flags;           /* +0xF4: bits 0x0F in the air (grapple_attempt_resolve catches them); bit 0x04 lets ai_press_focus_switch start from 0x0A */
    u8   call_flags;          /* +0xF5: guest_referee_call / the apron wait; cleared by wrestler_set_state */
    u8   stay_flags;          /* +0xF6: stay down / referee count (0xA7, 0xA6, strike_land, run_inputs); cleared by wrestler_set_state */
    u8   flags_f7;            /* ? bits 0x0C cleared every frame, 0x01 cleared by wrestler_set_state */
    u8   team_member;         /* +0xF8: which member of the slot's line-up is in now (code 2026-11-02): elimination_record moves it on after a fall (the next roster id from table 0x800BD4D0 by member; flags_f1 0x04 at the last one); falls_left shows team_size - team_member under g_match_type 0x4000000 */
    u8   team_size;           /* +0xF9: members in the slot's line-up, set at match set-up (0x8009ED24) */
    u8   _unk0fa[2];
    struct Vec3 pos;          /* position (x, y, z): ring_zone_of reads it; point_at_offset places a wrestler relative to the opponent's pos and facing */
    s32  tilt_x;              /* +0x108: with tilt_z the body's tilt in 1/4096 turns while on a ladder (location 0x08): model_pose_in_place passes both to model_animate, which turns them into the x and z angles of the model's rotation (code 2026-11-02) */
    s32  facing;              /* facing angle, used with pos by point_at_offset */
    s32  tilt_z;              /* +0x110: see tilt_x */
    u8   _unk114[0xC];
    struct Vec3 point_120;    /* +0x120: the body's centre: wrestler_joints_update first copies model joint 1 here, then overwrites it with (model joint 1 + the point at model block +0xAA8) / 2 each frame; facing_to_opponent and opp_distance come from its own and the opponent's (RAM: 85 above pos at the start of a match) */
    struct Vec3 joints[18];   /* +0x12C: the model's world joint positions 2-19 (model block joint records, 124 bytes apart), copied by wrestler_joints_update each frame after joint 1 into point_120; airborne_fall_step takes the lowest y of point_120 and joints 0-17 (code 2026-11-02) */
    u8   _unk204[0x60];
    struct Vec3 approach_pts[3]; /* +0x264: the three control points of the walk beside a lying opponent (ground_approach_plan: start, mid, end), y = the walker's pos.y */
    struct Vec3 step_vec;     /* +0x288: the per-frame step of a run or walk-in: run_start builds it from the run direction at walk_speed(w, 8) (the fast column doubled, 9.0-13.5) and run_step moves the wrestler by it every frame (code 2026-11-02); also set by state_BE */
    u8   _unk294[4];
    s32  turn_step;           /* +0x298: a per-frame turn (or a direction) a state keeps: the run start (run_dir), the walk-in, the slide into a lock-up ((lockup_facing - facing) / slide_frames); state_97 too */
    u8   _unk29c[4];
    struct Vec3 dive_target;  /* +0x2A0: where a dive / aerial move aims (dive_aim: point_120 + the reach along the aim), read by aerial_step's flight (code 2026-11-03) */
    u8   _unk2ac[0xC];
    f32  opp_distance;        /* +0x2B8: ? distance to the opponent; the strike state picks close / far moves and corner or table strikes with it */
    f32  approach_t;          /* +0x2BC: where along the approach curve the walker is (0..1; ground_approach_plan) */
    f32  approach_dt;         /* +0x2C0: the approach curve's step per frame (1 / (distance * speed factor / 6)) */
    f32  region[5];           /* +0x2C4: body-part condition, one per region (wrestler_region_condition reads it; 50 in finisher mode); apply_damage lowers each by the move's region damage and records the weakest in +0xE7 */
    f32  hold_dist;           /* +0x2D8: the whip keeps the pair this far apart (state_0B steps 0 / 4; code 2026-11-03) */
    f32  fall_vy;             /* +0x2DC: vertical speed while flags8a 0x8000 (falling): airborne_fall_step adds it to pos.y each frame and adds fall_accel to it (code 2026-11-02) */
    f32  fall_accel;          /* +0x2E0: added to fall_vy each frame (gravity) */
    u32  attrs;               /* +0x2E4: pointer to the slot's 64-byte attribute record (0x8009FE78 + 64 * slot), unpacked bit by bit by wrestler_data_unpack: +0x00 speed class, +0x01 follow-up variant set, +0x02 bleed class, +0x03 / +0x04 regen, +0x05 spirit bonus, +0x06 weapon trait, +0x07 TKO recovery, +0x08 / +0x09 crowd weapons, +0x0A-+0x0E strength, +0x0F-+0x13 defence, +0x14 height?, +0x15 size, +0x16 class, +0x18 partner AI, +0x19-+0x27 AI personality, +0x1C / +0x20 finishers, +0x2E tag partner id, +0x34 trait bits, +0x38 / +0x3C has-move bits set by moveset_unpack (full table in damage_and_spirit.md) */
    u8   anim_player[0x1C];   /* animation player state at +0x2E8, set by anim_player_set in wrestler_set_anim */
    u8   move_slot;           /* +0x304: slot whose move set the current move comes from (wrestler_start_move) */
    u8   _unk305;
    s16  move_repeat;         /* +0x306: 1, or +1 when the same move starts again */
    s16  move_num;            /* +0x308: current move number (0-547) */
    s16  move_anim;           /* +0x30A: its animation, from g_move_anims */
    s16  move_anim2;          /* +0x30C: the partner's animation, from g_move_anims2 */
    s16  move_anim2_prev;     /* +0x30E */
    u32  move;                /* +0x310: pointer to the current move record (36 bytes) in g_movesets; its +0x04 is a follow-up move, +0x20 flags (0x8000 -> flags8c 0x01; 0x10, 0x80000, 0x1000000 tested by states) */
    u8   hit_by;              /* +0x314: slot (or object owner) of the last hit (object_hit_check) */
    u8   _unk315;
    s16  hit_move_repeat;     /* +0x316: the attacker's move_repeat, copied by hit_move_copy with the rest */
    s16  hit_move_num;        /* +0x318: move number of the last hit taken (5 * type + 111 for a thrown object) */
    s16  hit_move_anim;       /* +0x31A: the attacker's move_anim */
    s16  hit_move_anim2;      /* +0x31C: the attacker's move_anim2 */
    s16  hit_move_anim2_prev; /* +0x31E: the attacker's move_anim2_prev; apply_damage uses the alternate damage (+0x1B) when it equals hit_move.move_check */
    struct MoveRecord hit_move; /* +0x320: copy of the move record that hit it (hit_move_copy) */
    u32  damage_history;      /* +0x344: pointer to 30 (a, b) byte pairs; damage_history_ratio returns sum b / sum a, used by escape_time */
    u32  zone_info;           /* +0x348: the arena collision segment the wrestler is against (code 2026-11-02): a quad of 4 s16 points, +0x18 the plane normal (floats), +0x24 the zone id (minus 1 becomes dir in 0xC8 / 0xCB), +0x26 / +0x28 flags (0x26: exit / direction bits, 0x28: 0x10 ring edge, 0x40 weapons here, 0x100 ropes), +0x2C the next segment; dword_8014D3B0[ring_zone] from state_01_stand, or the segment hit by move_hits_segment (which also writes +0x34C) */
    u32  zone_info2;          /* +0x34C: the second wall keep_in_bounds touched this push (the same as zone_info when only one), code 2026-11-02 */
    u32  bounds_override;     /* +0x350: per-state collision set for keep_in_bounds (0 = by location), zeroed by wrestler_set_state */
    u32  grab_zone_info;      /* +0x354: copy of zone_info taken when a weapon grab starts (input_weapon_grab); state_8A reads the zone id at +0x24 */
};

/* Filled by wrestler_update for the slot being updated and passed to its state handler.
 * Checked in RAM 2026-11-01 (singles match, Rev 0 emulator, everything shifted by +0x1520). */
/* One joint record of a model block (124 bytes): record j of a block is at +0x7C * j, j = 1..22
 * (1-19 the skeleton joints, 20-22 the extra nodes on the head and feet). model_decode_frame /
 * model_decode_frame_interp write rot and pos from the animation (pos only for the parentless
 * joints 1, 2, 3, 7, 12, 14, 19; model_build_skin places the rest from the parent), model_build_skin
 * keeps the frame's rot / pos in prev_* for model_blend_prev, and model_animate fills the world
 * points from the render node's matrix (code 2026-11-02). */
struct ModelJoint {
    struct Vec3 rot;          /* +0x00: the joint's rotation, Euler angles in degrees (x, y, z) */
    struct Vec3 pos;          /* +0x0C: the joint's position in model space */
    struct Vec3 prev_rot;     /* +0x18: rot of the frame posed before (model_build_skin) */
    struct Vec3 prev_pos;     /* +0x24: pos of the frame posed before */
    u8   _unk30[0x18];
    struct Vec3 pt_a;         /* +0x48: a point in the node's space (the joint's pivot) */
    struct Vec3 pt_a_world;   /* +0x54: pt_a in world space with x mirrored: the joint positions wrestler_joints_update copies, joint 1 the root model_animate anchors on an animation switch */
    struct Vec3 pt_b;         /* +0x60: a second point in the node's space */
    struct Vec3 pt_b_world;   /* +0x6C: pt_b in world space, x mirrored */
    f32  yaw;                 /* +0x78: rot.y plus the body's yaw (degrees) */
};

/* A wrestler's model block, 2852 bytes at 0x800A1130 + 2852 * slot: record 0 is the body, records
 * 1-22 the joints (struct ModelJoint). Posed by model_animate each frame (code 2026-11-02). */
struct ModelBlock {
    u8   _unk00[0xC];
    struct Vec3 translation;  /* +0x0C: where the model is drawn: (-pos.x, pos.y + the ladder-carry offset, pos.z) */
    u8   _unk18[0x60];
    f32  yaw;                 /* +0x78: the body's yaw in degrees, -(facing + 2048) * 360 / 4096 */
    struct ModelJoint joint[22];  /* +0x7C: joint records 1-22 (joint[0] is record 1) */
};

/* A slot's animation stream, 192 bytes at 0x80069F20 + 192 * slot (anim_system_init, anim_stream_open,
 * anim_stream_fetch, anim_stream_advance, model_animate; code 2026-11-02). Keyframes (98 bytes) are
 * DMA'd from the ROM one at a time into two 128-byte heap buffers: buf[cur_buf] holds the keyframe
 * being posed, the other the next one. */
struct AnimStream {
    u8   first;               /* +0x00: 1 from anim_system_init until the slot's first model_animate: no root anchoring and no blend on the first animation */
    u8   same_anim;           /* +0x01: the animation just started is the one already streaming (a restart): no blend from the previous pose */
    u8   cur_buf;             /* +0x02: which of buf holds the current keyframe */
    u8   dma_pending;         /* +0x03: a keyframe DMA into buf[cur_buf ^ 1] has been started and not waited for */
    s16  anim;                /* +0x04: the animation streaming, -1 none */
    s16  frame;               /* +0x06: the frame posed last; -1 after an animation change */
    u32  buf[2];              /* +0x08: the two 128-byte keyframe buffers (heap addresses) */
    u32  rom_next;            /* +0x10: ROM address of the next keyframe to fetch */
    s32  bytes_left;          /* +0x14: bytes of the animation file still to fetch */
    f32  scale[16];           /* +0x18: the model's scale matrix (row-major 4 x 4), the root of the render tree */
    u8   _unk58[0x68];        /* +0x58: libultra state of the two DMAs: two OSMesgQueue (24 bytes each), two OSIoMesg (24 bytes each) and their message slots */
};

/* A render node of the wrestler's model tree, 220 bytes; 28 per slot at 0x80091CC0 + 6160 * slot:
 * node 0 is the body (its world matrix is built by model_animate from the position, yaw and tilt
 * and the stream's scale), node j (1-22) the part on joint j. node_tree_update multiplies each
 * node's local matrix by its parent's world one (code 2026-11-02). */
struct ModelNode {
    u32  parent;              /* +0x00: the parent node's address, 0 at the root */
    u32  child;               /* +0x04: the first child's address, 0 none */
    u32  next;                /* +0x08: the next sibling's address, 0 none */
    u8   _unk0c[0x10];
    f32  local[16];           /* +0x1C: the node's matrix in its parent's space (model_build_skin's skin matrix for a joint) */
    f32  world[16];           /* +0x5C: local x the parent's world (node_tree_update); for the root a copy of its own world matrix */
    u8   _unk9c[0x40];
};

/* g_objects (0x8016AE10): weapons and props, 180 bytes each, 7 records (0-6) seen in use.
 * Read in RAM 2026-11-01: a thrown stop sign (type 17) lying on the floor, a crowd weapon (type 5)
 * in P1's hand; the ring steps use records 4 and 5. */
struct Object {
    s32  type;                /* weapon type (crowd types from g_crowd_weapon_types; 17 = stop sign); -1 unused */
    s32  type2;               /* current form of the object; 25 is a standing ladder (input_ladder_climb); state_8A copies type into it */
    s32  holder;              /* slot holding it, -1 when loose */
    s32  owner;               /* slot that last held / threw it (object hit code uses its record) */
    s32  last_hit;            /* slot (or 100 + object) it last hit, -1 none */
    s32  climber0;            /* +0x14: slot climbing side 0 of a ladder (type2 25), -1 free */
    s32  climber1;            /* +0x18: slot climbing side 1, -1 free */
    s32  _unk1c;
    u16  flags;               /* 0x01 / 0x02 held or carried, 0x04 lying loose (RAM), 0x08 flying, 0x80 can hit while flying, 0x100 a standing ladder, 0x400 landed, 0x800 landing, 0x4000 falling (tested by block and 0xB8), 0x10 knocked by another object, 0x8000 */
    u8   _unk22[0x22];
    struct Vec3 pos;          /* +0x44 world position */
    u8   _unk50[0x58];        /* +0x50 / +0x54 tilt angles, +0x5C yaw, +0x60-+0x68 velocity, +0x78 spin, +0x7C-+0x84 second velocity, +0x94 spin change, +0x98-+0xA4 grow factors (objects_update) */
    struct Vec3 prev_pos;     /* +0xA8 */
};

/* Controller state as pads_read (0x8000xxxx main segment) leaves it, 0x8009EDA0 + 12 * port; the
 * code reads it from 0x8009EDA4 (held). Code 2026-11-02. */
struct Pad {
    u8   _unk00[4];
    u16  held;                /* +0x04: buttons down (N64 bit order) */
    u16  pressed;             /* +0x06: down now, up last read */
    u16  released;            /* +0x08: up now, down last read */
    s8   stick_x;             /* +0x0A */
    s8   stick_y;             /* +0x0B */
};

/* Per-slot input block, 0x80159518 + 56 * slot (code 2026-11-02: wrestler_tick fills the entry at
 * +0x28 from the slot's pad through input_remap, or from the AI block for a CPU slot; then
 * input_history_push shifts the histories and fills +0x00 / +0x14 / +0x32-+0x36). WrestlerCtx
 * copies the entry from +0x28. */
struct InputSlot {
    u16  held_hist[10];       /* +0x00: held buttons, this frame first, then the 9 frames before */
    u16  pressed_hist[10];    /* +0x14: newly pressed buttons, same order (input_press_kind reads both) */
    u16  held;                /* +0x28: the entry: buttons held this frame (after input_remap) */
    u16  pressed;             /* +0x2A: newly pressed this frame */
    s16  stick_x;             /* +0x2C: control stick (from the pad's s8, or the AI block) */
    s16  stick_y;             /* +0x2E */
    u8   stick_pushed;        /* +0x30: 1 when |stick_x| or |stick_y| is past 40 (wrestler_tick) */
    u8   _pad31;
    u16  held2;               /* +0x32: held this frame or the frame before (WrestlerCtx.in_0a) */
    u16  pressed2;            /* +0x34: pressed this frame or the frame before (WrestlerCtx.in_0c) */
    u16  held_prev;           /* +0x36: held the frame before (WrestlerCtx.in_0e) */
};

/* The input entries copied into WrestlerCtx sit at 0x80159540 + 56 * slot, the tail of a 56-byte
 * per-slot block at 0x80159518 whose first 20 u16 are a button history (input_press_kind reads it
 * to tell taps from holds); see struct InputSlot. */
struct WrestlerCtx {
    s32 slot;                 /* the slot being updated (0-3) */
    s32 slot_a;               /* copy of the record's +0x08: the opponent it is working on (tag: the other team's legal man) */
    s32 slot_b;               /* copy of +0x0C: the tag partner (its own slot in singles) */
    s32 slot_c;               /* copy of +0x00: ? the last wrestler it grappled (0 until then) */
    s32 slot_d;               /* copy of +0x04 */
    s32 in_byte;              /* input entry +0x08 (a byte): 1 when the control stick (the taunt control: AI mode 50 pushes it to 80) is pushed past 40 on either axis (in_s16_a / in_s16_b, set in wrestler_tick's input copy, code 2026-11-02); state_01_stand clears it; the down / pin states wait while it is set */
    s32 in_s16_a;             /* input entry +0x04: control stick x (|x| > 40 sets in_byte) */
    s32 in_s16_b;             /* input entry +0x06: control stick y */
    u16 buttons;              /* input entry +0x00: buttons held (N64 bit order, B = 0x4000) */
    u16 pressed;              /* input entry +0x02: buttons newly pressed; handlers test A|B|L|R (0xC030), A (0x8000), R (0x10) */
    u16 in_0a;                /* input entry +0x0A: ? set from the press and kept one frame after release */
    u16 in_0c;                /* input entry +0x0C: same as in_0a so far; grapple code tests A+B (0xC000) in it */
    u16 in_0e;                /* input entry +0x0E: ? released (set the frame the button goes up) */
    u16 _pad2a;
    struct Wrestler *self;                 /* pointer to this slot's g_wrestlers record */
    struct Wrestler *rec_a;                /* records of slot_a .. slot_d, in the same order */
    struct Wrestler *rec_b;
    struct Wrestler *rec_c;
    struct Wrestler *rec_d;
};

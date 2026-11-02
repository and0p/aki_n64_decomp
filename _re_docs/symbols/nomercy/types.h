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

struct Wrestler {             /* 0x358 bytes; g_wrestlers[4] at 0x80159990 */
    s32  pair_other;          /* slot of the wrestler it is paired with in a move; set by wrestler_pair_set_state (0 at the start) */
    s32  slot_d;              /* ? a slot index, copied to WrestlerCtx.slot_d */
    s32  opponent;            /* slot it is working on (tag: the other team's legal man) */
    s32  partner;             /* tag partner's slot; its own slot in singles */
    s32  _unk010;             /* -1 in every record seen */
    s32  state;               /* action state, index into g_state_handlers */
    s32  state_step;          /* step within the state */
    s32  prev_state;          /* the state before this one */
    s32  prev_step;           /* the step it left prev_state at */
    s32  facing_to_opponent;  /* ? set every frame from point_120 of itself and of the opponent */
    u8   _unk028[8];
    s32  pair_slot;           /* the paired wrestler's slot while a pair move runs; -1 after wrestler_unpair */
    s32  pair_slot2;          /* ? also set to -1 by wrestler_unpair */
    u8   _unk038[4];
    s32  held_obj;            /* index into g_objects of the object held: 4 / 5 are the two ring steps (input_weapon_grab sets them directly), a crowd weapon gets a free record (1 for the first after a thrown one, RAM 2026-11-01) */
    s32  ladder_side;         /* +0x40: which side of the ladder it climbs (0 / 1), set with held_obj by input_ladder_climb */
    s32  contact;             /* +0x44: what it is touching this frame: 100 + object index for an object (input_ladder_climb), cleared by wrestler_update */
    u8   _unk048[0x18];
    s32  ring_zone;           /* ring_zone_of(&pos), set by wrestler_update every frame */
    u8   _unk064[4];
    s32  _unk068;             /* set to -1 by wrestler_update every frame */
    u8   _unk06c[4];
    u32  strike_flags;        /* +0x70: the incoming strike's flags: 0x100600 unblockable (0x600 a weapon), 0x40 with location 0x10, 0x800 / 0x8000 / 0x80 pick falls in state_2D_knocked_down */
    u32  hit_flags;           /* +0x74: how the last hit landed, set by strike_land; 0x100000-0x1000000 pick the reaction (5 kinds), 0x2000000 its mirror, 0x10 / 0x01 / 0x8000 / 0x80 / 0x20 / 0x04 other falls (state_2D_knocked_down, strike_reaction_anim) */
    u32  flags78;             /* grapple and situation bits (grapple_start_pair, grapple_attempt_resolve, state_50): 0x04 strong grapple (A held), 0x40 running, 0x80 rear, 0x100 opponent staggered, 0x200 / 0x400 defender / attacker on the apron, 0x800 normal standing grapple, 0x20000 / 0x40000 floor to apron / ring, 0x8000 / 0x8000000 tested by 0x84 / 0x89; copied to the opponent when a grapple starts */
    u8   _unk07c[4];
    u16  flags80;             /* flag words +0x80-+0x8E: wrestler_update clears some bits of each every frame. flags80: 0x01 in a grapple or move, 0x08 down, 0x40 / 0x80 ground hold attacker / held, 0x10 down sitting slouched (cleared when a hit lays it flat) (RAM) */
    u16  location;            /* where it is: 0x01 ring, 0x02 apron, 0x04 floor, 0x40 / 0x80 / 0x100 / 0x200 at one of four raised places (state_6F picks one from flags84 bits 0x01 / 0x02 / 0x10 / 0x20; 0x40, 0x100 and 0x200 are the three breakable tables, intact bits 0x04 / 0x08 / 0x10 of g_tables_intact, 0x80 has no such check), 0x20 up on top of it (anim_end_location place 7 keeps the place bits and adds 0x20; RAM read 0x40 beside the announce table and 0x60 on it); place codes from anim_end_location: 1 ring, 2 floor, 3 apron, 6 0x400, 7 on top; bits 0xF800 survive those changes; 0x08 on a ladder (set by 0xAA, the climb, cleared by 0xAB); 0x10 set by 0xAF / cleared by 0xB0; 0x800 and 0x1000 are per-frame: wrestler_update clears both every frame and the state handler sets them again (0x800 by the tag states 0x0A and 0x95, the partner on the apron; 0x1000 by the whip 0x12 / 0x14, the shove 0x0E, the cheap shot 0x6E and the chain moves 0x87 / 0x9D; read by the collision and landing code) (code); 0x400 probably up on a turnbuckle (states 0xC4-0xCF: strikes, grapples and attacks at or from it) */
    u16  state_counter;       /* ? zeroed by wrestler_set_state */
    u16  flags86;
    u16  flags88;             /* bit 2: in the match (wrestlers_update_all), 1 / 2 pick the update pass; 0x01 set by wrestler_set_state_now (rerun now?), 0x02 by the pair setters, 0x04 while paired (cleared by wrestler_unpair), 0x10 set on every state change */
    u16  flags8a;             /* ? RAM: 0x0300 standing, 0x2000 in a grapple / move or on the table, 0x8000 while falling through the table, 0x6000 getting up */
    u16  flags8c;             /* ? 0x80 on the waiting partners on the apron */
    u16  flags8e;
    u8   _unk090[4];
    u16  end_flags;           /* +0x94: why this wrestler lost a fall, read by match_end: 0x08 pinned (count of 3), 0x10 out over the top: set by state_01_stand when it stands on the floor under g_match_rules 0x400 (the battle royal / Rumble rule; also sets flags80 0x01 and drops the input), result 0x80000 in match_end, 0x20 submitted (ground hold 0x49), 0x40 TKO, 0x100 last one standing: set on itself by state_01_stand when it has flags_f1 0x10 (entered under elimination rules or by a run-in) and every other slot is on the floor with end_flags 0x01, without flags_f1 0x01 and not in state 0xA3 (code; the bell sounds), so the flags also mark a winner, 0x200 a fall called by the guest referee, 0x400 disqualified (strike_dq_check, guest_referee_call), 0x800 lost the ladder match (the opponent grabbed the prize, state 0xBD), 0x1000 the opponent escaped the cage (set by state_C5_cage_escape, code); 0x01 / 0x04 by match_record_fall */
    u16  partner_flags80;     /* copy of the partner's flags80, made by wrestler_update */
    u16  _unk098;             /* cleared every frame */
    u16  _unk09a;
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
    u8   _unk0b6[4];
    s16  pin_count;           /* pins taken this match: state_45_pin_defender adds 1 after each escape_time op 1, wrestler_reset clears it; each one adds 2 frames to the next pin's escape time (code) */
    u8   _unk0bc[2];
    s16  finisher_heat;       /* +0xBE: set to 180 when a finisher lands (escape_time case 0); escape_time looks it up in 0x801461E4 */
    u8   _unk0c0[0xC];
    s16  dir;                 /* ? direction index into the angle table at 0x8014D1D8 */
    s16  down_count;          /* +0xCE: knockdowns so far: state_31_down adds one each time; each one takes 80 frames off the time down (escape_time op 0) */
    u16  _unk0d0;             /* frames counter: d-pad held in a lock-up (grapple_walk switches to walking at 7), the walk-in countdown in 0xBE, the referee count in 0xA6 */
    u8   _unk0d2[6];
    s16  ally_id;             /* roster id of the wrestler who runs in for this one (run_in_try) */
    u8   _unk0da[4];
    s16  _unk0de;             /* ? compared with health (difference >= 51) by sub_800FB4C0; set with health at the start */
    u8   _unk0e0[2];
    u8   end_pose;            /* +0xE2: pose class the last move or animation ended in; state_30_settle switches on it to pick the next state */
    u8   _unk0e3[6];
    u8   flags_e9;            /* ? 0x02 slot may take a run-in (run_in_try); bits 1 / 2 / 4 tested by stand (0x03) and 0x00; RAM: 0x10 the two wrestlers, 0x0A the spare slots; in a guest-referee match 0x18 the wrestlers and 0x04 the guest referee's slot (2) */
    u8   flags_ea;            /* ? 0x80 tested on rec_c by the pin defender, 0x08 by 0x73 */
    u8   _unk0eb;
    u8   frame_flags;         /* cleared by wrestler_update every frame; states set bits (0x01 corner slump, 0x08 state_34) */
    u8   press_kind;          /* +0xED: input_press_kind result: 0x01 held (strong), 0x02 tapped, 0x04 A, 0x08 B, 0x10 A+B held, 0x20 with a d-pad direction */
    u8   flags_ee;            /* meter mode: 0x01 empty (spirit 0 for 300 frames, then 40), 0x02 SPECIAL (at 100, 600 frames, then 70), 0x04 finisher mode (a taunt while SPECIAL, spirit_update op 5: 600 frames, damage x1.2, health_eff reads health_max, then 60); 0x10 hit this frame (cleared every frame), 0x20 set when health hits 0 */
    u8   flags_ef;            /* ? 0x01 holding a weapon (the strike states pick weapon swings 0x26 / 0x27 on it) */
    u8   flags_f0;            /* ? bits 1 / 2 tested on the opponent by grapple states 0x55 / 0x56 / 0x5D */
    u8   flags_f1;            /* ? 0x08 set when this wrestler's ally has run in */
    u8   _unk0f2[5];
    u8   flags_f7;            /* ? bits 0x0C cleared every frame, 0x01 cleared by wrestler_set_state */
    u8   _unk0f8[4];
    struct Vec3 pos;          /* position (x, y, z): ring_zone_of reads it; point_at_offset places a wrestler relative to the opponent's pos and facing */
    u8   _unk108[4];
    s32  facing;              /* facing angle, used with pos by point_at_offset */
    u8   _unk110[0x10];
    struct Vec3 point_120;    /* +0x120: start of the world joint positions (12 bytes each, about 20) copied from the model each frame by wrestler_joints_update; facing_to_opponent comes from its own and the opponent's (RAM: 85 above pos at the start of a match) */
    u8   _unk12c[0x15C];
    struct Vec3 step_vec;     /* ? +0x288: a vector built from facing (vec_from_angle) in state_BE_fetch_weapon and passed to sub_80131BD0 */
    u8   _unk294[4];
    s32  angle_rel;           /* ? +0x298: facing_to_opponent relative to facing (state_97) */
    u8   _unk29c[0x1C];
    f32  opp_distance;        /* +0x2B8: ? distance to the opponent; the strike state picks close / far moves and corner or table strikes with it */
    u8   _unk2bc[8];
    f32  region[5];           /* +0x2C4: body-part condition, one per region (wrestler_region_condition reads it; 50 in finisher mode); apply_damage lowers each by the move's region damage and records the weakest in +0xE7 */
    u8   _unk2d8[0xC];
    u32  attrs;               /* +0x2E4: pointer to the wrestler's attribute record: +0x00 speed class (walk_speed), +0x03 / +0x04 health regen per tick, +0x05 bonus index, +0x06 weapon trait, +0x08 forced crowd weapon, +0x0A-+0x0E strength by body region (apply_damage), +0x0F-+0x13 defence by hit kind, +0x15 style (size / 42), +0x16 class, +0x19-+0x27 AI personality, +0x1C / +0x20 finisher move numbers, +0x34 / +0x38 / +0x3C trait bits */
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
    s16  _unk316;
    s16  hit_move_num;        /* +0x318: move number of the last hit taken (5 * type + 111 for a thrown object) */
    s16  _unk31a;
    s16  _unk31c;
    s16  _unk31e;             /* ? compared with hit_move+0x1C by apply_damage (picks the alternate damage +0x1B) */
    u8   hit_move[36];        /* +0x320: copy of the move record that hit it: +0x06 damage, +0x07 / +0x08 spirit for attacker / defender (spirit_exchange), +0x0B region, +0x0C hit kind (4 = no region record), +0x12-+0x16 damage to each of the 5 regions, +0x1B alternate damage, +0x1C s16 move check, +0x20 u32 flags (0x8000000: damage does not lower health_max) */
    u32  damage_history;      /* +0x344: pointer to 30 (a, b) byte pairs; damage_history_ratio returns sum b / sum a, used by escape_time */
    u32  zone_info;           /* ? +0x348: dword_8014D3B0[ring_zone], set by state_01_stand; its s16 at +0x24 minus 1 becomes dir in 0xC8 / 0xCB */
    u8   _unk34c[4];
    u32  bounds_override;     /* +0x350: per-state collision set for keep_in_bounds (0 = by location), zeroed by wrestler_set_state */
    u32  grab_zone_info;      /* +0x354: copy of zone_info taken when a weapon grab starts (input_weapon_grab); state_8A reads the zone id at +0x24 */
};

/* Filled by wrestler_update for the slot being updated and passed to its state handler.
 * Checked in RAM 2026-11-01 (singles match, Rev 0 emulator, everything shifted by +0x1520). */
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

/* The input entries copied into WrestlerCtx sit at 0x80159540 + 56 * slot, the tail of a 56-byte
 * per-slot block at 0x80159518 whose first 20 u16 are a button history (input_press_kind reads it
 * to tell taps from holds). */
struct WrestlerCtx {
    s32 slot;                 /* the slot being updated (0-3) */
    s32 slot_a;               /* copy of the record's +0x08: the opponent it is working on (tag: the other team's legal man) */
    s32 slot_b;               /* copy of +0x0C: the tag partner (its own slot in singles) */
    s32 slot_c;               /* copy of +0x00: ? the last wrestler it grappled (0 until then) */
    s32 slot_d;               /* copy of +0x04 */
    s32 in_byte;              /* input entry +0x08 (a byte); ? some handlers act when it is non-zero, state_01_stand clears it */
    s32 in_s16_a;             /* input entry +0x04 */
    s32 in_s16_b;             /* input entry +0x06 */
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

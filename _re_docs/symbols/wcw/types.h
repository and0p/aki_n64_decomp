/*
 * WCW vs. nWo: World Tour (US) - structures recovered from the original game.
 *
 * Reverse-engineering reference only, not the port's own types. Pointers are 32-bit on
 * the N64; the IDA build adds __ptr32 to every pointer automatically.
 *
 * Field descriptions: the trailing comment of each field is indexed by tools/sym. Start
 * it with "?" when the meaning is a guess. Fields named _unkXXX are not understood yet.
 */
typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;
typedef unsigned int u32; typedef int s32; typedef float f32;

struct Vec3 { f32 x, y, z; };
struct Vec4 { f32 x, y, z, w; };

struct FileInfo {
    u32 rom_start;            /* ROM address of the file data */
    u32 rom_end;
    u32 size;                 /* unpacked size */
    u32 packed;               /* packed length, 0 when stored raw */
};

struct Pad {                  /* g_pads[], one per controller port, 12 bytes */
    u16 _unk00;
    u16 _unk02;
    u16 held;                 /* N64 buttons held (enum N64Button) */
    u16 pressed;              /* ? buttons newly pressed this frame */
    u16 _unk08;
    s8  stick_x;
    s8  stick_y;
};

struct WrestlerHeader {       /* move-set file 0 (0x20 bytes), one per wrestler */
    u8  _unk00[0x1C];
    u32 traits;               /* ? bit 0 and bit 1 change grapple results and stat categories; bits 2-5 pick the stance style 0-3 used by wrestler_base_anim (idle, walk, block) */
};

struct AnimPlayer {           /* 0x18 bytes, filled by anim_player_set from file 618 metadata */
    u32 yaw_delta;            /* animation header bits 16-25 * 4 (1/4096 turns); added to Wrestler.facing by wrestler_apply_anim_frame */
    u8  frame_count;           /* animation header bits 0-15 (anim_frame_count), truncated to u8 */
    s8  _unk05;               /* meta[1] >> 7 */
    u8  location;             /* meta[0] & 0xF: 1 ring, 2 floor, 3 apron?, see wrestler_apply_anim_location */
    u8  pose_class;            /* animation header bits 26-30 (anim_pose_class); copied to Wrestler.pose every frame */
    u8  meta3;                /* meta[3] */
    u8  meta4;
    u8  meta5;
    u8  meta6;
    u8  window_start;         /* ? meta[7]: flags_hi |= 4 while window_start <= anim_frame <= window_end */
    u8  window_end;           /* ? meta[8] */
    u8  reversal_frame;       /* ? meta[9]: state 0x42 reverses when anim_frame equals this */
    u8  meta10;
    u8  _unk10[3];
    u8  meta2;                /* meta[2] */
    u8  _unk14[4];
};

struct MatchClock {
    s16 minutes;              /* counts up; resets at 60 when there is no time limit */
    s16 seconds;              /* 0..59 */
    s16 ticks;                /* 0..29, 30 ticks per second */
};

struct MoveParams {           /* unpacked from a 24-byte move record by move_record_unpack; field notes in individual_games/wcw_world_tour/moves.md */
    u16 move_id;              /* W0 & 0x7FF, global move id */
    u16 end_value;            /* ? (W5 >> 5) & 0xFFF, rises with slot number; copied to the receiver's +0x03A when the move ends (sub_800A7300, sub_800CBC94) */
    u8  power;                /* (W0 >> 11) & 0x3F; move_apply_hit scales all damage by it (0-40+) */
    s8  spirit_atk;           /* (W0 >> 17) & 0x3F signed; attacker's spirit change on the hit (0..15) */
    s8  spirit_def;           /* (W0 >> 23) & 0x3F signed; defender's spirit change on the hit (-15..+5) */
    u8  f07;                  /* W1 & 7; 0-5, rare (grapples); read by sub_8009EBF0 (spirit-based timing) and sub_800CBC94 */
    u8  f08;                  /* (W1 >> 3) & 7; 0-5, rare; indexes a float table at 0x800E6E38 in sub_8009EBF0 */
    u8  atk_part;             /* (W1 >> 6) & 7; attacker body part used, 0 head, 1 body, 2 arms, 3 legs, 4 whole body; selects the attack stat header[5 + part] and the attacker's condition */
    u8  target_region;        /* (W1 >> 9) & 7; defender region hit, same numbering, 4 = none; selects the defence stat header[10 + region] and can set g_injury_region */
    u8  hit_joint;            /* W2 & 0x1F; strikes: attacker joint that lands the blow (ai_learn_strike_reach measures from it), 0 for grapples */
    u8  hit_joint2;           /* (W2 >> 5) & 0x1F; strikes: second joint of the striking limb */
    u8  hit_joint3;           /* (W2 >> 10) & 0x1F; strikes: third joint of the striking limb */
    u8  aim_joint;            /* W5 & 0x1F; ground attacks and dives (slots 154-182): defender joint 1-4 the attacker aims at (sub_800BC888) */
    u8  hit_reaction;         /* (W3 >> 22) & 7; strikes (slots 118-130, 157): switch value for the receiver's reaction (state_24, sub_800CA9B0) */
    u8  dmg_head;             /* (W3 >> 28) & 0xF; region 0 damage, subtracted from the defender's +0x33C[0] */
    u8  dmg_body;             /* W4 & 0xF; region 1 damage */
    u8  dmg_arms;             /* (W4 >> 4) & 0xF; region 2 damage */
    u8  dmg_legs;             /* (W1 >> 28) & 0xF; region 3 damage */
    u8  dmg_region4;          /* (W1 >> 12) & 0xF; region 4 damage */
    u8  reaction_variant;     /* (W3 >> 25) & 7; 0-2 on a few strikes; state_24 plays animation 3309 + this */
    u8  dive_value;           /* (W3 >> 16) & 0x3F, signed 6-bit (52-62 = -12..-2); slots 154-182; spirit lost by a diver who misses and lands lying (state_5D step 6, sub_800BD484) */
    u8  next_slot;            /* W3 & 0xFF; move slot chained after this one (strike counters 136-144, rear slots 63-68); state_1E and sub_800A7300 start it */
    u8  f18;                  /* (W2 >> 20) & 0x3F; always 0 in World Tour */
    u8  f19;                  /* (W2 >> 15) & 0x1F; 1/3/5/7/13, mostly 1 strikes, 3 grapples; no reader found */
    u8  dive_pct;             /* (W3 >> 8) & 0xFF; slots 166-182 only, 50/75/100/125; scales header[0x1A] in the dive aim code (sub_800BC888) */
    u8  followup_190;         /* (W2 >> 26) & 0x3F; pins, holds and failed moves continue with move slot 190 + this */
    s8  meter_atk;            /* (W1 >> 16) & 0x3F signed; added to the attacker's 0x800F1CB0[slot] (clamped 30..200); always 0 in World Tour */
    s8  meter_def;            /* (W1 >> 22) & 0x3F signed; same for the defender; always 0 in World Tour */
    u8  _pad1E[2];
    u32 flags;                /* (W4 >> 8) & 0xFFFFFF; bit 0 reversible (grapple_check_reversal); 0x3000 not possible in this ring location (state_46); others in moves.md */
};

struct Wrestler {             /* 0x3A0 bytes; g_wrestlers[4] */
    s8   slot;                /* index in g_wrestlers */
    s8   _unk001;
    s16  roster_entry;        /* who this is: group << 8 | (index + 1), the same encoding as g_roster_slots (0x101 Lex Luger, 0x10B Chris Benoit, 0x007 Scott Hall); -1 empty slot */
    s16  move_frames_left;    /* counts down during a move; hit applied at 1, move ends at 0 */
    s16  anim_frame;          /* ? frames into the current animation; tested against anim.window_* and anim.reversal_frame */
    s16  anim_id;             /* current animation = archive file number (>= 2901) */
    s16  state;               /* action state, index into g_state_handlers */
    s16  health;              /* current health; wrestler_add_health clamps it to 0..health_max; move_apply_hit subtracts the damage */
    s16  health_max;          /* recoverable cap of health; each hit also lowers it by damage / 4, never below 128. While SPECIAL, wrestler_health_eff reads this instead of health */
    s16  spirit;              /* 0..100 */
    s16  state_step;          /* step within the current state; reset by wrestler_set_state */
    s16  state_timer;         /* general per-state counter: states zero it on entry and count it up (frames, retries); 20 states use it */
    s16  recover_count;       /* frames left to recover from a pin or a daze: counts down every frame and once more per A/B/L/R press (recover_tick / sub_800A3B40); set by state_32_pin_defender and the dazed state */
    u32  flags_hi;            /* status flags, high word */
    u32  flags_lo;            /* status flags, low word; & 0x1800 blocks spirit changes */
    u16  run_flags;           /* ? bits set and tested by the run / rope-rebound / whip states (0x8, 0x20, 0x40, 0x100) */
    u16  _unk022;
    u32  grapple_ctx;         /* grapple context bits, see enum GrappleCtx; picks which move-set slots apply */
    u32  hit_info;            /* set by strike_land on the receiver: hit kind (1 tap, 2 strong, 4 vs runner, 8/0x10 running, 0x200 weapon), 0x800 from behind, reaction 0x2000 flinch / 0x4000 stagger / 0x8000 knockdown / 0x10000-0x400000 special and apron, height 0x800000 high / 0x1000000 mid / 0x2000000 low */
    u8   dive_flags;          /* ? state_5D bits 0x2/0x4/0x8/0x20 */
    u8   state25_flags;       /* ? state_25/26 bits 0x4/0x8/0x10/0x20 */
    u8   ground_grapple_flags;/* state_33: bit 0 near the head (slots 158/159, else +4 = legs), bit 1 opponent face down, bit 4 forced pick-up (set by state_3B) */
    u8   _unk02F;
    u16  state51_flags;       /* caught-strike record set by block_counter for states 0x51/0x52: 1 far strike, 2 counter roll won, 4/8/0x10/0x20/0x40/0x80/0x100/0x200 = slot 123/124/125/126/128/131/5/6 */
    s16  ring_zone;           /* ? result of ring_zone_of(&pos) */
    s16  facing_quadrant;     /* 0..3, indexes g_facing_quadrant_angles */
    s16  down_count;          /* times knocked down since last standing: state_28 (down) adds 1, state_01_stand / state_70 reset it; sub_8009E6C4 takes 60 per count off the recovery value. Snapshot 7: 1 for the wrestler lying outside */
    u8   pose;                /* pose class of the current animation (copied from anim.pose_class each frame); state_27_recover dispatches on it. 1 = standing, 7-0x1C = lying variants */
    s8   hits_taken;          /* strikes taken in a row, 1-4; cleared when this wrestler lands one and by state_28 / state_23; picks the reaction table row */
    s16  last_move_value;     /* MoveParams.end_value of the last move received (copied by sub_800A7300 / sub_800CBC94 at the end of a move), -1 while pinned (state_32) */
    struct Vec3 pos;          /* y up, ring mat at y = 90 */
    s32  facing;              /* 0x1000 per turn, 0 = facing +Z, positive toward +X */
    struct Vec4 joint0;       /* joint 0 slot: code indexes the joint list from here (joints[] below starts at joint 1); always zero in both RAM snapshots */
    struct Vec4 joints[19];   /* world-space joint positions (x, y, z, 0) */
    u8   _unk18C[0x40];       /* always zero in both RAM snapshots */
    struct Vec4 joints2[19];  /* ? second joint set, nearly equal to joints */
    u8   _unk2FC[0x30];       /* always zero in both RAM snapshots */
    struct Vec3 slide_vel;    /* per-frame x/z slide applied by the state code (wrestler_slide_to, run, rope rebound, whips) */
    s32  turn_step;           /* facing change per frame while a state turns the wrestler toward a target angle */
    u8   region_condition[5]; /* per-body-region condition; wrestler_base_anim switches to injured idle/walk when the region in g_injury_region[slot] is below 26 (reads 50 while flags_lo & 0x1000) */
    s8   partner_idx;         /* wrestler index used for g_cur_partner: the one this wrestler is linked to in a move, pin or hold; credited as the winner of a pinfall / submission / TKO */
    s8   other_idx;           /* ? wrestler index used for g_cur_other */
    s8   opponent_idx;        /* g_opp_index while this wrestler updates */
    s8   teammate_idx;        /* tag partner's wrestler index (snapshot 9: 0<->1 and 2<->3); 0 in singles. wrestler_update copies its flags into teammate_flags_* */
    u8   _unk345[3];
    u32  teammate_flags_hi;   /* copy of g_wrestlers[teammate_idx].flags_hi at the start of this wrestler's update (confirmed in both RAM snapshots) */
    u32  teammate_flags_lo;     /* copy of its flags_lo, taken before UPDATED (0x2000000) is set */
    u16  _unk350;             /* per-state scratch bits, cleared by wrestler_set_state (pin/hold attacker: 1 released, 2 kicked out / escaped, 4 rope break, 8 won / gave up, 0x10 debug, 0x20 ending, 0x200 power-out) */
    s8   linked_idx[2];       /* wrestlers updated right after this one (grapple partners); -1 = none */
    struct WrestlerHeader *header; /* this wrestler's move-set header file (g_moveset_hdr[slot]) */
    struct AnimPlayer anim;   /* animation player, see anim_player_set */
    s8   moveset;             /* move set (wrestler slot) of this wrestler's current or last move */
    s8   _unk371;
    s16  move_slot;           /* 0..221 slot in that move set */
    s16  stat_category;       /* move statistics category set by move_count_stats (58 = none) */
    u8   _unk376[2];
    struct MoveParams move;   /* unpacked parameters of the current move */
    u8   _unk39C[4];
};

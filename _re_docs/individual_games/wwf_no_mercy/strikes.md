# No Mercy: strikes, blocks and counters

Read from the code (2026-11-01 / 02) with RAM checks from Andrew's save states (a caught kick, an R
counter of a punch, an L deflection of a grapple). Names in `symbols/nomercy/`; World Tour's
equivalent is `../wcw_world_tour/strikes.md`. Frames are game frames (30 a second).

## Starting a strike: state 0x22 (`state_22_strike`)

B (or A) from standing goes to 0x22, which asks `input_press_kind` how the button was pressed
(the 20-entry input history: a **tap** is a recent press, a **hold** is the button held through the
last 8 entries, A+B held is its own kind):

- **A alone** becomes the grapple attempt 0x50 (`grapples.md`).
- **A hold** (strong): 0x23 (`state_23_strong_strike`), or 0x27 with a weapon.
- **A tap** picks by place and target:
  - 0xC9 at an opponent up on the cage wall (location 0x400) in front and close;
  - 0x26 with a weapon (`state_26_weapon_swing`);
  - 0xBF from a raised place (location 0x3C0) at an opponent on the floor below;
  - 0x24 at an opponent facing away (its flags80 0x20): own move 205, the strike from behind;
  - 0x2B at an opponent slumped in the corner (its frame_flags 0x01): moves 176 / 177;
  - 0x2C at an opponent with frame_flags 0x08 (set by 0x34): moves 178 / 179;
  - otherwise the **weak strike** itself: move 172 close / 174 far, +1 with a d-pad direction; the hit
    test runs at step 2.
- **Strong strike** (0x23): move 186, 188 with a direction, **190 with A+B held** (the long-press
  lunge); case 1 uses the wrestler's own move from its data. 0x23 is also the counter-strike after an
  R counter (0x64).
- Other strike states: 0x1A own move 207, 0x06 move 208, 0x25 the get-up attack (move 217), 0x28 a
  weapon at a downed opponent (generic moves 104 / 110), 0xB7 across the top of a ladder.

Strike moves come from the wrestler's own move set; their animations from `anim_style`
(`frame_loop.md`). Distance close / far comes from `opp_distance` (+0x2B8).

## Does it hit? (`strike_hit_test`, 0x8012069C)

Returns the target slot (or 100 + object index), -1 for none. Only inside the move's active window
(anim_frame between anim_player +8 and +9), except for a thrown object (kind 24). Candidates come from
`hit_candidates`. The height window runs from the target's lowest joint plus a per-slot offset
(0x8016B778 + 112 × slot); for an object from its y + 126. By strike kind:

- kinds 1, 3, 17-19, 21, 22, 24-27, 29, 30 (and 5-16, 28, 31 with another joint list): the two
  wrestlers' joint 0 within **460**, then each attacking joint of the move's joint list (`test_kind`, move +0x0D, indexes
  the lists at 0x80146844; the weapon tip for kinds 3 / 24) against the target's joints 5 and 1 (list 0x8014689C): distance at most
  the per-slot joint radius (0x8016B728 + 112 × slot) and height inside the window;
- kinds 2 / 4: the current opponent in the same place (location & 0x3C5);
- kinds 20 / 23: any wrestler.

## Landing (`strike_land`, 0x80121330)

- **Hitting an object** (target 100 + object; read 2026-11-02): it is knocked along the line from the
  striker with `object_set_motion` cause 4 or 5 (by strike flags 0x4418), 6 (flags 0x106) or, for a
  weapon swing (flags 0x200), 7 from the heavy swing (state 0x27) or 8: so a ladder topples always, at
  25% or at 50% when someone is on it (`objects_and_weapons.md`).
- **Who can be hit:** a wrestler in a pin or hold (flags80 0xC0) only by strikes with flags 0x200, and none
  with stay_flags 0x05 (staying down). The defender then records the striker as its pair_other and, unless
  they are partners, its opponent, takes the strike flags, drops any weapon (`weapon_release` 0) and gets
  the side it was hit from (`relative_facing`; flags 0x100000 also sets hit_flags 0x4000000).
- Resets the defender's hit_flags and copies the move into it (`hit_move_copy`).
- **A blocking defender** (state 0x4C): if it started blocking **less than 3 frames ago** (its
  count_timer < 3) and `strike_counter_check` accepts, the strike is **countered** (below); otherwise
  the block holds (state 0x4D) unless the strike is unblockable: strike_flags 0x100600 (0x600 a
  weapon), from behind (flags8a 0x02), or 0x40 at location 0x10 (the ladder top).
- Defenders in 0x34 / 0x35 can escape by mashing; 0x34 / 0xC1 add hit flags.
- Stats, DQ checks (`strike_dq_check`: weapons under g_match_rules 0x2000, hits on the referee or the
  guest referee), a hit on a ladder climber (location 0x08) knocks it off (0xB2).
- The **reaction** comes from hit_flags (+0x74), chosen with chances (700, 800, 500, 300) and
  `chance_roll` kind 18: 0x100000-0x1000000 pick one of five reaction kinds, 0x2000000 mirrors it,
  others pick falls. Then `apply_damage` (`damage_and_spirit.md`) and the reaction state: 0x17
  (knocked onto the ropes, 12147-12150), 0x70 (over / through the ropes), 0x2D (knocked down), 0x32
  (leaning on the ropes), 0x35 (knocked back), 0x18 (slumped in the corner)...

**The reaction roll** (read 2026-11-02, end of `strike_land`): when the strike has no special outcome,
the defender's `health_eff` picks a band (≥ 128, ≥ 64, ≥ 32, else; thresholds 0x801468A0) and a table of
two per-mille weights per band is chosen by the defender's condition (running flags80 0x20, off balance
0x33, leaning on the ropes 0x32, otherwise standing) and the strike's weight (strike_flags 0x100202
heavy, 0x04, 0x01; tables 0x801468E8-0x80146980, plus one indexed by hits_taken +0xE3). A
`rand_pool_next` roll walks the two weights to outcome 0 or 1, and **heavy strikes (strike_flags
0x100306) add one**: outcome 0 → hit_flags 0x40000 and a knockdown (0x2D), 1 → 0x10000 knocked back
(0x35), 2 → 0x20000 and a knockdown (0x2D). For example one table gives 70 / 30, 60 / 40, 50 / 50, 30 /
70 down the bands, so a hurt wrestler more often gets the second outcome. Before that: strikes into the
ropes (strike_flags 0x80000) roll `chance_roll` kind 18 for going over (0x70) or being draped (0x17 step
6); finisher mode on the attacker turns a knock-back chance of 800 into 300 / 850 splits (0x04 strikes);
hits_taken (+0xE3) counts up to 4 and decays (`hits_taken_decay`); several strike_flags bits map straight
to hit_flags bits (0x20010 → 0x10020, 0x44000 → 0x02, 0x8000 → 0x40 plus 50% 0x10000 ...).

**Over the ropes** (0x70, `state_70_over_ropes`): from the apron down to the floor (12908-12911 by
hit_flags 0x1000 / 0x400 and position); from the ring **20% straight to the floor** (12143 / 12144) and
**80% onto the apron** (12145 / 12146), location changed to match; in a cage (state_counter 0x01) into
the cage instead (12151 / 12152); then a slide (8.0) with flags8a 0x6000.

## L from standing (`input_l`, read 2026-11-02)

In order: with a held weapon, state 0x8B; at a runner coming at it (within 1823 angle units) **0x1D**;
at an opponent leaning on the ropes (0x32) within **85** in the same place, the pair **0x97 / 0x98**;
at a downed opponent, `running_ground_attack_check` or the cover **0x42 / 0x43** (L pins); both in the
ring and within **120**: an opponent slumped in the corner (frame_flags 0x01, not 0x02) → **0x3C / 0x3D**,
setting it up on the top turnbuckle, or one with frame_flags 0x08 (set while held upside down, 0x34, or knocked down, 0x2D) → **0x40 /
0x41**, the yank down; otherwise the evade **0x6B** in the ring, or 0x09 elsewhere.

## Strikes from the apron (0x1F-0x21, read 2026-11-02)

A strike started on the apron goes through `strike_across_ropes`: **0x1F** into the ring (the d-pad
toward the ring for the ring zone, table 0x8014D220, or an opponent in the ring), **0x20** down at the
floor or a raised place (table 0x8014D218, or the opponent's location & 0x3C4), **0x21** along the apron
at an opponent on the apron. One handler serves all three.

## R from standing (`input_r`, read 2026-11-02)

`input_r` (0x8013E9C4, called `input_strike` before; it tests R held, buttons 0x10) runs in order: nothing at
location 0x400; on the apron state 0x09; holding a weapon 0x8B; at a downed opponent in the same place,
`running_ground_attack_check` → **0x38 / 0x39**; both in the ring and within **120**: an opponent slumped in
the corner (frame_flags 0x01, not 0x02) → **0x3A / 0x3B**, pulling it out of the corner (12066 / 12068), or
one with frame_flags 0x08 → **0x40 / 0x41**; otherwise the block **0x4C**.

## Blocking: states 0x4C / 0x4D

R from standing starts **0x4C** (`state_4C_block`). Each frame it turns toward the opponent (341 /
512) and counts count_timer (+0xB2) up from 0 (capped at 100). It holds at least **5 frames**; after
that, letting go of R (when `match_can_interrupt` allows) plays the release and stands. A strike that
hits the block puts the blocker in **0x4D** (`state_4D_block_hit`) and back. Steps 5-7 of both are
the **ladder** version (12989 / 12990 / 12991, and it ends when the ladder falls, object flags 0x4000).

## The timed R counter (`strike_counter_check`, 0x8012201C)

Called from `strike_land` for a defender that started blocking less than 3 frames before the hit:

- Not when the strike's flags have 0x100000 against the defender's own partner; not on a ladder
  (location 0x08), except that a strike across the ladder top (attacker state 0xB7) gives the ladder
  counter pair **0xBA / 0xBB** (shove / yank).
- The attacker must be in a strike state (7, 8, 0x20, 0x22, 0x23, 0x26, 0x27, 0xBF, 0xCC) in the same
  place (location & 0x3C5).
- The attacker's move number picks a counter bit (stored in the defender's +0x7C): 174 / 175 (the far
  weak strike, the **kick**) → 0x01; 186 → 0x02; 188 → 0x04; 190 (lunge) → 0x08; 192 → 0x10; 194 → 0x20;
  196 → 0x40; 198 → 0x80; 200 → 0x100; 202 → 0x200; 219 → 0x400; 107 → 0x800; 108 → 0x1000; 109 → 0x2000;
  73 → 0x4000.
- Then:
  - weapon swings (moves 107-109, bits 0x3800): **0x66 / 0x67**, the weapon counter;
  - moves 73 / 219 (bits 0x4400): **0x86 / 0x87**, a grab out of the counter (flags78 0x4000000, plus
    0x20000 for 219);
  - the kick (0x01): **0x69 / 0x6A, the kick catch**;
  - anything else: **0x64 / 0x65, the R counter** (the counterer strikes back through 0x23 while the
    attacker sits off balance in 0x33). **The counter move mirrors the strike** (read 2026-11-02 in
    `state_64_counter_strike`): bits 0x02-0x200 give the counterer's move **187, 189, 191 ... 203**, one
    above each countered move (186 → 187, 188 → 189 ... 202 → 203), from the attacker's move set; the kick
    bit (0x01) gives 183 or 184 at random; with no bit set, move 180 (181 when `finisher_full_spirit_roll` is non-zero)
    from its own set;
  - an attacker in 0xCC (on the cage): 0xCD / 0xCE with sound 278.

When the defender is attacked from behind (its flags8a 0x02) the check gives the dodge 0x68 instead
(or 0x90 with an unblockable strike), for attackers in states 7-8, 0x20, 0x22-0x23, 0x26-0x27, 0xBF,
0xCC in the same place.

## The kick catch: 0x69 / 0x6A

The blocker holds the leg (0x69 `state_69_catch_holder`), the kicker hops (0x6A). In step 2, before
count_timer runs out, **A starts move 183, B move 184** (state_flags 0x10 gives move 185): the
kick-catch follow-ups. With no input the caught wrestler reverses: the two swap 0x69 / 0x6A (15454 /
15455). The CPU waits a time from its style tables and presses A or B 50 / 50 (AI mode 69).

## L against grapples and runners

- L from standing starts the evade **0x6B** (11877 / 11878). A grapple attempt that reaches a defender
  in 0x6B with count_timer 4 or less (a **5-frame window**) in the same place is **deflected**: 0x6C /
  0x6D, and the grappler is off balance (0x33, about 50 VIs). Against a running grapple the defender
  counters instead: 0x5B with move 90 against 0x5C (`grapples.md`).
- L at an opponent running at you (and roughly facing you) starts 0x1D; L at an opponent leaning on
  the ropes (0x32) close by starts 0x97 / 0x98.

## Dives: state 0x79 (`state_79_dive`, first pass 2026-11-02)

From the turnbuckle perch (0x0A), the apron (0x73-0x77) or the ladder (0xB6):

- `dive_aim` picks the landing point against the arena geometry (`dive_aim_target`, segment tests); the
  diver turns toward it (136 units a frame) and plays the move's parts (`anim_style`; 12091 for some).
- In flight `aerial_step` / `aerial_height` move it; `strike_hit_test` runs with the **dive kinds 13, 14
  and 15**; a hit goes through `dive_land_hit`. The target can answer through `dive_counter` (the dodge
  roll 0x68, or catching the flyer 0x88 / 0x89 from a grapple attempt), and the CPU has modes 47 (block a
  dive) and 56 (step away from the corner).
- On a hit the damage is the move's × **1.25** (constant 0x80151CB0); a target lying down goes to 0x63;
  some dives land straight into a **pin pair 0x44 / 0x45** (a diving cover); finishers call
  `finisher_effects` (breaking a table under the target).
- On a miss the diver takes generic move 26 itself (also × 1.25), and goes down (0x31) or settles.
- Objects can be knocked by the dive (`object_set_motion`).

## The reaction weight tables (ROM data, read 2026-11-02)

The end of `strike_land`, when nothing special happened to the strike. Health bands from `health_eff`
(0x801468A0: >= 128 band 0, >= 64 band 1, >= 32 band 2, else 3). Each table entry is a pair of per-mille
weights for outcome 0 / 1 (a `rand_pool_next` roll walks them); the strike's own flags then add one to
the outcome for heavy strikes (strike_flags 0x100306). Outcome 0: hit_flags 0x40000 and a knockdown
(0x2D); 1: hit_flags 0x10000 and knocked back (0x35); 2: hit_flags 0x20000 and a knockdown (0x2D); 3:
a plain knockdown (0x2D). Which table (strike_flags: 0x100202 "heavy", 0x04, 0x01 "light", else):

| Defender | heavy, bands 0-3 | 0x04 | light, bands 0-3 | other |
|----------|------------------|------|------------------|-------|
| standing | 900/100, 700/300, 500/500, 350/650 (0x801468E8) | 300/700 | by hits_taken (+0xE3, 0-4; 0x801468A8 + 16 x hits): 0: 900/100, 800/200, 700/300, 550/450; 1: 800/200, 700/300, 550/450, 450/550; 2: 700/300, 550/450, 450/550, 400/600; 3: 550/450, 450/550, 400/600, 200/800; 4: the heavy row | 900/100 |
| running (flags80 0x20) | 600/400, 500/500, 350/650, 250/750 (0x80146910) | 300/700 | 700/300, 600/400, 500/500, 300/700 (0x80146900) | 200/800 |
| off balance (0x33) | 400/600, 300/700, 200/800, 100/900 (0x80146938) | 0/1000 | 850/150, 700/300, 500/500, 400/600 (0x80146928) | 400/600 |
| leaning on the ropes (0x32) | 300/700, 200/800, 100/900, 50/950 (0x80146968) | 0/1000 | three outcomes, 6 bytes a band (0x80146950): 850/150/0, 600/300/100, 300/500/200, 200/500/300 | 200/800 |
| any, strike_flags 0x40 | 750/250, 600/400, 350/650, 250/750 (0x80146980) | | | |

Before the tables: a hit move whose copied record byte +0x21 (Wrestler +0x331) is 4 always gives outcome 0
(hit_flags 0x40000); a strike with strike_flags 0x1000000 skips them: outside finisher mode 80% outcome 0
else knocked back, in finisher mode 30% outcome 0, else 85% knocked back, else outcome 2. So a hurt
wrestler (low band) and one that has taken several hits in a row is knocked down by light strikes more
often, and a running or staggered wrestler almost always goes down to a heavy one.

## Hit reactions (names from Andrew's viewer tags)

0x17 knocked onto the ropes and dazed over them; 0x32 leaning on the ropes (12557-12559); 0x33 off
balance, shaking the head (12562 / 12563); 0x2D knocked down (12565 flat, 12579 onto the butt,
slouched); 0x35 knocked back into something; 0x18 slumped in the corner, then 12060 stumbling out; 0x16
falling as if knocked out (from a run); 0x2E failing to get up (12611-12616, the TKO check).

## Open

- Strike frame windows per move: record bytes 3 / 4 of ROM file 703 (`animation_data.md`); dump them per
  strike move once the converter reads that file.
- Decompiled and trace-checked 2026-11-02 (`src/nomercy/nm_strike.cpp`): the hit test, damage and
  spirit, the reactions 0x22 / 0x23 / 0x2D / 0x33 / 0x35 / 0x4C, the slide and the ropes. Not reached
  by a trace yet: strike_hit_test's thrown-object kind (24) and the weapon tip, chance_roll kind 6,
  strikes across the ropes at a wrestler on the apron, the running ground attack on a lying opponent.

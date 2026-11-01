# World Tour: in and out of the ring, weapons, the top rope

First pass (2026-10-30), read from the code: `input_cup_ropes`, `input_crowd_weapon`
(`0x800D84A4`), `state_6F_crowd_weapon`, `state_09_turnbuckle`, the dive picker. Not checked
against a live match. Walking, running, the apron line and the dives off a run are in
`movement.md`; the ring-out count in `match_state.md` / `match_end.md`.

## Climbing in and out (C-up toward the ropes)

`input_cup_ropes` (C-up with the d-pad pointing at the ropes):

| Where | Result |
|-------|--------|
| in the ring | tag if possible (`tag_team.md`), else **state 0x54**: through the ropes to the apron |
| on the apron, toward the ring | tag, or `apron_enter_try` (climb in, 0x55, when allowed) |
| on the apron, away from the ring | 0x54: drop to the floor, **unless rule 0x04** |
| on the floor, toward the ring | **0x55**: climb onto the apron / in |

The direction tables are `0x800E7078` (toward the ring) and `0x800E7070` (away), per ring zone.
Rule 0x04 (ring-out count off) also stops wrestlers stepping off the apron and makes a wrestler
on the turnbuckle climb down when the opponent is outside, so 0x04 looks like the "no leaving
the ring" setting rather than just "count off".

### Going through the ropes (states 0x54 / 0x55)

Both are two-step: the wrestler stops on the apron, and **keeps going only while C-up is still
held** at the end of the first part.

- **Out (0x54):** from the ring onto the apron with 3021, or 3024 for wrestlers with header
  traits 0x1400 while `health_eff` ≥ 128 (a quicker exit). Then, with C-up still held (and not
  rule 0x04), on down to the floor; otherwise it stays on the apron. An eliminated wrestler
  always goes all the way out. From the apron straight to the floor: 3345.
- **In (0x55):** from the floor onto the apron with 3027, then into the ring with C-up still
  held (and allowed to act, `wrestler_may_act`). From the apron into the ring the entry depends
  on the wrestler: with `health_eff` ≥ 128, header trait 0x400 → 3337, 0x800 → 3338,
  0x1000 → 3339, 0x2000 → 3343 (the agile entries over the top rope); otherwise the plain
  climb 3336.

So header traits 0x400–0x2000 are the ring-entry styles, and a hurt wrestler (below 128)
always uses the plain ones.

## Weapons from the crowd (state 0x6F)

Not a tag state, as the earlier name said: `input_crowd_weapon` fires on the **floor** with
C-up pressed and the d-pad toward the barrier (table `0x800E708E`), when not already holding
one.

- The wrestler reaches out (3244). `chance_roll(w, 0, 10)` decides whether the crowd hands one
  over; then a free weapon object is taken (`sub_800D8980`, objects of 32 bytes at
  `0x800F1C20`, the wrestler's in `0x800F1CA0[slot]`), its type is `rand / 250` (0–3), types
  0–1 set `WEAPON_ALT` (`flags_hi` 0x4000), and `HOLDING_WEAPON` (0x2000) is set.
- 3261/3262 take it (by type), 3263 is the empty-handed version.
- With a weapon: idle/walk 3245/3246 or 3253/3254, B swings it (states 0x21/0x22, which ignore
  blocks, `strikes.md`), and `input_strike` / `state_1D` use the weapon branches.

- **Doing anything else drops it**: `weapon_drop` (`0x800D8094`) runs at the start of
  grapples, runs, R, L, C-up, the turnbuckle climb and the apron moves. The object is detached
  at the hand's position (`0x800EBD40`, per object) and falls each frame (`weapons_tick`,
  `0x800D7EEC`) until it is below the floor, then the slot is freed. **A dropped weapon cannot be
  picked up again**; you ask the crowd for another.
- There are 4 weapon slots and a new one is only handed out while more than (4 − wrestlers in
  the match) are free: **one weapon at a time in singles, up to two with three wrestlers, up to
  three in tag**.

## The turnbuckle (state 0x09)

Entered by running into a corner with C-down held (`movement.md`), or by a partner coming off
the apron.

**Climbing** (steps 0–3):

- **From the apron:** anyone climbs (3356 / 3357, or 3358 / 3359 when running, by which side of
  the corner).
- **From inside the ring, only wrestlers with header trait 0x4000, or anyone while SPECIAL,
  climb to the top.** The dive is chosen *before* climbing (`dive_pick`), and the climb
  animation is 3012 + a per-wrestler, per-dive byte (header bytes 17–22 for slots 166–171, 2 for
  the carried dives). A lying opponent's `recover_count` is extended by the climb's length, so
  it stays down while you climb.
- **Everyone else just poses on the turnbuckle** (3016) and steps down: generic move 38, +5
  spirit, no dive.
- R at any time during the climb climbs back down.

On top (step 4) it sets `flags_hi` 0x40410 and:

- **holding A, B or C-down** keeps it waiting on top for up to 150 frames (5 s);
- **releasing** them (or the 5 s running out) **dives**: `dive_pick` (`0x800BC63C`, read from
  the disassembly) picks the move slot from the target:

  | Target | Slot | State |
  |--------|------|-------|
  | in the ring, standing | 166 | 0x5D |
  | in the ring, lying face down, or face up for most wrestlers | 167 | 0x5D |
  | in the ring, lying face up and **across** the diver's line (50°–110° off), wrestlers with header trait 0x2000000 | 168, or **169 with SPECIAL** (a finisher from the top) | 0x5F |
  | on the apron, or standing on the floor | 170 | 0x5E |
  | lying on the floor | 171 | 0x5E |
  | being carried (state 0x67, early), in the ring / on the floor | 181 / 182 | 0x60 / 0x61 |
  | anything else | no dive (the wrestler stays) | |

- **stick + a button** within the 5 s → slot 149 played on top, then back to waiting on top
  (its hit is applied with the opponent as target; probably a top-rope taunt or pose);
- **C-up** → tag or come in (`tag_try` / `apron_enter_try`);
- **R** → climb down (3017/3018, or 3360/3361 from the apron side);
- it is forced down when the match is decided, when an illegal tag partner may not act, or with
  rule 0x04 and the opponent outside.

### The dive itself (`state_5D`, states 0x5D–0x61)

1. On top: the heading is aimed at the target (`sub_800BC888`, using the move's `aim_joint` and
   `dive_pct`; turned round 180° for moves with `flags & 0x10`), the wrestler turns at 136 a
   frame (3019), then plays the dive.
2. In the air the hit test depends on the dive: lying targets mode 12, carried targets (0x60 /
   0x61) mode 13, standing targets mode 11 (`strikes.md`).
   - A **standing** target that blocks at the right moment (the same 3-frame window as strike
     counters, or an apron chance roll 15) **catches the dive** (`dive_counter`, `0x800BD684`).
   - A lying target that is **in a pin or hold** has it broken up (`_unk350 |= 0x10`), the
     dive's version of a save.
   - A lying target otherwise gets 30 frames added to its time down.
3. Landing:
   - **missed** a lying target → the crash part (move part 3);
   - hit → the target goes to 0x50 (knocked down by a dive), credited to the diver;
   - **0x5F (the across-the-body special dive) hit on the current opponent → straight into a
     pin** (states 0x31 / 0x32);
   - a target that waited for the dive (`wait_for_diver`) is pinned to the mat in state 0x28.

While someone dives at a downed wrestler, that wrestler does not get up (`wait_for_diver`,
`knockdown.md`).

## Open questions

- ~~`sub_800BC888` (dive aiming) and what `dive_value` does.~~ Read 2026-11-01:
  - `dive_aim` stores the flight time per slot (`0x800F1980` = the animation window end − start
    + 1, at least 1) and takes the heading to the target's aim joint. For each dive slot the
    heading is clamped toward a table direction: slots 166 / 181 (and 167–169, which also set
    `dive_flags` 2) to within 455 (40°) of the corner direction at `0x800E7060`, slots 170 / 182
    (and 171) to within 1479 (130°) of `0x800E7050`; 176–180 aim along the side (`0x800E7030` /
    `0x800E7040`, reach 50 or 100 when off-line), 177 limits the turn to 512.
  - The dive **connects** (`dive_flags` bit 1) when `dive_pct` × header +0x1A (dive reach, 10 or
    12, high flyers 12) × constants is at least the distance; the horizontal speed is the
    distance over the flight time.
  - `dive_value` is a signed 6-bit field (sign-extended by `move_record_unpack`), so 52–62 are
    **−12 to −2**: the spirit a diver loses when the dive misses and they land lying (state
    0x5D step 6, after hitting themselves with generic move 36).
- What the 4 weapon types are. On a drop, `sub_80091DA8` rebuilds the dropped object from one of
  three model lists filled at run time (`0x800EBBF0`, `0x800EBC48`, `0x800EBCC8`), so the
  models are only visible in RAM.
- State 0x08 (R on the apron: animation 3364, then standing) is not identified (`QUESTIONS.md`).

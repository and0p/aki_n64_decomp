# No Mercy: standing, walking, running and where a wrestler is

Read from the code (2026-11-02). Names in `symbols/nomercy/`; World Tour's equivalent is
`../wcw_world_tour/movement.md`. Frames are game frames (30 a second).

## Standing: state 0x01 (`state_01_stand`, 0x800E5EB8)

Every frame, in this order (the first that acts ends the frame):

1. **The match is over** (global 0x8016B599 bit 0x40, set by `match_tick` as it enters the
   post-match phase 4; `frame_loop.md`): a wrestler with end_flags 0x04 on the apron goes to 0x6F
   (climbing / place change, `zone_info` from the zone table 0x8014D3B0) or 0x1B (apron wait);
   anyone else turns toward the opponent (`wrestler_turn_toward`, 341 / 512) and walks
   (`walk_step`) if it is somewhere (location 0x3C7), else drops down through 0xC7 (`state_C7_drop_down`;
   the cage climb and hang do the same once the match is over).
2. flags80 bit 0x02 is set while end_flags 0x01 (it has lost a fall); `opponent_switch_nearest` may take over.
3. **Lost a fall** (end_flags 0x01): in the ring drop any weapon and go to **0x96** (leaving); on the
   apron go to **0x6E** (dropping off the apron).
4. **Entered under elimination rules** (flags_f1 0x10, set at match start under g_match_type 0x4000
   and by run-ins): clear it; if every other wrestler is on the floor with end_flags 0x01, without
   flags_f1 0x01 and not running in (state 0xA3), it **wins**: end_flags 0x100 and the bell.
5. `stand_flags_update` non-zero: state **0x92**, a tag partner who may not act walking back to its
   corner and out to the apron (`wrestler_may_act`: in tag matches without rule 0x40 the partner may
   only act while a legal wrestler is outside).
6. A spare / run-in slot (flags_e9 & 3) that `illegal_check` does not allow: in the ring 0xA4
   (leaving), on the apron 0x6E.
7. flags_f1 0x01: the input is dropped (buttons, presses and the latch cleared).
8. **Battle royal rule** (g_match_rules 0x400): standing on the floor (location 0x04) without end_flags
   0x01 eliminates it: flags80 0x01, end_flags 0x10, input dropped.
9. Clears down_count (+0xCE), +0xB8, +0x10 = -1, +0xBC = -1; turns toward the opponent when in a
   place (location & 0x3E5); `health_regen` and `spirit_update` op 1 (the 120-frame drift);
   `illegal_man_check` (tag rules).
10. `walk_step(ctx, buttons, 2)` moves it with the d-pad and stores the d-pad bits in +0x98.
11. `input_apron_dpad` (on the apron, the d-pad toward the ring side).
12. **Only with a d-pad direction held**: `input_climb_turnbuckle`, `input_apron_moves`, `input_c_up`
    (climb / exit / table / tag by zone flags), `input_weapon_grab` (C-left at the steps or crowd),
    `input_weapon_grab_zone`, `cage_climb_check`.
13. Always: `input_leapfrog` (C-down at a runner), `input_r` (R held: the block and its special cases, `strikes.md`; this was `input_strike`, but it tests R, not B), `input_a`,
    `input_cup_opponent` (C-up at the opponent), `input_l` (L: evade, counters), `input_c_left_held`
    (C-left, guest referee only), `input_tag` (C-up), `input_ladder_climb` (C-up at a ladder),
    `input_stick` (A).

Corrections from the decompilation (2026-11-02): step 10 stores walk_step's result in +0x98, and that is
**the direction bits of the walls keep_in_bounds pushed the wrestler off** (wall +0x26 & 0xF00), not the
d-pad: so the place checks of step 12 run only against a wall. In step 13, C-down is tested *held*
(input_leapfrog: the run, or the leapfrog over a downed wrestler ahead), B, L and C-left held, C-up
pressed; `input_a` is A held (the grapple attempt 0x50) and the last check,
`input_stick`, is the control stick (the taunt).

So a d-pad direction is needed for the place actions (climbing, apron moves, weapons, the cage), and
the button checks run in a fixed priority: C-down, B, taunts, C-up, L, C-left, tag, ladder, A.

## Walking (`walk_step`, 0x80131E08)

- **Speeds** come from `walk_speed` (0x80131C40): the speed class (attrs +0, 1-3) lowered by one for
  each threshold the legs' condition (region 3) is below (class 1: [10]; classes 2 and 3: [30, 10]);
  speeds by class 0-3 (0x8014DBB0, {fast, normal}): {4.5, 3.4}, {5.25, 3.7}, {6.0, 4.0}, {6.75, 4.3}.
  Flags (re-read 2026-11-02): **0x01 picks the normal column**, otherwise the fast one, which the other
  flags can override: 0x08 doubles both, 0x10 / 0x20 → 1.5, on the apron 0x02 → 3.0 and 0x08 → 12.0, 0x40
  → 8.0 (the overrides only matter without 0x01).
- `walk_step` reads the two speeds walk_speed(w, 2) and walk_speed(w, 3). The d-pad moves in
  **world axes** (up +z, down -z, right +x, left -x). On each axis, moving **toward** the opponent uses
  the first, the **fast** column (4.5-6.75), and moving away the second, the normal column (3.4-4.3):
  **a wrestler walks faster toward its opponent than away**. On the apron (flag 0x02) the toward speed
  is 3.0. A diagonal (from `dpad_dir8`) is scaled by 0.7071; the
  caller's flag 4 scales everything by 0.125.
- **On the apron** (location 0x02): the d-pad moves along the apron at the first speed, `side_input_check`
  picks the walk animation (11794) or the apron idle (11745), and `wrestler_slide` (4.0) keeps it on
  the apron line. The corner is passed through state **0x02** (`state_02_apron_corner_walk`, 11795 /
  11796): sliding round the outer corner while holding the ropes.
- With flags8a 0x20 and the opponent leaning on the ropes (state 0x32) right in front (within 256
  angle units), the step is halved and both wrestlers move together.
- When the wrestler stands on the turnbuckle / cage (location 0x400) `walk_move` runs instead
  (code 2026-11-02): its speeds come from `walk_vector` (pairs at 0x801451D0, {normal, finisher mode}),
  forward lowered by (4 - spirit / 20) steps outside finisher mode and the whole vector scaled by the
  legs' condition; animations 12807 + kind (12814 + in finisher mode) or 12818 + spirit level forward;
  idle plays 12804 after the countdown (spirit -2), an empty meter sends it to 0xC6 (12861).

## Climbing the turnbuckle (`input_climb_turnbuckle`)

From standing with a d-pad direction held (read 2026-11-02): in the ring, the nearest corner
(`nearest_corner`, kept in +0xCC) when the d-pad points at it (0x8014D228) and `corner_free` agrees:

- **C-down**: `climb_check` gives the move and the wrestler climbs into **0x0A**, perched on the
  turnbuckle (12092); from there the aerial moves (0x73 / 0x74, moves 269 / 270) and dives (0x79).
- A with trait attrs +0x38 0x4000 at an opponent down in the ring: 0x74 directly.
- in_byte set: 0x1B, or 0x1C when holding a weapon.
- **On the perch (0x0A)** (first pass): it drops any weapon; the corner taunt is move 233 and runs
  `spirit_update` op 5, so a taunt up there turns SPECIAL into finisher mode; R and match_can_interrupt
  manage climbing down; the tag checks (`tag_try_partner`, `illegal_man_leave`) also run there; the
  attack is the move `climb_check` picked (a dive from the top, through `move_event`, objects spawned
  for some moves).
- From the apron: C-down with the d-pad toward the ring (0x8014DF6C by ring zone) climbs the corner
  beside it (one corner back when `side_input_check` says so) into 0x0A.

## Running

`run_start` (C-down) puts the wrestler into **0x03** (`state_03_run`); `run_step` moves it along
`dir`, `run_inputs` reads the buttons and `run_collide` / `run_target_downed` handle what it meets.
Rope rebounds go through **0x04** (`state_04_run_rebound`).

0x03's steps (read 2026-11-02): it sets flags80 0x20 (running) and runs `illegal_man_check` every
frame. **Step 0:** `run_start` picks the run direction (per slot at 0x801590D0); with none it stands (on
the apron with the apron idle 11745); a wrestler bounced off the ropes (flags86 0x1000) goes straight to
0x04 at step 5. Otherwise the start-up animation (`anim_pick` 4) plays and the facing turns by
(direction − facing) / frames each frame, so it ends facing the run (+0x298 holds the turn per
frame). **Step 1 on:** once the start-up ends the facing snaps to the direction; on the apron the run
follows the apron line (`anim_pick` 5, keep_in_bounds); otherwise `run_step` moves it every frame and
`run_exit_check` handles leaving the ring (C-up / C-down at an exit segment -> 0x9A / 0x9B, see
`symbols/nomercy/bulk.toml`) and the cage (C-right at the wall -> 0xC8). `stand_flags_update` can stop it.

**Run speed** (read 2026-11-02): `run_start` sets the step (`step_vec`, +0x288) from the run direction
at `walk_speed(w, 8)`, the fast column doubled: **9.0 / 10.5 / 12.0 / 13.5 a frame** for speed classes
0-3, one class lower per leg-condition threshold. `run_step` moves the wrestler by it every frame, then
`run_collide` and `keep_in_bounds_self`; meeting the ropes head on (the step against the bound's normal
past about 92°, dot < −0.035) rebounds through 0x04, within 55 of a corner post (0x80146160) it is 0x15,
and hitting some other bound (a wall or the barricade; the flag test is not pinned) takes generic move 92 and goes 50 / 50 to
0x35 (knocked back) or 0x2D (down); over the ropes 0x70; the cage wall 0xC1.

**Buttons while running** (`run_inputs`, 0x80110410, read 2026-11-02; in this order): a whipped runner
(flags86 0x20) uses `whip_run_inputs` instead. On the apron, A at an opponent on the floor → 0x76 (the
apron dive). In the ring, A near a target → 0x71; `run_corner_stop` → 0x78; B at a downed opponent →
`running_ground_attack_check` / `run_attack_input` (0x2A when it faces the target within 512 angle
units). Then, somewhere it can act (location & 0x3C5), by the **held** buttons: **R → 0x06** (the
wrestler's own move 0 from the run), **L → step 4** of the run, **B → 0x07** (the running strike, the
shoulder block; **0x08**, generic move 109, with a weapon; in a cage match at an opponent up on the cage
wall **0xCB**), **A just released with attrs +0x38 0x1800000 → 0x50 at step 8** (the running grapple
attempt). (**0x06** is the wrestler's own running move 208; **0x08** the weapon version of the running strike.)
A whipped runner (flags86 0xA0) reaching a corner (`run_step`, `nearest_corner`) goes to 0x19 (flags86
0x400), **0x1A** (flags86 0x800: own move 207 struck out of the corner) or, in a free corner, the slump 0x18; running into
the corner it is heading for with the stick pushed gives **0x1C**, the corner taunt (own move 232; it
runs the taunt rule, spirit_update op 5, and holds a downed opponent down while it plays), and C-down there climbs
(0x0A). A newly pressed d-pad direction sends the run to step 3: the turn (`anim_pick` 6, a slide, B still
checked for a running attack). **L, step 4**, is **a side-step dodge out of the run** (11850, sliding with
`wrestler_slide`); only when a vulnerable opponent is within 120 and roughly placed (683 / 1308 angle
units) does it go on to **11851, spinning round behind the opponent** and ending facing its back. So the
two halves are split because the second needs a target (animations named by Andrew).

**The rope rebound (0x04, read 2026-11-02; animations named by Andrew):** the rebound animation (from the
move set) plays with `rope_rebound_slide` moving the wrestler off the ropes and `run_collide_check`
running. In its **first 3 frames**, outside finisher mode, pressing A, B, L or R rolls `chance_roll` kind 3
(by spirit). On success with B, L or R the runner does **11955, a very fast turn off the ropes**; with A it
marks flags86 0x200 and, at the end of the rebound, plays **11956, grabbing the ropes to stop the whip**
(as in the earlier games), ending through `move_event`. Otherwise at the end it relaunches at
`walk_speed(w, 8)` along the new direction, turning over up to **7 frames** (a third of the difference a
frame), and goes back to the run (0x03), or stands (0x01) when the run was a single bounce.

What the run can turn into:

| From the run | State |
|--------------|-------|
| A near the opponent | the running grapple 0xA0 / 0xA1 (`grapple_start_pair`, flags78 0x40), moves 108 / 130 / 145 |
| A at a target within 390 units | 0x71 (a running move toward a target) |
| A at a run's end, `run_corner_stop` | 0x78 (`state_78_running_move`, move 278) |
| A running along the apron at an opponent on the floor (trait attrs +0x38 0x10000) | 0x76 (a dive off the apron) |
| B at a downed opponent in the same place | `run_attack_input`: a running attack on the downed opponent |
| into a corner or wall | 0x15 (`state_15_run_into_corner`, 12003 / 12005) |
| a quick stop and turn | 0x19 (12055) |
| over a lying wrestler | 0x1E (a hop over, 11978) |
| hit hard | 0x16 (falling as if knocked out, 12625) |
| shoulder first | 0x07 (`state_07_running_shoulder_block`, 15256; RAM) |
| into the cage wall (location 0x400) | 0xC1 |

**Running collisions** (`run_collide`, 0x801112F0): two runners both bounce (0x05), or both fall
(0x2D) from the apron; a blocker facing the runner (0x4C / 0x4D) bounces it (runner 0x05, blocker
0x4D step 2). With a charged run (flags8a 0x20): a wrestler slumped in the corner (0x18) is knocked
down (0x2D) and the runner bounces; one with frame_flags 0x10 goes over the ropes (0x70); one on a
ladder is knocked off (0xB2) while the runner is knocked down 10% (0x2D) or back 90% (0x35).
`run_target_downed` 1 is a dodge (0x9F). From the apron into a standing wrestler both go down.
Objects are pushed (`object_set_motion`); a charged run into a ladder tips it.

**Responses to a runner:** C-down facing a runner within 512 starts the leapfrog (`input_leapfrog`),
L starts 0x1D (`state_1D_l_vs_runner`), a blocker stops it, and the CPU has its own modes for it
(`ai.md`). The whip (0x0B / 0x0C) is a run the defender did not choose; its reversal is in
`grapples.md`.

## Where a wrestler is: location (+0x82)

| Bits | Meaning |
|------|---------|
| 0x01 / 0x02 / 0x04 | ring / apron / floor |
| 0x08 | on a ladder (0xAA sets, 0xAB clears) |
| 0x10 | at the top of a ladder (0xAF / 0xB0) |
| 0x20 | up on top of the raised place (plus its place bit) |
| 0x40 / 0x80 / 0x100 / 0x200 | at one of four raised places; 0x40, 0x100 and 0x200 are the three breakable tables (`g_tables_intact` bits 0x04 / 0x08 / 0x10), 0x80 is not breakable |
| 0x400 | up on the cage wall (the cage family 0xC2-0xCF) |
| 0x800 / 0x1000 | per frame: cleared by `wrestler_update`, set again by the turnbuckle perch 0x0A and the apron partner 0x95 (0x800) and the whip, shove, cheap-shot and chain states (0x1000); read by collisions and landings |

Animation end places (`anim_end_location`) move a wrestler between them: 1 ring, 2 floor, 3 apron,
6 0x400, 7 on top of the current raised place (keeps the place bits, adds 0x20). Bits 0xF800 survive.
`state_6F_climb_onto` picks the raised place from flags84 bits 0x01 / 0x02 / 0x10 / 0x20 and needs the
table's intact bit; `finisher_effects` breaks a table when a finisher lands on it.

## The zone record (`zone_info`, +0x348)

The record for the wrestler's ring zone (0x8014D3B0[ring_zone], set by standing; read 2026-11-02 by
scanning its users): +0x00 / +0x04 / +0x06 / +0x0A positions the CPU walks to for a weapon (AI mode 39);
**+0x24** an s16 used as a direction or place id (`area_change`, `input_c_up`, `lockup_place_move`, the
shove, the climbs); **+0x26** the d-pad directions that do something here (the d-pad bits 0x0100-0x0800,
plus 0x02-0x20 and 0x1000 / 0x2000: tested by `input_c_up`, `input_apron_moves`, the weapon grabs, the
cage checks and `lockup_place_move`); **+0x28** what the zone offers (bits 0x01-0x400: tested by the same
input functions and `run_start`: the ring steps, the tables, the crowd, the cage, the apron sides). The
exact name of each bit needs the per-zone data dumped against the arena (low priority).

## Staying inside (`keep_in_bounds`, 0x8012CB8C)

The wrestler's radius is half its joint spread, or 30 on raised places (location 0x340) and in state
0x49 step 1. The collision set is the state's override (+0x350, cleared on every state change) or by
location: ring 0x80146BF0; apron by ring zone (0x8014D390); floor by zone and area (0x8014D290 + 16 ×
area, backstage areas from g_arena 10); 0x40 0x80147E20; 0x80 0x8014BC8C; 0x100 0x8014B704; the cage
wall (0x400) by zone (0x8014D3A0); otherwise 0x8014C500. Each set is a list of triangle lists tested
as segments against triangles (`run_step_corner`); the nearest hit pushes the wrestler back.
`keep_in_bounds_pair` does the same for two wrestlers in a paired move.

**The collision data** (read 2026-11-02; static data in the match overlay, so a converter can take it
from the ROM). A set is a 0-ended list of pointers to **groups**; a group is a (first, last) pair of
consecutive **walls**; walls are 48 bytes: four s16 corner points (x, y, z; 24 bytes), a normal (3 floats,
zero in the ROM, filled in at run time), +0x24 a u16 wall id, +0x26 two type bytes, +0x28 flags (0x0094
on the ropes), and +0x2C the next-wall link (filled at run time). `run_step_corner` tests the wrestler as
a sphere: the plane distance minus the radius, then whether the pushed point lies inside the quad
(`quad_contains_point`, radius × 0.98) or near its first two corners; the nearest hit pushes it back by the
overlap.

The ring's walls (0x80146A20-0x80146BCF): **1-4 the ropes**, x / z = ±230 from **y = 90 (the mat) to
220 (the top rope)**; 5-8 full-height planes at ±230 (y 0-1000, ±530 long) used by other groups; **9 the
mat**, the y = 90 square. The ring set (0x80146BF0) is the rope group. The other sets (all decoded 2026-11-02;
`nomercy_models.py collision` prints every wall):

| Set | Where it is | Extent (x / y / z) |
|-----|-------------|--------------------|
| apron, by ring zone (0x8014D390) | 4 walls along each apron, between the ropes and the outer edge | e.g. zone 0: x ±208, y 90-220, z 235-295 |
| cage wall, by zone (0x8014D3A0) | 4 walls per side, up on the cage | x or z ±225, y 270-465 |
| raised 0x40 (0x80147E20) | the announce table | x ±113, z −690 to −530 |
| raised 0x80 (0x8014BC8C) | the unbreakable raised place | x −395 to −125, z 435-1345 |
| raised 0x100 (0x8014B704), default (0x8014C500) | 4 walls each | small boxes |
| floor, by area and zone (0x8014D290 + 16 × area) | the ringside floor (area 0: 3-12 walls per side, the barricades about x ±530, z −690 to 530) and the 9 backstage areas (3-17 walls each) | y 0-100 ringside, 0-1000 backstage |
| object surfaces (0x80147BB4, 8 sets by `arena_area_lookup`) | what thrown objects land on: the floor (y 0, x ±530, z −690 to 800), the ring block (x / z ±280, up to 220) and the barricades by side; ladders use a variant with the ring block's sides only up to the mat (90) | |


**Pushing apart** (`wrestler_slide` -> `wrestler_move_step`, every walk frame): each wrestler is an ellipse
(33 x 33 standing; lying and cage bodies come from lying_body_axis) round point_120; everyone within the
two radii (+20 for a downed one) pushes it straight out of the overlap, the sum capped at 7.5 a frame;
ladders standing on the floor count as 60 x 60 bodies. The walk's result is kept in +0x68 (the slot it
touched). **The walls** (`keep_in_bounds`): a sphere of radius 30 round point_120 (10 above the feet),
the nearest wall then the nearest other one, each pushing out by its overlap; the wall the wrestler faces
more squarely becomes zone_info (+0x348), the other +0x34C.

## Open

- Nothing major; the run's frame-by-frame timings come from the animations' file 703 records.

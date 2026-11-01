# World Tour: walking, running and collisions

Read from the code (2026-10-30), not yet checked against a live run. Function names are in
`symbols/wcw/movement.toml`; tables are read from the US ROM.

## Directions

The d-pad gives one of 8 fixed world directions (`dpad_dir8`, `0x800A4010`), not directions
relative to the camera or the wrestler:

| D-pad | Index | Heading (`g_dir8_angles`, `0x800E709C`) |
|-------|-------|------------------------------------------|
| up | 1 | 0 (+Z) |
| up-right | 2 | 0x200 |
| right | 3 | 0x400 (+X) |
| down-right | 4 | 0x600 |
| down | 5 | 0x800 |
| down-left | 6 | −0x600 |
| left | 7 | −0x400 |
| up-left | 8 | −0x200 |

(Headings use the `facing` convention: 0x1000 per turn, 0 = +Z, positive toward +X.)

## Standing and walking (`state_01_stand`, `walk_step`)

`state_01_stand` is the neutral state. Each frame it:

1. turns the wrestler toward its current opponent, at most 341 units (30°) per frame, when
   in the ring or on the floor (`flags_lo & (LOC_RING | LOC_FLOOR)`). **Wrestlers always face
   their opponent**, so walking is a strafe around them;
2. ticks spirit (`wrestler_spirit_tick`) and clears `down_count`;
3. enters state 0x17 once when SPECIAL first lights (`flags_lo & 0x1000` without `& 0x2000`,
   the pose);
4. calls `walk_step` with the held buttons, then tries the input handlers in order (apron
   moves, turnbuckle climb, ropes, tag, run, R, strike, grapple, L, C-up) and finally
   `input_move`.

`walk_step` (`0x800A2964`):

- **No d-pad:** play the idle animation from `wrestler_base_anim` (stance style, injury,
  weapon).
- **D-pad held:** move this frame by the wrestler's walk speed in the d-pad direction
  (`wrestler_translate`) and play the walk animation from `wrestler_base_anim`.
- **On the apron** (`LOC_APRON`): the move is a shuffle along the apron (animation 3335, or
  3349 when the direction points along the side).
- **Pushing:** when the wrestler walks straight at an opponent who is **dazed** (state 0x29,
  below; facing it within ±2° and the d-pad within ±22.5° of the facing), `flags_hi | 2` is
  set, both are moved together and the pusher plays 2915.
- **Diagonals are faster:** the d-pad bits are applied per axis (up +z, down −z, right +x,
  left −x), each at the full speed, so a diagonal walk covers about 1.41 times the distance.
- Every frame ends with the bounds check (`keep_in_bounds`, below) and the wrestler push-apart
  (`push_apart`).

### Walk speed (`walk_speed`, `0x800A2574`)

The wrestler header byte 4 is a walk class 1–3. The class drops by one for each threshold the
**legs condition** (`region_condition[3]`) is below:

| Class | Thresholds (legs below → one class slower) |
|-------|--------------------------------------------|
| 1 | 10 |
| 2 | 30, 10 |
| 3 | 30, 10 |

The resulting class indexes `0x800E6FB4`, pairs of floats per frame:

| Index | Speed (returned) | Second value (written to `out.z`) |
|-------|------------------|-----------------------------------|
| 0 | 4.5 | 3.25 |
| 1 | 5.0 | 3.5 |
| 2 | 5.5 | 3.75 |
| 3 | 6.0 | 4.0 |

`walk_speed(w, out, mode)` returns the speed and, if `out` is given, writes (speed, 0,
second value). By mode:

| Mode | Use | Speed |
|------|-----|-------|
| 0 | walking | the table value; **3.0 on the apron** |
| 2 | running | **twice** the table value (9–12), both values doubled; **12.0 on the apron** |
| 3, 4 | (slow moves) | 1.5 |
| 5 | | 8.0 |

Walking only uses the returned speed. The second value is written to `out.z` for the two
callers that pass `out` (`walk_step` and the failed-move state 0x46), and both overwrite or
ignore it, so **it is never used** in World Tour (maybe a leftover, or used by the later
games).

The failed-move state 0x46 is where a move that needs another place happens first: the
attacker walks the receiver along (both translated together) at walk speed, or run speed
when the move has `flags & 0x20000`. With `flags & 0x2000` they head for the ropes of the
current side (`aim_at_side_point`) and the move goes ahead on arrival.

Details of the walk (`state_46_move_walk_attacker`, 2026-10-31): the attacker turns toward the
target from `move_walk_target` and walks the receiver backwards in front of it, at walk speed
(run speed with move flags 0x20000). Move flags **0x2000** walk to the **ropes** of the facing
side (`aim_at_side_point`; done when the run target is that side), otherwise (0x1000) toward
the **corner** direction until the bounds check reports that wall. On arrival the move plays its
part 2. The receiver can still reverse it at the animation's reversal frame, and if the walk
runs out of time the move switches to its follow-up slot (190 + `followup_190`).

## Dazed and staggering (states 0x29, 0x2A)

`state_27_recover` sends a wrestler whose last animation ended in pose class 3 to **state
0x29, dazed on the feet**: animation 3087 in the ring, 3089 on the apron. It also follows
some grapple breaks (`sub_800A7944`) and the C-up stun (`input_cup_opponent`). The wrestler
stays dazed while `recover_count` (+0x16) runs down: one per frame and one more per A, B, L or
R press (`recover_tick`), so mashing gets you out sooner. Then 3088 (3090 on the apron) and
back to neutral. While dazed it can still be pushed by a walking opponent.

The earlier name `state_29_apron` came from seeing a dazed wrestler on the apron; the state
is not apron-specific. Pose class 2 sends the wrestler to **0x2A**, a short stagger (3093
then 3094) with no input.

## The analog stick press

`input_move` with `g_input_far` set (the analog stick pressed) enters **state 0x16, the
taunt**: move slot 148 in the ring, 153 on the apron. That matches the FAQ ("to taunt, press
the analog stick"). The state was previously named `state_16_walk`.

## Running (`state_03_run`)

- Started by C-down (`input_run`). `run_start` (`0x800B7430`) clears `run_flags` and picks
  where to run, storing the heading in `0x800F16A0[slot]` and the target ring side in
  `facing_quadrant` (+0x34):

  | `run_flags` bit | Set when | Heading |
  |-----------------|----------|---------|
  | 0x1 | a direction is held | |
  | 0x2 | there is no rope target | a fixed heading: the side's direction from `0x800E7030`, or straight ahead |
  | 0x4 | no direction held | ahead: the side the wrestler is facing |
  | 0x8 | in the ring, heading for a side's ropes | re-aimed at the middle of those ropes every frame (`aim_at_side_point`); no run if already at those ropes |
  | 0x10 | on the floor with a direction toward a side | `0x800E7050[side]` |

  On the apron the run goes along the apron, left or right (`flags_hi & 0x20` = toward the
  lower-numbered side). The other bits:

  | `run_flags` bit | Meaning |
  |-----------------|---------|
  | 0x20 | **whipped**: an Irish-whip runner, not under its own control (the whip sets 0x22) |
  | 0x40 | hurdling: collisions with the hurdled wrestler are ignored (set by the hurdle, cleared at the next rebound) |
  | 0x80 | the hurdle animation is still playing |
  | 0x100 | a whipped runner took control back at the ropes (pressed A) |
  | 0x200 | a whipped runner has used its one try on the way to a corner |
  | 0x400 / 0x800 | that try succeeded with B/L/R or with A |
- Step 0 plays the start animation (2925, or 3352/3353 on the apron) and spreads the turn to
  the target heading over its frames (`turn_step`).
- Step 2 is the run loop (2924). The wrestler moves by `slide_vel` every frame
  (`run_step`, `0x800B7A60`) at twice the walk speed (`walk_speed` mode 2: 9–12 units a
  frame). With
  `run_flags & 8` the heading is re-aimed every frame at a fixed point for its side of the
  ring (`aim_at_side_point`, table `0x800E6FF0`), the middle of the ropes.
- When the bounds check reports a wall: with `run_flags & 4` and a heading 2°–178° off the
  wall, the run stops; near a corner with C-down held it becomes the turnbuckle climb
  (state 0x09). The run inputs (running attacks, dives) are handled by `sub_800B87DC`.
- Step 3 stops (2926, or 2931 for wrestlers with header trait bits `0x42`) and returns to
  neutral.

### Rope rebounds (state 0x04)

Running into the ropes of the target side gives state 0x04: 2995 (hitting the ropes), then
2996 (coming off) and the run continues the other way (`facing_quadrant ^= 2`). The game
counts rebounds per wrestler (`0x800F06D8`).

An **Irish-whipped** runner gets one chance on its **first** rebound: pressing A, B, L or R
within the first 3 frames, with a `chance_roll` (kind 3) win, and not when only the whipper is
in SPECIAL:

- **A**: takes control back (`run_flags | 0x100`); the rest of the run is its own.
- **B, L or R**: hooks the ropes and stops (2997, then 3000 — Andrew's tag "was whipped but
  stopped it by hooking onto ropes with arms").

Without a counter, a whipped runner keeps going and stops after the **second** rebound.

### Sending the whip (`state_0A_whip_attacker`, 2026-10-31)

C-down in a front grapple starts the whip (`grapples.md`). The attacker:

1. **Picks the target.** There are 8 directions (`0x800E70A0` angles): even ones are corners,
   odd ones the middle of a side's ropes. With no d-pad the whip goes to the ropes or corner
   nearest the direction the attacker is facing; the d-pad picks another. Out of a corner
   lock-up (`CORNER`) the runner is sent across to the opposite side. The runner gets
   `run_flags` 0x22 (whipped), `facing_quadrant` = the target side, and a side target re-aims at
   the middle of those ropes while running (`run_flags` 8).
2. **Heavyweights:** a whipper with header trait 0x80 gets the heavy whip (2981, the pair does
   not swing round); **whipping a wrestler with trait 0x80 fails** (2983 / 2993 from a corner,
   then generic move 6 on the whipper: no damage) unless the whipper has SPECIAL ready and the
   opponent is not SPECIAL.
3. **Strong-grapple whips near an edge** (`whip_attacker_input`, only with `STRONG`): close to
   the ropes or the barrier in the chosen direction the whip becomes a special throw (2985,
   2987, 2989; Andrew's tags call this set whipping between the floor, ring and barrier) and
   the receiver gets `_unk350` 4.
4. Swing (2975, or 2991 from a corner): the pair turns together toward the target, then the
   attacker releases (2977 / 2981) and returns to neutral. **If the runner has `CAN_REVERSE`
   at that point, the whip is reversed**: the attacker becomes the runner (state 0x0B, 2980)
   and is sent the same way. (Step 3 of the attacker state, which applies generic move 4,
   turns both round and whips again, is the other half of this exchange; not traced in detail.)

### Whipped into a corner

A runner whipped toward a corner may press A, B, L or R once inside a distance window on the
way (`whip_run_inputs`, `chance_roll` kind 3). On arrival it goes to **0x13** (hits the corner,
3001/3002), or with a successful press to **0x15** (A) or **0x14** (B/L/R), the corner
counters.

### Hurdling (state 0x19)

Running at a downed wrestler hurdles it (2927). During the hurdle, collisions with that
wrestler are ignored; hurdling close to the ropes can carry the runner over them onto the
apron (state 0x56).

### Bumping into someone while running (`run_collide`)

| Who is hit | Result |
|------------|--------|
| another runner | both go to state 0x05 (0x24 on the apron) |
| someone blocking (0x39 / 0x3A), not facing away | runner 0x05, blocker 0x3A (hit while blocking) |
| an opponent resting in the corner, by a whipped runner | runner 0x05, opponent 0x24 (knocked out of the corner) |
| anyone else | the run stops |

Whoever was facing away at the moment gets `flags_lo & 0x800000` (`BACK_HIT`).

### Buttons while running (`run_inputs`)

| Input | Result |
|-------|--------|
| B | running attack, state 0x07; at a lying opponent in reach, the running ground attack 0x4F |
| L or R | state 0x06, move slot 127 (probably the FAQ's somersault roll) |
| A near the ropes, opponent on the floor | dive to the floor, 0x57: slot 173 running to floor, 172 running to post (reach 180 units, 390 for wrestlers with header trait 0x200) |
| A on the apron (header trait 0x20000) | 0x5A, slot 177 (probably a springboard to the floor) |
| A toward a corner (header trait 0x80000) | 0x5C, slot 180 |

The header trait bits (`WrestlerHeader.traits`) decide who can dive at all (0x300), and how.

## Ring and arena bounds (`keep_in_bounds`, `0x800A4994`)

Every wrestler is kept inside a box chosen by where it is (`g_zone_bounds`, `0x800E70D0`,
four floats per zone: z max, x max, z min, x min):

| Zone | Where | Box |
|------|-------|-----|
| 0 | in the ring | x, z within ±230 (the ropes) |
| 3–6 | on the floor, one per side (`ring_zone_of + 3`) | the arena (±497) minus the ring block (±280), e.g. side 0: z 280…497 |
| 7–10 | on the apron, one per side (`ring_zone_of + 7`) | a line: z = 265, x within ±178 (and the rotated equivalents) |

- The body counts, not just the origin: the box is compared with the bounding box of the
  joints selected by the caller (the pelvis plus a joint mask), so arms and legs stay inside.
- On the floor, crossing into the next side's strip is allowed where the per-side mask
  (`0x800E70B4`) says the side is open; the check then repeats with the new zone.
- The push back is the overlap divided by the caller's factor (1 = at once, 4 = a quarter
  per frame), applied only along the axes of the d-pad bits the caller passes (default all).
  Grapple partners passed in are moved by the same amount.
- `ring_zone_of(pos)` is the side of the ring the position is on: the angle from the centre,
  offset by 45° and split into quarters (0–3).
- It returns which sides were hit (bits 0x100 / 0x200 / 0x400 / 0x800, shifted up 16 when the
  hit came from the zone change), which the run code uses to stop.

## Wrestler against wrestler (`push_apart`, `0x800A4170`)

- For every other wrestler (from `sub_800A5860`'s distance list) closer than **66 units**,
  the wrestler is pushed away by the overlap: a quarter per frame along the line between
  them (mode 2), or half per frame straight back from its own facing (mode 1).
- A lying wrestler (`flags_lo & 0x10000`) is not a circle: `sub_800A0100` measures the
  distance against an ellipse stretched along its body (its facing), so you can stand next to
  a downed opponent's side but not on top of it.
- It returns the index of the first wrestler it collided with; `walk_step` uses that for the
  push check above.

## Open questions

- Slots 175, 177, 179 and 180 are matched by position only; watching them in the viewer would confirm them.
- ~~`sub_800A5860`~~ Read 2026-11-01: `wrestler_dist_list` fills up to three 12-byte entries
  (slot, distance, angle) for the other active wrestlers, filtered by a flags mask (any of the
  given `flags_hi` / `flags_lo` bits) and skipping those with `flags_hi` 0x200, sorted nearest
  first. In `keep_in_bounds` the joint mask is a stack argument: bit i selects joint i (2–19)
  for the box, and bit 33 adds a fixed pad; Revenge passes 0xFFFFE (every joint) there, which
  World Tour very likely does too. (Old note: the exact joint mask used for the body
  box.

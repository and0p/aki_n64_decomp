# No Mercy: objects, weapons, ladders and tables

Read from the code (2026-11-01 / 02) with RAM checks (a thrown stop sign, a crowd weapon in hand, the
ring steps). Names in `symbols/nomercy/`; the record is `struct Object` in `types.h`.

## The object records

`g_objects` (0x8016AE10), 180 bytes each (`struct Object`); records 4 and 5 are the two ring steps,
record 6 the ladder match's prize, the others are given out as weapons are taken.

| Offset | Field |
|--------|-------|
| +0x00 | type: the weapon type (crowd types from `g_crowd_weapon_types`; 17 the stop sign); −1 unused |
| +0x04 | type2: the object's current form (25 a standing ladder) |
| +0x08 / +0x0C | holder (−1 loose) / owner (who last held or threw it) |
| +0x10 | last hit (slot, or 100 + object) |
| +0x14 / +0x18 | ladder climbers on sides 0 / 1 (−1 free) |
| +0x20 | flags: 0x01 / 0x02 held or carried, 0x04 loose, 0x08 flying, 0x80 can hit while flying, 0x100 a standing ladder, 0x400 landed, 0x800 landing, 0x4000 falling, 0x10 knocked by another object |
| +0x44 | position; +0x50 / +0x54 tilt, +0x5C yaw, +0x60 velocity, +0x78 spin, +0x7C a second velocity, +0x94 spin change, +0x98-+0xA4 grow factors; +0xA8 previous position |

Per-object physics are 140-byte records at 0x800A9C70, filled at run time by code in the main segment
(0x80023950: flags, -1 links), not loaded from a file.

## Each frame (`objects_update`, 0x80140A84)

- **Flying** (flags 0x08): `object_hit_check` while it can hit (0x80, not 0x400), then `object_fly`:
  position += velocity, the second vector += its change, yaw += spin, spin += its change; below a speed of
  **1.5** the horizontal velocity is zeroed (it comes to rest). Bounces keep a third of the speed.
- **Landed** (0x400): frees its ladder sides; a ladder that is not standing sets the ladder camera.
- Below **y = −20** an object is cleared.
- **Held / carried** (0x03): follows the holder's hand angle; grows (+0x98 / +0xA0) up to 1.0.
- `object_set_motion` starts a throw, a drop or a knock (how 0-5 and more), e.g. a charged run into an
  object (`run_collide`).
- `object_hit_check`: a hit test from the owner's record (`strike_hit_test` kind 24); on a wrestler it
  starts the hit with generic move **5 × type + 111** (from 0x800A6040); on another object it knocks that
  one away with a random spin.

## Throwing, dropping and landing (read 2026-11-02)

`weapon_release(w, how)` puts the object at the hand (joint +0x120; in the ring clamped to ±200 when past
±225), sets it flying (flags 0x08) and gives it a start:

| how | Used for | Start | Spin |
|-----|----------|-------|------|
| 0 | knocked out of the hand, dropped | flags 0x10; straight up **16-22** | (rand 0-9 − 5) × 11.4 a frame |
| 1 | let go to the side | flags 0x20; **3.0** sideways (facing + 910), **5.25** down | — |
| 2 | put down | flags 0x440 (landed); 10 down; a standing ladder tilts to −1024, others lie flat | — |
| 3 | **thrown** | flags 0x80 (can hit); **30.0** along the thrower's facing, **5.0** up; from the floor at an opponent in the ring a lob instead: × 0.65 forward and 17-21 up | (rand 0-19 − 10) × 11.4 |
| 4 | handed over | the object record is cleared | — |

Every start uses **gravity −1.75** a frame (+0x80) and an air drag of velocity × −1/75 (+0x7C / +0x84).

`object_fly` moves it and tests a few probe points from its 140-byte record (0x800A9C70 + 140 × object:
+1 the number of points, 2 / 4 / 8 by type from 0x800528F0, +0x2C the points, 12 bytes each, from the
object's model) against the area's surfaces (0x80147BB4-
0x80147C24 by `arena_area_lookup`). **The first landing** keeps a third of the horizontal speed and
bounces up at |vertical| / 5 (/ 10 for a ladder), flags 0x400, a sound; **the second** sets 10 down and
flags 0x800 (settled). Below a speed of 1.5 the horizontal motion stops (`objects_update`).

## Getting a weapon

- **C-left with the d-pad toward a zone that allows it** (`input_weapon_grab`; when running, C-left
  alone): zone ids 46-53 and 188-195 are the **ring steps** (state 0x8C, held_obj 4 or 5); otherwise the
  **crowd** hands one over: zone_info is saved in grab_zone_info and state 0x8A starts.
- `weapon_pick_type` (for 0x8A, 0x8C, 0x91, 0xBD): a zone can force a type (area data +8) or allow a
  chance_check(800) special; otherwise a random entry of `g_crowd_weapon_types` (0-5, 7-17, 20-24) that
  `weapon_type_allowed` keeps (12 lockable types, mask 0x8009FDEA, probably the unlocks); 14 and 22 are
  left out in ladder matches (g_match_type 0x100000). The crowd weapon chance uses
  `chance_roll` kind 9 (by attrs +6, the weapon trait).
- **Picking up a loose weapon:** 0x91 (12379 picking it up, 12384 failing and looking surprised); the ring
  steps 0x8C (12962 lifting something big at waist height, 12964 grabbing at it and missing).
- Holding one: flags_ef 0x01, held_obj (+0x3C). Two-handed carry 0x8B (12390); hit while holding a
  big weapon 0x90 (12458 / 12459, throwing while jumping 12460).

## Using it

- A tap of B with a weapon: 0x26 (`state_26_weapon_swing`); a hold: 0x27 (swinging the steps in RAM:
  the opponent went down 0x2D). Generic moves 107-111 are the weapon attacks (+5 × the object's type2).
- At a downed opponent: 0x28 (generic moves 104 / 110; the opponent goes 0x31 → 0x63).
- Carrying a ladder (object kind 3) to a zone that allows it (`input_weapon_grab_zone`): **0x8D stands it
  up** (12965, then flags 0x100 and a wobble, 12966), or at zones with bit 0x10 **0x8E**, a paired move with
  it (read 2026-11-02). Throwing a weapon is **0x8F** (`state_8F_weapon_throw`, from `input_weapon_use`): animation 12515 +
  the object's kind, released at **frame 14** with `weapon_release(w, 3)`.
- **Weapon hits are unblockable** (strike_flags 0x600 within 0x100600); a timed R block against a swing
  gives the weapon counter 0x66 / 0x67 (`strikes.md`).
- **DQs:** `strike_dq_check` from `strike_land` when weapons are illegal (g_match_rules 0x2000) and for
  hits on the referee; the guest referee can call it too.
- `weapon_release` lets go: a loose object at the hand, kept within ±200 in the ring. Grapples make the
  defender drop its weapon; a defender swinging a weapon (0x1B) loses 10 spirit when grappled.
- Weapons stay where they land (the guide's "weapons persist").

## Ladders

- A ladder is an object with type2 **25** (flags 0x100 standing, 0x4000 falling; `ladder_object_update`).
- **C-up** in the ring or on the floor while touching a standing ladder and facing it
  (`input_ladder_climb`): takes a free side (climber0 / climber1), sets held_obj and ladder_side, state
  **0xAA** (location 0x08).
- States 0xAA-0xBD: 0xAA start (12967), 0xAB get down (12968), 0xAC / 0xAD a step up / down (12969 /
  12970), 0xAE idle (12971, 12989 near the top), 0xAF / 0xB0 reaching / leaving the top (location 0x10;
  12987 / 12988), 0xB8 the very top (12996), 0xB7 / 0xBA / 0xBB punching, shoving and yanking across the
  top, 0xB4 pulling a climber off (12982 / 12983), 0xB6 jumping off (12972-12974), 0xB2 / 0xB3 knocked off
  and falling (12979 / 12984), 0xBC C-up on the ladder, **0xBD grabbing the prize** (12997-13001; the
  opponent gets end_flags 0x800).
- `ladder_pose` builds the ladder's rotation and places the climber; blocking on a ladder uses the
  ladder poses (0x4C / 0x4D steps 5-7); any strike on a climber knocks it off (0xB2); a charged run tips a
  ladder.
- **Knocking a ladder** (`object_set_motion`, read 2026-11-02): each cause (`how`) has a topple chance:
  0, 1 and 5 never; 2, 3, 4 and 11 always; 6 and 7 always when nobody is on it, else **25%**; 8, 9 and 10
  always when empty, else **50%**. A topple sets flags 0x4000 (falling) and pushes it along the given
  direction at 5-7 with a random spin; otherwise it wobbles (flags 0x2000) for **9-18 frames** (cause 0)
  or **20-39 frames** and settles back. Its callers are the charged run, strikes, the ladder states
  (knocked off, falling, jumping off, the prize grab), dives, thrown objects and the ladder itself.
- The CPU: modes 133 / 135 / 139 (go for object 6, climb) and 138 (on the ladder); `ai_ladder_climbers`.

## Tables and the steps

- Four raised places (location 0x40 / 0x80 / 0x100 / 0x200; 0x20 on top). The three tables at 0x40,
  0x100 and 0x200 can break: `g_tables_intact` (0x8016B928) bits 0x04 / 0x08 / 0x10.
- Climbing on: 0x6F (`state_6F_climb_onto`, needs the table's bit; 13016) or from a run (0x9B);
  getting off: 0x6E (13017).
- Putting the opponent on a table: C-up / C-down in the lock-up facing it (`lockup_place_move`: zones
  29-54 the announce tables, 25-27 the steps) → 0x0F / 0x10, then the opponent lies on it (0x31).
- **A finisher landing on a table breaks it** (`finisher_effects`): sound 51, a crowd flash or pyro,
  `arena_zones_swap` 2 / 3 / 4 (the broken table's collision), and 0x8009FE68 bit 0x40 for the first.
- In RAM the table slam ran about 180 VIs in 0x55 / 0x56, then the defender lay in 0x31.

## Open

- Where exactly the probe points sit: the count is fixed by type (0x800528F0: 2 for types 0-10, the sticks;
  4 for 11-17 and 22-24; 8 for 18-21 and 25, the ladder), and the positions follow the object's bound model
  (0x80024F84 binds it by type); reading them once from RAM would confirm.
- ~~Which zone allows which crowd weapon (the area data's +8)~~: zones do not choose the type (code 2026-11-02).
  `weapon_pick_type` reads the **wrestler's** attribute record: attrs +8 (nonzero) forces a type through the
  byte table at 0x8014E05B (values 1-9 -> type 0, 10-14 -> 1-5, 15-23 -> 7-15, ...), the wrestler's own
  weapon; attrs +9 allows the 80% special; otherwise a random allowed entry of `g_crowd_weapon_types`
  (0x8014E064). The segment flag +0x28 0x40 only says a weapon can be taken there.

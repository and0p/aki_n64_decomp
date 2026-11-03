# World Tour: knockdowns, getting up and recovering

First pass (2026-10-30), read from the code: `state_28_down`, `state_3C_get_up`,
`wrestler_neutral_by_pose` (`0x800A7944`), `state_27_recover`, the corner slump 0x13 and the
crash states 0x10/0x11. Not checked against a live match yet. Recovery numbers come from
`recover_difficulty` (`match_end.md`).

## Where a wrestler goes after a move (`wrestler_neutral_by_pose`)

At the end of most moves and reactions the receiver is sent on by the **pose class** of the
last animation frame (`AnimPlayer` +7, copied to `Wrestler.pose`):

| Pose | Next state |
|------|------------|
| 0, 1 | 0x27 recover (sets `flags_hi` 0x80000) |
| 2 | 0x2A (plays 3093, then 3094, then recover) |
| 3 | 0x29 dazed |
| 4, 5 | 0x01 standing |
| 7–0x1C odd | 0x28 down, face up (clears FACE_DOWN) |
| 8–0x1C even | 0x28 down, face down (sets FACE_DOWN 0x8000) |

Byte +0x35E (`AnimPlayer` +6, the animation's location class, 3 = apron as in `state_56`) = 5,
the corner, sends it to 0x13 instead (slumped in the corner).
`state_27_recover` does the same table at the start of a state.

## Lying down (`state_28_down`, state 0x28)

On entry:

- `hits_taken` = 0, `down_count` += 1 (times down since last standing).
- `recover_count = recover_difficulty(w, 0, 0)`:
  `200 + 3 * last_damage - (health + spirit) / 2 - 60 * down_count`, at least 0, or **0 while
  SPECIAL**. `last_damage` (`g_last_damage`, `0x800F0610`) is the size of the last health
  change, written by `wrestler_add_health`. So a big hit keeps you down longer, health and
  spirit get you up sooner, and each further knockdown before standing takes **2 seconds off**.
- `chance_roll(w, 0, 8)` sets `flags_hi |= 0x2000000`: the wrestler first plays 3052 (3053 face
  down) and goes on by pose instead of waiting (probably rolling over); not identified.

While down: `flags_lo |= 0x10000` (lying; ordinary strikes skip such targets, only ground
attacks hit) and `flags_hi |= 4` (not hittable this frame). The lying loop comes from the pose
table at `0x800E7202`. If the wrestler is already out of the match (`flags_lo & 0x100`) it
gets 0x200 too.

Every frame, `recover_tick`: −1, and −1 more per 4 presses of A/B/L/R. When it runs out:

- **stick pushed** → stays down (count held at 0): playing possum;
- someone is diving at this wrestler from the top rope (state 0x5D–0x5F, `dive_flags` 2, in
  its step 2 or 3) → stays down, and the diver's `dive_flags |= 0x10`, so the dive does not
  miss because the target got up (`wait_for_diver`, `0x800BC460`);
- otherwise → **get up** (0x3C).

When the match is decided the winner (`flags_lo & 0x80`) gets up at once and the loser stays
down.

## Getting up (`state_3C_get_up`, state 0x3C)

The get-up animation (`get_up_anim`, `0x800BBFC4`):

- SPECIAL: 3060 (3061 face down), 2 less with header trait 0x40000 (a kip-up).
- Otherwise 3043 + speed, +6 face down. Speed 0, 1 or 2 is from `last_damage` (≥15 → 1,
  ≥22 → 2) or, after smaller hits, from `health_eff` (<128 → 1, <64 → 2).

During the get-up, holding A or B is remembered (`0x800F06C0` / `0x800F06C8`, with the d-pad in
`g_grapple_input`). At the end:

- **Holding R** (and allowed to act, below) → kneel (3054) for up to **150 frames**:
  - L → 3055, then stand (probably a roll away);
  - stick → state 0x16 (taunt);
  - A or B with R still held → **get-up attack** (state 0x20, slot 157, always a knockdown,
    `strikes.md`);
  - releasing R, or 150 frames → stand.
- Otherwise the wrestler turns to face the opponent and stands; if A was held it goes straight
  into a **grapple attempt** (0x3D), or with header trait bit 1 an A strike (state 0x1D with
  `flags_hi` 0x200000, slot base 0); if B was held, a **strike** (0x1D).

`wrestler_may_act` (`0x800DD200`) gates the kneel and the attacks out of a get-up: always true in
singles, with rule 0x40 (tagging not required, so 0x40 is the "no tagging" format),
for the legal man, or when a legal man is on the floor; otherwise an illegal tag partner just
stands.

## Slumped in the corner (state 0x13)

Whipped into the turnbuckle: snapped to the corner position (`0x800E7010` + quadrant), 3001 /
3002, damage from generic move 13, then 3003 slumped with `recover_count =
recover_difficulty(w, 0, 2)` = `100 - spirit/2 - health/4` (0 while SPECIAL). Mashing as
above; the stick keeps it slumped; then 3005 and out. A running strike at a wrestler in this
state is the corner attack (slot 128, `strikes.md`).

## Running into the post or barrier (states 0x10 / 0x11)

Started by `run_step` when a runner hits something (0x10 and 0x11 face different ways, tables
`0x800E7030` / `0x800E7040`). 3011 and a 7-frame turn. Pressing A/B/L/R once during it rolls
`chance_roll(w, 0, 3)`: success → 3010 and generic move 16 (caught yourself, less damage);
failure → 3009, generic move 15, and a lie-down time of `recover_difficulty(w, 0, 4)` =
`180 - spirit/2 - health/4`, **cut to 0 when the ring-out count is down to its last 5** so a
wrestler outside is not counted out lying there.

## Acting on a downed opponent

All need the opponent lying (`flags_lo` 0x10000), not untouchable (`flags_hi` 4), both in the
ring or both on the floor, and the wrestler within reach of a point beside the opponent
(`sub_800A0100`, from its position and facing).

| Input | Result |
|-------|--------|
| **R** (`input_r`) | **pick up**: states 0x2D / 0x2E. On the apron, R is state 0x08 instead; otherwise R blocks |
| **C-up** (`input_cup_opponent`) | **flip over**: states 0x2B / 0x2C |
| **L** (`input_l`, via `sub_800BAA0C`) | **ground grapple / drag**: states 0x33 / 0x34 (not for wrestlers with header trait bit 1) |
| C-down (running) | hurdle, or the running ground attack 0x4F (`movement.md`) |
| B | ground attack 0x23 (`strikes.md`) |

Near a **dazed** opponent (state 0x29, early, not on the apron) C-up instead **turns it around**
(3091/3092, the opponent into 0x29 step 3) so it faces away for a rear grapple.

### Picking up (0x2D / 0x2E)

The picker crouches (3062), walks to a spot beside the opponent and lifts with 3063 / 3065, or
3071 / 3073 when the opponent is face down. The lifted wrestler can **counter**: when its
`health_eff` is under 129 and `chance_roll(def, 0, 13)` wins, both play the failed versions
(3080–3086); from a face-down pick-up the counter goes into the defender's own rear-grapple
slot 70 or 71 (50/50, states 0x4C / 0x4D). A successful face-down pick-up ends in the rear
grapple hold (2940 / 2941); face up, both return to neutral (the opponent standing, ready to be
grappled).

## Open questions

- What 3052/3053 (the `chance_roll` 8 branch) and 3055 look like; checking them in the viewer
  would settle it.
- Pose 2 → state 0x2A, and the byte at `AnimPlayer` +6.
- The AI's use of the stick to stay down (the `chance_roll` kinds 3 and 8 are in `frame_loop.md`).

## Taunting at a downed opponent (`input_a`, 2026-10-31)

Taunting (0x16) next to a lying opponent in the same area has a 30 % chance to make it stir:
it plays 3052 (3053 face down), the same animations as the `chance_roll` 8 branch, and drops
straight into the end of the down state. So 3052/3053 look like "rolling over / stirring".

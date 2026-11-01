# World Tour: strikes, hit detection and blocking

First pass (2026-10-30), read from the code: `state_1D_strike`, the hit test
`strike_hit_test` (`0x800C98B8`), `strike_land` (`0x800CA9B0`), the receiver states 0x24
(hit reaction), 0x39 (block) and 0x3A (block absorbs a hit), and `block_counter`
(`0x800CB694`). Not checked against a live match yet.

## Starting a strike (`state_1D_strike`)

B (`input_strike`) enters state 0x1D. For the first frames the wrestler can still walk and turn
toward the opponent, and the d-pad is collected into `g_grapple_input`. Then:

- **B held for 8 frames** → state 0x1E (strong strike, slot 123 or 124 with Up), or 0x22 with a
  weapon.
- Released earlier: with a weapon state 0x21 (weapon swing); against a **running** opponent
  state 0x1F (strike that stops a runner); otherwise the tap strike: **slot 118**, **+2** when the
  opponent is **more than 95 units away** (`g_opp_dist`, pelvis to pelvis in x/z, against the
  double at `0x800E8C10`; the far version, 120), **+1** when any d-pad direction was held during
  the start-up (`g_grapple_input[slot]`, which the strike ORs the held d-pad into; 119 / 121).
  So the tap strikes are 118–121, which corrects the 195 / 148 guess in `moves.md`.
- `flags_hi & 0x200000` swaps the button to A and the slot base to 0; not identified.

Other attacking states call the same hit test: running attack 0x07, 0x15, 0x1A, 0x1E–0x21, the
ground attack 0x23, dives 0x5D. Each passes a **hit kind** to `strike_land`:

| Kind | State | Strike |
|------|-------|--------|
| 0x001 | 0x1D | tap strike |
| 0x002 | 0x1E | strong (held) strike |
| 0x004 | 0x1F | strike against a runner |
| 0x008 | 0x07, 0x15 | running strike (slot 125 or 126 by `run_flags & 4`), or the counter when whipped into the corner (0x15) |
| 0x010 | 0x07 | running strike at an opponent in the corner (slot 128: the opponent was whipped into it, or is in state 0x13) |
| 0x020 | 0x1A | strike across the ropes at an opponent in the ring (slot 130) |
| 0x040 | 0x1B | strike across the ropes at an opponent on the floor (slot 131) |
| 0x080 | 0x1C | strike at an opponent on the apron (slot 132, or 133 depending on which way it faces) |
| 0x100 | 0x20 | get-up attack: R held while getting up, then A or B (slot 157) |
| 0x200 | 0x21 | weapon swing |

States 0x1A–0x1C come from `strike_across_ropes` (`0x800BA694`, called by `input_strike` and
`input_grapple`): it picks one by where the opponent is (ring, floor, apron) and the
d-pad direction against the wrestler's ring zone. Per ring side 0–3 the d-pad bit "toward the
ring" is `0x800E7078` = {Down, Left, Up, Right} and "away from the ring" is `0x800E7070` = {Up,
Right, Down, Left}: toward the ring picks 0x1A (into the ring), away picks 0x1B (to the floor),
otherwise it goes by where the opponent is. Their hit tests only accept a target in that area
(modes 14, 16, 15).

## The hit test (`strike_hit_test`)

Run every frame of the strike; returns the wrestler hit, or -1.

1. **Active frames:** only while `anim_frame` is between the animation's `window_start` and
   `window_end` (`AnimPlayer` +8 / +9). This is the window that sets `flags_hi & 4`
   (`IN_ANIM_WINDOW`), so that bit means "attack frames active".
2. **Candidates** (`strike_targets`, `0x800CA318`): every other wrestler without `flags_hi &
   0x14` or `flags_lo & 0x10000`, the current opponent first. In this mode a tag partner **can**
   be hit. (`flags_hi & 4` excludes a wrestler whose own attack is active, so it looks as if a
   wrestler mid-strike cannot be hit; not confirmed.)
3. **Same area:** from inside the ring you only hit someone in the ring or on the apron; from
   the apron, anyone; from the floor, only someone on the floor.
4. **Broad test:** pelvis to pelvis closer than **175**.
5. **Contact:** for each of the move's three hit joints (`hit_joint`, `hit_joint2`,
   `hit_joint3`), take that joint's point (or, for knees, hips, elbows and shoulders, the two
   ends of the bone below it from the second joint set at +0x1CC: knee → ankle, hip → knee,
   elbow → wrist, shoulder → elbow). The strike lands when either point is closer than
   `r_target[joint] + r_attacker[hit joint]` to the target's **pelvis (joint 1)** or **head
   (joint 5)**.

All distances go through `vec_xz_dist_angle`, so they are **horizontal (x/z) only**; height
is ignored (a strike aimed high can hit a crouching opponent, as long as the limb reaches).

The weapon swing (mode 3) tests the weapon's tip (`sub_800D8210`) against the same two points.

### Joint radii (`joint_radii_init`, `0x800C9680`)

`g_joint_radii` (`0x800F1A20`, 23 floats per wrestler, joints 1–19 used) is computed when the
model loads: each joint's mesh extent in x or z (the larger) times a per-joint factor
(`0x800E7958`). So radii follow the body type. In the tag snapshot they read:

| Joint | 1 pelvis | 2 waist | 3 chest | 4 neck | 5 head | 9 hand | 8 foot |
|-------|----------|---------|---------|--------|--------|--------|--------|
| Benoit / Mysterio / Hall | 28.9 | 21.6 | 28.8 | 9.6 | 13.5 | 11.0 | 13.2 |
| Nash | 28.9 | 21.6 | 28.8 | 15.6 | 19.5 | 11.0 | 13.2 |

## What happens on a hit (`strike_land`)

`strike_land(attacker, receiver, kind)` first looks at the receiver's state:

- **Pinning or holding** (states 0x31, 0x35, 0x37) or **pinned / held** (0x32, 0x36, 0x38):
  the pin or hold is **broken up**: `_unk350 |= 0x10` on the one on top. (That bit, called
  "debug" in `match_end.md`, is mainly this save; debug Z also sets it.) Nothing else happens.
- **Blocking (0x39):** unless it is a weapon swing, `block_counter` may catch the strike (below);
  if it does not and the strike is from the front, the receiver goes to **0x3A** (the block
  takes it). From behind (`BACK_HIT`) the block does nothing and the hit lands normally.
- **Already absorbing a block (0x3A):** from the front, the hit is ignored.
- **On the apron (0x29):** a 15-table `chance_roll` lets the wrestler counter as if blocking.
- Taunting (0x16): −10 spirit, then the normal hit. Carrying someone (0x66): drops them.

Otherwise the hit lands. The receiver's `hit_info` (+0x028) gets the kind, BACK_HIT (0x800)
and the **height** from the attacker's `hit_reaction`: 0–1 high (0x800000), 2 and 5 middle
(0x1000000), 3–4 low (0x2000000). The damage is applied (`sub_800A7018`), then
`move_aftermath_check` (bleeding / TKO, `match_end.md`).

### Choosing the reaction

`hits_taken` (+0x039) counts strikes taken in a row (1–4; reset when the wrestler itself lands
a strike, by states 0x28 and 0x23, and after 2 s without being hit, `hits_taken_decay`). The receiver's `health_eff` picks a bracket: ≥128, ≥64, ≥32,
or lower (`0x800E79B4`). A weighted table, read with `rand_pool_next` (0–999), picks the
reaction (0 flinch, 1 stagger, 2 knockdown); strong strikes, runner strikes and weapons add 1.

Tap strike (`0x800E79BC`), per hit in a row (rows) and health bracket (columns), chance of a
flinch / a stagger, out of 1000:

| Hit in a row | ≥128 | ≥64 | ≥32 | <32 |
|--------------|------|-----|-----|-----|
| 1 | 900 / 100 | 800 / 200 | 700 / 300 | 550 / 450 |
| 2 | 800 / 200 | 700 / 300 | 550 / 450 | 450 / 550 |
| 3 | 700 / 300 | 550 / 450 | 450 / 550 | 400 / 600 |
| 4+ | 550 / 450 | 450 / 550 | 400 / 600 | 200 / 800 |

Strong strike (`0x800E79FC`, then +1): stagger / knockdown 900/100, 700/300, 500/500, 350/650.
A runner strike always staggers. Running attacks (kinds 0x08, 0x10) and kind 0x100 always knock
down. Other tables cover a running receiver (`0x800E7A14`–`0x800E7A38`), receivers in states
0x2A and 0x29, and kind 0x40 (`0x800E7A94`).

On the apron the choice is different: a tap strike knocks the wrestler off 30 % of the time
(`rand ≥ 701`), kinds 0x20–0x80 20 % of the time; strong, runner and weapon strikes, running
attacks and kind 0x100 always do. Knocked off → state 0x56; otherwise it reels on the apron
(3380).

### Reaction animations (`state_24`)

| `hit_info` | Reaction | Animation (+1 when hit from behind) |
|------------|----------|-------------------------------------|
| 0x2000 | flinch | by the strike's `hit_reaction` 0–5: 3276, 3278, 3280, 3282, 3323, 3284 |
| 0x4000 | stagger | high 3286, middle 3288, low 3290; can stumble into the ropes (state 0x13) when facing them |
| 0x8000 | knockdown | high 3297, middle 3299, low 3301; +6 when `health_eff` < 192 |
| 0x10000 | running attack from the front | 3309 + `reaction_variant` |
| 0x20000 / 0x40000 | special cases | 3006 / 3004 |
| 0x80000 / 0x400000 | on the apron | 3380 / 3389 |

## Blocking

- **R** (`input_r`) enters **state 0x39**: block animation (`wrestler_base_anim` kind 2), the
  wrestler keeps turning toward the opponent, `flags_hi |= 0x400` (blocking). `state_timer`
  counts frames since R went down (capped at 100). Releasing R plays the idle animation and
  returns to standing 6 frames later.
- **A block only works from the front.** A strike from behind lands as normal; weapon swings
  ignore blocks.
- **A blocked strike** sends the blocker to **state 0x3A**: a block-hit animation by height
  (`base_anim` 3–5 for tap-type strikes, 6–8 for heavier ones), then a self-hit through
  generic move 3, then back to blocking with `state_timer` = 100. Generic moves 2 and 3
  (file 221) have power 0: **a block costs no health**, and the blocker gains spirit (+2 per
  blocked strike, +7 from generic move 2 at the end of the block state). Further front
  hits are ignored while in 0x3A.

### Block counters (`block_counter`)

If the strike is a tap strike (0x1D), strong strike (0x1E), running attack (0x07) or 0x1B
(the tap strike only when the attacker lacks header trait bit 1) and **the block started at most N frames ago**,
the blocker catches it:

- N is `0x800E7950[header byte 0x18]` (byte 0x19 against the far strikes 120/121 when the
  attacker lacks trait bit 1): **3 frames for values 0–2, none for 3–5**. On frame 3 it is a coin
  flip (`chance_check(500)`). So it is a just-frame block: press R right as the blow arrives.
- On a catch: sound 279, `sub_800A7158`, both wrestlers are linked, and they go to **states
  0x51 / 0x52** (blocker / attacker), or 0x6B / 0x6C when the strike was slot 131. Which strike
  was caught is recorded in the blocker's `state51_flags` (+0x030: 5 → 0x100, 6 → 0x200,
  123 → 4, 124 → 8, 125 → 0x10, 126 → 0x20, 128 → 0x40, 131 → 0x80; 1 = a far tap strike
  caught with the other header byte). `chance_roll(blocker, 0, 11)` sets bit 2.

### What a caught strike becomes (`state_51` / `state_52`)

The blocker (0x51) plays a move and the striker (0x52) plays its receiving part:

| Caught strike | Move played | From whose move set |
|---------------|-------------|---------------------|
| tap strike | slot 136, or 137 when the counter roll (bit 2) won | the blocker's |
| far tap strike (bit 1) | slot 138, or 139 with the roll | the blocker's |
| strong strikes 123 / 124 | slot 140 / 141 | the **striker's** |
| running strikes 125 / 126 / 128 | slot 142 / 143 / 144 | the striker's |
| slots 5 / 6 | slot 7 / 8 | the striker's |

So 136–139 are each wrestler's own strike counters (a plain catch and a better one), and
140–144 are counters stored with the strike they answer. `moves.md` had 136–144 as combos;
they are these counters. The damage is applied at the end of the move, and
`move_aftermath_check` runs on the striker.

Slot 131 (a strike from the apron at someone on the floor) caught from the floor goes to
states 0x6B / 0x6C instead: a scripted pull (`sub_800C4CC8` / `sub_800C515C`, per-slot data
at `0x800F06EC`) with animations 3383/3385/3391/3393 and damage from generic moves 30–32.

## Open questions

- ~~Slots 5/6 (strikes that can be caught, then 7/8).~~ Read 2026-11-01: wrestlers with header
  trait bit 1 (0x2) turn the grapple button into a strike: `input_grapple` sets `flags_hi`
  0x200000 and the strong strike state 0x1E then plays **slot 5** (6 with a direction) instead of
  123 / 124. These are the "strikers'" grapple-button blows; caught by a block they become the
  striker's slots 7 / 8. State 0x1D strikes of trait-2 wrestlers cannot be caught at all.
- Whether a wrestler with its own attack frames active really cannot be hit (trading blows).

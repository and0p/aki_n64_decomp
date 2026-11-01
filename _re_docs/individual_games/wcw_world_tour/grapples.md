# World Tour: what happens during a grapple

Read from the code of the grapple states (2026-10-30). Function names are in
`symbols/wcw/moves.toml` and `states.toml`; the animation side (lock-up distances, which
animation each side plays) is in `animation_states.md`. Not yet checked frame by frame in a
live match.

## The two states

Once a grapple attempt connects (`state_3D_grapple_attempt`, below), the attacker goes to
**state 0x3E** (`state_3E_grapple_attacker`) and the
defender to **0x3F** (`state_3F_grapple_defender`). The attacker leads (`GRAPPLE_LEAD`,
defender in `linked_idx[0]`), so both are updated together, attacker first.

Attacker steps (`state_step`):

| Step | What happens |
|------|--------------|
| 0 | `grapple_anim_attacker`: pick the lock-up animation and target spot from `grapple_ctx`, snap `pos` onto the pelvis |
| 1 | `grapple_lock_attacker`: slide and turn into place, 1 frame per tick, for 6 frames (10 for the test of strength). Then open the **input window** in `state_timer`: 60 frames for a weak grapple, 90 for a strong one (`STRONG`), 60 on the apron or across the ropes, and only **30 if the defender is in SPECIAL**. The `0x40000` context skips straight to step 7 |
| 2 | `grapple_timeout` every frame: count the window down and watch the attacker's buttons (below). Button → step 3; window runs out → step 7 (break) |
| 3–4 | `grapple_choose_move` turns the stored input into a move or a state pair. A strong corner grapple with move flag `0x200000` first plays 2967 (lifting the defender onto the turnbuckle, step 5) |
| 6 | start the move: attacker plays part 0 and goes to **0x42** (`state_42_attack_move`) |
| 7 | break: play the break animation (2938/2942/2946/2950/2954). On the apron it continues with 3276; otherwise back to neutral (`sub_800A7944`) and the link is dropped |
| 8 | not entered from the steps above (set from elsewhere): the attacker takes a hit, becomes the defender (state 0x3F) and plays 2941, "grappled from behind" |

The defender's steps mirror these: it slides into its spot (`grapple_lock_defender`), then
waits on flags the attacker sets in `_unk350` (bit 1 = move chosen, bit 0 = broke, bit 2 =
reversed) and plays the matching animation: the attacker's break + 1, the move's part 1 in
state **0x43**, or 2968 for the turnbuckle lift.

## Can the wrestlers move around?

No. Apart from the 6-frame slide into the lock-up spot, neither state moves the wrestlers or
reads the d-pad for walking. The d-pad is only read as part of the move choice (up / down /
none, or "any direction" from behind). Turning toward the opponent also happens only during
the slide.

## The attacker's input window (`grapple_timeout`)

Each frame of the window the attacker's newly pressed buttons are checked:

- **Front, side, corner, apron:** A, B or C-down. The first one pressed is stored in
  `g_grapple_input[slot]` (`0x800F06D0`) together with the held d-pad bits, and the window
  closes.
- **From behind:** A or B (also L / R in some match types, `0x80062058 & 0x600`).
- **Special:** in a front grapple with `SPECIAL_READY`, `g_input_far` (the analog-stick press
  the FAQ describes, likely) sets `SPECIAL_ALLOWED` and closes the window.
- Nothing pressed for the whole window: the grapple **breaks** (step 7). A match ending
  (`g_match_flags & 0x40`) empties the window at once.

## What the input does (`grapple_choose_move`)

Checked in this order:

| Situation and input | Result |
|---------------------|--------|
| From behind + **R** | put the opponent on your shoulders: states 0x66 / 0x67 |
| From behind + **L** | **hold the opponent for a partner to hit**: states 0x64 / 0x65 (from a player guide Andrew found: "strong grapple from the back and press L") |
| Apron or across the ropes (`DEF_ON_APRON` / `ATK_ON_APRON`) + A or B | move slot 105 (apron) or 106 (ropes): the suplex out of the ring / over the ropes from the FAQ |
| Apron or ropes + **C-down** | states 0x0C / 0x0D (apron) or 0x0E / 0x0F (ropes): the FAQ's "ram the head into the turnbuckle" |
| Corner + A or B | slot 83 (weak), or 88 / 93 (up) / 98 (down) when strong |
| **C-down** (front only) | **Irish whip**: states 0x0A / 0x0B (`state_0A_whip_attacker` / `state_0B_whip_runner`) |
| Special allowed (strong, `SPECIAL_READY`, stick pressed) | own finisher: slot 58 front, 69 behind |
| `SPECIAL_READY` + **A and B together** | the **opponent's** finisher: slot 183 front / 184 behind of the *defender's* move set |
| Strong front B, for wrestlers with header trait bit 0 | states 0x44 / 0x45 |
| A or B otherwise | normal grapple move from `g_grapple_slots`: front by strength, button and up / down / none; behind by strength, button and "any direction" |

After a normal move is chosen:

1. `move_count_stats` updates the move statistics.
2. `grapple_check_reversal`: if the defender earned `CAN_REVERSE` (below) and the move is
   reversible (`MoveParams.flags & 1`), the defender gets the counter move instead (slot + 1 to
   4 from its own reversal table; slots 70/71 from behind, 185/186 in the corner) and the
   roles swap: the defender becomes the attacker in 0x42 and the attacker receives in 0x43.
3. If the move cannot be done here (`flags & 0x3000`): in the ring the pair goes to the
   states 0x46 / 0x47, where the attacker first walks the receiver to the right place (the
   ropes of that side for `flags & 0x2000`, see `movement.md`) and then does the move;
   elsewhere the slot 5 higher (the next input's move) is used instead. (These states were
   named "move fail" before this was read.)

## The defender's side (`grapple_defender_input`)

The defender gets **one** chance per grapple. The first time it presses A, B, L or R during
the lock-up or the window, a random roll is made from its own spirit (`chance_roll` kind 0,
table `0x800E6D60`):

| Defender spirit | Chance |
|-----------------|--------|
| 100 | 500 / 1000 |
| 81–99 | 333 |
| 61–80 | 250 |
| 31–60 | 125 |
| 11–30 | 62 |
| 0–10 | 31 |

A win sets `CAN_REVERSE` (`flags_lo & 0x100000`); the press is recorded either way
(`flags_lo & 0x200000`) so mashing does not help. The defender cannot try at all when the
attacker is in SPECIAL and the defender is not. A debug path also grants it when
`0x800F16E0 & 1` and the Z button is held on the defender's pad.

Reading the chance as "out of 1000" is a guess from the table values; `chance_roll` draws
from a 32-entry random pool at `0x800F05C8` that has not been decoded.

## Throwing out of the ring

There is no separate "throw out" action. Leaving the ring comes from the moves themselves:
the apron / ropes contexts use slots 105 and 106 (the suplexes to the floor in the FAQ), and
a move's animation sets the receiver's location class when it lands outside (see
`wrestler_apply_anim_location`).

## Starting a grapple (`state_3D_grapple_attempt`)

Pressing A in the neutral state enters **state 0x3D**:

1. **Weak or strong.** While the wrestler reaches (it can still walk and keeps turning toward
   the opponent), the game counts the frames A stays held (`0x800F06C0[slot]`) and collects
   the d-pad (`g_grapple_input`). **A held for 8 frames** (about a quarter of a second) sets
   `STRONG`; letting go earlier makes it weak. The FAQ's "hold for about a second" overstates
   it. A wrestler in SPECIAL gets `SPECIAL_READY`. From the apron there is no strong grab.
2. **The reach** (`grapple_reach_anim`, `0x800C5C7C`):
   - from the apron: 3365, with context `0x2000`;
   - at an opponent who is running off the ropes or an Irish whip
     (`flags_lo & RUNNING` and `run_flags & 0x120`): **catch the runner** (context `0x4`),
     move slot 78 (79 with a direction, +2 when strong), the FAQ's "as your opponent runs
     toward you, press A";
   - from the floor at an opponent in the ring, facing within 90°: 2956, the reach in
     (the FAQ's trip / drag to the floor);
   - otherwise 2933 strong or 2932 weak.
3. **Contact** (`grapple_contact`, `0x800C5210`), checked only inside the reach animation's
   active window (`anim` bytes `0x360`–`0x361`): a hand (joint 9 or 17) within **30 units** of
   a candidate's pelvis, **45** from the floor into the ring, or a point in front of the
   attacker within **56**. A runner-catch connects within **85** of the pelvis.
4. **What the grab becomes** (`grapple_connect`, `0x800C2A00`), in order:

   | Situation | Result |
   |-----------|--------|
   | attacker behind the defender (`sub_8009FEEC`) | `REAR` |
   | defender taunting (state 0x16) | defender loses 10 spirit, grab continues |
   | defender lying down | nothing |
   | defender up on the turnbuckle and attacker in the ring | pull him down: states 0x6D / 0x6E |
   | runner catch | states 0x48 / 0x49 |
   | defender dodging (state 0x53, first frames) | parried: attacker plays 2965 |
   | dazed defender who wins `chance_roll` kind 15 | he dodges into 0x53 instead |
   | **both grabbing at once** (defender in 0x3D with A held < 3 frames, or a dazed defender's lucky roll) | context `0x40000`: a mutual lock-up (2934/2935) that breaks straight away |
   | both reaching in the ring, inside each other's active window, both with **health ≥ 151** | **test of strength** (context `0x10000`), states 0x40 / 0x41 |
   | attacker on the floor, defender on the apron / in the ring | contexts `0x4000` / `0x8000`, states 0x6B / 0x6C |
   | otherwise (`grapple_lock`, `0x800C3228`) | a normal lock-up, states 0x3E / 0x3F |

   For the normal lock-up the context gets `CORNER` if the defender is resting in the corner
   after an Irish whip (state 0x13, step 3), `ATK_ON_APRON` if the **attacker is on the apron**,
   `DEF_ON_APRON` if the **defender is on the apron** (both apron cases drop `STRONG`), and `HOLD`
   when none of these apply. So a corner grapple only exists after whipping the opponent
   into the corner.

The grab links the pair (`GRAPPLE_LEAD`, `linked_idx`, `opponent_idx` / `partner_idx` both
ways) and copies the context to the defender without `STRONG`.

### Behind or in front (`relative_facing`)

A grab counts as **from behind** when the defender faces more than 90° away from the
attacker (the angle from the defender to the attacker). The attacker's own facing does not
matter.

### Double teams (`double_team_check`, `double_team_start`)

Before a normal lock-up, the game checks whether the target is **already held** by someone
else (state 0x3F in a front lock-up, less than 11 frames into the window, both attackers in
the ring or both on the floor, the new attacker not on the apron). If so the three go into
the double-team states: **0x68** the leading attacker, **0x69** the helper, **0x6A** the
receiver. The kind of double team is

| Bit | Positions |
|-----|-----------|
| `0x100` | both attackers in front |
| `0x200` | both behind |
| `0x400` | one in front, one behind |

and `0x800` / `0x1000` record which side each attacker stands on. The leader is the one whose
header trait bit 0 is set when only one of them has it. This matches the FAQ's "press A when
both wrestlers are next to your opponent; depending on your position and your partner's
position, different moves will occur".

The move comes from the **leader's** move set:

| Kind | Weak | Strong |
|------|------|--------|
| both in front | slot 107 | slot 108 |
| both behind | slot 109 | slot 110 |
| one of each | slot 111 | slot 111 |

The leader plays part 0 or 1 of that move depending on its side (`0x800`), the helper plays
the other part, and the receiver plays **part 2**, so double-team moves are three-part
animations. In the mixed kind the attacker in front plays part 0 and the one behind part 1.

Both attackers first slide into place over **10 frames** (`double_team_place`), relative to
the receiver's pelvis and facing:

| Kind | Position (sideways, forward) | Facing |
|------|------------------------------|--------|
| both in front | (±50, 80) | turned back toward the receiver, 150° / 210° from its facing |
| both behind | (±50, −80) | the receiver's facing ±30° |
| one of each | (0, 80) and (0, −80) | facing it, and the same way as it |

### R

R does not release a normal lock-up; that only ends by choosing a move or running out of
time. R is read in the **submission / rest hold** (`state_35_hold_attacker`), the **hold for a
partner** (0x64) and the **shoulder carry** (0x66), where it lets go. So the FAQ's "release the
grapple or a submission hold" means those holds.

## Playing the move (states 0x42 / 0x43 and their variants)

Once a move is chosen both wrestlers are linked (the attacker leads, `GRAPPLE_LEAD`), the
attacker plays move part 0 and the receiver part 1 of the same move-set slot. The pair states:

| Attacker / receiver | Used for |
|---------------------|----------|
| **0x42 / 0x43** | ordinary grapple moves (`grapple_choose_move`, reversals from the lock-up) |
| 0x48 / 0x49 | catching a runner (`grapple_connect`): the attacker switches from the catch slot to slot − 4 (the throw) |
| 0x4A / 0x4B | the move that ends a strike combo (state 0x44 / 0x45, slots 187–189) |
| **0x4C / 0x4D** | a **chained** second move (below), and the move that wins a test of strength |

Every frame both are kept snapped together (`pair_follow`, `0x800A483C`; skipped for the apron
and corner contexts, `grapple_ctx & 0x70`).

**The hit lands on the frame before the end** (`move_frames_left == 1`: `move_apply_hit`
with the attacker's move; 0x48 applies it on the last frame). At the end the receiver goes on by
pose (`wrestler_neutral_by_pose`, `knockdown.md`) and gets `move_aftermath_check` (bleeding,
TKO), and the attacker runs `move_followup`.

### Reversal in the middle of a move

If the receiver has `REVERSING` (`flags_lo` 0x80000, from its reversal try in the lock-up,
`grapple_defender_input`) and the attacker's animation reaches the move's **reversal frame**
(`AnimPlayer.reversal_frame`), the roles swap on the spot: the attacker takes
`REVERSAL_ATTACKER` (0x40000), switches to the receiver's part of its opponent's move and becomes
0x43; the receiver sees the flag, takes the lead and becomes 0x42 playing part 0. 0x4A / 0x4B
swap the same way (to 0x4B / 0x4A).

### What comes after (`move_followup`, `0x800A7300`)

Checked in this order when the attacker's move ends:

1. **Ends with the opponent's back turned** (last pose class 5 and no chained slot): the pair
   drops straight into a **rear grapple** (0x3E / 0x3F with `REAR`, keeping `STRONG`), with a
   90-frame input window for a strong grapple and 60 otherwise (2940 / 2941). So moves that spin
   the opponent round leave you holding them from behind.
2. **Move flags 0x3C00** (moves that end on the mat in a hold or a cover): both take the
   location of the last frame (`wrestler_apply_anim_location`), the receiver remembers the
   move, and the pair goes to **a pin (0x37 / 0x38) with flag 0x400**, otherwise to **a hold
   (0x35 / 0x36)**: the move turns straight into a pinning combination or a submission.
3. **A chained slot** (`MoveParams.next_slot`):
   - 123 / 124 → the attacker goes straight into a strong strike (0x1E);
   - 136–144 (the strike counters) → states 0x51 / 0x52 with the matching `state51_flags`;
   - anything else → **the chained move plays at once** as 0x4C / 0x4D (for example the rear
     slots 63–68 chain into their follow-ups).
4. Otherwise both go back to neutral (attacker to 0x27).

## Carrying the opponent (states 0x66 / 0x67)

1. **The lift** succeeds by `chance_roll` kind 12 (weight difference and the opponent's
   health; always while SPECIAL, `frame_loop.md`): 3404 / 3405. A failed lift is 3418 /
   3419 and the lifter takes generic move 37.
2. **Carrying** (3406 / 3407): the carrier walks with the d-pad (`carry_walk`); both have
   `flags_hi` 0x800, so the carried man's partner may come in to help.
3. The carried wrestler mashes to escape: `recover_count` = `recover_difficulty` kind 6
   (`(300 + carrier spirit / 2 − (health + spirit / 4)) / 2`). When it runs out it escapes
   with **its own slot 114**, taking the lead as 0x4C / 0x4D (the stick keeps it carried).
4. The carry ends with:
   - **R** (or the match being decided): put down (3408 / 3409);
   - **a strike by a third wrestler** on either (`strike_land` sets `_unk350` 0x10): both
     fall (3414–3417), the carried one taking the striker's hit;
   - **a partner's dive onto the carried opponent** (dive slots 181 / 182, `outside_ring.md`):
     3410 / 3411, the dive's damage on the carried wrestler. This is the tag-team
     "carry and dive" double team;
   - the carried man's escape (above).

## Test of strength (states 0x40 / 0x41)

Started by `grapple_connect` (`STRENGTH_TEST`, animations 2957 / 2958, a 10-frame lock-up):

1. Both push (2959 / 2960) and **mash A/B/L/R for 30 frames**; every press counts
   (`mash_count` mode 2).
2. The wrestler with less spirit (the defender on a tie) is pushed back with 2961 / 2962 while
   both keep mashing, **unless it has header trait bit 0** (the same trait that gives the
   strike combo), in which case this phase is skipped.
3. **More presses wins** (a tie goes to the attacker). The winner plays **slot 82 of its own
   move set** on the loser as 0x4C / 0x4D.

## Open questions

- ~~The FAQ's "release the grapple with R"~~ Confirmed 2026-11-01: `grapple_timeout` reads only A, B
  and C-down (0xC004) in a front grapple, and A / B behind (plus L / R in match types 0x600).
  There is no release button; the grapple ends by itself when the input window runs out (the
  break animations 2938–2954).

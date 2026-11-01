# World Tour: how a match ends

First pass (2026-10-30), read from the code: the end checks in `match_tick`'s FIGHT phase, the
pin and hold states, the TKO state and the result recorder. Not yet checked against a live
match.

## Overview

Every FIGHT frame `match_tick` runs, in order:

1. `referee_count_tick` (`0x800B2238`): the pin count.
2. `match_clock_update`: the time limit, which calls `match_end(0x4000)`.
3. `countout_tick`: the ring-out count, which calls `match_end(0x10000)` (`movement.md`,
   `match_state.md`).
4. `match_check_finish_flags(0, 0x40)` and `(0, 0x20)` (`0x800B29A0`): a wrestler whose
   state machine set `flags_lo` 0x40 (TKO) or 0x20 (submission) loses.

All of them end in `match_record_result` (`0x800B2BAC`), described below. A pinfall,
submission or TKO always means "this wrestler lost to the wrestler at its `partner_idx`
(+0x341)", the one it is linked to in the pin or hold.

## Rule switches (`g_match_rules`, `0x8006205A`)

`0x0033` in every match seen so far.

| Bit | Meaning | Where it is read |
|-----|---------|------------------|
| 0x01 | **TKOs allowed** | `move_aftermath_check` only rolls for a TKO with it set |
| 0x02 | **rope breaks**: a pin or a rope-break hold is broken when the defender is within reach (4.0) of the ropes | `state_32_pin_defender`, `state_36_hold_defender` |
| 0x04 | ring-out count off (and other changes to leaving the ring; maybe lumberjack) | `countout_tick`, `run_inputs`, `input_apron_moves`, `input_cup_ropes`, `state_09_turnbuckle` |
| 0x08 | a once-per-hold random give-up (chance 7) | `state_36_hold_defender`; off by default, meaning unknown |
| 0x10 | **submissions allowed** | `state_36_hold_defender` |
| 0x20 | **pins allowed**; off, every pin is kicked out after 5 frames | `state_32_pin_defender` |
| 0x40 | tag format: **tagging not required** (illegal partners may fight, `wrestler_may_act`) | `match_tick`, `wrestler_may_act` and other tag functions |
| 0x80 | tag elimination: the team loses only when both members are out | `match_end`, `wrestler_eliminate` |

That is the three finishes Andrew listed (pins 0x20, submissions 0x10, TKO 0x01).

`g_match_type` (`0x80062058`, singles 0x012F, tag 0x02C0): 0x100 one-on-one, 0x200 two teams,
0x400 everyone for themselves (the count-out code drops whoever is outside and carries on;
probably the 4-way / battle royal), **0x800 decide a time limit or double count-out on
health** instead of calling a draw. The low byte looks like a per-slot team mask (table
`0x800E7454`).

## Pinfall

### The cover

`state_31_pin_attacker` (also 0x37) and `state_32_pin_defender` (also 0x38). When the
defender's pin starts (step 0):

- `g_times_pinned[slot]` (`0x800F05A8`) is incremented.
- On the floor (`LOC_FLOOR`): `recover_count = 30` and the pin is **never counted**. So pins
  outside the ring do nothing but cost time.
- With pins off: `recover_count = 5`.
- Otherwise `recover_count = recover_difficulty(def, att, 1)` and, if this is singles or both
  are the legal men and the defender is not already out, `flags_lo |= 0x400` (**being
  counted**).

### Kick-out difficulty (`recover_difficulty` case 1)

```
k = 110 + att.spirit / 4 + 3 * g_times_pinned[def]
    - (health_eff(def) + def.spirit / 2)
if k >= 90 and def.spirit >= 90: k = 87
if g_ring_kickouts[def] < 2 and k >= 88: k = 87
k = max(k, 0)
```

`recover_count` is the number of frames until the defender kicks out. It drops by 1 every
frame, and more for mashing A/B/L/R (`mash_count`, `0x800A3B40`):

| Referee count | Presses per extra step | Extra per step |
|---------------|------------------------|----------------|
| before 1 | 4 | 1 |
| 1 | 2 | 1 |
| 2, first 16 ticks | 1 | 1 |
| 2, after 16 ticks | 1 | 2 |

`g_ring_kickouts` (`0x800F05B0`, s16 per slot) counts the defender's kick-outs in the ring:
it goes up when a kicked-out pin ends (`_unk350` 0x20 with 0x2) while the defender is LOC_RING.
So the 87 cap protects the **first two kick-outs**, not the first pin.

So mashing gets worth more as the count nears three. When `recover_count` goes below 0 the
defender kicks out (`_unk350 |= 2` on the attacker).

The count reaches three after 90 frames (below), so **a wrestler kicks out without pressing
anything whenever k < 90**. The 87 caps make the first two pins kicked out in the ring, and any pin on a
wrestler with 90+ spirit, an automatic kick-out.

### Seen in RAM (aki_debug, 2026-10-31)

Andrew's save of a CPU (slot 1, health 244, spirit 59) pinned by P1 (spirit 48), states
0x37 / 0x38, first pin: `k` works out negative, so `recover_count` started at 0, went below 0
on the second frame and the kick-out was decided at once (attacker `_unk350` 2); the referee
never started a count. The pin animations (3852 / 3853) still play out their 30 frames before
the kick-out animations (3854 / 3855) and `_unk350` 0x20. During those frames the CPU kept
mashing: its counter `0x800F06B0[1]` went 2 → 3 → 0 on the fourth press, and that frame
`recover_count` dropped by 2 instead of 1, matching "4 presses per extra step" before the
first count. `g_times_pinned` (`0x800F05A8`) and `g_ring_kickouts` were 1 afterwards.

### The referee (`referee_count_tick`)

- One count at a time: `g_ref_count_slot` (`0x800F16F3`, -1 = idle). A count starts only when
  no ring-out count is running (`g_countout_left` is still at its starting value) and some
  wrestler has `flags_lo & 0x400`.
- Each wrestler has a pin clock (`g_pin_clocks`, `0x800F16C8`, a `MatchClock` per slot) ticked
  at 30 per second. **The count number is its seconds field**: one count per second, and on
  each new number `g_ref_count` (`0x800F16F2`) is updated, shown (`sub_80097530`) and, when
  the pinned wrestler's health (+0x00C, the +0x7FC in the code) is under 50, a sound from
  `0x800E74F0` plays.
- At **3** the pinned wrestler loses by pinfall (result 0x2000).
- When the 0x400 flag goes away first (kick-out, rope break, release), the count stops. If it
  had reached 2 and 20 ticks (2.67 s) it is a near fall: `sub_80098B04` plus sounds 40 and 41
  (for a pinned wrestler under 50 health). Then the clock and the number reset.

### Other ways the pin ends

Attacker `_unk350` bits during the pin:

| Bit | Meaning | Attacker end animation (move part) |
|-----|---------|------------------------------------|
| 0x01 | released: R pressed, or the match is already decided (`g_match_flags & 0x40`) | 2 |
| 0x02 | defender kicked out | 2 |
| 0x04 | rope break (rule 0x02, defender in the ring, near the ropes) | 2 |
| 0x08 | the defender was eliminated (won the fall) | 4 |
| 0x10 | broken up: someone struck one of them (`strike_land`, `strikes.md`); also debug Z | 6 |
| 0x20 | the pin is over, play the end | |

The defender plays part 3 (kick-out), 5 (lost) or 7 (debug). Afterwards it goes to state 0x27
(recover) or, when the match is over, `sub_800A7944`; then `move_aftermath_check` can still
start bleeding or a TKO.

## Submission

`state_35_hold_attacker` / `state_36_hold_defender`. Holds use the move's `flags & 0x100` as
"can make the opponent give up".

- Escape difficulty (`recover_difficulty` case 3): `((300 + att.spirit / 2) - (def.health +
  def.spirit / 4)) / 2`, or a flat 60 while the defender is SPECIAL. Mashing A/B/L/R (one
  extra step every 4 presses) lowers it; below 0 the defender escapes.
- Every 31 frames the hold does damage (`move_apply_hit`).
- **Give-up**, while `recover_count` is 0–4 and the hold is still on, when submissions are
  allowed (rule 0x10), the defender is in the ring, the move can submit, it is singles or both
  are legal, and the defender is not already out:
  - spirit below 91, **the targeted body part's condition (+0x33C[region]) at 0 or below**, and
    `flags_hi & 0x1000`. That bit is set when the wrestler **escapes a hold in the ring** (or
    pushes the stick while its count is at 0) and is **never cleared** (`wrestler_update` clears
    other `flags_hi` bits every frame, not this one). So, like the first pin, **the first hold a
    wrestler is caught in can never make it give up**; only later ones can.
  - Or, once per hold at the first 31-frame tick with rule 0x08: spirit below 91 and
    `chance_roll(def, 0, 7)`.
- On a give-up the defender gets `flags_lo |= 0x20` and the attacker `_unk350 |= 8`; the next
  `match_check_finish_flags` makes it a submission loss (result 0x1000).
- Rope breaks (rule 0x02) work as in pins for holds with `flags & 0x200`.
- A wrestler with trait `0x1000000` in a hold with `flags & 0x8000` can **power out** (human:
  debug Z; CPU: `chance_roll(def, 0, 14)`, once per hold): spirit set to 100, `flags_hi |=
  0x8000`, `sub_8000BA20`.

## TKO

`move_aftermath_check` (`0x800CBC94`) runs on the receiver at the end of a move, pin or hold
(attacker, receiver, kind):

- **Blood:** receiver not bleeding, the move's `f07` non-zero, and `chance_roll(att, def, 4)`
  → state 0x26 (`state_26`, starts bleeding).
- **TKO:** rule 0x01, receiver not SPECIAL and not on the apron, `health_eff < 26`, the move's
  `f08` non-zero, and `chance_roll(att, 0, 5)` → state 0x25. In tag, if the attacker's
  `teammate_idx` is the receiver (hit by its own partner) `state25_flags |= 0x20`, which
  cancels the TKO.

So `MoveParams.f07` is a "can cause bleeding" value and `f08` a "can cause a TKO" value (they
also set the chance: TKO 13.3 % for f08 = 1 up to 66.6 % for f08 = 5, doubled for a SPECIAL
attacker; blood 1000 / (15 − def.header[0] − f07 − att.spirit / 30) ‰; see the table in `frame_loop.md`).

`state_25` (renamed `state_25_tko_check`) plays the stagger (3314, 3315 hit from behind, 3318),
a pose-dependent animation, then 3319/3320. During that, the wrestler is **saved**
(`state25_flags |= 0x10`) if it is on the floor or already out, is not the legal man in tag,
or **mashes A/B/L/R and wins `chance_roll(w, 0, 6)`**. Saved: 3327/3328 and back to recover.
Not saved: 3325/3326 and `flags_lo |= 0x40`, a TKO loss (result 0x8000).

## Count-out and time limit (`match_end`)

`match_end(reason)` sorts the wrestlers still in (not `flags_lo & 0x100`) into team A and B
(first and second wrestler in singles; the team mask in tag) and sums health + spirit per team
(averaged in tag). Then:

- **Count-out (0x10000)** — losers are the (legal) wrestlers on the floor:
  - singles: if both are outside, decide on health + spirit with `g_match_type & 0x800`
    (higher wins, tie `chance_check(500)`), else a draw; otherwise the one outside loses.
  - tag without elimination: the same with teams.
  - tag elimination (rule 0x80): the outside man is eliminated and their partner takes over
    (`tag_partner_takes_over`); the team loses when both are out; both teams outside at once
    gives result 0x20000.
  - 0x400 matches: whoever is outside is dropped; the last one left wins; all outside is a draw.
- **Time limit (0x4000)**: with `g_match_type & 0x800` (and not 0x400) the side with more
  health + spirit wins (tie: coin flip), else a draw.

## Recording the result (`match_record_result`, `0x800B2BAC`)

`match_record_result(winners[2], losers[3], result)`:

- Appends a 12-byte entry to `g_fall_history` (`0x80062020`, index `g_fall_count`
  `0x80062050`): match clock minutes and seconds, the loser's `last_move_value` (the move that
  finished them, or -1 when pinned) and the result word.
- `g_match_result` (`0x80062054`) = result | team bit (0x100 team B won, 0x200 team A, in tag)
  | each winner's bit from `0x800E745C` | each loser's bit from `0x800E7464`.
- Winners get `flags_lo |= 0x80`; losers get 0x100 (out) and lose LEGAL.
- `g_result_margin` (`0x800F16F4`) = average winner health − average loser health (not below
  0), scaled by a float (`0x800E84B4`).
- `result_announce` (`0x80098194`) picks the result banner (`0x800ED628`: 0 pinfall,
  1 submission, 2 count-out, 3 TKO, 4 time limit, 5 double count-out, 6 draw) and rings the bell
  (`sub_80098ACC` with 3, 5, 7 or 11).
- 90 frames after the decision `results_screen` (`0x80099D40`) shows it, reading the per-slot
  winner bits (1 << slot, table `0x800E745C`) and loser bits (16 << slot, `0x800E7464`) and the
  margin.

`wrestler_eliminate` (`0x800B31F8`) marks a loser out (0x100) and says whether that ends the
match: always in singles; in elimination tag only when the partner is also out, otherwise the
partner becomes legal and, if one of them is human, **the human takes over the partner**
(`tag_partner_takes_over` swaps the CPU flags); in 0x400 matches when one is left.

`match_pick_winner` later reads the bits back to choose the wrestler the end screen follows
(preferring a human when the winner is CPU) and, in tag, marks the winner's partner 0x80 too.

### Result word

| Bits | Meaning |
|------|---------|
| 0x100 / 0x200 | team B / team A won (tag) |
| 0x400 | decision (on health + spirit) |
| 0x800 | draw |
| 0x1000 | submission |
| 0x2000 | pinfall |
| 0x4000 | time limit |
| 0x8000 | TKO |
| 0x10000 | count-out |
| 0x20000 | both sides counted out |
| 0x100000 | the match is over (without it, only one man of a team is out) |

## Debug controls

`g_debug_flags` (`0x800F16E0`) & 1, cleared at match load and never set by the match code,
enables Z shortcuts: kick out at once, force the pin win, force a give-up, force bleeding, a
TKO or a power-out. Probably a developer or cheat option.

## For the port

- The pin count, kick-out formula, mash thresholds, give-up conditions and TKO checks are all
  deterministic apart from `chance_roll`; the numbers above are enough to reproduce them.
- The TKO, blood and give-up chances are the `chance_roll` kinds 4–7 and 14, tabled in
  `frame_loop.md`.

## Open questions

- How `flags_hi & 0x1000` gets set for a give-up in normal play (stick input, AI).
- Rule bits 0x04, 0x08, 0x40 exactly; the menu code that writes `0x80062058` / `0x8006205A`.
- ~~What the result margin shows on the results screen.~~ Read 2026-11-01: inside the match code
  it shows nothing. `results_screen` stores it in `0x800F01BA`, and the only other reader
  (`results_text_draw`) uses that word as "the results screen is up" (>= 0). If it is used, it
  is by code outside the overlay (championship / career). The scale is the double 255.0 at
  `0x800E84B0`.

# No Mercy: knockdowns, getting up, pins and holds

Read from the code (2026-11-01 / 02). Names in `symbols/nomercy/`; World Tour's equivalents are
`../wcw_world_tour/knockdown.md` and `match_end.md`. Frames are game frames (30 a second).

## escape_time (0x8011241C): every timer here

All in frames; negative results become 0; every op except 1 and 3 is **0 in finisher mode**
(flags_ee 0x04). w is the wrestler on the bottom, other the one on top.

| Op | Used by | Value |
|----|---------|-------|
| 0 | time down after a knockdown (0x31, 0x63, 0x70) | 3 × finisher_heat + 180 − (health + spirit) / 2 − 80 × down_count |
| 1 | the pin (0x45) | 95 + other.spirit / 5 + finisher bonus + 2 × w.pin_count − (w.spirit / 3 + w.health / 2) + max(0, damage_history_ratio − 15); 90+ with w.spirit 90+ becomes 87 |
| 2, 9 | 0x56 / 0x3F / 0x18 (2), 0x17 (9) | 100 − (spirit / 2 + health / 4) |
| 3 | the ground hold (0x49) | 60 in finisher mode, else op 6 |
| 4 | 0x15 | 180 − (spirit / 2 + health / 4); with count-outs on, 0 once fewer than 6 counts are left |
| 5 | escaping the rear lift (0x80) | (200 + other.spirit / 2 − (w.spirit / 4 + w.health)) / 2 |
| 6 | the carry (0x82) | (300 + other.spirit / 2 − (w.spirit / 4 + w.health)) / 2 |
| 7 | held upside down (0x34) | (100 − (spirit / 2 + health / 4)) × 4, × 8 against a strong hold (flags78 0x04) |
| 10 | hanging on the cage (0xC6, 0xCA) | (100 − (spirit / 2 + health / 4)) × 4 |

- **finisher_heat** (+0xBE) is set to 180 by a finisher (flags8c 0x800).
- **down_count** (+0xCE) counts knockdowns this match (state 0x31 adds one each time; two fewer after
  a finisher: the attacker's flags8c 0x04, moves 70 / 81). Standing clears it.
- **pin_count** (+0xBA) counts pins taken this match.
- The **finisher bonus** is 10 / 15 / 20 / 25 for finisher_heat up to 15 / 30 / 45 / more (0x801461E4).
- `damage_history_ratio`: the damage history (+0x344) holds 30 one-second entries of (hits, damage), each
  hit adding 1 and its damage to the current second (`damage_history_add`, from `apply_damage`);
  the ratio is total damage / total hits.
- In tag matches op 0 is scaled on the floor / apron (0x80150968 = 1.5 / 0x80150970).

Examples: the first knockdown at 100 health and 20 spirit gives 180 − 60 − 80 = 40 frames down; a
finisher on the same wrestler gives 540 + 180 − 60 + 80 = 740.

## Down: state 0x31 (`state_31_down`)

- **Step 0:** counts the knockdown (down_count + 1) and sets the time down (`escape` +0xB4 =
  escape_time op 0; 15 when the fall is already lost), and `chance_roll` kind 7 (by health and
  (100 − spirit) / 40) toggles flags8c 0x100, a choice to stay down.
- **Steps 1-2:** the lying pose (46 / 47 face up / down; 12877 / 12878).
- **Step 3:** escape counts down by 1 a frame and once more per mash (`recover_tick` / `mash_count`); at
  0 it gets up through **0x4F**, or rolls out of the ring (**0x99**) when it has lost a fall in the
  ring. It waits while another wrestler is down (`down_count_others`) or while in_byte is set (**the
  control stick pushed past 40** on either axis; `frame_loop.md`).
- Under the battle royal rule (g_match_rules 0x400) a wrestler down on the floor is eliminated
  (end_flags 0x10).
- Hit while down: **0x63** (by a weapon on the ground 0x28, or a dive landing), back to 0x31 after about
  35 VIs.

## Getting up: 0x4F, 0x4E and the get-up attack

- 0x4F (`state_4F_get_up`, read 2026-11-02):
  - step 0: the get-up animation (`get_up_anim`: 12153 face up, 12158 face down, 12220 / 12245
    slouched), count_timer = 60;
  - step 1: slides (8.0) with flags8a 0x6000; at the end, **holding R** (when `match_can_interrupt`
    allows) goes into the **crouch** (12171, step 2), otherwise step 3;
  - step 2, **the crouch** (up to 60 frames while R is held): it turns toward the nearest wrestler
    (113 / 512); **A starts a grapple attempt (0x50), B the get-up strike (0x25, move 217), L a roll
    backwards (12172)** (not for flags_e9 0x01 slots); letting go of R, or 60 frames, stands up;
  - step 3: stands from the crouch (12157), turning toward the opponent evenly over the animation;
  - at the end `get_up_finish` (below).
- **The get-up attack** (`get_up_finish`, confirmed): when the wrestler may act (or is on the floor),
  **B held goes straight into a strike** (0x22 at step 1; the get-up strike is state 0x25, move 217) and
  **A held into a grapple attempt** (0x50 at step 1); otherwise 0x30.
- 0xA7: staying down and breaking a pick-up by ducking back (12943 / 12950); 0x99 rolling away.
- **The TKO check, 0x2E** (`state_2E_tko_check`, read 2026-11-02; entered after `move_aftermath_check`
  sets the TKO stagger, rule 0x01 and health_eff < 26 in World Tour's terms):
  - step 0: the stagger, 12602, or 12603 + the move's reaction kind (table 0x8014503C), + 1 from behind;
    then down (step 2) with the lying pose (0x8014DA98 by end_pose), flags80 0x01;
  - step 3: `spirit_update` op 4 (**the meter empties for 300 frames**), 12611 face up / 12612 face down;
  - step 4: 12613 / 12614, **count_timer = 60**, a TKO counter (+0x58) + 1; when tko_flags 0x20 is set
    only a 50% roll allows a recovery try (state_counter 0x01);
  - step 5, 60 frames: with a recovery allowed, **each press of A / B / L / R rolls `chance_roll` kind 6** (1000 / (17, 17, 12 or 8 by attrs +7, plus 6 per
    earlier TKO try): 58-125‰ a press the first time);
    success sets tko_flags 0x10. At 0 (if `match_can_interrupt` objects, tko_flags 0x40 instead):
    recovered → 12617 / 12618, it gets up; otherwise 12615 / 12616, it fails;
  - step 6: failed and not blocked → **end_flags 0x40, the TKO fall** (under g_match_rules 0x80000 a fall
    is counted instead, `fall_count_add`), with the bell; blocked (0x40) → flags8c 0x800.

## Pins: 0x43, 0x44 / 0x4A and 0x45 / 0x4B

- **0x43** (`state_43_pin_start`): starting a cover on a lying opponent (0x36 / 0x42 step to its side
  first: `cover_side_pick`).
- **The pinner, 0x44 / 0x4A** (`state_44_pin_attacker`): the pin move is **292 + a per-move pin variant**
  (byte table 0x800A5720 + 548 × slot, indexed by the move just done), so each move has its own cover;
  flags80 0x140 while held. R (pressed 0x10) or `match_can_interrupt` lets go (state_flags 0x01, the
  defender gets 0x40); a lost fall (the defender's end_flags 0x01) marks the count done (state_flags
  0x08). The end animation is the pin's +2 (let go), +4 (count done) or +6 (kicked out); then 0x30.
- **The pinned wrestler, 0x45 / 0x4B** (`state_45_pin_defender`):
  - step 0: flags80 0x180, the pinned animation (state 0x45: part 1 of the pin move; under 0x45 with
    flags84 0x01 the pinner's animation + 1); **escape = escape_time op 1, at least 15**; pin_count + 1.
    Under g_match_rules 0x20, in the ring (or with rules 0x4000), with both wrestlers in play (flags_e9
    0x10), flags8a 0x400: the referee counts it.
  - every frame: escape − 1, and per mash **another 1, 2 or 3** (`mash_count`, below). Below 0 (and not held by in_byte) it **kicks out**: the pinner's state_flags 0x02, and the
    kicker takes generic move 19 (its spirit) through `spirit_exchange`.
  - **0x4A / 0x4B** are a pin reached as a chain follow-up: `move_chain_check` sends a chained move with
    move flags 0x200 to the pair 0x4A / 0x4B instead of the ground hold 0x48 / 0x49 (0x4A runs the pinner's
    handler, 0x4B the defender's). **0x4B** is a pin that also hurts: a per-slot frame counter (0x801591D0) applies the hold's damage
    every 31 frames (`apply_damage`).
  - **Rope breaks:** when `state_bounds` says the pin touches the ropes, under g_match_rules 0x02, in
    the ring: in a guest-referee match the pinner's state_flags 0x100 (the guest referee decides); else
    0x04 (broken) and the HUD message when both are in play.
- **The count** (`referee_count_tick`, `g_pin_counts` 0x8016B8F0): one count per **30 frames**
  (`clock_tick(.., 30)`); at **3** the pinned wrestler's end_flags gets 0x08 (pinned). A pin broken at
  2, 20 frames or more into it, plays the near-fall message. So a pin escape time of 90 frames or more
  means pinned; 87 (the 90-spirit cap) always kicks out just before three. In guest-referee matches the
  count does not run by itself.

## The stick: staying down (read 2026-11-02)

The control stick (`in_byte`, pushed past 40) is the taunt control standing up, and **the stay-down
control on the ground**: in 0x31 a pushed stick keeps the time down at 0 so the wrestler lies there
instead of getting up (unless it is out of the fall, flags_f1 0x01); in 0x45 / 0x4B, when the escape
time runs out while the stick is pushed, it is held at 0, so **the pinned wrestler does not kick out**
(letting a player stay down on purpose, at the cost of the fall); 0xA7 (staying down, breaking a
pick-up) starts its duck-back on it; and on the turnbuckle perch (0x0A) it sets flags84 0x04 (the
taunt up there).

## Mashing (`mash_count`, 0x80132C30)

Per slot: a press counter (0x80159420), a raw press counter (0x80159430) and a cooldown (0x80159440).

- A press of A / B / L / R only counts when the cooldown is 2 or more, so **presses must be at least
  2 frames apart**; the cooldown counts up every frame and resets on a counted press.
- Counted presses add up to a threshold set by the state: the ground grapple 0x47 needs **8**; the pin
  (0x45 / 0x4B) depends on the referee's count for this wrestler: **2 at count 0, 1 at count 1, and at
  count 2 every press, worth 2 or (16+ frames into the count) 3**; anything else **4**.
- Reaching the threshold resets the counter and returns 1 (one step), 3 (two) or 4 (three); the pin
  takes 1 / 2 / 3 frames off its escape time for them. So mashing gets stronger as the count goes on: at
  one a frame per two presses early, two or three frames per press at two.
- op 2 / op 3 count raw presses (no cooldown) for states that read the total.

## Holds: 0x48 / 0x49 (on the ground and in the corner)

(The states were named state_48_ground_hold / state_49_ground_held until 2026-11-03; they also run standing
holds: in RAM the corner moves 108 / 113, A / B at an opponent slumped in the corner, chain into holds 305 / 307,
record flags 0xE0240C: no rope break, no crawl, `tools/trace/scripts/nm_corner_lockup.json`.)

Read from the code and decompiled 2026-11-03 (`src/nomercy/nm_hold.cpp`), checked against the emulator
(`tools/trace/scripts/nm_ground_hold.json`, `nm_submission.json`; not replayed, since 0x46 / 0x47 are not ported).

- **How a hold starts.** A ground grapple (0x46 / 0x47; L or A near a lying opponent) plays one fixed
  move per position (`ground_grapple_start`: table 0x8014DC90 by head / legs, face up / down and sitting:
  240, 242, 248, 250 at the legs, 236, 238, 244, 246 at the head; the four ground specials 155-158 in
  finisher mode). The direction pressed does not change it. When that move (or any move) has record
  flags 0x400 / 0x800 / 0x1000, `move_chain_check` sends the pair to **0x48 / 0x49** (0x200 goes to the
  pin pair 0x4A / 0x4B). The hold's move is **292 + the move's follow-up variant** (byte table 0x800A5720
  by slot and move, the same table the pin reads): moves 292-322 of each move set are its pins
  (record flags 0x2300) and holds (0x2500, 0x2580, ...).
- In RAM (2026-11-03, the default move set of slot 0): the head move 236 (flags 0xA501) chains into
  hold 316 (0x2500: breaks at the ropes, no submission, no region damage), the leg move 240 into hold 318
  (0x2580: a submission on region 3, the legs, 0.7 off the region a tick).
- **Record flags the hold reads:** 0x02 the holder ends lying down, 0x04 the held wrestler stays down,
  0x08 flags80 0x400 on both, 0x80 a submission, 0x100 breaks at the ropes, 0x200000 no crawl,
  0x400000 no bounds while held (after it, only past the player's frame 0x11).
- **Escaping:** escape = escape_time op 3 (60 frames in finisher mode, else (300 + other.spirit / 2 −
  (w.spirit / 4 + w.health)) / 2: 85 and 77 at the start of the two holds in RAM), down one a frame and one
  more per four counted presses (`recover_tick`, `mash_count`); below 0 the holder's state_flags 0x02
  (mashed out). **The held wrestler's stick pushed keeps escape at 0**, so it never escapes (as in the pin).
- **Damage:** every **31 frames** (per-slot clock 0x801594C0) `hit_move_copy` + `apply_damage`.
  `apply_damage` uses health mode 1 in states 0x45 / 0x49 / 0x4B, and in 0x49 the region damage is
  raised by the two wrestlers' attrs +0x16 classes (float table 0x801461C0); `spirit_exchange` gives the
  receiver no low-health bonus there. In RAM the leg hold took 3-7 health a tick, all the way to 0.
- **Giving up:** after a damage tick, a submission (record flag 0x80) gives up when **region[hit_kind]
  (the hold's MoveRecord +0x0C) is 0 or below and spirit is 90 or less**, unless **the holder pushes its
  stick** (in 0x48 the stick sets state_flags 0x100, which blocks the give-up: the holder can keep a hold on
  for the damage). The holder gets 0x08, and with submissions on (g_match_rules 0x10), outside
  guest-referee matches, in the ring (or rule 0x4000) and both in play 0x408. 0x48 then writes the give-up
  as the whole word on both. The fall comes at the holder animation's event-2 frame (or its end): the bell
  and end_flags 0x20, or under rule 0x80000 (ironman: falls counted) `fall_count_add` and the region reset
  to 5.0, after which the held wrestler stands up through 0x30. A give-up that is not a fall sets flags8c
  0x800. **Checked in RAM 2026-11-03** (`nm_submission.json`, the leg region poked to 0.5 after 200 frames
  of the hold): at the next tick the region went to 0 (it does not go below), the holder's state_flags
  became 0x408 that frame and the held wrestler's the next; the holder's end animation started 27 frames
  later, the bell (state_counter 0x01) came at its event-2 frame, `match_end(0x20)` turned the cause into the
  fall the same frame (end_flags 0x01 on the loser, 0x04 on the winner), and the winner went into the
  post-match pose (0x1B steps 4-6) while the loser lay down (0x31).
- **Rope breaks:** under rule 0x02, in the ring, for record flag 0x100: if any of the held wrestler's 19
  body points (point_120 and joints[0..17]) or its model's 19 joint points (`pt_b_world`) has **|x| or |z|
  over 230** (the ring centre is 0), the hold breaks (the holder's 0x04 and the HUD message; in a
  guest-referee match 0x200, for the referee to call). Pressing A / B / L / R / a C button under rule 0x02
  calls `ground_hold_crawl`: with the holder in the ring both wrestlers move 2.5 toward the nearest ring
  side (x when |z| <= |x|), and d-pad right / left turn the pair by 11 (about 1 degree) round the holder.
- **Letting go:** R pressed (or a pause) sets 0x01 on both.
- **The end:** once anything ended it (bits 0x1F) and the hold's animation has run out, the holder plays
  its style animation + 6 (0x10: the held wrestler slipped the ground grapple, set in 0x47, or a third
  wrestler broke the hold up, `state_29_break_up`; seen in RAM when the opponent's ally ran in), + 4 (gave up)
  or + 2 (everything else; seen in RAM for a mash-out), the held wrestler + 7 / + 5 / + 3; then the holder
  goes to 0x30 (or lies down for record flag 0x02) and the held wrestler to 0x30 / down, with
  `move_aftermath_check`.
- `ref_count_reset(4)` at the start of a submission is a no-op in No Mercy (only kind 9 does anything).
- The CPU in the hold decides by chance each frame (AI mode 31).

## Open

- Decompiled and trace-checked 2026-11-02 (`src/nomercy/nm_down.cpp`): lying 0x31, getting up 0x4F,
  the counts (`down_count_allowed`, `down_count_others`), mashing and the escape times (ops 0, 1, 2, 8,
  9), the pin from a move (0x44 / 0x45, also 0x4A / 0x4B), 0x4E and 0x6B, through a roll-up and the
  strong grapple that rolls into a pin (`tools/trace/scripts/nm_pins.json`, `nm_pin_move.json`).
- ~~The cover from standing (L over a lying opponent, states 0x42 / 0x43) is not traced yet~~ (code and
  trace 2026-11-02, `tools/trace/scripts/nm_cover.json`): from a slam that puts both down, the lying
  wrestler holding the control stick keeps its down time reset (state_31_down step 3 zeroes `escape`
  while the stick is pushed), so the other can get up, walk over and cover. The approach is a
  quadratic curve through three control points (`ground_approach_plan`, Wrestler +0x264 / +0x2BC), the
  side by `cover_side_pick` (tables 0x80146330 / 0x8014D1D8), the cover 12358 + 2 * side (face up) or
  the turn-over 12256 + 10 * side first; the count then runs in 0x44 / 0x45. With the stick held
  through the count (`nm_count3.json`) the three lands: end_flags 0x08 on the loser, match_end records
  the fall (`match_rules.md`), the match goes to its end phases, the winner poses (the taunt state
  0x1B, steps 4-6; `nm_match_end.json`) and the results are copied out: a singles match replays end
  to end.
- ~~Holds and submissions (0x72-0x75) and the other escape-time ops~~ (the hold states are 0x48 / 0x49;
  0x72-0x75 are not holds): decompiled 2026-11-03 from the code with every escape_time op, checked against
  the emulator but not replayed (`src/nomercy/nm_hold.cpp`). A replay needs 0x46 / 0x47 (the ground grapple:
  `ground_grapple_defend`, `recover_tick_ground`, `ground_grapple_start`) first.

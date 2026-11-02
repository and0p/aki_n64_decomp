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
| 4 | 0x15 | 180 − (spirit / 2 + health / 4), only with count-outs on and 6+ counts left |
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

## Holds: the ground hold 0x48 / 0x49

- 0x46 / 0x47 step into place, then **0x48** (attacker, flags80 0x40) and **0x49** (held, flags80 0x80).
- The held wrestler escapes on escape_time op 3 (60 frames in finisher mode, else (300 + other.spirit
  / 2 − (w.spirit / 4 + w.health)) / 2), counted down by frames and mashing.
- `apply_damage` uses health mode 1 in states 0x45 / 0x49 / 0x4B, and in 0x49 the region damage is
  raised by the two wrestlers' attrs +0x16 classes (float table 0x801461C0); `spirit_exchange` gives
  the receiver no low-health bonus there.
- **Giving up** (read 2026-11-02): every **31 frames** the hold applies its damage again (a per-slot
  frame counter at 0x801594C0, `apply_damage`). After that, a hold whose move is a submission (move flags
  0x80) makes the held wrestler **give up when the region it works on (move +0x0C indexes `region[]`)
  is at 0 or below and its spirit is 90 or less**: the holder's state_flags 0x08, and with submissions
  allowed (g_match_rules 0x10), outside guest-referee matches, in the ring (or rules 0x4000) and both
  in play, 0x408: the submission fall (end_flags 0x20, `fall_count_add`).
- **Escaping:** escape (op 3) counts down by frames and `recover_tick`'s mashing; below 0 the holder's
  state_flags 0x02 (escaped).
- **Rope breaks:** under g_match_rules 0x02, in the ring, for moves with flags 0x100 (and not 0x400000):
  if any of the 20 joints of either wrestler is within 230 of the ring's edge in x or z, the hold breaks.
  Pressing A / B / L / R / a C button under rule 0x02 calls `ground_hold_crawl` (code 2026-11-02): with the
  holder in the ring both wrestlers move 2.5 toward the nearest ring side (the held wrestler crawls for
  the ropes), and d-pad right / left turn the pair by about 1 degree a frame round the holder.
- At the start of a submission hold outside guest-referee matches (move flags 0x80) the referee's count
  display is reset (`ref_count_reset(4)`).
- Rope breaks are `state_bounds` again; the CPU in the hold decides by chance each frame (AI mode 31).

## Open


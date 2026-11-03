# No Mercy: the CPU opponent

Read from the code (2026-11-02): the per-frame update, perception, situations, the mode table, the
deciders and the weighted tables; the modes from a profile of the buttons each writes. Names in
`symbols/nomercy/ai.toml` (all 145 modes and the AI helpers) and `bulk.toml` (deciders). World Tour's
equivalent is `../wcw_world_tour/ai.md`; No Mercy's AI is about three times the size.

## How it plugs in

- `wrestler_tick` runs `ai_update` (0x800F03F0) for a CPU slot every frame before the state handler.
- The CPU **plays the pad**: each mode writes buttons into its AI block (+0x0C held, +0x0E pressed,
  +0x12 / +0x13 stick) and `wrestler_tick` copies them into the slot's input entry. Everything the CPU
  does goes through the same input checks as a player (`movement.md`: standing's priority order).
- The AI block: `g_ai_blocks` (0x801596C0), 176 bytes per slot (World Tour 84, Revenge 96):

| Offset | Field |
|--------|-------|
| +0x00 / +0x04 / +0x08 | mode, step, a timer |
| +0x0C / +0x0E / +0x12 / +0x13 | the pad this frame |
| +0x18 | a mode timer |
| +0x1C | CPU level 0-4 (from 0x8009ED10 per slot; 5 → 3, 6 → 4) |
| +0x20 | **skill**: 100 / 300 / 500 / 750 / 1000 by level, ±20 at random, kept within 10-990 |
| +0x24 / +0x48 | the situation class of its own wrestler / of the opponent (`ai_situation`) |
| +0x28 | five perceived body regions |
| +0x3C / +0x3E / +0x4C / +0x4E | perceived own health / spirit, the opponent's |
| +0x42 / +0x54 | ring zones |
| +0x50 | perceived time down |
| +0x56 | the opponent's state when the mode was set |
| +0x60 | perceived distance (f32) |
| +0x6C | distances to the four ring corners |
| +0x80 | flags (0x01 / 0x02 from the tables, 0x04 / 0x08 focus, 0x40) |
| +0x82 | **where-class**: both wrestlers' places (below) |
| +0x84 | style / target bits |

## Each frame (`ai_update`)

1. Sets the AI globals: self (0x8016B8E8), opponent (0x80159618), partner (0x8016B92C), partner's
   opponent, opponent's partner, the AI block (0x8015A6F0).
2. **Perception** (`ai_update_perception`): every value the CPU reads goes through `ai_perceive`:
   error e = value × (1 − skill / 1000), at most value × a cap; result value + e × (r / 1000 − 0.5),
   not below 0. A skill-1000 CPU reads exactly; a skill-100 one is off by up to 45%. Health, spirit,
   distances, regions and time down are all perceived.
3. Situations: `ai_situation` maps each wrestler's state (and step / flags) to a class (enum
   `AiSituation`, 61 classes: STANDING 1, RUNNING 2, ROPE_REBOUND 4, GRAPPLED_HELD 6, STRIKING 8,
   GRAPPLE_ATTEMPT 11, LOCKUP 13, BLOCKING 14, HIT_WHILE_DOWN 15, DAZED 16, KNOCKED_BACK 17, OFF_BALANCE
   18, LYING 19, CORNER_SLUMP 20, DIVING 25, WHIPPING 28, WHIPPED 29, CATCHING_KICK 34, KICK_CAUGHT 35,
   CARRIED 38, COVER_APPROACH 53, PICKUP 54, PINNING 56, TURNBUCKLE 60 ...); state 0x30 keeps the last.
4. Copies **ten personality bytes** from the attribute record (attrs +25, +26, +30, +31, +34 to +39)
   into globals; they are trait **levels** that index small chance arrays in `g_ai_mode_tables`.
5. Runs `g_ai_modes[mode]` (0x80145A10, 145 entries, 111 functions). Mode 0 calls `ai_choose_mode`.
   `ai_set_mode` (and `_b`, `_now`, `_if_new`, `_arg`) sets mode, clears step and timers and notes the
   opponent's state. `ai_mode_history_push` keeps the last 10 modes (`ai_mode_recent` checks them, to
   avoid repeats).

Decompiled 2026-11-03 (`src/nomercy/nm_ai.cpp`: `ai_update`, perception, `ai_situation`, the mode
bookkeeping; the modes and deciders are not). Details the C settles:

- The perception caps: own health 0.1, the opponent's health 0.3, own spirit 0.1, the opponent's spirit
  0.2, distance 0.2, height difference 0.1, own regions 0.1, the opponent's time down 0.3. So the CPU
  reads its own state almost exactly at high skill and the opponent's health least reliably.
- The where-class bits (+0x82): self in the ring with the opponent in the ring 0x01, on the apron 0x02,
  elsewhere 0x04, on the cage wall 0x2000; self on the apron: 0x08 / 0x10 / 0x20 (opponent in the ring /
  on the apron / elsewhere); self on the floor: 0x40 / 0x80 (opponent in the ring / on the apron); self on
  the cage wall: 0x4000 / 0x8000 (opponent in the ring / on the wall); anything else 0x100.
- Overrides before the mode runs: running without a target for 151 frames, or a legal man on the apron
  for 151, sends the CPU back to mode 0; a mode that has run 300 frames is reset by a watchdog; an
  opponent taunting (0x1B / 0x1C) under health conditions starts a weighted pick (`ai_pick_mode_weighted_idle`).
- `ai_situation` gives lying (0x31) class 15 for steps 0-3 and 19 for 4-6; holds 0x46 / 0x48 are 57 and
  the held 0x47 / 0x49 7. **Checked in RAM**: 4,905 CPU readings in the 4-CPU tag save match the C mapping
  (the class is taken before the state handler runs, so it lags a state change by one frame).

Also in C (2026-11-03): `ai_choose_mode` (mode 0), `ai_decide_in_ring`, `ai_decide_standing`, mode 32 and the
helpers `ai_press_focus_switch` (renamed from ai_can_act: it presses C-right) and `ai_mode_recent` (renamed from
ai_move_listed: it reads the mode history). Mode 32 matches the 4-CPU recording (871 waits of 8-35 frames, every exit
explained). The recording uses 26 modes: 0, 32, 41, 53, 6, 15, 54, 45, 49, 59, 1, 9, 25, 30, 39, 10, 3, 29, 31, 47, 2,
48, 40, 119, 26, 33.

## Perception and the finisher plan

Read 2026-11-02. `ai_update_perception` stores noisy readings (`ai_perceive`, the noise by CPU level)
in the AI block: **+0x3C own health, +0x3E own spirit, +0x4C the opponent's health, +0x4E the
opponent's spirit**, and **+0x50 how long the opponent will stay down** (its escape counter +0xB4, read
only in the down / pinned / held states, 60 in some, 0 otherwise). The deciders compare these, never the real values.

- **The CPU's finisher** (`ai_state_init`): 0x8016B580[slot] = attrs +0x1C, or attrs +0x20 when that is 0
  (the wrestler's finisher move numbers; Austin's +0x1C is 70, the front-grapple finisher; The Rock's
  +0x20 is 155), or move 236 for wrestlers whose move animation resolves to 15933.
- **How the finisher is done: AI block +0x84** (`ai_style_class`, 0x800FB35C, run at set-up): the finisher
  move number picks one of 26 classes and the class's bits from 0x80145924 go to +0x84. Classes by move:
  5-20 → 0x0105, 25-65 → 0x4105, 70 → 0x410D, 73-76 → 0x0115, 77-80 → 0x4115, 81 → 0x411D, 86 / 90 →
  0x1105, 88 / 92 → 0x5105, 94 → 0x110D, 104 → 0x1805, 105 → 0x0805, 108 / 113 → 0x2105, 118 / 123 →
  0x6105, 128 → 0x610D, 130 / 132 → 0x2115, 134 / 136 → 0x6115, 138 → 0x611D, 186-190 → 0x0205,
  192-198 → 0x1405, 200 / 202 → 0x2405, 205 → 0x1205, 256 → 0x0025, 257 → 0x002D, 273 → 0x0026,
  275 → 0x002E, 278 → 0x1025. Read as: **0x4000 a strong grapple, 0x2000 a running move, 0x1000 done
  on a staggered or lying opponent, 0x0010 from behind, 0x0008 the finisher slot itself (70, 81, 94, 128,
  138, 257, 275), 0x0100 a grapple, 0x0200 / 0x0400 strike kinds, 0x0020 from the turnbuckle or apron
  side (256-278)** (meanings from the move numbers; the low bits 0x05 / 0x06 are in every class).
  `ai_decide_finisher`, `_vs_lying_c`, `ai_lockup_special` and the in-ring main decider test these bits to
  set up the right situation for the finisher.
- **The plan to finish** (`ai_choose_mode`): when the CPU can act, standing (situation 1 or 43), the
  opponent looks beaten (perceived health **below 30** and spirit **below 50**) and it has a finisher,
  0x8016B718[slot] = 1, otherwise 0. `ai_decide_in_ring_main`, `ai_decide_special`, `ai_finisher_check`,
  `ai_decide_vs_lying`, the grapple modes and `ai_pick_grapple_move` read it to steer toward the
  finisher (`ai_decide_finisher`, below).

## Choosing (mode 0 → `ai_choose_mode`, 0x800F0EF0)

1. **Focus:** in multi-wrestler matches a worn-down CPU (hits taken while hurt, +0x5E, re-rolled 0-7)
   switches to the nearest opponent attacking it (+0x80 0x04 / 0x08); modes 121 / 122 press C-right.
2. Unless its own situation is STANDING or 43 it only reacts: in states 3 / 4 for 151+ frames it stops;
   on a ladder it goes to mode 136 or 138.
3. Standing: `ai_press_focus_switch` (state 0x01, 0x0A (perched on the turnbuckle) with +0xF4 bit 2, or 0x4F at step 2) must agree; then, in the
   ring arenas (g_arena < 10), the **decider for the where-class** (+0x82):

| +0x82 | Where | Decider |
|-------|-------|---------|
| 0x01 / 0x02 | both in the ring / CPU in, opponent on the apron | `ai_decide_in_ring` |
| 0x04 | CPU in the ring, opponent on the floor | `ai_decide_ring_vs_floor` |
| 0x08 | CPU on the apron, opponent in the ring | `ai_decide_apron_vs_ring` |
| 0x10 | both on the apron | `ai_decide_apron_vs_apron` |
| 0x20 | CPU on the apron, opponent on the floor | `ai_decide_apron_vs_floor` |
| 0x40 / 0x80 | CPU on the floor, opponent in the ring / on the apron | `ai_decide_floor_vs_ring` |
| 0x100 | both on the floor, or anything else | `ai_decide_floor` |
| 0x2000 / 0x4000 / 0x8000 | the cage wall (location 0x400): opponent up / CPU up / both (the states they check are the cage family 0xC4-0xCF) | `ai_decide_vs_cage_wall`, `_cage_wall_vs_ring`, `_both_cage_wall` |

   Backstage (g_arena ≥ 10) uses `ai_decide_backstage`; on raised places (location 0x3C0)
   `ai_decide_both_raised` / `_self_raised` / `_opponent_raised`; `ai_think` only under g_match_type
   0x100000; mode 144 (chase the last one) under elimination when it has lost a fall.
4. Unless g_match_rules 0x2000, an opponent down (situations 15-20) nearby gets a C-left (drag / flip).

### The in-ring decider (`ai_decide_in_ring`, 0x800F1740), in order

Checked line by line 2026-11-02:

1. Tag logic (`ai_decide_tag`) and `ai_wants_tag_out` → mode 61.
2. **Opponent on the apron:** with AI flag 0x40 and farther than **170** → evade (10). Otherwise, unless
   the over-the-top rule: 20% × a +38 chance 0x801452E0 (**0, 10, 35, 50, 15**), with neither mode 33
   nor 34 among its last 10 modes → **go out after it (36)**; when the trait roll fails or 33 / 34 were
   used, farther than 170 → 10. (A failed 20% roll or rule 0x400 goes straight on.)
3. **By the opponent's situation:** diving (25-26) → `ai_decide_wait_or_engage`; slumped in the corner
   (20) or running (30) → `ai_decide_in_ring_main`; carried (38) → `ai_decide_ropes` (if it acts);
   situation 40: beyond **170** `ai_try_grapple(1)`, 130-170 walk up (48), within **130** a +37 chance
   0x801452D4 (**800, 600, 500, 400, 200**) → follow-up strikes (9), else engage (1).
4. **Opponent on the apron and the CPU in finisher mode:** beyond 130 walk up (48), else grapple (2).
5. `ai_vs_runner_armed`.
6. **Multi-wrestler matches (g_match_type 0x1000 / 0x2000):** with fixed teams (0x1000) it watches its
   partner (slot ^ 2), in tag matches (0x2000) the AI partner (0x8016B92C); in other multi matches every other wrestler in turn.
   When that wrestler is in the ring, in a strong grapple attempt (state 0x50, flags78 0x04), not both in
   finisher mode: if it is going for the CPU's own opponent and that is within **160** → engage (1);
   otherwise, within 160 of the pair and in the same place, a chance = skill (half the skill unless the
   target is down, situations 15-19) → `ai_press_focus_switch` (which also writes C-right, 0x0001, to the AI pad's
   pressed buttons: switching focus to the pair). **So the CPU breaks up or joins a grapple near it.**
7. **Under g_match_type 0x10000**, with the opponent down (15, 16, 19) and its own perceived spirit 71 or
   more: when the opponent will stay down longer (AI +0x50) than 0x801452EC[its perceived health / 100]
   (**20, 15, 10, 10, 5, 5 frames** for health 0-99 ... 500+) and mode 19 is not among its last 5 →
   mode 127 (a walk).
8. Then `ai_decide_vs_down`; an opponent in finisher mode → `ai_decide_change_area`; then
   `ai_decide_standing`.

### The in-ring main decider (`ai_decide_in_ring_main`, 0x800F75A8)

From `ai_decide_in_ring` for an opponent slumped in the corner (situation 20) or running (30); first pass
2026-11-02, the order is approximate:

- Opponent in the corner and the CPU in SPECIAL → taunt (50) into finisher mode.
- With the finish plan (0x8016B718) set, by the finisher class (AI +0x84): 0x0400 → a grapple move (51); else
  by distance beyond **170** run (40), beyond **130** walk up (48), then mode 94 or engage (1) by the
  opponent's facing (flags8a 0x01).
- In the corner case: tables 0x80145734 (**90% grapple move (51), 10% run (40)**) or 0x80145740 (30% 51,
  30% 40); a +37 chance 0x80145764 (**0, 100, 150, 200, 300**) → fetch a weapon (39); 30% → mode 20 (pick
  up); a hurt CPU (health below 91 / 40) with a +39 chance 0x8014577C (**150, 75, 0, 0, 400**) → wait (49).
- Last: table 0x8014571C (**50% strike (6), 50% follow-up strikes (9)**) or 0x80145728 (**50% engage (1),
  50% grapple (2)**).

### Ring vs floor (`ai_decide_ring_vs_floor`, 0x800F2BBC)

The CPU in the ring, its opponent on the floor (first pass 2026-11-02):

- Tag logic first (`ai_decide_tag`). A spare slot (no flags_e9 0x10) with its partner-flags 0xC0 set
  goes 50 / 50 to mode 41 or 32.
- In tag matches, when all three others are on the floor and modes 33 / 34 were not used lately, it may
  go straight out (mode 36).
- Two chances by **trait +38** (weapons and fighting outside): "go out" s2 = 0x80145400[level] = **50,
  100, 400, 600, 100** (+150 under g_match_rules 0x4000) and "attack from the ring" s0 = 0x801453F4[level]
  = **200, 200, 200, 200, 600**; both × 1.5 when the CPU is in finisher mode and × 0.5 when the opponent
  is.
- An opponent carried (38) → `ai_decide_ropes`. Having just been in a lock-up (history) with an opponent
  not in finisher mode skips to going out.
- With a skill roll, by the opponent's situation (11-19): a downed opponent → chance s0 (+200 with
  `ai_trait_flag2`) to pick a move from the floor lists (`ai_list_pick_floor_a` / `_b`; dives over the
  ropes), halved for some situations; a close opponent within 160 → mode 44 (run at); 30% → the ring-edge
  decider (`ai_decide_ring_edge`).
- Otherwise: chance s2, and modes 33 / 34 not used lately → **mode 36, go out after it**; chance → mode 49
  (wait on the apron); else table 0x801453E0 (35% approach, 35% back off, 30% wait).

### Against a diving opponent (`ai_decide_wait_or_engage`, 0x800F7BFC)

For an opponent in situation 25-26 (diving or about to; read 2026-11-02): a failed skill roll waits (55).
Farther than **380**: a +39 chance 0x80145784 (**400, 200, 0, 0, 1000**) → mode 49 (wait, a taunt in
SPECIAL). Then by the dive and where the CPU stands: near the corner the dive aims at → step away (56); a
skill roll → walk in to block (86, which moves on to the dive block 47), else back off (45); within **200**
→ walk out of the corner (57 / 58) or 86; finally a +35 chance → block the dive (47), else 86 or wait (55).
So a skilled CPU with a high +35 blocks dives, and +39 level 4 just waits (taunts) when the diver is far.

### Floor vs ring (`ai_decide_floor_vs_ring`, 0x800F4164)

The CPU on the floor, its opponent in the ring or on the apron (read 2026-11-02). In order:

1. **The count-out check** (`ai_decide_floor_timeout`, below); if it set a mode, done.
2. Tag logic (`ai_decide_tag`).
3. An opponent in situation 25-26 → `ai_decide_wait_or_engage`.
4. History checks (`ai_history_count`) and a 50% chance, then a `rand_pool_next` roll split at 200 / 400.
5. Opponent within **160**: a chance by trait +38 (0x8014550C: 200, 400, 600, 800, 400) and the
   opponent's flags pick strike modes **9** or **6**, or engage (**1**); against an opponent that is
   attacking, chance → block **46** or **47**; otherwise table 0x801454E8 (**65% approach (41), 35% wait (32)**).
6. Beyond **220**, chance → mode **10** (evade).
7. Not holding a weapon (flags_ef 0x01) and not +0x94 bit 0: chance 0x801454F4[+38] (**0, 0, 0, 500, 0**)
   × the weapon attribute's factor (attrs +6: 0, 0.3, 0.6, 1.0), and with AI flag 0x20 only where
   `apron_walk_dpad` gives a direction → mode **101** (go to another area).
8. Chance 0x80145500[+38] (**600, 500, 350, 300, 400**) → back to the ring: 50 / 50 mode **33** or **34**.
9. Not armed: chance 0x80145530[+39] (**100, 50, 0, 0, 0**) → mode **38**.
10. Otherwise table 0x801454C8: **35% approach (41), 10% wait (32), 15% mode 33, 15% mode 34, 25% fetch
    a weapon (39)**.

**The count-out check** (`ai_decide_floor_timeout`, 0x800F6154): only for a wrestler in play (flags_e9
0x10) on the floor (location 0x04) of a ring arena (g_arena < 10), not +0x94 bit 0. By the count-out
option (0x800A9462):

- **Off (0):** the CPU keeps a time outside (min / sec, per slot) and compares it with a limit by trait
  +38: 0x80145554 = **15, 20, 30, 45, 20 seconds** (with g_match_rules 0x4000: 0x80145564 = **30, 45, 60,
  90, 45**). From 9 seconds before the limit on, chance 500 plus the difference grows; then chance 1000
  (AI flag 0x100) or 500 picks mode **33**, else **34**. An AI flag 0x100 with flags_ee 0x04 (finisher mode)
  skips it.
- **10 counts (1) / 20 counts (2):** a trait threshold, 0x8014526C = **4, 4, 2, 1, 4** (10 counts) or
  0x80145278 = **10, 8, 3, 1, 10** (20 counts); once `g_countout_left − (threshold − 1)` is below 5:
  with fewer than 3 counts left mode **34** at once; otherwise chance 1000 / that difference, then
  farther than **530** from the ring mode 34, else 50 / 50 mode 33 or 34.

So trait +38 level 3 (the brawler) stays out longest (one count left with count-outs on, 45 / 90 seconds
without), and level 0 / 1 come back early.

### Both on the floor (`ai_decide_floor`, 0x800F49C8)

In order (read 2026-11-02), the first that acts wins: `ai_tag_reach_check` / `ai_tag_reach`; under
g_match_type 0x8000 `ai_tag_extra`; **carrying in state 0x81** farther than 130 from the chosen corner
(AI block +0x40): the d-pad toward that corner (`dir8_to` 0x8014D1A8, d-pad bits 0x8014D258), so it walks
the carried opponent there; the count-out check (`ai_decide_floor_timeout`); `ai_tag_partner_check`,
`ai_tag_help_check`, `ai_vs_runner_armed`; an opponent in finisher mode (flags_ee 0x04) →
`ai_decide_change_area` (get away); `ai_decide_floor_b` (same zone: modes by traits +38 × attrs +6);
then the shared `ai_decide_standing`.

### On the apron, opponent in the ring (`ai_decide_apron_vs_ring`, 0x800F356C)

First pass 2026-11-02 (mostly tag logic, which is where an apron wrestler usually is):

1. Over-the-top rule and in play → mode 33. Then `ai_tag_partner_check`, `ai_tag_help_check`; an
   opponent being carried (38) → `ai_decide_ropes`; `ai_tag_reach_check` / `ai_tag_reach`; under
   g_match_type 0x8000 `ai_tag_extra`; with AI flag 0x20 `ai_decide_apron_side`.
2. A jump table on the opponent's situation (2-26):
   - an opponent working on this CPU (its +0x08 is the CPU's slot): chance = skill, then **45% back
     in (33)**, else mode 7 (hold R for 1-13 frames: a block);
   - an opponent down or hurt (with chances by +39, and finisher mode): mode 49 (wait on the apron)
     through `ai_mode_recent`, or `ai_try_corner_climb` at 50% (a top-rope try) in tag matches;
     some situations go to mode 2 (grapple) or 67;
   - near (within **160**, by trait +37 chances): table 0x8014543C (**20% strike (6), 20% engage (1),
     60% grapple (2)**) or 0x80145450 (20% / 20% / 10% grapple, the rest a mode not yet read);
   - an attacking opponent → `ai_decide_wait_or_engage`.
3. Otherwise 50% the 33 / 34 rotation, else mode 35; a wrestler in play → 33.

### On the apron, opponent on the floor (`ai_decide_apron_vs_floor`, 0x800F312C)

Read 2026-11-02, in order:

1. Over-the-top rule (g_match_rules 0x400) and in play → mode 33. Then `ai_decide_tag`.
2. AI flag 0x20 with a direction from `apron_walk_dpad` → mode 35. An opponent in situation 38 (carried)
   → `ai_decide_ropes`. AI flag 0x20 → `ai_apron_walk`, else mode 60 (a random walk).
3. Count-outs on, not rules 0x4000, in play, **fewer than 5 counts left → mode 33** (back in).
4. Chance = skill (AI block +0x20), else the 35 / 36 rotation (or 104). Two trait +38 chances: "attack"
   0x8014541C = **200, 200, 200, 200, 600** and "go down after it" 0x80145428 = **105, 200, 450, 700,
   150** (+150 under rules 0x4000).
5. **Different zones:** the go-down chance → the 33 / 34 / 35 rotation; else 33 (in play), or with AI flag
   0x20 `apron_walk_dpad`.
6. **Same zone, opponent down** (situation 15-19): the attack chance, +200 with `ai_trait_flag2` (for a
   lying opponent, situation 19, first a +39 chance 0x80145434 = **400, 200, 0, 0, 200** for mode 49,
   waiting on the apron, unless in finisher mode, and +300 with the trait flag) → 50 / 50 `ai_decide_apron_jump` (a dive off the
   apron) or `ai_try_corner_climb`; then the 33 / 34 / 35 rotation; else 35 / 36 or 104.

### Both on the apron (`ai_decide_apron_vs_apron`, 0x800F473C)

Read 2026-11-02. Under the over-the-top rule (g_match_rules 0x400) a wrestler in play goes to mode
**33** at once. Then `ai_decide_tag`. The apron modes 33 / 34 / 35 / 36 (33 holds the d-pad toward the ring
centre and presses C-up, so it gets back in; 34 holds C-down the same way, climbing the corner from the
apron; 35 drops off the apron to the floor (not with few counts left or the opponent in the ring); 36 goes through the ropes toward an area; first read of 33 / 34) are
picked through `ai_mode_recent(mode, 10)`, which is true when the mode is among the slot's last 10 modes
(0x8014521C + 20 × slot), so the CPU rotates between them. The trait chance is 0x80145548[+38] = **50, 100, 300,
500, 100**.

- **Same zone, within 170:** 30% a strike (mode 6); with AI flag 0x20 on the apron `ai_apron_walk`, else
  30% the trait chance for 33 / 34 / 35 (35 / 36 / 33 when it fails), else 15% mode **56** (step away).
- **Same zone, farther:** the trait chance for 33 / 34 / 35; else 15% mode 56; else 35 / 36 / 33.
- **Different zones:** the trait chance for 33 / 34 / 35; else a wrestler in play takes 33.

### Opponent up on the cage wall (`ai_decide_vs_cage_wall`, 0x800FB64C)

Read 2026-11-02. Modes 121 / 122 press **C-right**, which in a cage match starts the cage climb (0xC8):

1. CPU in SPECIAL and the opponent hanging (0xC6) → taunt (50).
2. Farther than **200**: a failed skill roll walks up (48); in finisher mode with the opponent not yet
   climbing (0xC4) → **climb after it (121)**; else 50% climb (122), 50% evade (10).
3. Near: a failed skill roll does nothing; beyond **160** walk up (48). A +35 chance 0x80145984 (**900,
   750, 600, 0**) scaled by +37 (0x8014598C: 0.4, 0.55, 0.7, 0.85, 1.0): against an attack from the top
   (0xCF) → block (131), else table 0x80145958 (45% walk in to block (86), 45% back off (45), 10% wait (55)).
4. An opponent climbing (0xC4) → walk (123).
5. The opponent still low on the wall (height difference below **180**): idle on the cage (0xCC) → the
   +35 chance and then its scaled version give hold R (7), else block (47). In finisher mode, unless mode
   33 was among its last 5 modes (`ai_mode_recent`, the mode history at 0x8014521C) → walk (128). A +34
   chance 0x8014596C (**400, 500, 600, 700, 800**) → engage (1); within **130**, 1000 minus that → strike
   (6). With its own perceived spirit (AI block +0x3E) 51 or more → walk (128); below 51 it goes on to 6.
6. Higher up (or from 5): a chance 0x80145978[+37] (**500, 400, 300, 200, 100**, +500 in finisher mode);
   an opponent climbing (0xC4) → walk (123); a hanging opponent (0xC6) skips the chance. Failed → 123.
   Then with its own perceived spirit below 51 → 123; else 123 if mode 33 was among its last 5, otherwise
   128. (Checked line by line 2026-11-02.)

**The CPU up on the wall, opponent in the ring** (`ai_decide_cage_wall_vs_ring`, 0x800FBA08): at a corner
zone (`zone_is_turnbuckle_a`) walk (129); a failed skill roll walks along the wall (124, up / down to the
zone flags). Within **160**, a small height difference (< 180) and within **130** of an opponent who is
down (situations 15-20) and not lying flat (flags80 0x05): a chance 0x801459A0[+37] (**900, 800, 700,
600, 500**) × 0x801459AC[+34] (1.0 down to 0.6) → **B (132), an attack from the wall**. An opponent within
130 starting to climb (0xC8), with the CPU's perceived spirit (AI block +0x3E) below 10: a skill roll
**presses A**
(knocking the climber down); otherwise 124. **Both up on the wall** (`ai_decide_both_cage_wall`, 0x800FBBC0, checked line by line 2026-11-02). The
fallback is walking along the wall: 129 at a corner zone (`zone_is_turnbuckle_a`), else 124.

1. A failed skill roll → the fallback.
2. Within **130** of an opponent idle on the wall (0xCC): a +35 chance 0x801459CC (**800, 650, 500, 0**)
   → hold R (7).
3. The attack chance is 0x801459C0[+37] (**1000, 900, 800, 700, 600**) × 0x801459D4[+34] (1.0, 0.95,
   0.9, 0.95, 0.8); **+300** against a climber (0xC4), and against a hanging opponent (0xC6) +300 when
   its own perceived spirit is 51 or more, −150 when below 20.
4. **Same zone:** a failed attack roll, or farther than **250** → the fallback. Within **140** with a
   height difference under **180** → B (132). With its own perceived spirit 20-80 → the fallback.
   Otherwise a height difference up to 180 → mode 126; more → 124 when it is the lower of the two
   (joint +0x124 height), else 125 (climbing toward the opponent).
5. **Different zones:** an opponent climbing (0xC4), or the CPU lower than the opponent → the fallback;
   higher → 125 at the other corner type (`zone_is_turnbuckle_b`), else 130.

### On the raised places (`ai_decide_both_raised` / `_self_raised` / `_opponent_raised`)

Read 2026-11-02; the three are the same apart from one mode. **In finisher mode** the CPU goes for the
table: beyond **130** it walks up (48 when both are up, 58 when only one is), within 130 it grapples (2),
so the finisher lands on the table and breaks it (`objects_and_weapons.md`). Otherwise
`ai_vs_runner_armed`, an opponent in finisher mode → `ai_decide_change_area`, then `ai_decide_standing`.

### Backstage (`ai_decide_backstage`, 0x800FBF54)

Used for every position when g_arena is 10 or more (first pass 2026-11-02). In order: `ai_decide_tag`;
`ai_wants_tag_out` → mode 61; a few AI-flag cases with a **170** band (20% plus a +38 chance for the 33 / 34
rotation, else 36; mode 10 evade); `ai_decide_wait_or_engage`; the in-ring main decider and
`ai_decide_ropes`; within **170** `ai_try_grapple(1)`; within **130** a +37 chance → strike follow-up (9),
else walk up (48) or grapple (2); `ai_vs_runner_armed`; in tag-style types (g_match_type 0x1000 / 0x2000)
a partner within **160** → engage (1); then `ai_decide_vs_down`, an opponent in finisher mode →
`ai_decide_change_area`, and `ai_decide_standing`. So backstage the CPU fights much as in the ring, with
no apron or floor logic.

### At a distance (`ai_decide_vs_down`, 0x800F7DB0)

Only when the opponent is farther than **200** (read 2026-11-02; the name is older and loose). The size
class is attrs +0x15 / 3 (0-3, light to heavy):

1. Farther than 250: chance 0x801457B0[+39] (**200, 100, 0, 0, 300**) → mode **49** (wait).
2. An opponent down (situation 15, 16 or 19), or the CPU in SPECIAL or finisher mode: chance
   0x801457A8[size] (**300, 200, 100, 0**) → **run at it (40)**, else walk up (48). Heavier wrestlers run less.
3. In a tag match (g_match_type 0x2000, not rule 0x40) after the first minute, with the CPU in the ring,
   its partner's opponent on the apron, both in play and the opponent on its own side: chance = skill, then
   the size chance + 450 → 75% run (40), else `ai_try_grapple(1)`.
4. `ai_decide_vs_down_b`.
5. An opponent in finisher mode → mode **41** (approach).
6. Farther than 250 and in the ring: chance size-chance / 5 → run (40).
7. Otherwise table 0x80145794: **40% approach (41), 50% fetch a weapon (39), 10% back off (45)**.

### Both on the floor, the trait choices (`ai_decide_floor_b`, 0x800F812C)

From `ai_decide_floor` (read 2026-11-02). Only after a skill roll (AI block +0x20), and in different
floor zones only beyond **200** (in the same zone the same list runs with a **250** band). By trait +38
unless noted, the first that passes wins:

1. (Different zones) both with a lost fall and in facing zones → approach (41).
2. Not armed: 0x801457CC (**0, 250, 500, 0, 0**) × the weapon attribute's factor (attrs +6) → mode 101
   (another area); then a +39 chance 0x801457F0 (500, 250, 0, 0; level 4 reads into the next table) →
   mode 38.
3. 0x801457D8 (**500, 300, 100, 50, 300**) → 50 / 50 mode 33 or 34.
4. 0x801457E4 (**100, 200, 450, 600, 150**) → run (40).
5. Else table 0x801457B8: **30% approach (41), 15% wait (32), 55% fetch a weapon (39)**.

So the brawler levels (+38 = 2, 3) run around and go to other areas, and levels 0 / 1 / 4 mostly take
modes 33 / 34.

### Specials and finishers (`ai_decide_special`, 0x800F89D0)

Read 2026-11-02, in order:

1. With flags8c 0x04 (outside g_match_type 0x100000): `ai_finisher_check`.
2. Not in finisher mode, when the opponent will stay down a while (AI +0x50, the perceived time it has
   left down, **61 or more**) or is in the TKO stagger (0x2E): a +39 chance 0x80145814 (**500, 250, 0, 0, 236**; halved while the finish plan is on)
   → in tag matches in the ring 5% the special move (113), else mode 49 (a wait of up to 60 frames).
3. The opponent in the TKO stagger (outside type 0x100000, not in finisher mode): a +37 chance
   0x80145808 (**500, 400, 300, 200, 100**) → wait (55), letting the TKO count run.
4. In the ring with the finish plan on → `ai_decide_finisher`.
5. In SPECIAL → taunt (50) into finisher mode. Otherwise `ai_decide_move_list`.

So the CPU builds toward a finisher in stages: pauses (+39 levels 0, 1, 4) and the TKO wait, a taunt at
SPECIAL to enter finisher mode, then, once the opponent looks beaten, its finisher.

### Going for the finisher (`ai_decide_finisher`, 0x800F8C04)

Read 2026-11-02. In SPECIAL it taunts (50). In finisher mode, while the meter timer (+0xB0) is under
**150**, it first tries `ai_finisher_check`. Then, with a finisher (0x8016B580) set:

1. A finisher class (AI +0x84) set → `ai_try_pick_up` first.
2. **261** → `ai_try_corner_climb`; **262** (only in finisher mode) → `ai_try_corner_climb`; **274** →
   `ai_try_ground_move(2)`.
3. Farther than its reach (AI +0x68 beyond +0x7C) → walk up (48).
4. **Ground finishers 209-216** → modes 209 → 73, 210 → 74, 211 → 78, 212 → 79, 213 → 75, 214 → 76,
   215 → 80, 216 → 81 (the ground-move modes).
5. Unless another wrestler is down nearby (`ai_third_party_active`): **236-250 (even)** → mode 110 (C-left:
   drag or turn the opponent over) with the follow-up mode 236 → 82, 238 → 83, 240 → 84, 242 → 85,
   244 → 96, 246 → 97, 248 → 98, 250 → 99; for 236 with a wrestler whose move animation is 15933, only
   with its own perceived spirit 70 or more.
6. Anything else (the grapple finishers 70 / 81 ...) returns 0, so the normal deciders walk in and grapple
   and the lock-up code (`ai_lockup_finisher`) picks it.

### A lying opponent (`ai_decide_vs_lying` 0x800F9E40, `_b`, `_c`; first pass)

Read 2026-11-02:

- **`ai_decide_vs_lying`** (checked line by line 2026-11-02): in the ring with the finish plan on → `_c`
  (if it acts); in SPECIAL → taunt (50); not in finisher mode and the opponent down for **81 frames or
  more** (AI +0x50) → a +39 chance 0x8014585C (**500, 250, 0, 0, 900**) → in tag matches in the ring 5%
  the special move (113), else wait (49); an opponent flat on its back (19) farther than **130** → walk up
  (48); otherwise `_b`.
- **`_b`** (checked line by line): after a skill roll, a level: an opponent looking healthy (perceived
  health 100 or more) uses +25 at 40%, otherwise a random 0-2; a hurt one uses +26 at 40%, else random. On
  the floor level 2 becomes a random 0 / 1. **In finisher mode:** at level 2 with the meter timer over
  300 and move 257 in its set → mode 65 at 450‰ (200‰ unless that move is preferred); else farther than
  **130** → walk up (48), flat on its back (19) → wait (55), otherwise grapple (2). **Not in finisher
  mode:** level 0 → `ai_decide_vs_down_far`, 1 → `_near`, 2 → move list D.
- **`_c`** (the finish plan on a lying opponent): flat on the back (19) → wait (55) or walk up (48) beyond
  130; in SPECIAL → taunt (50); a finisher-slot class (+0x84 0x08) only in finisher mode. Finisher 257 or
  275 (finisher mode) → `ai_try_corner_climb` / `ai_try_mode_67`; 273 → `ai_try_mode_67`; 256 →
  `ai_try_corner_climb`; 105 → mode 5. Beyond **130** → walk up (48). Class 0x1000 (on a staggered or
  lying opponent): with AI flag 0x10 and the angle right (`ai_faced_by`) engage (1) or grapple (2,
  class 0x4000), else mode 100; by number 104 → mode 4, 192 → 11, 194 → 12, 196 → 13, 198 → 14, 278 →
  `ai_try_mode_89`. Class 0x2000 (running): the same angle check → 1, else 100. Class 0x0100 → 1 / 2 by
  angle and 0x4000. Class 0x0200 → follow-up strikes (9).

- **`ai_decide_vs_down_far`** (checked line by line): flat on its back (19) → wait (55). Farther than
  **200**: 60% × a +37 chance 0x80145864 → evade (10). An opponent looking healthy (100 or more) → 50 / 50
  strike (6) or follow-up strikes (9). A hurt one: in the ring, 25% of the time, the first of moves 192,
  194, 196, 198 that is preferred (move flags 0x8000) gives **mode 11, 12, 13 or 14**; otherwise 90%
  strike (6), 10% follow-up (9). A strike beyond 200, or follow-ups beyond 200, walk up (48) first.
- **`_near`**: flat (19) → wait (55). Healthy → 50 / 50 engage (1) or grapple (2). Hurt: in the ring at
  25%, move 104 preferred → **mode 4**, move 105 → **mode 5**; otherwise 90% engage (1), 10% grapple (2).
  Engage beyond **160** or grapple beyond **130** walk up (48) first.

So +25 / +26 pick **how** a lying opponent is worked on (0 from a distance, 1 up close, 2 from list D),
while `ai_decide_vs_down_b` uses level 2 of the same traits as the "keen" switch.

### Moves on a downed opponent (`ai_decide_vs_down_b`, 0x800F87A8)

Read 2026-11-02. After a skill roll, for an opponent in situation 15, 16 or 19 (on the ground): the
chance is **500 when the deciding trait is at level 2, else 50**. The trait depends on the opponent's
perceived health and situation: healthy (100 or more) uses +30 (situation 15) or +25 (16 / 19); hurt
uses +31 or +26. Then situation 15 → list C (`ai_list_pick_c`), 16 / 19 → list D; in finisher mode a
wrestler with move 262 (has-move 0x400 in +0x3C) or 257 (0x200) first tries `ai_try_corner_climb` (a
top-rope move: mode 65 when the opponent lies near a corner). So **+25 / +26 / +30 / +31 are on-off switches**: level 2 means "works on a downed
opponent" (10 times as often), any other level hardly does.

### Far from the opponent (`ai_decide_far`, 0x800F7318)

From `ai_decide_standing`: SPECIAL (flags_ee 0x02) → mode **50** (the taunt into finisher mode).
Farther than **170**: chance = the CPU skill (AI block +0x20) → `ai_try_grapple(1)`, else mode **48**
(walk up to it). Nearer than **160**: chance 10 × 0x801456C4[trait +34] (**800, 600, 500, 400, 200**) → table
0x801456AC (**60% strike (6), 40% strike follow-up (9)**), else table 0x801456B8 (**60% engage (1), 40%
grapple (2)**). In between: mode 48.

### The standing decision (`ai_decide_standing`, 0x800F1EA4)

Shared by the in-ring, floor, backstage and raised-place deciders; checked line by line 2026-11-02.
"skill" is a chance_check(skill) roll. **"Armed"** means the CPU holds a weapon (flags_ef 0x01) with
trait +38 at **3**, or at **2** only when both are on the floor (where-class 0x100). Situation numbers
are the opponent's (AI +0x48).

**Close (perceived distance below 170):**

1. Armed: an opponent in a move (flags80 0x01) → wait (32). Otherwise skill → against a downed opponent
   (15) walk up (48) when it is out of reach (AI +0x68 beyond +0x7C), else the weapon strike (22); against
   anything else 70% follow-up strikes (9), 30% strike (6). A failed skill roll: skill again → approach
   (41), else wait (32).
2. By situation: **8 (striking) / 11 (grapple attempt)** → a chance = skill (for 11: × 1.2 against a
   running or strong attempt, flags78 0x44, × 0.8 otherwise) → `ai_decide_close` (11) or
   `ai_decide_opponent_focused` (8); failing that, skill → table 0x801452F4, else approach (41).
   **15** → `ai_decide_special`; **16, 19, 22, 23** (down, lying) → `ai_decide_vs_lying`; **17**
   (knocked back) → `ai_decide_far`; **18** (off balance) → `ai_decide_far_b`; **24** (getting up) →
   `ai_decide_close_b`; **45** (on the receiving end of a move) → wait (32); 2 (running) does nothing here.
3. Any other situation: the opponent on the apron → table 0x80145330; in a move → wait (32); else
   `ai_decide_engage_chance`.

**Far (170 or more):**

1. An opponent in a move: skill → walk up (48), else wait (32).
2. Armed: an opponent down (15) → walk up (48); skill → evade (10), else table 0x80145318.
3. skill → by situation: **8 / 11** as above (`ai_decide_close` / `_opponent_focused`); **15, 16, 19**
   (down): in SPECIAL taunt (50); in the ring `ai_decide_vs_down_b`; otherwise (or when that does nothing)
   walk up (48) and go on to 5; **17, 22, 23** → `ai_decide_far`; **18** → `ai_decide_far_b`; **24** →
   `ai_decide_close_b`; others: the opponent on the apron → table 0x80145330, else on to 5.
4. (A failed skill roll in 3 goes to 5.)
5. skill × a +37 chance 0x80145354 (**200, 200, 150, 100, 100**) × 10% → `ai_try_grapple(1)`.
6. Else the meter: in SPECIAL or finisher mode (flags_ee 0x06) → table 0x80145348, otherwise table
   0x80145318.

### Close quarters when the opponent targets the CPU (`ai_decide_close`, 0x800F6D30)

Read in order 2026-11-02. Only when the opponent's target is the CPU. A base chance c = T36[trait +36]
× F37[trait +37] with T36 (0x80145648) = 900, 600, 300, 0, 500 by level and F37 (0x80145634) = 0.85,
0.95, 1.0, 1.05, 1.1.

1. The opponent is running at it (flags78 0x40), a skill roll passes and it is within 170: chance c →
   mode 42 (evade the grapple with L), else mode 21 (L counter).
2. Beyond 200: 30% → `ai_try_grapple(2)`, else table 0x80145580 (25% strike 6, 25% mode 9, 10% run at
   44, 20% approach 41, 20% wait 32).
3. Beyond 175: chance c → table 0x801455CC (10% engage 1, 5% grapple 2, 25% strike 6, 20% mode 9, 20%
   approach 41, 20% L counter 21), else 0x801455A0 (20% engage, 10% grapple, 30% strike, 20% mode 9, 10%
   approach, 10% run at).
4. Within 175: c' = (c + its perceived own spirit) × 1.2 × F37; a CPU of level 5-6 facing a run-in
   wrestler (flags_e9 0x08) whose perceived health is 41+ above its own → table 0x80145620 (20% mode 42,
   50% mode 43, 30% L counter); otherwise chance c' × T36 → 0x8014560C (40% evade 42, 40% mode 43, 20% L
   counter), else 0x801455F8 (50% strike 6, 20% mode 9, 30% L counter).

Checked line by line 2026-11-02: all of the above matches the code. One detail: step 4's last roll is
chance_check(c' × T36), a product of two per-mille values, so it passes for every level except +36 = 3
(T36 = 0, or c' = 0). **So +36 level 3 always takes 0x801455F8 (strike first), the others the evade /
counter table 0x8014560C.**

So close in, the personality decides between striking first and waiting to evade or counter.

### The CPU in the lock-up (mode 25, `ai_mode_25_lockup_choose`)

Read 2026-11-02 (the start; the choice itself is long):

- **Wait:** base frames from 0x80145D84 by the CPU wrestler's size class (attrs +0x15 / 3): 15, 15, 15,
  25, 30; a spread from 0x80145D90 (5, 5, 5, 5, 7); 10% of the time 15 ± 5 instead. The wait is base −
  spread + rand(2 × spread), so a heavy wrestler waits longer before choosing. Against an opponent in
  finisher mode it is 15 ± 7. The timer runs only while its own situation is LOCKUP (13).
- Then a slot flag (0x8016B718) can hand over to `ai_lockup_special`; a strong front lock-up with trait
  bits (attrs +0x3C) and a 20% roll tries the finisher path; an opponent slumped in the corner (flags78 0x100) and the
  apron (0x200) have their own branches.
- **Move lists** (`ai_lockup_move_list` returns one, despite its name): pairs of (move number, the
  buttons to press), e.g. front weak 0x80145D9C: 1 A, 2 A + left / right, 3 A + up, 4 A + down, 5 B, 10
  B + left / right, 15 B + up, 20 B + down; front strong 0x80145DCC (25-60, the same buttons; 0x80145DEC
  adds 65 for wrestlers with attrs +0x34 bit 0x02); rear 0x80145DBC (73 A, 74 A + any direction, 75 B, 76
  B + direction) and strong 0x80145E10 (77-80); the corner moves (an opponent slumped in the corner) front 0x80145E20 / 0x80145E28 (108 / 113, 118 /
  123) and rear 0x80145E30 / 0x80145E38 (130 / 132, 134 / 136). So the CPU picks a move number and
  presses the same buttons a player would (`lockup_input`).

### Weighted tables (`ai_pick_mode_weighted`)

The choices are data: 6-byte entries {u16 cumulative chance out of 1000, s16 mode, u8, u8 bits for
+0x80}, scanned until the threshold reaches a random roll. Every table the code points at (2026-11-02;
chance %: mode):

| Table | Used by | Picks |
|-------|---------|-------|
| 0x80145298 | `ai_update` | 45: 41, 45: 45, 10: 32 |
| 0x801452AC | `ai_update` | 15: 49, 45: 41, 30: 45, 10: 32 |
| 0x801452F4 | standing, close | 30: 6, 10: 9, 30: 1, 10: 2, 10: 41, 10: 32 |
| 0x80145318 | standing, far | 35: 39, 45: 41, 10: 45, 10: 32 |
| 0x80145330 | standing, opponent on the apron | 10: 39, 40: 41, 40: 45, 10: 32 |
| 0x80145348 | standing, far, SPECIAL / finisher mode | 65: 39, 35: 48 |
| 0x801453CC | armed vs a runner | 40: 44, 40: 86, 20: 41 |
| 0x801453E0 | ring vs floor | 35: 41, 35: 45, 30: 32 |
| 0x8014543C | apron vs ring | 20: 6, 20: 1, 60: 2 |
| 0x80145450 | apron vs ring | 20: 6, 20: 1, 10: 2, 20: 41, 30: 32 |
| 0x801454C8 | floor vs ring | 35: 41, 10: 32, 15: 33, 15: 34, 25: 39 |
| 0x801454E8 | floor vs ring | 65: 41, 35: 32 |
| 0x80145580 | close decider, beyond 200 | 25: 6, 25: 9, 10: 44, 20: 41, 20: 32 |
| 0x801455A0 | close decider, beyond 175 | 20: 1, 10: 2, 30: 6, 10: 9, 10: 9, 10: 41, 10: 44 |
| 0x801455CC | close decider, beyond 175 | 10: 1, 5: 2, 25: 6, 10: 9, 10: 9, 20: 41, 20: 21 |
| 0x801455F8 | close decider, within 175 | 50: 6, 20: 9, 30: 21 |
| 0x8014560C | close decider, within 175 | 40: 42, 40: 43, 20: 21 |
| 0x80145620 | close decider, level 5-6 behind on health | 20: 42, 50: 43, 30: 21 |
| 0x80145650 | opponent focused | 50: 6, 15: 9, 15: 9, 20: 46 |
| 0x80145668 | opponent focused | 50: 46, 25: 47, 5: 44, 10: 32, 10: 41 |
| 0x801456AC / 0x801456B8 | far | 60: 6, 40: 9 / 60: 1, 40: 2 |
| 0x801456D0 / 0x801456DC | far (b) | 80: 6, 20: 9 / 80: 1, 20: 2 |
| 0x801456F4 / 0x80145708 | close (b) | 25: 45, 45: 41, 30: 55 / 20: 45, 40: 41, 40: 55 |
| 0x8014571C / 0x80145728 | in-ring main | 50: 6, 50: 9 / 50: 1, 50: 2 |
| 0x80145734 / 0x80145740 | in-ring main | 90: 51, 10: 40 / 30: 51, 30: 40, 20: 48, 20: 41 |
| 0x80145794 | vs a downed opponent | 40: 41, 50: 39, 10: 45 |
| 0x801457B8 | both on the floor | 30: 41, 15: 32, 55: 39 |
| 0x80145958 | vs the turnbuckle | 45: 86, 45: 45, 10: 55 |

Modes: 1 engage, 2 grapple, 6 strike, 9 follow-up strikes, 21 L counter, 32 wait, 33 / 34 apron, 39
fetch a weapon, 40 run, 41 approach, 42 evade a grapple, 43 (apron to floor), 44 run at, 45 back off, 46
block, 47 block a dive, 48 approach a downed opponent, 49 / 50 apron wait / taunt, 51 grapple move, 55
wait long, 86 approach then block.

## The CPU's move lists (`ai_lists_init`, 0x800FB038)

At AI set-up `ai_list_build` copies six lists from fixed data, keeping each entry whose **required bit**
is set in the wrestler's attrs +0x38 (0 = always kept) and marking entries whose move has move flags
0x8000 (the CPU prefers those). Each kept entry is 4 bytes (u16 move, u8 preferred); a list with nothing
kept gets −1. The counts go to 0x801594E0-0x801594EA. Read 2026-11-02:

| List (per slot) | From | Entries (bit → move number in the slot's move set) | Picked by |
|-----------------|------|-----------------------------------------------------|-----------|
| 0x8016B658 + 48 × slot | 0x80145FB0 | always: 236, 238, 240, 242, 244, 246, 248, 250, 155, 156, 157, 158 | `ai_list_pick_b` (counts the moves ≥ 236) |
| 0x8016B940 + 32 × slot | 0x80146010 | always: 209, 210, 213, 214, 211, 212, 215, 216 | `ai_list_pick_a` (400 / 1000 filter) |
| 0x80159630 + 16 × slot | 0x80146050 | 0x40 → 261, 0x2000 → 269, 0x4000 → 270, 0x40000 → 274 | `ai_list_pick_c` (the turnbuckle aerials 269 / 270 among them) |
| 0x8016B620 + 12 × slot | 0x80146070 | 0x20 → 256, 0x80000 → 278, 0x20000 → 273 | `ai_list_pick_d` (278 is the running move at a run's end) |
| 0x8016B300 + 16 × slot | 0x80146088 | 0x100 → 264, 0x200 → 265, 0x400 → 266, 0x1000 → 268 | `ai_list_pick_floor_a` (dives to the floor) |
| 0x80159680 + 16 × slot | 0x801460A8 | 0x80 → 263, 0x200 → 265, 0x400 → 266, 0x1000 → 268 | `ai_list_pick_floor_b` |

**Picking within a list** (`ai_list_filter`, read 2026-11-02): each picker splits the list into the
preferred moves (move flags 0x8000) and the rest; with both kinds present it takes a random preferred move
at **40%** (`ai_list_pick_a`, `_b`, `_floor_a`) or **50%** (`_c`, `_d`, `_floor_b`), otherwise a random
other one (always preferred when nothing else is in the list). The move then becomes a mode by its number
(for example list A's 209-216 → mode 22 when close, 48 to walk up first, or 77), or goes through
`ai_try_corner_climb`, `ai_try_ground_move` or `ai_decide_ring_edge`.

So **attrs +0x38 bits mean "the wrestler has this move"**: `moveset_unpack` sets each bit from the move
set (`damage_and_spirit.md`), and the same bits gate both the player's inputs (`movement.md`: 0x4000,
0x10000) and the CPU's lists. A list entry with bit 0x2000, say, is kept only for a wrestler with move 269. The move
numbers index the slot's 548-entry move set, so what each one is comes from the wrestler's data.

## Personality traits (attrs +25 … +39)

Levels that index chance arrays (readings from where each is used; mostly guesses):

Some arrays read so far (per mille by trait level 0, 1, 2, 3, 4): +37 → 0x801452D4 (follow-up strikes in
the ring): 800, 600, 500, 400, 200; +37 → 0x80145354 (a far grapple try): 200, 200, 150, 100, 100; +38 →
0x801452E0 (going out after an opponent on the apron): 0, 10, 35, 50, 15. So a low level is not always
"less": each trait's levels are styles, not a scale.

Trait +38 (outside fighting) arrays found 2026-11-02, levels 0-4:

| Array | Use | Values |
|-------|-----|--------|
| 0x80145400 | in the ring, go out after a floored opponent | 50, 100, 400, 600, 100 (+150 rules 0x4000) |
| 0x801453F4 | in the ring, attack a floored opponent from the ring | 200, 200, 200, 200, 600 |
| 0x80145428 | on the apron, go down after it | 105, 200, 450, 700, 150 (+150 rules 0x4000) |
| 0x8014541C | on the apron, attack it | 200, 200, 200, 200, 600 |
| 0x8014550C | on the floor, strike when close | 200, 400, 600, 800, 400 |
| 0x80145500 | on the floor, head back in | 600, 500, 350, 300, 400 |
| 0x801454F4 × attrs +6 | on the floor, go to another area | 0, 0, 0, 500, 0 |
| 0x80145548 | both on the apron, the apron-mode rotation | 50, 100, 300, 500, 100 |
| 0x801457CC × attrs +6 | both on the floor, another area | 0, 250, 500, 0, 0 |
| 0x801457D8 | both on the floor, modes 33 / 34 | 500, 300, 100, 50, 300 |
| 0x801457E4 | both on the floor, run | 100, 200, 450, 600, 150 |
| 0x8014526C / 0x80145278 | counts left before heading back (10 / 20 counts) | 4, 4, 2, 1, 4 / 10, 8, 3, 1, 10 |
| 0x80145554 / 0x80145564 | seconds outside without count-outs (normal / rules 0x4000) | 15, 20, 30, 45, 20 / 30, 45, 60, 90, 45 |

The other traits' arrays found so far (levels 0-4; read 2026-11-02 with their deciders):

| Trait | Array | Use | Values |
|-------|-------|-----|--------|
| +34 grapple | 0x801456C4 × 10 | near the opponent, strike rather than grapple (`ai_decide_far`) | 800, 600, 500, 400, 200 |
| +34 | 0x8014596C | engage an opponent low on the cage wall | 400, 500, 600, 700, 800 |
| +34 | 0x801459AC (factor) | attack down from the cage wall | 1.0, 0.9, 0.8, 0.7, 0.6 |
| +35 defence | 0x80145984 (× a +37 factor) | block or answer an attack from the cage top | 900, 750, 600, 0 |
| +36 close range | T36 0x80145648 | the close-quarters decider (`ai_decide_close`) | 900, 600, 300, 0, 500 |
| +37 aggression | 0x801452D4 | follow-up strikes in the ring | 800, 600, 500, 400, 200 |
| +37 | 0x80145354 (× 10%) | a far grapple try | 200, 200, 150, 100, 100 |
| +37 | 0x80145764 | fetch a weapon at a cornered opponent | 0, 100, 150, 200, 300 |
| +37 | 0x8014598C (factor) | scales the +35 cage-top chance | 0.4, 0.55, 0.7, 0.85, 1.0 |
| +37 | 0x80145978 | climb the cage wall after the opponent (+500 in finisher mode) | 500, 400, 300, 200, 100 |
| +37 | 0x801459A0 | attack down from the cage wall | 900, 800, 700, 600, 500 |
| +39 specials | 0x8014577C | a hurt CPU waits (taunts) at a cornered opponent | 150, 75, 0, 0, 400 |
| +39 | 0x80145784 | wait (taunt) against a far diver | 400, 200, 0, 0, 1000 |
| +39 | 0x801457B0 | wait (taunt) at a distance | 200, 100, 0, 0, 300 |
| +39 | 0x80145434 | wait on the apron | 400, 200, 0, 0, 200 |
| +39 | 0x80145530 / 0x801457F0 | mode 38 on the floor (evade unless in a meter mode) | 100, 50, 0, 0, 0 / 500, 250, 0, 0 |

**Trait reads inside the modes** (read 2026-11-02; the deciders' arrays are above):

| Mode / helper | Trait | What it does |
|---------------|-------|--------------|
| `ai_try_grapple` | +34 | grapple-try chance 0x801453C0: 800, 600, 500, 400, 200 (with 800 / 200 fallbacks) |
| `ai_try_mode_89` | +37 | 0x80145574: 10, 10, 15, 20, 20 |
| `ai_lockup_ropes` | +38 | toward the ropes from the lock-up: 0x80145E40 (0, 50, 100, 150, 50) → 102; 0x80145E4C (1000, 800, 250, 150, 400) → 103 |
| 29 (struggling in a lock-up) | +36, +37 | float factors 0x80145ECC / 0x80145EB8 on the struggle chance |
| 69 (holding a caught kick) | +35 | waits 0x80145EF8 (15, 35, 60, 0) + up to 0x80145F00 (5, 10, 15, 0) frames |
| 95 (answering a runner) | +35 | 0x80145F08: 400, 250, 100, 0, 0 |
| 32, 41 (wait, approach) | +38 = 3 | while holding a weapon, the grapple try (20% / 40%) is skipped unless the opponent is down (situation 15): this level keeps fighting with the weapon |
| 51 (grapple move) | +38 = 3 | armed, it always takes the d-pad branch (0x0F00) instead of the 50% split |
| 33 (apron) | +38 = 3 | a special case in its first step |
| 38, 49, 62, 65, 113 | +39 = 2 | level 2 skips the evade, the taunt into finisher mode, and the special-move and runner options: **+39 level 2 never taunts for SPECIAL** |
| 110 (C-left turn) | +30, +31 | index position tables by trait |

So +39 level 4 is the showman (it waits and taunts most), levels 2 / 3 never stop to taunt, and +37
level 0 is the most aggressive striker but the least likely to go for weapons or climb.

Reading across: level 0 stays in and attacks little, 1 is mild, **2 and 3 are brawlers** (3 the most:
it waits until the last count), and **4 attacks from the ring and apron** (600) rather than following.
Others: +39 0x80145530 (mode 38 on the floor) 100, 50, 0, 0, 0; +39 0x80145434 (wait on the apron) 400,
200, 0, 0, 200; +39 0x801457B0 (wait at a distance) 200, 100, 0, 0, 300; +34 0x801456C4 × 10 (strike
when near in `ai_decide_far`) 800, 600, 500, 400, 200; size class (attrs +0x15 / 3) 0x801457A8 (run at a
distant opponent) 300, 200, 100, 0.

| Trait | Used for |
|-------|----------|
| +25 / +26 | a downed / lying opponent, healthy / hurt: how to work on it (0 from a distance, 1 up close, 2 move list D; `ai_decide_vs_lying_b`), and level 2 makes `ai_decide_vs_down_b` 10 times keener (500‰ vs 50‰) |
| +30 / +31 | **which move list**, by the opponent's perceived health (healthy / hurt; 40% of the time a random one instead; `ai_decide_move_list`): 0 list A (moves 209-216), 1 list B (236-250, 155-158), 2 list C (the turnbuckle aerials 261 / 269 / 270 / 274), 3 a pick-up; level 2 also raises `ai_decide_vs_down_b`'s chance to 500‰; they index the C-left turn tables in mode 110 too |
| +34 | grapple keenness (`ai_try_grapple`, far decisions, turnbuckle) |
| +35 | defence and counters (kick catch, apron reactions, runners, wait or engage) |
| +36 | close-range aggression and struggling (`ai_decide_close`) |
| +37 | overall aggression (18 decisions, taunts, follow-up strikes) |
| +38 | weapons and fighting outside (armed checks, the floor, count-outs, chasing outside); 3 a special weapon behaviour |
| +39 | specials and finishers (SPECIAL, taunts, special moves) |

Trait bits in attrs +0x38 enable move kinds (rope moves 0x400000, running at a downed opponent 0x2000 /
0x40000 / 0x4000, lock-up finishers 0x1000000, apron dives 0x10000 ...; the CPU's move lists above).
`ai_decide_ropes` (0x800F7AAC, read 2026-11-02, with a carried opponent): in the ring without the
over-the-top rule, 0x400000 gives a 50% mode 67; near the CPU's corner point (within its radius + 50)
0x100000 (opponent in the ring) or 0x200000 (opponent on the floor) gives mode 66.

## The modes (by family)

Each mode writes buttons; read from their code (most `guess`):

- **Engage and grapple:** 1 engage (with chance = skill react to a strike with mode 46 block, to a grapple
  attempt with mode 21 L counter 75% or 43; otherwise A), 2 grapple, 23 / 24, 51 grapple move.
- **Lock-up:** 25 choose a move after a wait from the style tables (`ai_lockup_move_list`: front / rear,
  strong), 26, 102 place by direction, 111 / 112 whip or rear, 117, 120 move by direction; helpers
  `ai_pick_grapple_move` (lists at 0x80145C54 by kind, preferring moves with flags 0x8000 at 300‰),
  `ai_lockup_finisher`, `ai_lockup_ropes`.
- **Strikes:** 6 strike (then a grapple at chance = skill on an opponent knocked back or off balance), 9 /
  115 follow-ups, 22, 59 facing, 132, 136 from behind.
- **Defence:** 3 / 4 / 5 (R, L, A or C-down by chance), 7 (hold R 1-13 frames), 10 / 11 / 12 evade, 13 / 14 block or counter, 21 L
  counter, 42 evade a grapple (waits `ai_evade_timer`, then L), 46 / 47 / 131 block, 56 dodge a dive.
- **Struggles:** 29 held in a lock-up: presses A+B+L+R at a chance from skill, perceived health (< 65,
  < 128), the CPU level and traits +36 / +37; `ai_struggle_rate`: 0.75 below 65 health, 0.65 below
  129, else 0.55, plus (spirit × 16 / 100 − 8) × a constant. 30 being whipped (A or B by skill;
  `whip_run_inputs` does the rest). 31 the ground hold.
- **Movement:** 32 wait and watch (reacting to runs), 37 / 63 / 87 / 123 / 127 walk, 41 approach (the
  commonest pick; a grapple at 40% when close and both in the ring), 45 back off (15-30 frames), 48
  approach a downed opponent, 55 wait up to 150 frames, 60 / 104 circle, 71 / 72 / 124-126 positional
  walks, 103 circle around.
- **Running:** 40 / 68 run, 44 run at, 88 run attack, 90 run to, 105-109 while running (stop or dodge),
  116 run the ropes, 119 keep running; 16-18 meet a runner, 65 / 66 / 95 against a runner.
- **Downed opponent:** 8 go for the pin (L), 19 / 20 pick up (R), 52 / 73-85 / 96-99 / 134 C-left
  (drag, flip), C-down (step away) or A (ground grapple), 89 line up, 91, 92 go to a corner (dive?).
- **Outside and apron:** 33 / 34 / 49 / 50 / 61 / 118 apron, 35, 36 leave the ring, 39 fetch a weapon (the
  steps zones, C-left), 62 go outside after the opponent, 101 go to an area (`ai_decide_change_area`),
  141 after a crowd weapon.
- **Specials:** 50 taunt while SPECIAL (to start finisher mode), 113 special move, `ai_decide_finisher`
  (in finisher mode with spirit_timer ≥ 150 a finisher from its lists), `ai_finisher_check`,
  `ai_finisher_kind` (which finisher fits the opponent's pose and angle).
- **Kick catch:** 69 holding a caught kick (A or B 50 / 50 after a style-table wait), 70 leg caught.
- **Tag:** 15 partner decisions, 143 tag assist; `ai_tag_reach`, `ai_partner_busy` (don't tag while the
  partner is mid-move), `ai_try_corner_climb`.
- **Ladder match:** 133 / 135 / 139 head for and climb to object 6 (the prize), 138 on the ladder (R, up or
  B by chance).
- **No-ops:** 27, 28, 93, 137, 140, 142 go straight back to mode 0.

## Open

- Deciders read in order so far: in ring (top level), ring vs floor, floor vs ring, both on the floor
  (and its trait choices), the three apron deciders, at a distance, far, close, standing, the lock-up,
  backstage, the raised places, the three cage-wall deciders, specials, the finisher (first pass), the
  move-list choice, moves on a downed or lying opponent. Every decider has now been read; the in-ring top
  level, standing, close, cage-wall and lying ones were checked line by line on 2026-11-02.
- How the `ai_list_pick_*` helpers choose within a list (filters, the preferred mark).

# World Tour: CPU opponent AI

Read from the code (2026-10-30, two passes): `ai_think`, the decision functions, all 48 mode
handlers (most only by the buttons they press) and the weighted mode tables. Not yet checked
against a live match.

## How it plugs in

The AI **presses buttons**: for a CPU wrestler (`flags_lo & 1`) `wrestler_tick` calls
`ai_think`, which fills that slot's `g_ai` input (held +6, pressed +8, far/stick +0xC); the
states then read it exactly like pad input (`frame_loop.md`). So the port can run the AI as a
virtual controller and every rule (reversal windows, mashing, block timing) applies to it the
same way.

## The AI block (`g_ai_state`, `0x800F1710`, 84 bytes per slot)

| Offset | Meaning |
|--------|---------|
| +0x02 | current mode (index into the handler table `0x800E7AE0`, 48 entries) |
| +0x04 | step within the mode |
| +0x06 / +0x08 / +0x0C | buttons held / pressed / stick, output |
| +0x10 | difficulty level 0–2 |
| +0x12 | **skill**, 0–1000: used as a `chance_check` value for good decisions |
| +0x14 / +0x1C | situation class of self / opponent (`ai_situation`, below) |
| +0x16 / +0x1E | perceived health, self / opponent |
| +0x18 / +0x20 | perceived spirit |
| +0x1A / +0x22 | perceived `recover_count` |
| +0x24 | opponent's state when the mode was chosen |
| +0x28 | perceived x/z distance to the opponent (pelvis to pelvis) |
| +0x2C | distance and angle to the opponent |
| +0x3C | ring quarter the wrestler stands in (`sub_800DF4A8`, by the signs of x and z) |
| +0x40 | distances to 4 points (probably the corners) |
| +0x50 | flags: bits 0–2 cleared on a mode change; bits 16–24 = where self and opponent are |

Globals while it runs: `0x800F1860` self, `0x800F1864` opponent, `0x800F1868` the block.

### Difficulty

`ai_init_slot` (`0x800B5590`) reads the per-slot level from **`0x80062060[slot]`** (the
"unknown 1, 1, 1, 1" in `match_options.md`; values above 2 become 1), and sets skill =
`0x800E7560[level]` ± 30 at random: **level 0 → 50, level 1 → 250, level 2 → 900**.

### Perception noise (`ai_perceive`, `0x800DF360`)

Every frame the AI re-reads health, spirit, recover counts and the distance with an error:
the spread is `value × min(f, 1 − skill/1000)` (f = 0.3, or 0.2 for the reach), and the
reading is uniform within ± half of it, never below 0. So a level 2 CPU misjudges health by
about ±5 %, a level 0 one by ±15 %, and it misjudges distance as well.

## Each frame (`ai_think`)

1. Classify self and opponent (`ai_situation`, `0x800DEEF0`), from the state and step:

| Class | Situation |
|-------|-----------|
| 1 | standing, walking, taunting (0x01, 0x16, 0x17) |
| 2 / 5 / 14 | running; being whipped; whipped into the corner / slumped (0x13) |
| 3 | grappled, as defender (0x3F step 2, 0x45) |
| 4 | pinned, held, TKO stagger, test of strength (0x25, 0x32, 0x36, 0x38, 0x40, 0x41) |
| 6 | striking (0x07, 0x15, 0x1A–0x22) |
| 7 | 0x53 |
| 8 / 13 | down (0x28, 0x50) / about to get up |
| 9 / 12 | dazed (0x29) / 0x2A |
| 10 | grapple attempt in its late steps (0x3D) |
| 11 | staggered by a strike (0x24 with 0x4000) |
| 15 | getting up (0x3C) |
| 16 | grappling, as attacker (0x3E) |
| 17 | on the turnbuckle or diving (0x09, 0x5D–0x61) |
| 18 | blocking (0x39) |
| 19 | whip runner (0x0B) |

2. Update perception and the **area pair** in +0x50: self in ring / apron / floor × opponent
   in ring / apron / floor (0x10000 ring–ring … 0x1000000 floor–floor).
3. **Reflexes** override the mode: grappled as defender → mode 16; pinned / held / down /
   slumped → 18; being whipped → 17 (press A/B, the reversal); dazed → 37. Each frame they
   press A/B/L/R (0xC030) with chance `skill × h × k`, where h is 0.9 when the perceived
   health is ≥ 129, 1.0 for 65–128 and 1.2 below 65 (a hurt CPU struggles harder), and k is
   **0.6** for grapples (16) and **0.5** for pins, holds, getting up and dazes (18, 37). So per
   frame: level 0 about 2.5 %, level 1 about 12.5 %, level 2 about 45 % (roughly 1, 4 and 13
   presses a second against `mash_count`). Mode 37 then, with chance `skill × 1.5`, holds a
   follow-up: against a grapple attempt A (80 %) or L, against a strike R (block), otherwise B.
4. Run the current mode's handler, unless `flags_hi & 0x400000`.

Mode 0 is the **decision** (`ai_decide`, `0x800B5A8C`): standing (class 1) it branches on the
area pair; getting up → mode 38; grappling as attacker → 14 (choose a grapple move); whip runner
→ 46. Most modes end by going back to mode 0.

### Distances

+0x28 is the perceived pelvis-to-pelvis x/z distance (`sub_8009FC64`), with up to 20 % error.
The thresholds are doubles in `0x800E8510`–`0x800E85A0` (the decompiler shows several of them
as 0.0): **130** is "in reach" for most decisions, 250 and 230 are far, 190 is the block range
against a striker, 380 / 180 are corner distances (+0x40, per quarter) for the turnbuckle
cases.

### Pre-checks (every standing decision)

- `ai_check_floor` (`0x800B6598`): on the floor and not out → climb back in (mode 21) when the
  ring-out count is under 6, or 10 % of the time with no count.
- `ai_check_target_4way` (`0x800B665C`): in 0x400 matches, press switch-target (C-right) when
  the opponent is beyond 230 or eliminated.
- `ai_check_target_tag` (`0x800B67A0`): in tag (unless rule 0x40), switch target so the legal
  man faces the legal man.
- `ai_wants_tag_out` (`0x800DD7B8`): the legal man in the ring goes to tag (mode 45) with a 25 %
  chance per decision when it has lost 51+ health since `0x800F1D30` or has been in for 2701
  frames (90 s, `0x800F1D38`) and the partner is on the apron.

### Choosing what to do (standing, same area)

`ai_decide_fight` (`0x800B5C48`), after the pre-checks:

- tag, on the apron as the illegal man → mode 44; wants to tag out → 45; on the apron, 20 %
  → 21 (80 %) or 22;
- opponent on the turnbuckle or diving → `ai_vs_turnbuckle` (`0x800B71A0`): within 380 of its
  corner and it is in the climb steps, with a skill roll → 41 (follow it and grapple it off) or
  42; otherwise 40 (run to the corner) or 39 (wander);
- **out of reach**: walk (25), or run (26) a quarter of the time when over 250;
- **in reach**, opponent standing: half the time `0x800E760C`, otherwise by distance band
  `0x800E7568` / `0x800E75D4` / `0x800E75A4`;
- in reach, opponent in another situation: `ai_read_opponent` (`0x800B649C`) returns its
  class, **but with chance 1 − skill it misreads it**: striking is taken for a grapple attempt
  and the reverse, blocking (18) or 7 for either at 50 %. Then:

| Opponent | Handler | What the CPU does |
|----------|---------|-------------------|
| striking (6) | `0x800B6AD0` | within 190: R (31), or 32 at 30 % when its own health reads under 200; else table `0x800E775C` |
| down (8) | `0x800B6B78` | after a big hit (`g_last_damage`) with the opponent staying down (recover ≥ 91) and its own health < 120: taunt (34); **pin** (mode 5, C-down) only when the opponent's health reads < 25, it will stay down (≥ 91), and a 15 % roll; near (90): tables `0x800E7788` (skill ≥ 100) / `0x800E77AC`; else walk over (33) |
| dazed or about to rise (9, 13) | `0x800B6D9C` | taunt when safe; near (130): `0x800E77D0` / `0x800E77E8`; rising → 19 |
| attempting a grapple (10) | `0x800B6968` | by distance `0x800E76BC` / `0x800E76E0` / `0x800E7718`; against a strong grapple with a skill × 1.3 roll `0x800E7730` |
| staggered (11) | `0x800B6EEC` | over 100 away and a skill roll: running attack (7); near: `0x800E7808` / `0x800E7820` |
| 12 | `0x800B6FFC` | `0x800E7838` / `0x800E7850` |
| in the corner (14) | `0x800B7090` | skill roll and over 130: running corner attack (35); taunt 5 % when it is slumped and weak; `0x800E7868` / `0x800E7874` |

The "skill ≥ 100" tables (levels 1 and 2) favour attacks; the others have more walking (19, 27).

The tables are cumulative weights out of 1000 read with `rand_pool_next` (`ai_pick_mode`,
`0x800DF298`); each entry is (upper bound, mode, flag bits for +0x50), and all 25 found end at
exactly 1000. `tools/rom/wcw_ai.py ROM OUT.json` dumps them with the skill and error tables.
For example `0x800E760C` (standing opponent in reach): 30 % mode 3 (strike), 30 % mode 1 (grapple), 5 % mode 11,
25 % mode 27, 10 % mode 19; against a downed opponent (skill ≥ 100) 40 % mode 10 (R: pick up),
25 % 12, 20 % 13, 9 % 5 (pin), 3 % 34, 3 % 36.

Other area pairs: ring → opponent on the floor `ai_vs_floor` (`0x800B6010`): stays in when the
ring-out count is under 5 (`0x800E765C`); may dive (mode 47) with header traits 0x300 when the
opponent is over 490 away (280 with trait 0x100), 20 % against a dazed opponent or 5 % against a
standing one; otherwise `0x800E7638`. Apron → ring climbs in (21) when allowed; floor → ring
`0x800E768C` (70 % climb in); floor → apron `0x800E76A4`.

### The modes

Action bits as in `frame_loop.md` (A 0x8000 grapple, B 0x4000 strike, C-down 4 run, R 0x10,
L 0x20, C-up 8, C-right 1 switch target, d-pad 0xF00; `0x800E7090` maps 8 directions to d-pad
bits). Read from the code; not yet watched in RAM. "Steer" means the d-pad pointing at the
opponent (`ai_steer`, `0x800E55E8`; on the floor `ai_steer_axis`, `0x800E5644`, which uses
x and z separately).

| Mode | What it does |
|------|--------------|
| 0 | decide (`ai_decide`) |
| 1, 2 | hold A: grapple attempt; mode 1 then follows up by the opponent's situation (running attack → 31, grappling → 11 at 75 % or 29) |
| 3 | **strike**: B (half the time B + d-pad, the moving strike), then if still in reach: 25 % strike again; with a skill roll grapple a standing or blocking opponent (1), block a striker (R) or answer a grappler with A or L |
| 4 | hold R for 0–12 frames (a short block) |
| 10 | R next to a downed opponent: pick it up (state 0x2D) |
| 31 | hold R; against a striker, with a skill roll, keep blocking only until its active frames are over (`anim_frame > window_end`) |
| 32 | **timed block (block counter)**: wait until the striker's `window_start` (chance skill × 0.8, else block at once), then R, so the block starts inside the 3-frame catch window of `block_counter` (`strikes.md`) |
| 5 | hold C-down while the opponent is lying: the **pin** |
| 15 | the opponent slumped in the corner (0x13): run at it, then grapple (mode 2) |
| 6 | hold B: the **strong strike**; picks Up+B or plain B by which has the earlier learned hit frame (below), else 50/50 |
| 7 | **running attack**: C-down (toward the opponent's side), then while running: against a striker or grappler within 170, with chance skill × 1.3, L+R (the roll, slot 127); otherwise B once the opponent is closer than the learned reach of that running attack; then stop |
| 35 | the same against an opponent in the corner |
| 8 | in a grapple, **whip**: C-down + a random direction; when the opponent comes back running, 70 % mode 9, 30 % mode 43 |
| 9 | meet a runner: when it heads this way, hold A (with the d-pad half the time) for 15 or 10 frames, a grapple on the runner |
| 11 | hold L (evade); against a grappler with a skill roll: again (50 %), 28 or 29 |
| 12 | B: ground attack (state 0x4E); with a skill roll, when the opponent's perceived recover count is more than 25 above the learned hit frame of the ground attack (face up / face down), pick it up instead (mode 10) |
| 13 | hold A into a ground hold or submission (states 0x33 / 0x35); on the floor releases with R when the ring-out count is under 4 |
| 14 | **grappling as attacker** (below) |
| 16, 18, 37 | reflexes (mash A/B/L/R), above |
| 17 | being whipped: each frame with chance skill × h × 0.6 (h 1.3 when healthy, 0.9 when hurt) A or B, the reversal try at the ropes or corner |
| 19 | stand and watch for 8–39 frames; then, if the opponent did nothing new and is far, 20 % running attack |
| 20, 25 | steer toward the opponent until in reach (20 gives up after 0–15 frames) |
| 21 | C-up toward the ring: climb in (state 0x55) |
| 22 | C-up toward the opponent: go out through the ropes (state 0x54) |
| 36 | C-up next to a downed opponent (state 0x2B) |
| 23 | on the floor with the opponent on the floor: steer and press A (grapple outside) |
| 24, 34 | stick for 80 frames: taunt |
| 26 | run toward the opponent and stop when in reach |
| 27 | **circle**: pick a direction (straight at the opponent 40 % of the time, otherwise 2–6 eighths of a turn off it; half a step off when a wall is in the way) for 5–36 frames; then 40 % running attack when far, 25 % keep circling |
| 28, 29 | **timed counter**: wait until the frame before the opponent's grapple attempt becomes active (its `window_start`), with chance skill × 0.8 (else act at once), then L (28, evade) or A (29, grapple first) |
| 30 | run in a direction (toward the opponent, or 45° off near a wall) for 8–15 frames, then stop (any d-pad press while running sets the run's stop step, `run_inputs`) |
| 33 | walk to a downed opponent (to a point beside it, `sub_800A0100`) until within 125 |
| 38 | **getting up**: with a skill roll in reach, hold R (kneel) and then strike (B 45 %), evade (L 45 %) or rise; reads the opponent: R against a striker, A or L against a grappler |
| 39 | walk in a random direction for 8–15 frames |
| 40, 42 | run toward the opponent's corner and stop there (follow a climber) |
| 41 | walk to the corner, then grapple (mode 1): pull a climber off the top |
| 43 | B when facing the opponent and it is within the learned strike reach (meeting a runner) |
| 44 | walk toward a point for 5–20 frames |
| 45 | walk to the partner on the apron, C-up when close: tag |
| 46 | whip runner (hands on to mode 8) |
| 47 | C-down, then A when running: dive to the floor (state 0x57) |

### Grappling as attacker (mode 14)

In the lock-up (state 0x3E) the CPU waits 5–20 frames (1 % of the time 30–45), then:

- **Strong or rear grapple** (`grapple_ctx` 2 or 8): a direction from `0x800E7BA0` (a third
  each Up, Down, none) and a button: A or B 50/50 (wrestlers with header trait bit 0: B 90 %).
  With SPECIAL ready (`grapple_ctx` 1): 25 % A or B, otherwise 75 % the **stick (the special
  move)** and the rest A+B.
- **Weak grapple:** usually the same A/B choice, but **10 % of the time it whips** instead
  (mode 8), 25 % when SPECIAL is ready; never (0.1 %) when either wrestler has header trait 0x80.
- Wrestlers with header trait bit 0 pressing B from a strong front grapple go to **state 0x44**, a strike combo (slots 187–189, 6 hits, 9 with SPECIAL ready); the CPU keeps it going with A 10 % of the time (sometimes the stick or A+B) and otherwise B with a random direction.
- (For players too: A+B with SPECIAL ready plays slot 183/184 from the **defender's** move set, i.e. steals its special, `grapple_choose_move`.)

### Learning (`g_strike_learn`, `0x800F1870`, 64 bytes per wrestler)

Eight entries of {f32 reach, s16 hit frame, s16 count}, reset to reach 150 at match start
(`sub_800C95E0`). `ai_learn_strike_reach` (`0x800CD894`) updates one at the hit frame of each
strike the wrestler throws: the x/z distance from its position to the hitting joint, and the
animation's hit frame, both as running averages ((old + new) / 2). Entries: 0 slot 123/5
(hold B), 1 slot 124/6 (Up+hold B), 2 slot 125 (run+B), 3 slot 126 (run+L/R+B), 4 slot 128
(corner), 5 slot 154 and 6 slot 155 (ground attacks), 7 slot 122/4.

The CPU uses them: mode 6 picks the strong strike that lands sooner, mode 7 times its running
attack to the learned reach, mode 12 compares the ground attack's hit frame with how long the
opponent will stay down, mode 43 uses the reach to meet a runner. So **the AI calibrates to the
wrestler's own animations as the match goes on**, starting from 150.

## Open questions

- Watch `g_ai_state` in RAM during a CPU match (paused snapshots) to confirm the button readings
  and the per-frame mash rates.
- Which menu sets `0x80062060` and how "level 1" maps to the options screen.

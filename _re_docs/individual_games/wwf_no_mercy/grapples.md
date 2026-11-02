# No Mercy: grapples, the lock-up and grapple moves

Read from the code (2026-11-01 / 02) with RAM checks from Andrew's save states (light, strong,
running, rear and ground grapples, the table slam, an L deflection). Names in `symbols/nomercy/`;
World Tour's equivalent is `../wcw_world_tour/grapples.md`. Frames are game frames.

## The attempt: state 0x50 (`state_50_grapple_attempt`)

A from standing (`input_grapple_attempt`), from a get-up (A held, `get_up_finish`), from the run or
from 0x22 (A alone) starts 0x50.

- **Step 0-2:** on the apron it slides along the apron line (11794); otherwise it may walk
  (`walk_step`) and turns toward the opponent (341 / 512). `input_press_kind` reads the press: **B
  turns it into a strike** (0x22); a **hold sets flags78 0x04, a strong grapple**; `grapple_attempt_double`
  can take over (two attackers on one target). Any weapon is dropped; `grapple_attempt_start_anim` picks what the
  attacker plays: a grab move (86-94, flags78 0x08) at a draped / hung-up opponent (its flags86 0x220) or
  from behind, else the reach animation (11892 / 11896, +1 strong; 11901 sliding on the floor).
- **Step 3, the reach:** it keeps turning toward the target (34 units a frame) until the animation's
  active window ends (anim_frame vs anim_player +9); every frame `grapple_attempt_target` looks for a
  wrestler in reach (`hit_candidates`), and a hit goes to `grapple_attempt_resolve`. On the floor the
  attacker slides forward (`grapple_attempt_slide`).
- **Whiff:** step 4 (11892, 11894 for some kinds), then 0x30 and standing.
- From the floor at the apron and on raised places the attempt uses moves 86 / 88 / 90 / 94 / 96
  (attempt animations through `anim_style`).

## What a connecting attempt does (`grapple_attempt_resolve`, 0x801269DC)

- From behind (`relative_position` bit 0x08): flags78 0x80, a **rear grapple**.
- A defender swinging a weapon (0x1B) loses 10 spirit. No grapple on a lying wrestler (flags80 0x08)
  or one held by someone else. The defender drops any weapon.
- **A defender in the air** (states 121-125, 149 step 3, +0xF4 bits 0x0F) is caught when the attacker
  is in the ring and not running: **0x88 / 0x89** (catching a flyer; then move **141**, or **142** when the two face more than
  about 90 degrees apart).
- Not rear: a defender in the **L evade 0x6B with count_timer 4 or less** (a 5-frame window) in the
  same place **deflects** it: 0x6C / 0x6D; against a running grapple the defender **counters**: 0x5B
  with move 90 against the runner's 0x5C; `timed_press_check` kind 3 can also deflect.
- Two simultaneous attempts (the defender in 0x50 step 4 before frame 3): flags78 0x200000.
- A defender in the pick-up 0x34 with frame_flags 0x08 in the ring: 0x40 / 0x41.
- From the floor at the apron: `grapple_start_pair`. Otherwise, unless a double team takes over
  (`grapple_double_team_check`), `grapple_start_pair`. A staggered target (flags78 0x08): 0x5B / 0x5C, or
  with 0x1000 a double-team lock-up with slot_d.

## Which grapple (`grapple_start_pair`, 0x80126EEC)

From where both stand, through `wrestler_pair_set_state_move`:

| Where | States | flags78 |
|-------|--------|---------|
| on a ladder | 0xB4 / 0xB5 | |
| from the apron | 0x7F / 0x80 | |
| floor to apron | 0x9C / 0x9D | 0x20000 |
| floor to ring (or a raised place) | 0x86 / 0x87 | 0x40000 |
| at location 0x400 (the cage) | 0xCD / 0xCE | |
| running | 0xA0 / 0xA1 | 0x40 |
| standing (normal) | **0x51 / 0x52** | 0x800, plus 0x400 / 0x200 for apron sides, 0x100 a staggered defender |

flags78 is copied to the defender when the grapple starts.

## The lock-up: 0x51 (attacker) / 0x52 (defender)

Front lock-up animations 11905 / 11906, rear 11909 / 11910 (11918 a staggered defender); the
defender's animation comes from `grapple_defender_anim`.

The attacker's steps (`state_51_grapple_attacker`, read 2026-11-02; flags8a 0x2000 throughout):

| Step | What happens |
|------|--------------|
| 0 | clears flags_f0's low bits, `grapple_anim_defender` sets both lock-up animations |
| 1 | `lockup_slide_in` until both are in place; then two simultaneous attempts (flags78 0x200000) → step 8, a running grapple (flags78 0x240 or 0x140) → step 4, otherwise step 2 |
| 2 | first frame: the pair record (`g_grapple_pairs`): distance 53.5 rear (flags78 0x80) / 73.5 front, angle = facing + 180°, the d-pad counter cleared. Every frame `grapple_walk` and `grapple_timer`: 1 → step 3, 2 (time out or R) → step 7 with state_flags 0x01 |
| 3 / 4 | state_flags 0x02 and `lockup_input`: 2 = nothing usable yet, back to step 2 (state_flags 0x02 off); 3 = the defender reversed: state_flags 0x04 and the attacker plays the defender's part 1; 4 = a placed move (whip, shove, table): step 10, state_flags 0x08 |
| 5 | a staggered defender (flags78 0x100) with move flags 0x100000: 12099 front / 12101 rear first |
| 6 | waits for the animation |
| 7 | starts the move: the move's part 0 (`anim_style`), state **0x55** |
| 7-8 (release) | `pair_follow` keeps the defender in place (unless flags78 0x700); at the end both unpair; on the apron (flags78 0x200) 12531 and step 9, otherwise `wrestler_settle_pose` |

The defender (0x52) follows the attacker's state_flags (0x01 released, 0x02 move chosen, 0x04 reversed,
0x08 placed move) like World Tour's defender reads the attacker's bits.

- **Slide in:** `lockup_slide_in` / `lockup_slide_in_defender` place both over the first frames; the
  pair record `g_grapple_pairs` (0x80159290, 24 bytes per slot) keeps the distance: **73.5 front,
  53.5 rear**.
- **Time limit** (`lockup_time_limit`, `grapple_timer`): count_timer starts at **150 frames, 210 for a
  strong grapple, 90 when the defender is in finisher mode**, and counts down; **R alone** sets it to 0
  (letting go). At 0 the release plays: 11907 front / 11911 rear, 11915 / 11919 on a staggered opponent,
  11923 / 11927 on the apron.
- **Walking while grappled** (new since World Tour; `grapple_walk`): holding the d-pad 7 frames
  (`aux_timer`, +0xD0) switches walking on for normal standing grapples (flags78 0x800). A direction within about
  20 degrees of the facing (or of its back) walks both wrestlers that way at the pair distance; other
  directions turn the pair 45 units (about 4 degrees) a frame. `keep_in_bounds_pair` keeps them inside.
- **The attacker's choice** (`lockup_input`, from the latched buttons in_0c and the d-pad):

| Input | Result |
|-------|--------|
| A / B, front | move from 0x801469D0 [strong][B][dir], dir 0 none, 1 left / right, 2 up, 3 down: weak A 1 / 2 / 3 / 4, weak B 5 / 10 / 15 / 20, strong A 25 / 30 / 35 / 40, strong B 45 / 50 / 55 / 60 |
| A / B, rear | 0x801469F0 [strong][B][any dir]: moves 73-80 |
| strong grapple with finisher mode ready (flags78 0x400000) | move **70** (front) / **81** (rear), the finisher |
| A+B in a strong grapple, tag | 70 / 81 on the partner's slot: the double-team finisher |
| L, front | 0x3E / 0x3F: turn to a rear grapple / the corner swap |
| C-down | the whip (0x0B / 0x0C), or the drag 0xD0 / 0xD1 (`lockup_place_move`) |
| C-up | `lockup_place_move` kind 1: shove into a feature, onto the table or steps |
| C-right in a cage match | whip into the cage (flags78 0x10000000) |
| from behind, the other button | 0x7F / 0x80 the **rear lift** (onto the shoulders, new), or 0x81 / 0x82 the carry |
| running grapples | moves 145 (flags 0x240), 108 / 130 (0x140) |
| a defender who can reverse (attrs +0x34 0x1000), against a move without flags 0x2000 | moves 159 / 161 |

- **The defender's reversal try** (`grapple_defender_input`): one try per lock-up (flags_f0 0x08 marks it
  used), only while it is in finisher mode or the attacker is not. Any of A / B / L / R (wrestlers with
  attrs +0x34 0x1000 always count as pressing) rolls `chance_roll` kind 1: spirit 100 / 81 / 61 / 31 /
  11 / below → **500 / 333 / 250 / 125 / 62 / 31 per mille**, doubled when the defender is the bigger
  wrestler (attrs +0x15 / 3 higher), and against a strong grapple ×4 at health_eff 192+ or ×2 at 128+;
  wrestlers with attrs +0x34 0x1000 use kind 8 instead (500 − 10 × spirit deficit). Success sets
  flags_f0 0x04, which `grapple_reversal_start` turns into the reversal when the move is reversible
  (move flags 0x01): rear lock-up move 162 for a defender who can counter, else 85 (strong) / 84; on
  the apron 148 / 152; front: the move's own reversal (`move_anim_offset`).
- **Move reversals** (`reversal_check`): when a move starts, a reversible move (flags 0x01) against a
  defender who can reverse (attrs +0x34 0x1000), in finisher mode or against an attacker who is not, is
  reversed at `chance_roll` kind 8: **500 − 10 × max(0, attacker spirit − defender spirit) per mille**.

The CPU in the lock-up: AI modes 25 (choose a move after a wait from the style tables), 111 / 112 (whip,
rear), 102 (place by direction), 117, 120; see `ai.md`.

## Grapple moves: 0x55 / 0x56 and what follows

A chosen move puts the attacker in **0x55** and the defender in **0x56** (paired, ~180 VIs for the
table slam), both playing the move's two parts through `anim_style`. The attacker's frame (read
2026-11-02):

1. `move_follow_up` first (a move with a follow-up hands over to 0x59 / 0x5A).
2. Apron moves (flags78 0x600) move both wrestlers between ring, apron and floor by the animation's end
   place (1 ring, 3 apron, 2 floor) and `pair_follow` carries the defender through the ropes (a turn of
   768 or 3072 by the facing); others keep the pair inside with `state_bounds`.
3. **Mid-move reversal:** when the defender's flags_f0 has 0x02 (`reversal_check` passed) and the
   animation reaches the move's reversal frame (anim player +0x2F8), the attacker switches to the
   reversal animation (part 1) and the defender takes over.
4. Every frame `finisher_effects` (a finisher breaking a table) and, on frame 1 of a move with flags
   0x80000, the defender's flags8c 0x400; `object_spawn_for_move` (props in moves).
5. `move_event(0x2480)`: at the end of the animation **the move's damage is applied** (0x2000), a chain is
   tried (0x400) and the move is counted (0x80).
6. (Checked 2026-11-02.) For apron moves the attacker's location is set from the animation's end place
   right away; until the record's crossing frame (file 703 byte 16) the pair still counts as in the ring
   and `pair_follow` pulls the defender at 4.0, afterwards at 6.0. The defender (0x56) does the same, and
   when it wins the reversal it re-pairs the two the other way round (`wrestler_pair_set_state_move` 0x55
   / 0x56) and counts it; at its end it takes its escape time (op 2), applies the end pose / turn and
   `move_aftermath_check`. The start of every paired state places both wrestlers with
   `pair_scripted_place` (`animation_data.md`).

Then:

- **0x30 → 0x4E / 0x4F**: the attacker crouches / stands (12153, 12157, 12171), the defender goes down
  (0x31) or gets up through 0x4F.
- **Follow-ups**: a move with a follow-up (move +4) goes on through **0x59 / 0x5A** (`move_follow_up`).
  The follow-up, chain and reversal moves are a base number **plus 1-4** from `move_anim_offset`: a 2-bit
  entry per move animation code in the wrestler's variant set (attrs +0x01; `damage_and_spirit.md`).
- **Chains**: when the animation ends in pose 6 and the move has no follow-up, both go back into the
  lock-up (0x51 step 2, flags78 0x880 plus the strong bit): grapple moves that chain into another
  grapple (`move_chain_check`); some chain into a second move through **0x5D / 0x5E**.
- **From a carry or pick-up**: 0x5F / 0x60 (`move_chain_check`, `state_82_carried`,
  `state_39_pickup_defender`).
- `move_aftermath_check`: bleeding or a TKO stagger after a move (World Tour's rules).

## The whip: 0x0B / 0x0C

C-down in the lock-up (`lockup_place_move` → `whip_direction`): the attacker 0x0B, the runner 0x0C
(entered from 0x52). Outside cage matches the direction is aimed at a side; facing a feature it becomes
a shove (0x0D / 0x0E) or putting the opponent onto the table or steps (0x0F / 0x10; zones 29-54 the
announce tables, 25-27 the steps); not over the ropes in the ring under the battle royal rule
(g_match_rules 0x400).

The runner (0x0C, read 2026-11-02) is pushed off at **walk_speed(w, 8)**, double the normal speed,
along the whip direction and then simply becomes an ordinary run (state 0x03) with flags86 0x20
(being whipped); into a corner it plays 12001 / 12002, into the cage 12825 / 12826; the whipper takes
generic move 4 (its spirit) on a hit.

**The whip reversal** (`whip_run_inputs`, the runner): in the ring, only for wrestlers with trait
attrs +0x34 0x2000, while being whipped (flags86 0x20), in finisher mode or against a whipper who is not,
on the way back from the ropes and not yet tried: pressing A / B / L / R **between 75.3 and 175.3 units**
from the side it runs toward uses the one try and rolls `chance_roll` kind 3 (spirit 100 / 81 / 61 /
31 / 11 / below → **1000 / 500 / 333 / 250 / 200 / 125 per mille**); success sets flags86 0x800 (A) or
0x400 (otherwise), the two reversal kinds.

## Ground grapples, pick-ups and carries

- **Ground grapple:** A by a downed opponent: 0x46 (stepping into place at the head or legs, 12168 /
  15909) with 0x47 (15910), then the hold **0x48 / 0x49** (15911 / 15912; flags80 0x40 / 0x80). The
  submission and its escape are in `pins_and_holds.md`. `ground_grapple_start` picks the animation by
  head / legs and face up / down.
- **Pick-up:** R by a downed opponent (AI mode 19): 0x38 / 0x39 (the squatting rear grapple, 11909 /
  11910), resisting at `chance_roll` kind 11 (spirit 100 / 80 / 60 / 40 / below → 600 / 400 / 300 / 200
  / 100). The downed wrestler can break it by ducking back (0xA7, 12943 / 12950).
- **Lifts and carries:** 0x34 held upside down (12076, dropped 12081; escape_time op 7); 0x7F / 0x80 the
  rear lift (0x80 struggles 12642 and breaks out 12646, escape_time op 5); 0x81 / 0x82 the carry
  (escape_time op 6); lifting at `chance_roll` kind 10 (1000 in finisher mode, else by the size
  difference attrs +0x15 / 3 and the other's health).
- **Corner:** 0x3F reversing in the corner and throwing the other wrestler there instead (12082),
  0x3E the flip into the corner hang (12074; swapped 12083), 0x3A / 0x3C / 0x40 pulling out of the corner,
  setting up on the top turnbuckle, pulling down from the hang (table below).
- **Double teams:** 0x83 / 0x84 / 0x85 (`grapple_double_team_check`, the partner joins a lock-up).

### Double teams (code 2026-11-02)

`grapple_double_team_check(joiner, held)` runs from `grapple_attempt_resolve` when a wrestler grapples
someone already held. It starts a double team only when all of these hold:

- the held wrestler is in 0x52 (grapple defender) with flags78 0x800 (a normal standing grapple), and its
  `count_timer` is below 11;
- the `location` words of the joiner, the held wrestler and the held wrestler's opponent share a bit of
  0x3C5 (ring, floor or one of the raised places; never the apron).

It picks a position bit for the move: the joiner from behind (flags78 0x80) gives 0x4000; otherwise 0x2000
when the held wrestler has 0x80, else 0x1000. `double_team_grapple_start` then sets:

| Who | State | Links |
|---|---|---|
| the first attacker (the held wrestler's opponent) | 0x83 | `pair_slot` = joiner, `pair_slot2` = held, `slot_d` = joiner; flags78 gets the position bit and the joiner's 0x04 (strong grapple); flags88 0x06 |
| the joiner | 0x84 | `opponent` = held, `slot_d` = first attacker; flags78 keeps only 0x18080 and takes the position bits; flags88 0x02 |
| the held wrestler | 0x85 | `opponent` and `pair_other` = first attacker, `slot_d` = joiner; flags88 0x02 |

All three get flags80 0x01. By `angle_diff` of the two attackers' facings, one gets flags78 0x8000 and the
other 0x10000 (the left and right sides).

State 0x83 step 0: `double_team_place`, then the move from the first attacker's own move set: slot 163
(0x1000), 164 (0x2000) or 165 (0x4000). With flags78 0x08 it instead starts the next move after the current
one (`move_slot`, `move_num` + 1). The animation style is the mirror bit for 0x1000 / 0x2000 (flags78 0x8000)
and the rear bit otherwise; `stats_count` records the move against the opponent. Step 1 slides into place
(`double_team_slide`); step 2 runs `state_bounds`, `finisher_effects` and `move_event`. The joiner (0x84)
plays its side and gains spirit by the move record's +0x07; the held wrestler (0x85) is moved to the
attacker's pelvis and ends with `move_event(31232)`.
- **Drag:** 0xD0 pulling the opponent along (12042), 0xD1 being dragged (12041 / 12043).

## Small paired moves: 0x3A-0x41 (read 2026-11-02)

The attacker states 0x3A, 0x3C and 0x40 place the pair (`pair_scripted_place`), slide (`pair_slide_step`)
and end through `move_event`; the receivers 0x3B / 0x3D / 0x41 share one handler that plays the
attacker's animation + 1 and settles by the end pose (`move_event` 0x200).

| Pair | Control | What it is |
|---|---|---|
| 0x3A / 0x3B | **R** at an opponent slumped in the corner (frame_flags 0x01, not 0x02), both in the ring, within 120 (`input_r`) | pulling it out of the corner (12066, or 12068 on an opponent with flags8a 0x01) |
| 0x3C / 0x3D | **L** at the same slumped opponent (`input_l`, and the C-button code beside `guest_referee_call`); within 85 of an opponent on the ropes it is 0x97 / 0x98 instead | setting it up on the top turnbuckle (12070 / 12072) |
| 0x3E / 0x3F | **L in a front lock-up** (`lockup_input`; not a rear grapple, no stagger check) | the flip into the corner, hung upside down by the legs: 0x3E 12074, 0x3F the attacker's animation + 1 (12075), then 0x34 hanging (12076, dropped 12081) |
| 0x3F reversed | the held wrestler has flags_f0 0x02 / 0x04 | 0x3F plays 12082 and re-pairs 0x3E / 0x3F the other way round; 0x3E then plays 12083 |
| 0x40 / 0x41 | **L or R** at an opponent with frame_flags 0x08 (hanging in 0x34, or 0x2D), and `grapple_attempt_resolve` on a defender in 0x34 | pulling the hanging opponent down out of the corner (12077) |

## Open

- ~~0x51's step list in full~~ (code 2026-11-02, `state_51_grapple_attacker`): step 0 clears flags_f0's low
  bits and starts the defender's lock-up animation; step 1 slides in (`lockup_slide_in`) and then goes to
  step 8 (the clash, flags78 0x200000), step 4 (a running grapple at a staggered / rear target, 0x240 or
  0x140) or step 2; step 2 on entry (state_counter bit 0) fills the pair record (`g_grapple_pairs[slot]`:
  offsets 0, distance 73.5 front / 53.5 rear, the facing turned round) and every frame runs `grapple_walk`
  (so the walk can start at once) and `grapple_timer`: 0 stays, 1 -> step 3, 2 (the time limit) -> step 7
  with state_flags 0x01; step 3 -> 4; step 4 sets state_flags 0x02 and reads `lockup_input`: 2 lets go (back
  to step 2), 3 sets state_flags 0x04 and plays the defender's reversal animation, 4 -> step 10 with 0x08
  (switching sides); with no input a staggered target (flags78 0x100) whose move allows it (+0x20 0x100000)
  plays 12099 / 12101 (rear) and goes to step 5 (waits for its last frame), else step 6, which starts the
  move's animation (speed 1 under flags78 0x180, else 3) and state 0x55; step 7 (time out) follows the
  pair to the animation's end, unpairs, and with flags78 0x200 plays 12531 and step 9 (`move_event` 0x200),
  else settles; step 8 (the clash) at the animation's end applies its own generic move 0's damage to itself
  and plays 11910; step 10 plays 0x2E99 / 0x2E9D (rear) and toggles state_counter 0x04 / 0x08, step 11
  follows the pair to the end, flips flags78 0x80 (front <-> rear, 11905 / 11909), resets the time limit
  (`lockup_time_limit`) and returns to step 2. 0x55 / 0x56's damage lands at the end of the animation
  (`move_event` 0x2000) for every move.

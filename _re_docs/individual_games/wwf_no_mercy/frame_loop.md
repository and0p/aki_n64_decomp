# No Mercy: the match frame and how states run

Read from the code of the match overlay ovl_e (2026-11-01 / 02), with RAM checks where noted.
Addresses are USA Rev 1. Names are in `symbols/nomercy/` (`match.toml`, `states.toml`, `bulk.toml`,
`types.h`); `py tools/sym/sym.py --game nomercy show <name>` prints one. World Tour's equivalent is
`../wcw_world_tour/frame_loop.md`.

The game logic runs at **30 frames a second** (akiemu counts VIs, 2 per game frame). Timers below
are in game frames unless they say otherwise.

## match_tick (0x80114514)

A switch on the phase word `0x801591FC` (0-4):

- **Phase 0, set-up:** buffers reset, `g_match_minutes` and the flag blocks cleared, the count-out
  set up (`countout_tick(0)`), objects cleaned up, `match_init_wrestlers` (start positions; some
  start-table entries walk a wrestler in through `state_BE_walk_in`, 20 frames). Then a 30-frame
  wait (`0x801591E0`) and phase 1, where empty slots, slots with `flags_e9 0x02` and run-ins (state
  0xA3) are hidden.
- **Play, every frame** (unless `0x80162A7C` bit 1, probably paused, which runs
  `match_frame_extras` instead):
  1. `run_in_check`: interference rolls;
  2. `wrestlers_update_all(1)`: every wrestler's state handler (and the CPU, below);
  3. `referee_count_tick`: the pin count;
  4. `match_clock_tick`: the clock and the time limit (non-zero goes to phase 3);
  5. `countout_tick(2)`: the count-out (non-zero goes to phase 3);
  6. `match_end` for **one** fall cause a frame, in this order: 0x08 pin, 0x40 TKO, 0x20
     submission, 0x10 over the top, 0x100 last one standing, 0x200 guest referee call, 0x400 DQ,
     0x800 ladder, 0x1000 cage escape (the first that acts goes to phase 3);
  7. `match_events`.
- **Phase 3, after a decisive frame** (one frame): play moves on to it through a shared "phase + 1" exit
  whenever `match_clock_tick`, the count-out or `match_end` reports that it acted (the set-up phase 1 uses
  the same exit). It runs `match_events`, then sets the stop bit 0x8016B599 0x40 (standing wrestlers stop
  fighting and face their opponents, climbers drop down through 0xC7; `movement.md`); when 0x8016B599 0x10
  is set (`match_end_start` under g_match_type 0x2000000) it skips the stop bit and starts the phase
  timer 0x801591E4 at 150 instead.
  Then phase 4.
- **Phase 4, after the bell:** the wrestlers keep updating (`wrestlers_update_all`) while the timer
  0x801591E4 counts up. After **180 frames** under g_match_type 0x2000000, otherwise after **270** (held
  there while 0x80162A7C bit 0x100 is set), the timer jumps to 30000, which sets
  0x8016B599 0x80 and closes the match (per-slot results go to 0x80097FBE). So falls that do not end
  the match never get here: `match_end` only reports the deciding one.

The fall rules, count-out, clock and run-ins are in `match_rules.md`.

## Updating the wrestlers

- `wrestlers_update_all` (0x80117F28) runs the wrestlers in passes. Each pass starts at
  `g_update_first_slot` (0x801591F8) and goes round `(slot + 1) & 3`; after the frame the start
  moves on by one. **The update order rotates every frame** (World Tour and Revenge use a fixed
  order). Paired states read each other's records, so a frame-exact port must keep this order.
- `flags88` decides which wrestlers run in which pass (bit 0x04 in the match, bits 0x01 / 0x02 pick
  the pass). `wrestler_set_state_now` sets 0x01 so that the new state runs again in the same frame.
- `wrestler_tick` (0x80118368) also runs the CPU: `ai_update` writes the CPU's pad into its AI block
  and `wrestler_tick` copies it into the slot's input entry, so **CPU wrestlers play through the same
  input path as players** (`ai.md`).
- `wrestler_update` (0x800E5890), one slot:
  - fills `g_wrestler_ctx` (0x80159080): the slot, the related slots (record +0x00 / +0x04 /
    +0x08 opponent / +0x0C partner), pointers to their records, and the slot's input entry
    (0x80159540 + 56 × slot: held, newly pressed, latched, released);
  - clears per-frame bits (`frame_flags`, `contact`, flags_ee 0x10, location 0x800 / 0x1000, some bits
    of each flag word, +0x68 = -1), sets `ring_zone` from the position and `facing_to_opponent`;
  - counts `state_timer` (+0xA8) up and runs the meter (`spirit_update` op 2);
  - calls `g_state_handlers[state](&g_wrestler_ctx)` (0x80144CF0: 211 states, 182 distinct handlers).

Handlers read everything through the context (`ctx->self`, `ctx->rec_a` the opponent,
`ctx->pressed`); World Tour and Revenge used global "current wrestler" pointers instead.

### Input

N64 bit order: A 0x8000, B 0x4000, Z 0x2000, Start 0x1000, d-pad up / down / left / right
0x0800 / 0x0400 / 0x0200 / 0x0100, L 0x0020, R 0x0010, C-up / down / left / right 0x0008 / 0x0004 /
0x0002 / 0x0001. Handlers test A|B|L|R together (0xC030) as "any button", the mash. A per-slot
history of 20 entries (0x80159518 + 56 × slot, `input_history_push`) lets `input_press_kind` tell a
**tap** (A or B pressed recently: weak) from a **hold** (held through the last 8 entries: strong) and
A+B held (the lunge).

**Z: CPU / manual control** (read 2026-11-02, `wrestler_tick`): pressing Z while a C button is held
toggles the slot's flags_e9 0x08 and clears its AI block (mode 0), switching it between the player and
the CPU; team matches (type 0x1000), elimination (0x4000) and survival limit it through 0x8016B9CC bit 0x01
and the slot's flags. The buttons go through `input_remap` (the button configuration) first.

**C-right: changing focus** (`opponent_switch_nearest`, from standing, getting up and the ladder): a newly pressed
C-right sorts the other wrestlers by distance, skips the partner and the current opponent, and makes
the nearest one the new `opponent` (+0x08), so the player turns to and attacks that wrestler.

The input entry also holds the **control stick** (+0x04 x, +0x06 y); `wrestler_tick` sets the entry's byte
+0x08 (the context's `in_byte`) when either axis is past **40**. **The stick is the taunt control**: the
CPU taunts by pushing it (AI mode 50 writes 80 to its stick byte, `ai.md`). Standing clears the byte; lying
down (0x31), the pinned wrestler (0x45) and the turnbuckle climb check it.

## Changing state

`wrestler_set_state` (0x800EB5E0) is the base of every setter:

- saves `state` / `state_step` into `prev_state` / `prev_step`, **except from state 0x30**
  (`state_30_settle`, a pass-through that is never recorded);
- zeroes `state_step`, `state_counter` (+0x84), `state_flags` (+0x9C), `state_timer` (+0xA8), bytes
  +0xF5 / +0xF6 and `bounds_override` (+0x350, a per-state collision set);
- sets flags80 0x01 and flags88 0x10.

Variants: `_now` (also flags88 0x01; 97 callers), `_free` / `_free_now` (also `wrestler_unpair`),
`wrestler_pair_set_state` (two wrestlers into a paired move: each gets the other as `pair_other` and
`opponent`, the first gets `pair_slot` and flags88 0x04), `wrestler_pair_set_state_move` (also flags80
0x01 and flags88 0x02; used for every grapple). Many states are entered through a jump to a shared
setter call, so a search for "the state number next to a setter call" misses them; follow the jumps.

Each state counts its own steps in `state_step`; most steps end when `anim_frames_left` (+0xA2)
reaches 0. **`move_event(ctx, bits)`** (0x8012F660, 145 callers) is the common "animation finished" step:
it does nothing until `anim_frames_left` < 2, then by its bits: 0x40 applies generic move
0x8014DB00[bits & 0x3F] to the opponent; 0x2000 applies the current move's damage to the opponent (with
0x4000 the opponent's to itself); 0x400 tries a chain (`move_chain_check`; a chain returns at once);
0x80 counts a statistic (0x100: the opponent's); then always ends the animation and unpairs; 0x200
settles by the end pose (with 0x800 also `move_aftermath_check`, bleeding / TKO; 0x1000 on itself);
otherwise it goes to state 0x30. So a move called with 0x2000 lands its damage at the end of its animation; strikes apply theirs at
the hit (`strike_land`), and some states and animation frame events apply it earlier themselves.

## After a move: state 0x30 and end_pose

Moves and many animations end by setting `end_pose` (+0xE2, the pose the animation ended in) and
going to **0x30** (`state_30_settle`), which picks the next state from the pose: standing, getting
up, lying (0x31), off balance (0x33) and so on; on a ladder (location 0x08) 0xAE / 0xB1.
`wrestler_settle_pose` does the same directly. In RAM 0x30 lasts one frame.

`state_30_settle` (read 2026-11-02): on a ladder (location 0x08) it goes to 0xAE (or 0xB1). **Step 0**
copies the animation's end pose (anim player +0x2EF) into end_pose and plays the pose's idle by a jump
table (0x8014E5B8): standing 11745 on the apron (`settle_zone`), 12804 on the cage wall, 12745 / 12519
standing, 12171 crouched, 12558 leaning on the ropes, 12092 the corner crouch; poses 20-47 are kneeling
/ lying poses (odd ones set flags8a 0x01, the face-down side) from 0x8014DA98. **Step 1** picks the state
(0x8014E680): standing → 0x01; poses 20-47 → 0x4E, or 0x4F (count_timer 60) with flags8a 0x10; off
balance → 0x33; on the ropes → 0x32; the turnbuckle perch → 0x0A (step 3); 0xA5; one case with flags8e 0x40 → 0x50
at step 5 (12564, a grapple straight from the pose); default 0x01. It clears flags8a 0x10 on the way out.

## Moves and animations

- `wrestler_start_move(w, slot, move)` points `w->move` at record `move` (0-547) of the slot's move
  set (`g_movesets` + 0x4D10 × slot + 36 × move) and copies `move_anim` / `move_anim2` from
  `g_move_anims` / `g_move_anims2` (548 u16 per slot, 1,096 bytes apart). `wrestler_set_generic_move`
  uses the shared generic table at 0x800A6040 instead (weapon attacks add 5 × the object's type).
- **`move_anim` is a code, not a file number.** `anim_style(w, part)` turns it into the animation
  file: s16 RAM table 0x8015A700[code] + 13090 + part (checked in RAM 2026-11-02: code 4790 → 13093,
  the strike playing). Part 0 is the attacker's animation, part 1 the receiver's, and so on.
- `wrestler_set_anim(w, anim, mode)` stores the file in `anim_id` (+0xA6) and the per-slot table at
  0x801593A0 and restarts the animation player (+0x2E8) when it changed.

## Randomness

- `rng_match` (0x8012E07C): the LCG of World Tour and Revenge, `s = s × 0x5D588B65 + inc` (state
  0x80159350, increment 0x80159354), returning `(s & 0xFFFFF) / 1000` and rerolling 1000 and up.
- `rand_pool_next`: a pool of pre-drawn values (the AI and some states).
- `chance_check(n)`: true with probability n / 1000. `chance_roll(kind, ...)` first turns a stat into a
  per-mille chance (tables in `damage_and_spirit.md`).

## Other per-frame pieces

- `wrestler_joints_update`: copies the model's world joint positions into the record (+0x120 on,
  12 bytes each), used for hit tests and placement.
- `damage_history_tick`: every 30 frames moves each wrestler's damage history on one second.
- `health_regen`: every 90 frames (60 with +0xF3 bit 2) adds the attribute regen values.
- `objects_update`: weapons and props.

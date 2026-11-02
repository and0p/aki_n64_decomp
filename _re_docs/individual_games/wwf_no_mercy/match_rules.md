# No Mercy: match types, falls, count-outs, the clock and run-ins

Read from the code (2026-11-02); the guest-referee bit was confirmed in RAM. Names in
`symbols/nomercy/`; World Tour's equivalent is `../wcw_world_tour/match_end.md`. The per-frame order of
these checks is in `frame_loop.md`.

## The rule words

Both are set by the menus before the match; readings come from the functions that test each bit.

**g_match_type (0x80097EC0)** (re-checked 2026-11-02 by scanning every test of each bit in the match
overlay; `match_load` tests nearly all of them while setting up)

| Bit | Meaning (from its users) |
|-----|--------------------------|
| 0x1 - 0x800 | only `match_load` (set-up: probably arena, music or presentation choices) |
| 0x1000 | team play with fixed partners (the partner is slot ^ 2: `ai_decide_in_ring`, `ai_decide_tag`, the lock-up tag checks, mode 143); also run-ins with rule 0x200 (`run_in_check`) |
| 0x2000 | tag rules (`wrestler_may_act`, `illegal_man_check`, `apron_walk_dpad`, the tag deciders; the partner from 0x8016B92C) |
| 0x4000 | elimination: **Survival** (with rules 0x100000, slot 0's fall ends it) and **Royal Rumble** (`elimination_record`, AI mode 144, the next entrant through 0xA3) |
| 0x8000 | a tag variant (`tag_partner_takes_over`, `ai_decide_tag`, `ai_decide_floor`, the HUD panels; one-on-one count-out results in `match_result`) |
| 0x10000 | cage (`cage_climb_check`, `run_exit_check`, the lock-up's C-right whip into the cage, the camera) |
| 0x20000 | set by the **handicap** style (2 vs 1, with 0x40); it enables `run_in_check`'s `run_in_try(3, 2)` slot-3 entrant, state 0xA3, `match_events` and the start places |
| 0x40000 | **Ironman** (with rules 0x80000; `match_init_wrestlers` start positions) |
| 0x80000 | guest referee (RAM: 0x00081403 in a guest-referee match; the pause menu, camera, HUD) |
| 0x100000 | **Ladder match** (the menu's Ladder category: the prize and ladder objects, `area_change`, crowd weapons without types 14 / 22, the ladder AI `ai_think`) |
| 0x200000 | a time-limit **decision** (the healthier wrestler wins: `match_result`, `match_record_fall`); never set by the menus or the story script (2026-11-02), so unused in the retail game |
| 0x400000 | regen and elimination with the clock (run-ins at half chance; the pause menu, camera) |
| 0x800000, 0x1000000 | camera / pause menu only |
| 0x2000000 | ending on the clock by falls (ironman: `match_clock_tick`, `match_end_start`, HUD) |
| 0x4000000 | line-ups with falls left (survival / gauntlet: `falls_left`, `elimination_record`, `tag_partner_takes_over`) |
| 0x8000000 - 0x80000000 | `match_load` / `match_frame` / `match_init_wrestlers` only |

**g_match_rules (0x800A9950)**

| Bit | Meaning |
|-----|---------|
| 0x01 | TKOs allowed (`move_aftermath_check`'s TKO stagger, `match_end`) |
| 0x02 | rope breaks (pins and holds near the ropes are broken: `state_45_pin_defender`, `state_49_ground_held`) |
| 0x10 | submissions count (a hold's give-up is a fall; also `input_c_left`) |
| 0x20 | pins count (the referee counts: `state_45_pin_defender`, `ai_finisher_check`, `input_c_left`) |
| 0x40 | **no tag rules** (tornado): `wrestler_may_act` lets everyone act, and the CPU's tag checks are skipped |
| 0x200 | run-ins |
| 0x400 | over-the-top elimination: standing up on the floor eliminates (end_flags 0x10); no whips over the ropes in the ring |
| 0x800 | **bleeding on** (`move_aftermath_check` rolls `chance_roll` kind 4 only with it) |
| 0x1000 | a bleeding rule (`move_aftermath_check` after a cut, `anim_frame_event_c`): probably first blood |
| 0x2000 | the CPU does not use C-left on a downed opponent; weapon DQs (`strike_dq_check`) |
| 0x4000 | falls count anywhere (pins and submissions outside the ring), with longer count-out waits for the CPU |
| 0x40000 | `match_init_wrestlers` only |
| 0x80000 | falls are counted (`fall_count_add`) instead of ending the match; the clock counts down |
| 0x100000 | with g_match_type 0x4000, slot 0's fall ends it; also the HUD result text, `wrestler_tick` |
| 0x200000 | leaving the ring area allowed (`area_change`, `lockup_place_move`): backstage brawling |

## How the menus set them (`match_rules_build`, ovl_c 0x80135EE8)

Read 2026-11-02 in the ovl_c menu overlay (the recomp's `func_80135EE8`; ovl_b's 0x8010D29C is the story
mode's script interpreter, whose "start a match" opcode sets rules 0x200000 and a cage type for one arg).
It clears both words and builds them from the **match category** word 0x800A957C and the **rule option
bytes** 0x800AA045-0x800AA04F, which follow the order of the rule menu's names in ROM file 17524 ("Time
Limit, Count Out, Pin, Submission, TKO, Rope Break, DQ, Bloodshed, Interference, Tag Help Time").

**g_match_type bits 0x1-0x8 are the human players:** bit k is set when slot k has a controller
(0x800A6028[k] ≤ 0 and its pad 0x800A4038[k] ≠ −1); in tag and handicap matches the partner slot copies
its team-mate's bit. (A guest-referee match with two players read 0x00081403: bits 0 and 1.)

**The category word** is written by the menu code at 0x8012D9xx-0x8012DC9C from the selected item id
(34076 on) together with the number of wrestlers (0x800A9582). The low bits are the match style, named by
the Exhibition text in ROM file 17525:

| Category bits | Style | Wrestlers | g_match_type added |
|---------------|-------|-----------|--------------------|
| 0x01 | Single match (1 vs 1) | 2 | 0x1000 (and rules 0x200, interference, unless that option is off) |
| 0x02 | Tag match (2 vs 2) | 4 | 0x20C0 (tag); tag help time option 0x800AA04E; its 6th value ("No Tag") adds rules 0x40 |
| 0x04 | Triple threat (1 vs 1 vs 1) | 3 | 0x8000 |
| 0x08 | Handicap (2 vs 1) | 3 | 0x20040 and rules 0x40 |
| 0x10 | Cage match (1 vs 1) | 2 | 0x11000 |
| 0x20 | **Survival** (Single Play) | | 0x4000 and rules 0x100000 (the player's first fall ends the run) |
| 0x40 + style | **Ladder match** | | 0x100000, plus 0x1000 / 0x20C0 / 0x8000 / 0x20040 by style |
| 0x80 + style | **Guest Referee** (the referee is a 3rd / 4th wrestler: 129 → 3, 132 / 136 → 4) | | 0x80000, plus 0x1400 / 0x8800 / 0x20840 by style |
| 0x100 + style | **Ironman match** | | 0x40000 and rules 0x80000 (falls counted) |
| 0x200 | **Royal Rumble** (4 in the ring; 0x800A958C = 4; the word read 0x20200) | 4 | 0x4000, and rules 0x40000 unless option 0x800AA04F |

The rule options (0 = on unless noted):

| Byte | Menu name | Effect |
|------|-----------|--------|
| 0x800AA045 | Time Limit | index into 0x801500BC → the time-limit byte 0x8009FE6C (none for categories 0x220) |
| 0x800AA046 | Count Out | 0 → 10 counts, 1 → 20 counts, 2 → "Hardcore": no count-out and rules **0x4000** (falls count anywhere); in categories 0x220: 0 → rules 0x400 (over the top), 1 → 0x4000 |
| 0x800AA047 | Pin | rules 0x20 |
| 0x800AA048 | Submission | rules 0x10 |
| 0x800AA049 | TKO | rules 0x01 |
| 0x800AA04A | Rope Break | rules 0x02 |
| 0x800AA04B | DQ | rules 0x2000 |
| 0x800AA04C | Bloodshed | 0 → rules 0x800 (blood); 1 ("First Blood", outside category 0x100) → 0x1800 |
| 0x800AA04D | Interference | with style 0x01, 0 → rules 0x200 |
| 0x800AA04E | Tag Help Time | tag matches; the "No Tag" value → rules 0x40 |

Afterwards, for the game mode returned by `0x801091F0`: mode 8 adds g_match_type 0x400000 and rules 0x200000
/ 0x200 from 0x801064C4; mode 3 copies a line-up byte. **The modes were named in the emulator** (2026-11-02, a headless run driven through the
menus, reading 0x800A957C after choosing each): Multi Play → Royal Rumble 0x20200, King of the Ring 0x01
(a single match), Guest Referee 0x81, Ladder Match 0x41, Ironman Match 0x101; Single Play → Survival
0x20. Exhibition gives the styles 0x01-0x10. (Pay Per View starts with text entry and was not walked.)

## How a fall ends (`match_end`, 0x80116CDC)

Each wrestler's `end_flags` (+0x94) says how it lost (or, for 0x100, won). `match_tick` hands
`match_end` one cause a frame in this order:

| end_flags | Set by | Result code |
|-----------|--------|-------------|
| 0x08 pinned | `referee_count_tick` at three | 0x4000 |
| 0x40 TKO | the TKO check | 0x10000 |
| 0x20 submitted | the ground hold 0x49 | 0x2000 |
| 0x10 out over the top | standing on the floor under rules 0x400 | 0x80000 |
| 0x100 last one standing (a win) | standing, entered under elimination rules, everyone else out | 0x800 |
| 0x200 guest referee's call | `guest_referee_call` | 0x800000 |
| 0x400 disqualified | `strike_dq_check`, `guest_referee_call` | 0x20000 |
| 0x800 lost the ladder match | the opponent grabbed the prize (0xBD) | 0x40000 |
| 0x1000 the opponent escaped the cage | `state_C5_cage_escape` | 0x2000000 |

end_flags 0x01 / 0x04 are set by `match_record_fall` (0x01 = has lost a fall: such a wrestler leaves the
ring, 0x96 / 0x6E / 0x99).

What happens then:

- **0x800 (last one standing):** the first slot with the flag clears it and is recorded the winner.
- **0x800000 (guest referee's call):** `match_result(0x800000)` and the bell: the whole match ends.
- **0x80000 (over the top / count-out):** needs every wrestler in play (flags_e9 0x10) to be out; records
  a fall for each, then `match_result`; with elimination rules slot 0's fall ends it outright.
- **The rest:** for each loser the flag is saved (+0x90) and cleared; under rules 0x80000 a fall is
  counted (`fall_count_add`; for a DQ the slot in the loser's `dq_victim`, +0x6C) instead; otherwise `tag_partner_takes_over`
  decides whether the fall ends the match (`match_record_fall` with 0x400, then `match_end_start`, except
  for the cage escape) or play goes on.

## The count-out (`countout_tick`, 0x801166B8)

- On when the option byte 0x800A9462 is not 0: **10 or 20 counts** (table 0x80146568).
- Every frame, in the ring arenas (g_arena < 10): if any wrestler in play is off the ring and apron
  (location & 3 == 0) and `match_can_interrupt` allows, the referee counts: **a count every 2 seconds,
  3 seconds once 5 or fewer are left, 4 once fewer than 3 are left**; paused while a pin count runs; the
  count restarts when everyone is back in.
- At 0 with a wrestler still outside: a count-out (the bell; under rules 0x80000 a fall for each wrestler
  outside, otherwise `match_result(0x80000)`, the same result code as over the top).
- In guest-referee matches the count only moves when the guest referee counts (C-left,
  `state_A8_referee_countout`, one count per gesture).

## The clock (`match_clock_tick`)

g_match_minutes / seconds at 30 frames a second. With a time limit (byte 0x8009FE6C → 60 / 5 / 10 / 15 /
30 / 60 minutes, 0x80146574) it shows the clock (counting down under rules 0x80000) and at the limit
either flags the end by falls (ironman, g_match_type 0x2000000) or ends the match (`match_result(0x8000)`,
a time-limit draw).

## Run-ins

- `run_in_check` (every frame) with g_match_type 0x1000 and rules 0x200: `chance_check(500)` picks the
  order and tries `run_in_try(2, 0)` / `run_in_try(3, 1)`; a second path (types 0x20000 / 0x4000) tries
  slot 3 for side 2 and looks for wrestlers on the floor.
- `run_in_try` brings a run-in into an empty slot (roster_id −1, flags_e9 0x02) for a side when its
  condition word allows (0x800AA078[side]: 0 any time; 0x100 | t once the clock reaches t seconds; 0x200
  | n when g_arena is n) and its run-in is **armed**: one per side (bit 0x10 << side in 0x8016B650).
- **Royal Rumble entrants (type 0x20000)** (read 2026-11-02): `run_in_check` also tries `run_in_try(3, 2)`
  every frame: slot 3 for side 2, whose condition is the single word 0x800AA07C (not the per-side table);
  `match_init_wrestlers` arms it at the start (0x8016B650 bit 0x40) when that word is non-zero, so the
  entrant arrives when the clock reaches its time (0x100 | seconds) or at once (0).
- **Elimination / survival line-ups (type 0x4000):** when no one is in a held move (flags8c 0x8000), each
  slot whose wrestler has dropped out (flags_f1 0x01) and is on the floor (location 0x04), standing or lying (state 0x01 /
  0x31) clears that flag and its end_flags 0x01 and starts **0xA3**: the next member of its line-up
  (`team_member`, `elimination_record`) runs in.
- Arming: **a side's wrestler reaching 0 health** (`wrestler_add_health`: chance_check(500), or 250 under
  type 0x400000), and **each minute after 10** (`match_clock_tick`: chance_check(500), 250 once under
  0x400000).
- The run-in takes the side wrestler's `ally_id` (+0xD8) as its roster id, marks that wrestler's
  flags_f1 0x08, and starts state 0xA3 at step 13.
- **State 0xA3** (`state_A3_run_in`, first pass 2026-11-02): the entrance title on the HUD and its music
  (clock-timed steps of 30 frames), loads the wrestler (`slot_load_b`, `moveset_unpack`,
  `wrestler_reset`, `move_metrics_measure`, `wrestler_model_set_costume`, the name text), shows the
  model, and gives it **spirit + 100 and `spirit_update` op 5: a run-in arrives in finisher mode**. It
  then runs in from its start point at double speed (`walk_speed(w, 8)`) with the arena collision rebuilt
  as it goes, slides to a stop (11853), turns toward its target (within 171 angle units) and attacks
  (`move_event`); it comes over the apron through 0x6E.

## Special matches

- **Cage:** `cage_climb_check` (cage match, in the ring): C-right, or A / B at an opponent up on the cage
  (location 0x400), starts 0xC8. `state_C4_cage_climb`: a mash count of 20 in finisher mode, else 50,
  minus 5 plus rand 0-9 (so **15-24 or 45-54 presses**) for each of **two halves**; each loop of the
  climb animation costs 5 spirit; holding d-down 21 frames (or d-down + C-up) climbs back down; with
  flags_ee 0x01 it falls (0xC6). At the top `state_C5_cage_escape` drops to the floor (2.61 a frame until
  y < 218) and gives the opponent end_flags 0x1000. Hanging 0xC6, hit while hanging 0xCA, attacks from
  the top 0xCF, a running B at an opponent up on the cage 0xCB, the cage whip 0xC2 / 0xC3, into the cage wall 0xC1.
- **Ladder:** the prize is object 6; ladders are objects (type2 25) climbed with C-up
  (`input_ladder_climb`: a free side, held_obj, ladder_side, state 0xAA). States 0xAA-0xBD: climb, steps,
  top, strikes and shoves across the top, pulling a climber off (0xB4), jumping off (0xB6), knocked off
  (0xB2 / 0xB3), the prize grab 0xBD (end_flags 0x800 for the other).
- **Battle royal / Rumble:** rules 0x400 (over the top), type 0x4000 (elimination, the next entrant through 0xA3), last one standing
  (end_flags 0x100).
- **Guest referee:** the referee slot (flags_e9 0x04) calls falls and DQs with C-left (0xA9) and counts
  count-outs (0xA8).
- **Tag:** `illegal_man_check` / `illegal_man_leave`, tagging with C-up (`input_tag`; `tag_exchange` puts the partner on the apron in 0x93 and the legal man in 0x94), the
  partner waiting on the apron (0x1B), the double-team finisher (A+B in a strong grapple).
- **Tables:** three breakable tables (location 0x40 / 0x100 / 0x200, `g_tables_intact`); a finisher landing
  on one breaks it (`finisher_effects`).

## The result (`match_result`, 0x80117438)

Read from the code 2026-11-02 (a first pass: the per-side sums are clear, some stat arrays are not
named yet). It builds three slot lists on the stack (side A, side B, and everyone in play) and ends with
`match_record_fall(winners, losers, code, ...)`; when the winners and losers are the same list
(everyone) the code carries 0x1000, **a draw**. For each slot in play (roster_id not −1, flags_e9 0x10)
it adds to its side: health, the count of its wrestlers **outside** (location & 3 == 0), a per-slot score
(0x80159218) and a per-slot statistic (0x8009FDF8). The side comes from a per-slot bit (0x10 << slot,
table 0x8014636C).

- **The guest referee's call (0x800000):** with all four slots in play and none flagged, everyone is
  recorded (0x10400); otherwise the slot with end_flags 0x200 decides the side.
- **Count-out (0x80000):**
  - one-on-one (g_match_type 0x8000) and elimination (0x4000): nobody of side A outside → **a draw**
    (0x81400); one outside → side A loses (0x80400);
  - type 0x1000: equal numbers outside → **a draw** (0x101400); otherwise the side with more wrestlers
    outside loses;
  - types 0x20000 / 0x2000: both sides with someone outside → **a draw** (0x101400); otherwise the side
    outside loses.
- **Time limit (0x8000):**
  - elimination (type 0x4000), or neither type 0x200000 nor rules 0x80000: **a draw** (0x9400);
  - with rules 0x80000 (falls counted): the sides' scores are compared (sorted for more than two) and the
    higher wins (0x1008400), equal is a draw;
  - type 0x200000 without rules 0x80000: **the healthier wrestler wins** (0x8C00); equal health → the
    higher statistic at 0x8009FDF8; equal again → `chance_check(500)`, **a coin flip**.

So a normal one-fall match that runs out of time is a draw, an ironman / falls-counted match is decided
by the score, and the 0x200000 type (probably a "decision" setting) goes to health, then a coin flip.

## Open

- `match_result`'s words (code 2026-11-02): 0x80159218 is the per-slot **falls won** count (`fall_count_add`:
  a TKO, a count-out under rule 0x80000, a submission, `match_end`); 0x8009FDF8 is the first of three 4 x 4
  u16 tables kept by `stats_count(w, other, table)`: table 0 counts **attacks landed** by w on other and in
  w's own cell (strikes, moves, dives, double teams), table 1 (0x8009FE18) is counted by the receiving or
  countering side (move defenders, counters, reversals, the ground grapple), table 2 (0x8009FE38) by
  `move_event` codes; `match_result` compares each side's own table-0 cell, i.e. attacks landed. Still open:
  the menu names of types 0x200000 / 0x2000 / 0x1000.
- ~~Pay Per View and Smackdown modes' category words~~ (settled 2026-11-02 in the ovl_c IDA build, `py
  tools/ida/nomercy_build_idb.py --overlay c`): the **game mode** byte is 0x800A9578 (`0x801091F0` returns it),
  set when a menu screen (0x8015B9E0) is entered by `0x8010DAF8`: screen 1026 -> 0, 1028 -> 1 (Royal Rumble:
  0x8012D918's items 34082-34084 give 0x20200 / 0x10200 / 0x40200), 1032 -> 3 (King of the Ring, the
  line-up byte), 1037 -> 4 (Guest Referee), 1038 -> 5, 1039 -> 6, 1027 -> 7 (Survival, category 0x20),
  1036 -> 8 (Championship, the story: g_match_type 0x400000 and rules 0x200000, the script opcode in ovl_b
  adds the rest), 1031 -> 10, 1043 -> 15 (16 entrants), 1120 -> 2 (**Pay Per View**). Pay Per View builds
  its card as 34-byte entries at 0x800BD5A8 (wrestlers at +2, kinds at +10, the match kind at +14) and
  `0x8013AA48` sets the category word from the kind through the table at 0x8014FFAC: 0 single 0x01, 1 tag
  0x02, 2 Royal Rumble 0x20200 (40 entrants), 3 triple threat 0x04, 4 handicap 0x08, 5 cage 0x10, 6-9
  ladder 0x41 / 0x42 / 0x44 / 0x48, 10-12 guest referee 0x81 / 0x84 / 0x88, 13-16 ironman 0x101 / 0x102 /
  0x104 / 0x108. The Smackdown Mall is the shop, not a match mode. The mode order in the help text (ROM
  file 17525): Exhibition, Royal Rumble, Pay Per View, King of the Ring, Guest Referee, Ladder, Ironman,
  Championship, Survival, Smackdown Mall, Options. Still open: the exact menu screens behind modes 5 / 6 /
  10 / 15. ~~The menu names of types 0x200000 / 0x2000 / 0x1000~~: 0x1000 is every single-wrestler style
  (Single, and the ladder / guest referee / ironman singles) and 0x2000 every tag style, from
  `match_rules_build`'s category table above; **g_match_type 0x200000 is set by nothing**: neither ovl_c (the
  menus) nor ovl_b (the story script, which sets only g_match_rules = 0x200000, the backstage-brawling rule)
  writes it, so the time-limit decision path in `match_result` is dead code in the retail game (ovl_b and
  ovl_c IDA builds, 2026-11-02).

# WWF No Mercy: first notes

Started 2026-11-01. Written as differences from Revenge (`../wcw_revenge/README.md`) and World
Tour; anything not mentioned here is not checked yet. No Mercy is WrestleMania 2000's sequel
and shares its engine, which differs much more from Revenge than Revenge did from World Tour.
WrestleMania 2000 is not analysed yet.

## System docs

Per-system notes (2026-11-02), laid out like World Tour's; this README keeps the overview, tooling and
landmarks:

| Doc | Covers |
|-----|--------|
| `frame_loop.md` | match_tick's phases and per-frame order, the rotating update order, the context, state setters, 0x30 / end_pose, moves and animation codes, randomness |
| `movement.md` | standing (0x01) and its input priority, walking speeds, running and what it turns into, collisions, the location bits, keep_in_bounds |
| `strikes.md` | starting a strike (tap / hold / A+B), the hit test, landing and reactions, blocking, the timed R counter and its outcomes, the kick catch, L |
| `grapples.md` | the attempt and its resolution, grapple kinds by place, the lock-up (timers, walking, inputs, move numbers, reversals), grapple moves, the whip, ground grapples, lifts and carries |
| `pins_and_holds.md` | every escape_time op, down and getting up, the get-up attack, the TKO check, pins step by step, rope breaks, the ground hold |
| `damage_and_spirit.md` | health, regions, the damage formula, the attitude meter, chance_roll's tables, the attribute record |
| `match_rules.md` | the rule words, falls and result codes, the count-out, the clock, run-ins, cage / ladder / battle royal / guest referee / tag / tables |
| `ai.md` | the CPU: perception, situations, the where-class deciders, weighted tables, traits, modes |
| `objects_and_weapons.md` | object records and physics, getting and using weapons, ladders, tables and the steps |
| `animation_data.md` | move sets from the roster to an animation (files 495-497 and the packed move-set files), file 703's per-animation records (active window, end place / pose, paired-move frames), the frame events (sounds, hand shapes, ropes), the follow-up variant files |
| `wrestler_models.md` | appearances, part slots, texture layers, seams, the previewer |
| `anim_tags.json` | Andrew's viewer tags of the animations of states that were unclear |

## ROM revision

Everything here is for the **USA Rev 1** ROM (SHA1 `91cee3d0…`, `_roms/WWF No Mercy (USA) (Rev
A).z64`), the one the No Mercy recomp accepts. Rev 0 (`WWF No Mercy (USA).z64`) has its code
about 0x6C–0x9C bytes later in the ROM, by an amount that changes along the way, so the recomp's
symbol dump does not fit it. `akiemu --game nomercy` loads Rev 1.

## Tooling

- IDA database: `py tools/sym/build.py --game nomercy --ida` (`tools/ida/nomercy_build_idb.py`,
  `AKI_GAME=nomercy` in `wcw_idb_setup.py`), output in `_tools/ida_nomercy/`. It loads main and
  the match overlay ovl_e; function starts come from the recomp's `syms/dump.toml`. 1,874
  functions, 2 fail to decompile. 47 of the dump's starts sit inside a function IDA made longer.
  Three AI mode handlers (`ai_mode_49`, `ai_mode_55`, `ai_mode_63`) are only accepted by
  `add_func` with their end given, so the build retries refused starts with the dump's sizes
  (`AKI_SEED_SIZES`).
  `py tools/ida/nomercy_build_idb.py --overlay c` builds the **menu overlay ovl_c** instead (output in
  `_tools/ida_nomercy_c/`, 1,746 functions, main-segment names only) and `--overlay b` the **story overlay
  ovl_b** (`_tools/ida_nomercy_b/`, 1,248 functions); both are read through their `export.json` or the IDA
  MCP, not through `sym.py` (2026-11-02; used for the game modes, the Pay Per View card and the type bits).
- Symbols: `_re_docs/symbols/nomercy/`, same format; `types.h` maps the Wrestler record (most fields to +0x358),
  WrestlerCtx, MoveRecord and the AI block. After a field-layout change rebuild with `--ida`; the sym database also keeps
  an `access` table of typed field uses (`sym.py show` lists them).
  - `from_revenge.toml` / `from_wcw.toml`: names carried over by `match_games.py` (identical
    code, 281 pairs, mostly library code), `match_consts.py` and `match_calls.py`, merged by
    `tools/sym/merge_matches.py`. Unchecked; a state number inside a carried-over name is the
    source game's.
  - `states.toml` / `ai.toml`: every handler in the state and AI tables, under No Mercy's own
    numbers, with any carried-over name as a suffix.
- Archive: `tools/rom/aki_archive.py` knows Rev 1: **19,458 files**, data at ROM `0x1BD150`
  (right after the overlays), offset table at `0x16C31D8` (after the data); `verify` passes for
  all. `survey` recognises 3,335 CI4 and 391 CI8 textures with their palettes and 656 `TEX`
  files, but leaves 12,113 files unknown (animations and models not checked yet).

## Memory layout

From the recomp's splat config (`_recomps/WWFNoMercyRecomp/disasm/nomercy.yaml`):

| Piece | ROM | VRAM |
|-------|-----|------|
| main text / data | 0x1050–0x40F80 / –0x57310 | 0x80000450–0x80040380 / –0x80056670, bss –0x800BD960 |
| ovl_a | 0x57310 | slot 0x800D9960 |
| ovl_b, ovl_c, ovl_d | 0x81210, 0xAE390, 0xFD250 | slot 0x80106760 (each can sit beside ovl_a) |
| **ovl_e (the match)** | 0x144BE0–0x1BD150 | text 0x800D9960–0x80144670, data –0x80151ED0, bss –0x8016B9F0 |

ovl_e spans both overlay slots. It is taken as the match overlay because it holds the ROM's only
`rng_match` (`0x8012E07C`, same LCG as the earlier games) and the state and AI tables.

## First landmarks

| What | No Mercy | Revenge | World Tour |
|------|----------|---------|------------|
| `rng_match` | 0x8012E07C | 0x800D4204 | 0x800A0468 |
| `wrestler_update` | 0x800E5890 | 0x8009D8D0 | 0x800A7C80 |
| `g_state_handlers` | 0x80144CF0, **211 states** (182 handlers) | 146 | 120 |
| `g_ai_modes` | 0x80145A10, **145 modes** (111 functions) | 62 | 48 |
| `g_wrestlers` | 0x80159990, 4 × **856 bytes** | 944 | 928 |

## A different engine

- **Handlers get a context, not globals.** `wrestler_update(slot)` fills `g_wrestler_ctx`
  (`0x80159080`) with the slot, the related slots' indices (record +0x00 to +0x0C), pointers to
  their records and the slot's input words, then calls `g_state_handlers[w->state](&ctx)`.
  Revenge and World Tour kept "current wrestler" pointers in globals instead.
- **The state is a u32 at +0x14**; position at +0x120; flag words around +0x80 to +0x8E. The
  record is laid out anew, so Revenge's field map does not carry over.
- **Update order rotates.** `wrestlers_update_all` starts at `g_update_first_slot`
  (`0x801591F8`), goes round the four slots and moves the start on by one each frame (World
  Tour and Revenge update in a fixed order). This matters for frame-exact replay.
- Matched state handlers keep their order loosely (stand 0x01, run 0x03, corner slump 0x18,
  taunt 0x1B, down 0x31, dazed 0x32, block 0x4C) but with many states in between, so there is no
  simple offset table like Revenge's.

## Wrestler record and context (code plus RAM, 2026-11-01)

`struct Wrestler` (856 bytes) and `struct WrestlerCtx` are in `symbols/nomercy/types.h`; every
state handler now has the prototype `void state_XX(struct WrestlerCtx *ctx)`, so the pseudocode
reads `ctx->self->state_step` and so on, and `sym.py --game nomercy struct Wrestler` counts the
functions using each field. From RAM (singles and a 4-CPU tag match): +0x08 is the opponent
(the other team's legal man), +0x0C the tag partner (itself in singles), +0x14 / +0x18 state and
step, +0x1C / +0x20 the previous state and its last step. From code: position at +0xFC and facing
at +0x10C (`point_at_offset` places one wrestler against the other's), ring zone at +0x60,
animation id at +0xA6 (`wrestler_set_anim`), a countdown at +0xA2 that ends many steps, the
animation player at +0x2E8 and a pointer to the wrestler's data at +0x310 whose +0x20 holds trait
bits. The context's `pressed` word is the newly pressed buttons in N64 bit order.

State setters (`match.toml`): `wrestler_set_state` (0x800EB5E0) saves the old state into
prev_state (except from 0x30), zeroes the step, `state_timer` (+0xA8, counted up by
`wrestler_update`), `state_flags` (+0x9C) and `bounds_override` (+0x350, a per-state collision set); `_now` (97 callers)
also sets flags88 bit 0x01; `_free` / `_free_now` also unpair; `wrestler_pair_set_state`
(0x800EB824) puts two wrestlers into a paired move and links them through `pair_slot` (+0x30),
`pair_other` (+0x00) and `opponent`. Every handler's desc in `states.toml` now lists the functions
that set it. `location` (+0x82) is 0x01 in the ring and 0x02 on the apron (RAM).

Slots 2 and 3 are in use in a singles match too (slot 3 standing at the ring edge, probably the
referee).

## Gameplay systems (code, 2026-11-02)

Function and field details are in `symbols/nomercy/` (`match.toml`, `types.h`); this is the map.

- **Health and damage.** Health +0xAA (0-255), capped by health_max +0xAC. `apply_damage` takes the
  hit move's damage (hit_move +0x06), x1.2 in finisher mode, off health, and a quarter of it off
  health_max (not below 64); five body regions (+0x2C4, floats) drop by the move's per-region
  damage, and the weakest is remembered (+0xE7). Health thresholds: 0 (run-in roll), < 49 extra
  spirit, < 100 weakened reactions, 60 after a fall.
- **Attitude meter.** Spirit +0xAE, 0-100, with a mode in flags_ee and a timer +0xB0
  (`spirit_update`): drifts toward 50 every 120 frames, -8 a step while hiding outside; 100 gives
  SPECIAL for 600 frames (then 70); a taunt while SPECIAL turns it into finisher mode for 600
  frames (damage x1.2, health_eff reads health_max, regions read 50; then 60); 0 gives an empty
  meter for 300 frames (then 40). `spirit_exchange` moves spirit after each hit from the move
  record (+0x07 attacker, +0x08 defender).
- **Moves.** 548 moves per wrestler (`g_movesets`, 36-byte records, loaded per slot), with
  animations for both sides (`g_move_anims`, `g_move_anims2`); `wrestler_start_move` points
  Wrestler.move at one. The two finishers are named in the attribute record (attrs +0x1C /
  +0x20). Move numbers seen: 172-174 weak strikes, 183-185 kick-catch follow-ups, 186-188
  strong, 190 the A+B lunge. Grapple moves by number (lockup_input): front weak A 1-4 (none, side, up, down),
  weak B 5 / 10 / 15 / 20, strong A 25-40, strong B 45-60 (steps of 5), rear 73-80, finishers 70
  (front) / 81 (rear); running grapples 108 / 130 / 145; 159 / 161 reversed moves.
  Strikes: weak 172-175, corner 176-179, strong 186-189, lunge 190, from behind 205, 0x1A 207,
  get-up attack 217; kick-catch follow-ups 183-185; rear reversals 84 / 85 / 162, apron 148 / 152.
  Some states use the shared generic move table instead (0x800A6040: 62, 73, 87, 104, 109, 110, ...).
- **Odds.** `chance_roll` turns spirit, health, the escape counter or a spirit difference into a
  per-mille chance (its tables are in its desc). Grapple reversals use kind 1 (spirit), whip
  reversals kind 3, getting up kind 7, resisting a pick-up kind 11.
- **Pins and holds.** The defender's escape counter +0xB4 starts at `escape_time` (at least 15)
  and counts down, faster with mashing (1-3 a press); below 0 it gets out. `escape_time` uses
  finisher_heat (+0xBE, 180 after a finisher) and the damage history (+0x344); its last step is
  hidden by the decompiler.
- **CPU.** `ai_update` per slot, 176-byte AI blocks (`g_ai_blocks`), 145 modes; not read further.
- **Match types.** `g_match_type` (0x80097EC0) and `g_match_rules` (0x800A9950) are set in the
  menus (`match_rules_build`, ovl_c); the bit tables and the menu mapping (singles 0x1000, tag
  0x2000, cage 0x10000, handicap 0x20000, ironman 0x40000, guest referee 0x80000, ladder 0x100000,
  survival / Royal Rumble 0x4000) are in `match_rules.md`. Ladders are objects (type2 25) climbed through
  `input_ladder_climb`.

## Grapples, pins and the clock (code, 2026-11-02)

- **Moving while grappled** (new since World Tour): in the lock-up (0x51 step 2), holding the d-pad
  for 7 frames switches on `grapple_walk` for normal standing grapples. Pushing toward the facing
  (or straight back) walks both wrestlers that way at the pair distance (73.5 front, 53.5 rear,
  `g_grapple_pairs`); other directions turn the pair about 4 degrees a frame, sliding the
  opponent round. The lock-up has a time limit (`grapple_timer`, count_timer), and R lets go.
- **Pins.** The referee counts once a second (`referee_count_tick`, `g_pin_counts`, 30 frames a
  count); three counts set end_flags 0x08. The pinned wrestler's escape time is `escape_time` op 1
  (formula in its desc): 95 plus the pinner's spirit / 5 plus a finisher bonus (10-25) minus the
  pinned wrestler's spirit / 3 and health / 2, plus damage history; mashing takes 1-3 frames off
  per press; 90 frames or more means pinned, except that 90+ spirit caps it at 87. A broken pin
  at count 2 plays the near-fall.
- **Falls.** Each wrestler's end_flags (+0x94) says how it lost (pin 0x08, submission 0x20, TKO 0x40,
  floor under a rule 0x10, five more not identified); `match_end` turns them into result codes.
- **Clock.** `match_clock_tick`: time limits 5 / 10 / 15 / 30 / 60 minutes, then a draw (or, under
  one match type, end by falls: ironman?); after 10 minutes a run-in roll each minute.
- **CPU.** AI mode 0 runs `ai_choose_mode`, which picks the next mode from about 15 situation
  checks; `ai_can_act` gates starting one. Each mode plays the pad: it writes the buttons to its
  AI block (+0x0C), so a mode's code shows what it does. Profiled families: grapple (A), strike
  (B), defend / counter (R, L by chance), walk (d-pad), run and runner responses (C-down),
  ground actions by a downed opponent (C-left / C-down / A in situation 53), lock-up move choice,
  focus switch (C-right), apron, ladder. Situations come from `ai_situation`, a state -> class
  table (`AiSituation`, 61 classes) kept for the CPU and its opponent; ten personality bytes from
  the wrestler's attribute record feed the decisions; in multi-wrestler matches a worn-down CPU
  switches focus to the nearest opponent attacking it.
  Difficulty: the CPU level (0-4) gives a skill of 100 / 300 / 500 / 750 / 1000 (+-20); `ai_perceive`
  blurs everything the CPU reads (health, spirit, distances) by up to (1 - skill / 1000) / 2. Each
  frame the where-class (+0x82, from both wrestlers' locations: ring, apron, floor, turnbuckle)
  picks one of ten deciders (`ai_decide_*`); backstage areas (g_arena >= 10) have their own. The CPU's
  buttons go through the same input entry as a player's.
  The choices themselves are data: deciders hand weighted tables (6-byte entries, cumulative chance
  out of 1000 and a mode) to `ai_pick_mode_weighted`, e.g. ring vs floor: 35% mode 41, 35% mode 45,
  30% mode 32. The tables live in `g_ai_mode_tables` (0x80145298, 26 found by a scan; listed in its
  desc). The commonest picks: 41 approach, 45 back off, 32 wait and watch, 39 fetch a weapon
  (the steps), 6 strike, 1 engage (block or counter what the opponent starts, else grapple), 2
  grapple. Personality traits are levels that index small chance arrays inside the same data
  block (e.g. attrs +36 / +37 in `ai_decide_close`, +38 in `ai_decide_change_area`).
  Reading where each is used: +37 overall aggression, +38 weapons and fighting outside, +39
  specials and finishers, +34 grapple keenness, +35 defence and counters, +36 close-range
  aggression, +25 / +26 and +30 / +31 choices against a downed or hurt opponent (guesses).
  All 211 functions in the AI area are named (all 114 modes; about 90 helpers: deciders, tag
  and partner logic, finisher choice, move lists, lock-up choices, struggle rates).

## State families (profile pass, 2026-11-02)

Named from each handler's setters, animations and calls, then reviewed (2026-11-02): of 182 handlers **115
`confirmed`** (setter code plus Andrew's tags, RAM or a full handler read), **66 `likely`** (one code read; mostly paired
move states whose animation comes from the move under way) and **1 `guess`** (0x7E, no setter). States still needing RAM
or the viewer are labelled "Needs RAM" in their `states.toml` desc and listed in `QUESTIONS.md`.
running (0x03 run, 0x04 rope rebound, 0x05 / 0x35 / 0xC1 collisions, 0x9E leapfrog, 0x9F dodge,
0xA5 run to a side, 0x78 running move); strikes (0x22 / 0x23 and the variants 0x24, 0x25 get-up
attack, 0x2B, 0x2C, 0xBF, 0xC9); grapples (0x50-0x56, running 0xA0 / 0xA1, apron, floor, turnbuckle
0xCD / 0xCE, double team 0x83-0x85, carry 0x80-0x82); pins (0x43 start, 0x44 / 0x4A holding, 0x45 /
0x4B pinned; cover approaches 0x36 / 0x42); dives 0x79-0x7D; weapons (0x8A crowd grab, 0x8C steps,
0x91 pick up, 0x8B drop, 0x8D stand a ladder up, 0x26 / 0x27 / 0x28 attacks); ladders 0xAA-0xBD (climb, top,
strikes and moves from it, knocked off, jumping off, grabbing the prize); the cage 0xC2-0xCF
(climbing, hanging, attacks from the top, the escape; Andrew's viewer tags); tags
(0x93 tag received, 0x94 tag out, 0x95 apron partner, 0x1B apron wait, 0x02 apron corner walk, 0x6E dropping off the apron); end of match (0xA8 victory).

## Exact rules read from the disassembly (2026-11-02)

- Damage: `apply_damage` (formula in its desc: base x (region condition + 50)%, strength vs
  defence / 10, spirit difference / 20, x1.2 in finisher mode).
- Speed: `walk_speed` (classes 1-3 lowered by the legs' condition; 3.4-4.3 normal, 4.5-6.75 fast).
- Reversals: one try per lock-up (`grapple_defender_input`, chance_roll kind 1), move reversals
  (`reversal_check`, kind 8), the L deflect window of 5 frames (`grapple_attempt_resolve`), the
  timed R counter when blocking started less than 3 frames before the hit (`strike_land`).
- Lock-up move numbers (`lockup_input`), finishers 70 / 81.
- Strike reach (`strike_hit_test`), ring bounds by location (`keep_in_bounds`), objects
  (`objects_update`: rest below speed 1.5, cleared below y -20, 1/3 bounce).
- How a fall ends (`match_end`): each end_flags cause maps to a result code (pin, submission, TKO,
  over the top, last one standing, guest referee call, DQ, ladder, cage escape) and then to a
  recorded fall, a fall count, the tag partner taking over, or the end of the match.
- Cage: `cage_climb_check` (C-right, or A / B at an opponent up on the cage) starts the climb;
  `state_C4_cage_climb` needs 15-24 presses (finisher mode) or 45-54 per half, two halves, and
  costs spirit; `state_C5_cage_escape` drops to the floor and gives the opponent end_flags 0x1000.
- Count-out (`countout_tick`, called results_screen before): 10 or 20 counts from the option, on
  only in the ring arenas; a count every 2 seconds, then 3 (5 left), then 4 (2 left); paused
  during a pin count; restarts when everyone is back in; in guest-referee matches only the
  referee's own count (state_A8_referee_countout, C-left) advances it.
- Per frame (`match_tick`): run-ins, wrestlers, pin count, clock, count-out, then one fall cause.
- Battle royal rule (g_match_rules 0x400): standing up on the floor eliminates (end_flags 0x10);
  a wrestler entered under elimination rules who stands while every other one is out wins
  (end_flags 0x100).

## Coverage (2026-11-02)

Every function in the match overlay ovl_e (899) has a name; after the confirming passes of 2026-11-02 (61
functions in the damage, pins, grapple, strike, movement and object areas re-read, 24 renamed; then the 62
CPU helpers in `bulk.toml` and 52 of the mode handlers in `ai.toml` checked against their code, 9 renamed)
then every mode handler in `ai.toml`, the one-pass helpers in `bulk.toml` and the carried-over Revenge /
World Tour names checked against No Mercy's code, and the 20 large graphics / HUD / camera / set-up
functions read in full) the whole database stands at 258 `confirmed`, 868 `likely` and one `guess`, state
0x7E (no setter; RAM). Two carried-over names were wrong (`move_record_unpack` is a texture stamp,
`model_set_anim` the blood stamp). The camera (`camera_update`, `camera_target`, `camera_target_b`,
`camera_preset`) is described in `bulk.toml` for the port's camera work. The main segment
(engine, graphics, audio, file loading; 975 functions) is mostly unnamed (228 named).

## Next

- The open items per system are in each doc's **Open** list and in `QUESTIONS.md` ("No Mercy", including the RAM
  follow-ups). Code first: RAM only when an item is truly blocked.
- Confirming pass over the `guess` functions in `bulk.toml`, area by area, in the order a port needs them:
  damage, pins, grapples and the lock-up, strikes, movement, objects.
- Converter: add the file 703 records, the move sets and the attribute files to the library's asset output
  (`tools/rom/nomercy_models.py` already decodes them: `attrs`, `moves`, `collision`, the viewer).
- WrestleMania 2000 is the closer relative: its IDA build and symbols would make matching far better than
  going through Revenge.

## Working notes for the next thread (2026-11-02)

- **Reading code:** the recomp's `syms/dump.toml` gives function boundaries; IDA via `tools/sym/build.py --game
  nomercy --ida`; `py tools/sym/sym.py --game nomercy show <name>` prints desc, callers, callees and field uses, and
  `check` validates the toml files. Rebuild the database (`build.py --game nomercy`) after editing any toml.
- **Names:** a name in `from_revenge.toml` / `from_wcw.toml` is a call-graph match and may be wrong (`input_strike`
  was really `input_r`, R held, 2026-11-02). Numbers inside carried-over state names are the source game's.
- **Misreads to avoid:** a constant near a setter call is not always the state argument (0x88's moves 141 / 142 were
  once read as states 0x8D / 0x8E); check that the number lands in a1 (or a3 for the pair setters).
- **Emulator:** `py tools/n64emu/akiemu.py --game nomercy --headless --fast --port <port>` with
  `tools/recomp_dbg/akidbg.py` (`AKIDBG_PORT`); use a port other than Andrew's own instance (5664).
- **Viewer:** `py tools/rom/nomercy_models.py viewer` builds `_tools/viewer/nomercy_viewer.html` (git-ignored); headless Edge
  (`--dump-dom`) is enough to check that it loads.
- **Andrew's tags:** `anim_tags.json` holds his descriptions of animations; state names lean on them, so recheck a
  state's name if a tag is corrected.

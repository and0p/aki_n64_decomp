# Open questions

The tracker for reverse-engineering questions that are still open. Each entry names the doc it
belongs to. When one is answered, record the answer in that doc and delete the entry here.
Grouped by how it can be settled.

## World Tour: check in the animation viewer

- **State 0x08**, R on the apron: plays animation 3364, then back to standing. What is it?
  (`outside_ring.md`)
- Animations 3052/3053 (the `chance_roll` 8 branch when a wrestler goes down, probably a roll),
  3055 (L while kneeling after a get-up, probably a roll away) and 3093/3094 (state 0x2A, after
  a move ending in pose class 2). (`knockdown.md`)
- Dive slots 175, 177, 179 and 180 are matched by position only. (`movement.md`)
- Whether the game mirrors x when drawing (`model_animate` negates x); the previewer has a
  toggle. (`file_formats/animations.md`)

## World Tour: check in RAM (paused match)

The debug server in `tools/recomp_dbg/` (pause, frame step, RAM reads, value breakpoints,
snapshots) is the easiest way; IDA attached to the process still works.

- **Smoke test:** take a couple of pauses at random moments of an ordinary match (singles and
  tag) and check the RAM against what the docs predict: state numbers and steps, `grapple_ctx`,
  `recover_count`, `hits_taken`, the ring-out and pin counts, the AI block, region conditions.
  Anything that disagrees goes back into its doc.
- What the u32 at `0x800E678C` is: it went up by exactly one per match tick (found by a RAM
  search in the emulator). A frame counter would anchor traces. (`state_comparison.md`)
- The CPU's AI block (`g_ai_state`, `0x800F1710` + 84 × slot) during a CPU match: confirm the
  mode button readings and the per-frame mash rates. (`ai.md`)
- Whether a wrestler with its own attack frames active really cannot be hit (trading blows).
  (`strikes.md`)
- Whether an illegal tag partner can be counted out or pinned (the counts only use LEGAL
  wrestlers in tag). (`tag_team.md`)
- Rule bits 0x04, 0x08 and 0x40 of `g_match_rules` (`0x8006205A`) under different menu
  settings, and how the CPU level at `0x80062060` maps to the options screen.
  (`match_end.md`, `match_options.md`, `ai.md`)
- The 4 crowd weapon types (moved from "read more code"): their models come from lists filled
  at run time, so read the weapon object (`sub_80091DA8`) in RAM while a crowd weapon is held.
  Revenge's 11 named types (`wcw_revenge/weapons.md`) suggest what to look for. (`outside_ring.md`)

## World Tour: read more code

Answered 2026-11-01 (details in the linked pages, marked as read): dive aiming and `dive_value`,
slots 5/6, the tag-reach thresholds, the result margin, `sub_800A5860` and the body-box joint
mask, "release a grapple with R", the rope push kinds, the scale table and the header stat
bytes. Left here are graphics questions (no RAM needed, but they belong with the model and arena
work, which is on hold):

- Ropes: the rope texture. (`arena.md`)
- Arena: the 5 crowd instances' transforms, the two crowd palette sets, the objects built by
  `0x80091B80`, file 994 and the per-arena flag at `0x800E6288`. (`arena.md`)
- Models: the mesh flag bit (`header[1] & 0x80`), the face texture (`head[9]/[10]`), and the
  unused mesh files 722–890 / 985–1043 (referees, managers, props?).
  (`file_formats/wrestler_models.md`)

## Revenge

- **Battle royal mode**: mostly answered from code (`wcw_revenge/matches.md`: eliminations are
  falls, instant ring-out, entrant list `0x8007FBC0`, entry through 0x8F). Still open: the
  end-of-match check once every entrant is used, how losers leave (0x82 / 0x90), and whether
  over-the-top-rope has any special handling (none found).
- Leads from a feature list in an online guide (Andrew, 2026-11-01), to match against code:
  the run-in when one wrestler keeps the advantage (probably state 0x8F, `run_in_check`); the
  stance only near the focused opponent; runners always hop over a fallen opponent (maybe 0x89 /
  0x8A); fights going up the entrance aisle (0x8D is not it: it stands a ladder up); instant replays after a big move in singles; multi-hit combos (chained strikes); 6
  arena setups. (`wcw_revenge/states.md`, `matches.md`)
- Revenge move slots marked `weak` in `wcw_revenge/moves.md` (guide fit): settle them from
  Revenge's own grapple slot table (World Tour's `g_grapple_slots`). Hidden wrestlers are done; alternate entries, managers and THQ Man (reads as AKI Man) are not. (`wcw_revenge/moves.md`)
- The few new Revenge states still not identified (0x5C, 0x89, 0x13, 0x8E entry): code first,
  then the emulator if needed. (`wcw_revenge/states.md`)
- Hand-check the 73 `guess` pairs in `symbols/revenge/from_wcw.toml` (call-graph matches with
  a score below 1.0), and re-run `tools/sym/match_calls.py` as more World Tour functions get
  names. (`wcw_revenge/README.md`, "Function matching")
- RAM (paused CPU match): check the 16 new AI modes 46–61 and the AI block (`0x800F9B88` +
  96 × slot) against the first reading in the notes. (`wcw_revenge/ai.md`)
- Where Revenge sets CPU difficulty (World Tour's 50 / 250 / 900 skill table is not there in
  that form). Lead (2026-11-01): `ai_state_init` (`0x800A97A0`) copies a per-slot level from
  `0x8007E2A8` (values 0-2, 3+ becomes 1) into the AI block +0x10. (`wcw_revenge/ai.md`)

- Revenge arenas: how the untextured turnbuckle pads (mesh 755) are coloured, the objects
  loaded by `0x80011BB4` from `0x80036D18`, and the transforms of the shared stands at
  `0x80036750`. (`wcw_revenge/arena.md`)

## No Mercy

The system docs in `individual_games/wwf_no_mercy/` (frame_loop, movement, strikes, grapples,
pins_and_holds, damage_and_spirit, match_rules, ai, objects_and_weapons; 2026-11-02) each end with an
**Open** list: those are the current code questions per system. Settled in that pass: the mash
thresholds, the ground hold's give-up rule and rope breaks, the TKO check, the get-up crouch, the lock-up
steps, the CPU's lock-up choice and close-range decider, and the move record layout. Settled later the
same day: the CPU's position deciders (ring / floor / apron and the count-out return; the "turnbuckle"
deciders are the cage wall's), `match_result` (time-limit draws, count-out sides, decisions by health),
the tag partner's walk back (0x92), the match-over flag, the stick byte (`in_byte`), the walk and run
speeds, the R counter's move, chance_roll kinds 4-6 / 12 / 19, and several Wrestler fields. The items
below are older and partly overlap.

Addresses are USA Rev 1 (`symbols/nomercy/README.md`). Names in `symbols/nomercy/from_revenge.toml`
and `from_wcw.toml` come from call-graph and constant matching and are unchecked; in particular
the state numbers inside `state_XX_...` names are Revenge's (or World Tour's), not No Mercy's.

- Feature leads (Andrew, 2026-11-01, and `gamefaq/no_mercy_misc.txt`), to find in code: climbing
  onto the announce table (C-up) and putting or throwing an opponent onto it; walking along the
  apron and round the corners to other sides (maybe not new); countering strikes with a timed R;
  countering or evading grapples with L; lifting an opponent onto the shoulders from a rear
  grapple; spinning a groggy opponent (L, "flip standing opponent"); Z toggling CPU / manual
  control; C-right changing the focused opponent; weapons staying on the ground; C-left dragging
  a lying opponent; ref calls with C-left; ladder climbing and placing (ladder match).
  (2026-11-02: all found in code: the table climb 0x9B / 0x0F, the apron walk and corner 0x02, the R counter,
  the L deflect 0x6B-0x6D, the rear lift 0x7F-0x82, the "flip" is L in a front lock-up into the corner hang
  0x3E / 0x3F, Z and C-right in `frame_loop.md`, weapons in `objects_and_weapons.md`, C-left drag 0xD0 / 0xD1,
  the guest referee's calls, the ladder states 0xAA-0xBD.)
- Match types in the guide that the earlier games lack or differ in: triple threat, cage
  (escape to win), Royal Rumble (four in the ring, over the top rope), guest referee, ladder,
  ironman (most falls in 5 to 60 minutes), survival. Where each is set up and checked.
  (2026-11-02: set up by `match_rules_build` and checked in code, `match_rules.md`; Pay Per View's card
  table and the game-mode byte were read later that day from the ovl_c IDA build, `match_rules.md`.)
- `g_state_handlers` entry 0xD2 holds `stance_turn_anim` (a carried-over name). No state setter
  call with a constant goes above 0xCD; is 0xD2 a real state, reached with a computed number?

### No Mercy: RAM follow-ups
- States with no constant setter (2026-11-02): **0x7E** (`state_7E_walk_to_climb`, the only state still `guess`).
  Watch for it in RAM to see what sets it. (~~0x9A~~: settled from code later that day: `run_exit_check`
  sets 0x9A / 0x9B with a computed number, 0x9A for the run out of the ring at an exit segment with +0x26
  bit 0x02; `states.toml`.)
- State review (2026-11-02): 115 states confirmed, 66 likely (0x2D settled after: health below 100 picks the slouched fall). Labelled "Needs RAM" in `states.toml`: 0x75 / 0x76 (which dive each plays), 0xC9 / 0xCD / 0xCE (location 0x400 on the turnbuckle
  outside a cage, or only the cage wall), plus 0x7E and 0x9A above. The other `likely` ones rest on one code read
  without a tag or RAM sighting: mostly paired move states whose animation comes from the move under way.

- **[RAM]** Object probe points: read `0x800A9C70 + 140 × object` (+1 count, +0x2C the points, 12 bytes
  each) for a few weapon types (a stick, a mid-size weapon, a ladder) while they are loose, to pin where
  the 2 / 4 / 8 points sit relative to the model (`objects_and_weapons.md`).

### No Mercy: wrestler models (code, for the previewer)

From `wwf_no_mercy/wrestler_models.md`; `nomercy_models.py viewer` shows every wrestler, attire and
animation:
- Appearance bytes 57 and 60 are not read by `appearance_build`; 60 differs per wrestler. Probably
  not model data (look where the unpacked appearance is read outside the builder).
- The extra textures in record slots 25-27 (`appearance_colours`, model buffer +1300): what draws
  them? The previewer leaves them out and nothing looks missing so far.
- The joint offsets `model_nodes_build` applies (shoulders +-5, arms +-7) are not applied in the
  previewer yet.

### No Mercy: check in the animation viewer

Done 2026-11-02: Andrew tagged every `check` animation (`wwf_no_mercy/anim_tags.json`) and the
states in `symbols/nomercy/states.toml` are renamed from the tags. What the tags settled: 0x1B is
the apron wait (not a taunt), 0x02 the apron corner walk, 0x6E dropping off the apron (not a cheap
shot), 0x6F climbing onto the steps / table / off the cage, 0x17 / 0x32 draped and leaning on the
ropes, 0x34 held upside down, 0x3F corner reversal, 0x68 dodging a dive, 0xD0 / 0xD1 the drag,
and the 0xC2-0xCF family is the **cage** (hanging, hit while hanging, attacks from the top).

Still open from the tags (code or a match needed):
- ~~0x4D's ladder poses~~: yes, steps 5-7 are the ladder version (`strikes.md`).
- ~~Unnamed states~~ (2026-11-02, code): 0x06 R while running, 0x08 the running weapon strike, 0x11-0x14 C-down in a
  lock-up across the ropes, 0x1A a whipped runner's strike out of the corner, 0x1C the corner taunt (the stick when running into a
  corner, move 232), 0x1F-0x21 strikes from the apron, 0x3B the shared receiver, 0x7E walk and climb, 0x87 pulled
  from above, 0xB9 B mid-ladder. A later pass the same day checked every remaining `guess` against its setters, calls and Andrew's tags: all are
  `likely` or `confirmed` now except 0x7E (no setter; RAM follow-up). Renamed in that pass: 0x8B weapon drop, 0x93 /
  0x94 tag received / tag out, 0xCB a running B at a cage climber, 0x1C corner taunt.
- ~~0x3A / 0x3C / 0x3E / 0x40~~: small paired moves; 0x3C sets a slumped opponent up on the top turnbuckle (L) and 0x3A pulls it out of the corner (R; `input_r`, formerly `input_strike`) (`grapples.md`).
  The turnbuckle or the table? Watch them in a match.
- ~~0xC2 / 0xC3~~: the cage whip (C-right in the lock-up in a cage match, `match_rules.md`).
- ~~0x00 (two heads slammed together)~~: the pre-bell state; 12959 is the guest referee's start animation (2026-11-02).
- ~~0xD2 and 0x94~~: 0x94 is the legal man tagging out (`tag_exchange`; 0x93 the partner taking the tag); 0xD2 has no setter and poses the wrestler at the
  ring centre by the d-pad, probably a leftover test state (2026-11-02).
- States that pick their animation from tables (no fixed id): 0x06, 0x07, 0x1A, 0x1C, 0x1D,
  0x5A-0x60, 0x67, 0x71, 0x73-0x78, 0x7E, 0x7F, 0x88, 0x89, 0x8E, 0x8F, 0x92, 0x96-0x99, 0x9C /
  0x9D, 0xA4, 0xBC, 0xC1, 0xC7: watch them in a match.
  In a 4-CPU match (600 frames sampled, 2026-11-02) only 0x07 came up: from the run (0x03),
  animation 15256 for 52 frames: a running shoulder block (Andrew). The rest need set-ups that
  trigger them.
  Read from RAM instead (2026-11-02): states that start a fixed move get their animation from the
  move set through `anim_style` (code from `g_move_anims`, table 0x8015A700, + 13090 + part). For
  the four loaded wrestlers: 0x06 (move 208) 15320-15322, 0x1A (move 207) 15341-15343, 0x1C
  (move 232, per wrestler) 17141-17143 / 17504-17506 / 17510-17512, 0x73 (move 270, Austin only)
  16817-16819; labelled `check: ... (move-set (RAM))` in the viewer. The other runtime-picked
  states (0x5A-0x60, 0x71, 0x77, 0x88 ...) play the animation of whatever move is under way, so they
  have no single id.
  Code-first pass on their setters (2026-11-02, `states.toml`): 0x5B / 0x5C running-grapple counter,
  0x59 / 0x5A move follow-up, 0x5F / 0x60 a move from a carry or pick-up, 0x66 / 0x67 a second strike
  counter kind (strike flags 0x3800), 0x73 apron moves, 0x7F / 0x80 the rear lift, 0x96 leaving after
  a lost fall, 0xA4 an illegal man leaving, 0xBC C-up on the ladder, 0xC1 into the cage wall, 0xC7
  dropping down from the cage or turnbuckle (also once the match is over, 0x8016B599 bit 0x40). Second pass (setters reached through shared jumps): 0x1D L
  against a runner, 0x97 / 0x98 L at an opponent on the ropes, 0x5D / 0x5E chained grapple moves,
  0x71 a running move at a target, 0x73 / 0x74 aerial attacks from the apron / turnbuckle (moves
  269 / 270), 0x75 / 0x76 dives from the apron, 0x77 apron attacks into the ring, 0x88 / 0x89 catching
  a flyer (then moves 141 / 142; 0x8D / 0x8E are the ladder set-up and a paired move with a held
  object, from `input_weapon_grab_zone`), 0x9C / 0x9D grapple from the floor at the apron. No setter
  found at all: 0x7E (its own handler: drop the weapon, walk away from the ring centre and climb onto a raised place, `state_7E_walk_to_climb`); 0x8F is the weapon throw (from
  `input_weapon_use`), 0x92 the tag partner's walk back (through stand_flags_update).
  Later the same day: `anim_style`'s table 0x8015A700 is ROM file 496 (loaded by `match_load`), so a
  move's animation can be worked out from the ROM for every wrestler: the packed move-set files (the
  attire descriptor's +10) are decoded too (`nomercy_models.py moves`, checked in RAM); `animation_data.md` has each animation's
  record and events.
  Caution (2026-11-02): the second pass misread 0x88's moves 141 / 142 as states 0x8D / 0x8E, so its
  other "setters read from code" entries are worth a second look where the number is not the state
  argument of a setter call.

### No Mercy: wrestler record and states (open-ended)

Work these from code first (Andrew, 2026-11-01). **[RAM]** marks the few that code can't settle:
values set outside the match code (menus, unlocks) or what something looks like on screen.
Field names are from `symbols/nomercy/types.h`; answers so far are in the symbol files'
descriptions and `wwf_no_mercy/README.md`.

Answered from code (kept here briefly so the trail is visible):
- Run-ins: `wrestler_add_health` arms a side's run-in when its wrestler's health reaches 0
  (chance_check(500), or 250 with option 0x400000), and only with interference on (0x80097EC0 &
  0x1000). That is the "dominating" trigger. `run_in_check` / `run_in_try` then bring the side's
  ally (ally_id +0xD8) into a spare slot (2 or 3) in state 0xA3.
- Health (+0xAA, max +0xAC) thresholds: 0 run-in roll, < 49 extra spirit, < 50 count sound, 60
  floor after a fall, < 100 weakened hit reactions (the slouched fall), AI < 64 / < 128.
- Grapples (`grapple_start_pair`, `grapple_attempt_resolve`, `state_50_grapple_attempt`):
  flags78 0x04 strong (A held in the attempt), 0x80 rear (opponent's back to the attacker),
  0x40 running, 0x100 grappling a staggered opponent (its frame_flags & 6), 0x200 / 0x400
  defender / attacker on the apron, 0x800 a normal standing grapple, 0x20000 / 0x40000 from the
  floor to the apron / ring. Kinds: 0x51 / 0x52 normal (front or rear, by flag), 0xA0 / 0xA1
  running, 0x7F / 0x80 from the apron, 0x9C / 0x9D floor to apron, 0x86 / 0x87 floor to ring,
  0xCD / 0xCE at an opponent with location 0x400, 0xB4 / 0xB5 an opponent on a ladder. B during
  the attempt turns it into a strike (0x22).
- Kick catch (`state_69_catch_holder`): within a countdown (count_timer) A starts move 183 and
  B move 184; state_flags 0x10 gives move 185; when time runs out state_flags 0x04 is set and
  the caught wrestler reverses.
- Strikes: `state_22_strike` / `state_23_strong_strike` (move 190 is the A+B lunge).
- Ladder: `input_ladder_climb` (C-up at a standing ladder, object type2 25) into 0xAA, location
  0x08 while on it; objects keep the climber of each side (climber0 / climber1).

Still open, code first:
- `location` bits: 0x20 (on top) and 0x40 / 0x80 / 0x100 / 0x200 (the four raised places, three of
  them breakable tables, `g_tables_intact`) are read, and 0x400 is the turnbuckle / cage wall
  (anim_end_location place 6, the cage climb). Still open: 0x10 (ladder top, states 0xAF / 0xB0),
  what 0x800 / 0x1000 do in the collision code (both are per-frame bits that the tag and the
  whip / shove / chain states set again each frame); which raised place is which
  needs one RAM read per arena (low priority).
- Flag words `flags80`-`flags8e`, `flags_e9`-`flags_f7`: bit meanings from the setters and the
  tests, one word at a time (`sym.py --game nomercy field Wrestler.flags8a`).
- ~~`move_info` / `info`~~: +0x2E4 is the attribute record (`damage_and_spirit.md`), +0x310 the move record
  (`struct MoveRecord`). `zone_info` (+0x348): the ring-zone data (forced weapon at +8, place flags at
  +0x26 / +0x28), layout not fully named.
- `wrestler_start_move` and the move table it reads: move numbers 172-190 so far; tie them to the
  per-wrestler move sets.
- States: a profile pass (2026-11-02) named about 50 more from setters, animations and calls (many `guess`).
  (2026-11-02: 0x1B apron wait, 0x4E the crouch, 0x5B / 0x5C the running-grapple counter, 0x6F climbing
  onto, 0x83-0x85 the double team, 0xAE / 0xB1 ladder control, 0xA6 the referee's count all have names
  and descriptions in `states.toml` now; 0x4E is the attacker's get-up after a grapple move with a one-frame window for a taunt, the stick or C-left
  (its steps in `states.toml`; whether match_can_interrupt's nonzero really means "stand at once" is unchecked); the double team's
  start, moves 163-165 and steps are in `grapples.md`.)
- ~~`end_pose` values~~: listed in `frame_loop.md` (`state_30_settle`).
- ~~Start table 0x80146350~~: not start positions; four words 0x100-0x400 by the winning slot (0x801591E8), used
  by `match_tick` after the bell and the run-in; start positions come from `wrestler_start_place`.
- ~~Spinning a groggy opponent~~: not a spin. **L in a front lock-up** (`lockup_input` 0x80128B44, not a rear
  grapple) is the flip into the corner, hung upside down by the legs (a tree of woe): attacker 0x3E (12074),
  opponent 0x3F (the attacker's animation + 1, 12075), then 0x34 held upside down (12076, dropped 12081). An
  opponent with flags_f0 0x02 / 0x04 reverses it instead (12082) and the roles swap (0x3E plays 12083).
  Andrew's viewer tags, 2026-11-02, and code. Still open: whether anything requires a corner nearby.
  (The apron corner walk is state 0x02, `movement.md`.) (C-right focus, Z CPU / manual and the cage are read, 2026-11-02: `frame_loop.md`,
  `match_rules.md`.)
- `g_state_handlers` entry 0xD2: no setter; a centre-of-ring pose by d-pad, probably a test leftover.
- Every ovl_e function is named, about half `guess` from a single read (`symbols/nomercy/bulk.toml`).
  Worth a confirming pass, area by area, starting with the ones a port needs first: damage, pins,
  grapple and lock-up, strikes, movement, objects.
- Gameplay systems still open (code first; map in `wwf_no_mercy/README.md`):
  - end_flags: 0x200 (guest referee call), 0x400 (DQ), 0x800 (ladder lost), 0x1000 (the opponent
    escaped the cage, state_C5_cage_escape) and 0x100 (last one standing, from state_01_stand) are
    read, and so are match_end's result codes (`match_end` in `symbols/nomercy/match.toml`). 0x10 is
    the over-the-top elimination (g_match_rules 0x400). The count-out is `countout_tick` (10 or 20 counts
    from the option, 2-4 seconds a count; result 0x80000 like over the top). Still open:
    match_result's long tail (time limit, ironman). The World Tour name results_screen at the same
    call-graph spot may be worth rechecking in that game. (2026-11-02: `match_result` is read,
    `match_rules.md`.)
  - The CPU: the situation classes are mapped (`AiSituation` in `symbols/nomercy/ai.toml`); the deciders
    are named by where-class (`bulk.toml`); next is what each personality trait means (which chance
    table it picks in which decision), the remaining mode bodies, and a converter-side dump of
    the weighted mode tables (`ai_pick_mode_weighted`; 14 table addresses found directly, more
    passed as computed pointers); every function in the AI area (0x800F0000-0x8010A000) now has a name, most `guess` from one
    read; the trait readings in `ai_update`'s desc are guesses from where each trait is used; a profile pass (2026-11-02) gave about 40 modes `guess` names by the buttons
    they press. No RAM needed: the modes write the buttons to AI block +0x0C, so the code shows
    what each one does; RAM would only show how often each comes up. (2026-11-02: every decider and the
    traits are read, `ai.md`.)
  - ~~The damage history entries~~: `apply_damage` → `damage_history_add` adds 1 hit and the damage to the
    current second's (a, b) bytes, when the damage is non-zero (2026-11-02).
  - ~~`apply_damage`'s three parts~~: the formula is in `damage_and_spirit.md`.
  - The move record's remaining bytes (2026-11-02: +0x00 / +0x1E are padding; +0x0E-0x0F and +0x19-0x1A
    have values in file 495 but no reader in the match code, maybe menu data), and names for the 548 move slots (the move-set decoding is done:
    `animation_data.md`; the lock-up direction tables, the CPU lists and the finisher classes name many).
  - ~~Match types~~: settled 2026-11-02 from the menu code (`match_rules_build`, ovl_c) plus one emulator
    run naming the modes (`match_rules.md`).
- **[RAM]**: which weapon each type number is (17 = stop sign, 5 = the next crowd weapon) and
  which 12 types the unlock mask (`weapon_type_allowed`, 0x8009FDEA) covers; who a wrestler's
  ally is (ally_id, set outside the match); what `g_arena` (0x80159628) is per arena. (The option
  words are settled from the menu code.)

## World Tour: outside the match code

- The default button bindings at `0x80061FF8` (set by the options menu). (`frame_loop.md`)
- The menu code that writes `0x80062058` / `0x8006205A`. (`match_end.md`)
- What the frame-event sound ids sound like (needs the audio bank). (`file_formats/animations.md`)

## All games

- **Camera logic**: study each game's match camera (framing, angle changes, entrance and replay
  cameras) as the game is analysed. Revenge advertises improved camera angles, entrances with
  pyrotechnics and instant replays. World Tour's camera functions are partly named
  (`camera_frame_all`, `camera_closeup`, `anim_meta_class` gives a framing class) but not
  documented.

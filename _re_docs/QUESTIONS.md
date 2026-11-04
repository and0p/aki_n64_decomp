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
  0x40 running, 0x100 grappling an opponent slumped in the corner (its frame_flags & 6, set only by state 0x18; read as staggered until 2026-11-03), 0x200 / 0x400
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
  (Correction 2026-11-03 from the asm: the L test that starts 0x3E / 0x3F sits in `lockup_input`'s corner
  branch, flags78 0x100 without 0x80, so it is L from the front on an opponent slumped in the corner
  (frame_flags 0x02 / 0x04 from state 0x18): yes, it needs the corner; L in the
  normal lock-up switches sides instead. `grapples.md`.)
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

### No Mercy: from the decompilation (2026-11-02)

Found while translating the walk / run path (`src/nomercy/`, harness in `tools/trace/`). Code first:

- ~~model_animate (0x80012130)~~ decompiled 2026-11-02 (`src/nomercy/nm_model.cpp`, `animation_data.md`
  "Posing"); the replay streams the keyframes from the ROM (`nm_replay --rom`). Left from it:
  - Who sets the carry offset: model_animate draws slot `0x8009FDE0 - 1` raised by the s16 at
    `0x800A9352` (both written by the unnamed main-segment functions 0x8001B548 / 0x8001BDDC; 0 in the
    walk / run trace). A wrestler carried on another's shoulders, or the ladder?
  - Bytes +0x30-+0x47 of a joint record (`struct ModelJoint`) and +0x00-+0x0B / +0x18-+0x77 of the
    block's record 0: nothing in the posing touches them (the trace reports them as unknown).
  - `anim_event_special` (events 170-175): 1 / 2 write a +-0.25 value for slot 0 through 0x80013FFC
    (0x80076CD0, flag byte 0x800A8BD4 bit 2), 3 / 4 copy a 2,056-byte per-slot buffer (0x80076CF8 / CFC)
    into the model's texture area. Rendering (a sweat / blood or face change?), ignored by the oracle.
  - The 28-byte per-slot block at 0x800A3DC0 that wrestler_tick writes under elimination rules.
    (`frame_loop.md`)
- What object_model_update (0x80024F84, from objects_cleanup for every object) changes for an
  object in play; the walk / run trace has none. (`objects_and_weapons.md`)
- ~~Two input-function names do not fit~~ renamed 2026-11-03: `taunt_stir_opponent` (A held: the
  grapple attempt 0x50) is now `input_a`, `input_grapple_attempt` (the control stick: the taunt) is
  `input_stick`; also `state_1B_apron_wait` -> `state_1B_taunt`, `wrestler_set_move_from` ->
  `move_texture_stamp`, `state_42_cover_approach_b` -> `state_42_cover_from_standing`, `mtx_mul_vec4`
  -> `mtx_mul` (and 0x80038900's `mtx_mul` -> `mtx_mul_obj`). (`movement.md`)
- walk_speed for speed class 0 reads its threshold list through the caller's v1 (the wrestler record
  from walk_step); every wrestler checked so far is class 1-3. Which roster wrestlers are class 0?
  (`movement.md`)
- The replay start in replay_record (`0x80159400` back -15, speed 2, camera word 0x80159410): what
  match_frame_extras plays with them. (`frame_loop.md`)
- Found while translating strikes, grapples, the down states and pins (the same day; all six traces
  replay exactly, step and free):
  - The camera draws rng_main outside the match logic (seen as a camera cut on a move's first frame),
    so the trace's rng_main word cannot be compared and is `ignore` in `tools/trace/nomercy_regions.toml`.
    Which camera function draws it, and does anything in the match logic read the state after it?
  - ~~`lockup_input` (0x8012888C): only the normal lock-up's input table is ported~~ (every kind written
    out 2026-11-03 from the asm; see "holds and the lock-up kinds" below).
  - ~~The cover from standing (0x42 / 0x43) has no trace yet~~ (traced from the slam save 2026-11-02,
    `nm_cover.json`; `pins_and_holds.md`). A count of three and the fall are traced too
    (`nm_count3.json`: match_end, match_record_fall, match_events, match_tick phases 3 / 4, singles
    only), and the match end through the winner's pose and the results copy (`nm_match_end.json`;
    state 0x1B is the taunt state, renamed `state_1B_taunt` 2026-11-03). Still untraced: the
    tag / elimination / multi-fall results, match_result (count-outs, time limit, the guest referee),
    the end replay (match_frame_extras), the taunts themselves (0x1B steps 0-3 from the stick).
  - `wrestler_move_anim`'s second argument in 0x1B step 0 (a weapon held) is whatever a1 held, 105
    from wrestler_set_generic_move's call: only read when the weapon sets none of flags_ef 0x02-0x20.
    Is that reachable?
  - The CPU: the plan is a CPU-vs-CPU trace with the AI's decisions recorded as an oracle, so the
    match logic can be checked before the AI itself is ported. Which words of the CPU block to record.

### No Mercy: holds and the lock-up kinds (2026-11-03, understanding first)

Read from the code, written as annotated C that does not have to replay, and checked against the
emulator with recorded traces (`dump.py`):

- ~~Holds and submissions~~: the hold states are 0x48 / 0x49 (`src/nomercy/nm_hold.cpp`,
  `pins_and_holds.md`); a move with record flags 0x400 / 0x800 / 0x1000 chains into hold move 292 + its
  follow-up variant; escape_time op 3, the 31-frame damage, the mash-out, the end animations and a full
  submission (give-up, bell, match_end's 0x2000 result, the winner's pose) all seen in RAM
  (`tools/trace/scripts/nm_ground_hold.json`, `nm_submission.json`, the second with a RAM poke of the leg
  region). Every escape_time op is in the C now; op 4 was misread (it is the full value with count-outs off).
- Still open on holds:
  - The ground grapple 0x46 / 0x47 itself (`ground_grapple_defend`, `recover_tick_ground`,
    `ground_grapple_start`, the C-left drag at steps 6-8) is not decompiled; a hold replay needs it.
  - Rope breaks and `ground_hold_crawl` are read but not seen in RAM (the hold in the traces was mid-ring).
    A save with a submission near the ropes would show both.
  - The holder's stick blocking the give-up (state_flags 0x100) is code only.
  - Which roster wrestlers' move sets have holds with record flags 0x800 / 0x1000, and what those two bits
    change (the hold code tests only 0x1E00 as a whole; slot 0's set had 0x400 holds and one 0x800).
- ~~The other lock-up kinds~~: `lockup_input` is written out for every kind (`grapples.md`): running at
  the apron (0x240) / an opponent slumped in the corner (0x140), across the ropes (0x600, the apron specials 147 / 151
  through `move_allowed`, now decompiled), an opponent slumped in the corner (0x100, the corner moves 0x80146A00; read as staggered before), the cage whip.
  Two bugs fixed in the normal path (the finisher bit is 0x400000; the tag double team uses the
  partner's slot). The corner path is seen in RAM (`nm_corner_lockup.json`: L the flip, A / B moves 108 / 113
  chaining into corner holds) and so is L in a normal lock-up (the side switch); the running, rope and cage
  paths are not. Still open: `lockup_place_move` (the arena-zone place moves) is not decompiled; a save
  across the ropes (one wrestler on the apron) would check the 0x600 path.
- The CPU (2026-11-03): `ai_update`, perception, `ai_situation` and the mode bookkeeping are in C
  (`src/nomercy/nm_ai.cpp`, `ai.md`), the situations checked against RAM. Still open: the 145 modes
  (`g_ai_modes`, 111 functions) and the deciders (`ai_choose_mode` and the `ai_decide_*` family) are not
  decompiled; `ai_mode_run` names the mode it would run. The 136-byte per-slot block at 0x8016B350 (AI
  block +0x8C) is not named yet. Later the same day: `ai_choose_mode` (mode 0), `ai_decide_ring_simple`, mode 32
  (wait, checked against the recording) and the small helpers are in C; `ai_can_act` is renamed
  `ai_press_focus_switch` (it presses C-right). The 4-CPU recording uses 26 modes (0, 32, 41, 53, 6, 15, 54, 45,
  49, 59, 1, 9, 25, 30, 39, 10, 3, 29, 31, 47, 2, 48, 40, 119, 26, 33); the deciders (`ai_decide_in_ring` 0x764
  bytes and its tree: tag, standing, close, far, in-ring main, vs-down ...) are next, then those modes. Done later
  the same day: `ai_decide_in_ring` and `ai_decide_standing` (from the asm); `ai_move_listed` renamed `ai_mode_recent`
  (its list is the mode history). Still stubs (each names itself when reached): ai_decide_tag, ai_wants_tag_out,
  ai_try_grapple, ai_decide_vs_down / _b, _close / _close_b, _far / _far_b, _special, _vs_lying, _opponent_focused,
  _engage_chance, _change_area, _ropes, _wait_or_engage, _in_ring_main, ai_vs_runner_armed, the other where-class
  deciders, and every mode but 0 and 32. Later still (2026-11-03): all 26 modes the 4-CPU recording uses are in C (mode
  25 from the pseudocode only), and so are the standing sub-deciders (engage, close, opponent-focused, far / far_b,
  close_b, vs_down / _b, special, vs_lying, move list), the tag decider and tag-out wish, the rope and runner checks,
  wait-or-engage, in-ring main and ai_try_grapple. Renamed: ai_mode_11_evade -> ai_stick_taunt, ai_lockup_wait_time ->
  ai_lockup_move_list, ai_angle_off_facing -> ai_faced_by. About 45 helpers remain stubs (each names itself when a
  replay reaches it): the other where-class deciders (ring_vs_floor, apron_vs_*, floor_vs_ring, floor, the cage ones,
  backstage, raised, ai_think), ai_decide_change_area, the list pickers (ai_list_pick_a-d, ai_try_pick_up,
  vs_lying_b / _c, ai_finisher_check, ai_decide_finisher), the tag helpers (partner / help / reach /
  extra), the lock-up helpers of mode 25, ai_partner_*, ai_side_*, ai_evade_direction, ai_block_timer, apron_walk_dpad,
  nearest_segment_along and objects_near. Not checked against RAM beyond the mode timers and first buttons.
  Then the apron deciders (apron_vs_ring, apron_vs_apron, apron_side) with apron_walk_dpad, ai_apron_walk and
  ai_trait_flag2. Renamed: ai_go_to_partner -> ai_try_corner_climb (it never looks at the partner: it sets mode 65
  when the opponent lies near a corner; the top-rope move bits in attrs +0x38 / +0x3C call it). Fixed: the CPU's
  copy of its own point is at 0x80159674, not 0x80159604 (the C and the ai_update desc had the wrong address).
  Open: modes 65 / 66 (`ai_mode_65_vs_runner`) press C-down with the d-pad toward a corner, which reads like the
  turnbuckle climb rather than meeting a runner; a RAM check of mode 65 in a match would settle the name. The
  same for mode 67, which the springboard / apron move bits pick.
  Then the floor deciders: ai_decide_floor, floor_b, floor_timeout (the legal man heads back in by the count-out
  setting, or after 15-45 seconds on the floor by trait +38 with the count-out off; 30-90 under rule 0x4000),
  apron_vs_floor and ai_decide_apron_jump (modes 87 / 88, the apron dives). Open: ai_decide_apron_jump measures the
  gap along an odd-numbered ring side with the points' second component, which is the height in `pos` and
  point_120, so on those sides the gap is about 0 and only the near dive (87) is tried. It looks like a slip for z
  in the original; a RAM check of an apron dive on an odd side would confirm it. The C keeps the original reading.
  Then the rest of the where-class tree: ring_vs_floor (with ai_decide_ring_edge, the floor move lists and
  ai_list_filter), floor_vs_ring, change_area, the three cage-wall deciders (with zone_is_turnbuckle_a / _b),
  the raised-place ones, backstage and ai_think (the ladder match: object 6 is the prize). 29 helpers are still
  stubs: the move-list pickers (ai_list_pick_a-d, ai_try_pick_up, vs_lying_b / _c, ai_finisher_check,
  ai_decide_finisher), the tag helpers (partner / help / reach_check / reach / extra), mode 25's lock-up helpers,
  ai_partner_*, ai_side_*, ai_evade_direction, ai_block_timer, ai_arena_target and nearest_segment_along.
  Then the move-list pickers (ai_list_pick_a-d, ai_try_pick_up, ai_finisher_check / _kind, ai_decide_finisher,
  vs_lying_b / _c, vs_down_near / _far, ai_try_ground_move, ai_try_run_at_dazed_opponent / _89, ai_line_up, ai_side_*), the
  tag helpers (partner / help / reach_check / reach / extra, ai_tag_position, ai_partner_busy) and
  ai_set_mode_arg / ai_third_party_active. Quirk kept: ai_decide_vs_down_far returns 0 after setting mode 6.
  13 stubs left: mode 25's lock-up helpers (ai_lockup_*), ai_partner_distance / _mode, ai_evade_direction,
  ai_block_timer, ai_arena_target and nearest_segment_along. None of this is checked against RAM yet: the
  4-CPU tag recording would exercise the tag helpers.
  Later the same day: those 13 are in C too (with ai_others_dirs, ai_advantage, ai_chance_by_advantage and
  segment_hit kind 0 in nm_bounds.cpp), and mode 25 was checked against the asm (it matches). No ai stubs are
  left; the CPU modes outside the 26 the recording uses still stop a replay by number (ai_mode_run). Quirk kept:
  ai_lockup_tag_check's loop over the wrestlers never advances, so in types 0xC000 it only looks at slot 0.
  Checked in RAM the same day (nm_replay --step --keep-going over the 4-CPU tag recording, 1,085 of 1,500
  frames run): every mode, step, timer, held / pressed button, stick and target byte of the four AI blocks
  matches the game, over 706 mode choices (630 waits, 32, mostly the apron partners in apron_vs_ring; 76 in
  the ring: modes 1, 2, 6, 9, 29, 31, 33, 39, 40, 41, 45, 47, 48, 49). The only differences are the perceived
  distances, which follow other wrestlers' positions where their states are not ported yet. The skipped
  frames stop at states 0x0B (200), 0x24 (68), 0x05 (66), 0xA0 (55), whip_run_inputs (11), the taunt stick (6),
  0x4D (5), lockup_place_move (3), a charged runner (1).
  Then ported for it: objects_near, move_hits_segment, the whip's state 0x0B with whip_direction /
  whip_target / whip_place_runner, and pair_scripted_place (only its 0x0B case), pair_slide_step,
  place_from_zone, wrestler_move_pos_to_pelvis (Wrestler +0x2D8 is now `hold_dist`). 0x0B can only be
  checked once the runner's 0x0C is in (they share their frames). Open, found by the same replay
  (per-state field compare over the frames that ran):
  - The apron partner standing (state 1 on the apron, slot 3 here) advances its animation 2, 0, 2, 0 ...
    frames per tick in the game, 1 per tick in the C (about 900 slot-frames differ only in that and what
    follows from it). wrestlers_ground_contact steps every wrestler by 1, so something else steps it
    again (or skips it) on alternate frames: not found yet in the code.
  - State 0x22 (a strike) moved slot 2 from (88, 90) to (112, 109) in one frame at record 21 of the 4-CPU
    recording; the C left it in place (6 more frames like it).
  - whip_direction: at an opponent slumped in a corner, a corner whip in front of the whipper reads
    registers the original never set (stopped as unported).
  - Later: the runner's 0x0C and whip_run_inputs are in too, and the whip checks out: 367 of 376 slot-frames
    of 0x0B / 0x0C identical (after a fix: whip_direction reads the d-pad from the input entry's +0x0A, not
    the held buttons). The other 9 differ only in damage_hist_pos, most likely a recording artifact:
    with four wrestlers a record seems to catch the game mid-tick, after wrestlers_update_all but before
    match_clock_tick, so the clock (0x8016B930) moves 0 or 2 between such records and the once-a-second
    damage history lands a frame apart. Fields set after the wrestlers' update can lag that way.
    Fixed in the recorder 2026-11-03: `akiemu --tick-hook` ends a frame only between whole ticks (clock +1
    on every record of two 4-wrestler recordings, 409 and 74 misses without it). Still to do: re-record the
    4-CPU tag trace with the hook and re-run the replay to see whether the damage_hist_pos and apron-partner
    2 / 0 differences go away. The same measurement found the logic tick runs slower than 30 Hz in one
    save (28-29 Hz, P2 down with both run-ins on the floor) but never runs two ticks in one frame or
    catches up (frame_loop.md).
  - Andrew's tag-start save (`nm_tag_p1_near_partner_match_almost_done_loading.state`, script
    `nm_tag_start.json`, 600 frames, no input; 2026-11-03): play starts on frame 12, the humans stand, the
    CPU partners wait on the apron (mode 32) and taunt (0x1B). Replayed: AI blocks match on 597 of 600
    frames, standing / taunting on 2,317 of 2,332 slot-frames; the misses are frames 13-30 (the fade-in;
    the models' interpolation and the timers) and the ends of the partners' taunts (pos, models). The
    apron partner advances one animation frame per tick here, so the 2 / 0 cadence above is most likely
    the same mid-tick recording artifact, not a C bug.
  - Re-recorded with `akiemu --tick-hook` (frames end only between whole ticks; scripts `nm_tag_cpu.json`,
    `nm_tag_start.json`; traces `_h`): the artifacts are gone. Tag start: AI blocks 600 / 600, the
    wrestlers' states 2,332 / 2,336 slot-frames, 13 of 481 frames fail at all (the models' pose and pos as a
    taunt, 0x1B, ends). 4-CPU: AI blocks 1,491 / 1,500, the ported states 3,706 / 3,734; one record held two
    ticks (nm_replay now runs as many ticks as the clock moved). Open: the taunt's end (pose, pos).
  - Later the same day (hooked traces): P1's taunt (`nm_tag_taunt.json`) and P1 / P2 tagging in from the
    tag-start save (`nm_tag_tagin.json`) replay identical in every ported state. On the 4-CPU trace the
    running shoulder block (0x07) is in and identical (45 / 45 slot-frames, after a fix: `strike_slide`
    slides by 2.0 up to the hold frame and 4.0 past it, the C had it the other way round), and so is the
    ground attack at a downed opponent (0x28 / 0x61 / 0x62, `state_28_weapon_ground_attack`, with 0x63).
    11 frames of 1,500 still stop on unported code, each a one-frame path: `input_apron_dpad` (the apron
    corner walk, 3), `grapple_reversal_start` (front lock-up), `lockup_place_move`, `move_chain_check`,
    `run_collide` (a whipped runner meets a wrestler), `run_inputs` (B at a downed opponent), `run_step`
    (a whipped runner at a wall), state 0x04's reversal chance and state 0x80's apron-lift slide.
  - Then (same day) the apron corner walk input and B at a downed opponent while running are in, and
    three more fixes from the asm: `input_cup_opponent` gives 0x28 with a weapon and 0x61 without (the C
    had them swapped); `wrestler_settle_pose` clears down_hit_count / down_hit_kind (+0xCA / +0xE5), not
    dir / fall_chance; `run_start` aims at facing_to_opponent (+0x24), not facing. All four tag traces
    now replay identical in every ported state (4-CPU 5,972 / 5,972 slot-frames; tag start 2,356; taunt
    1,348; tag-in 1,312), with 7 one-frame unported paths left on the 4-CPU trace.
  - Open: on frame 309 of the 4-CPU trace the game makes 57 rng_main draws and the C 56 (every other
    frame matches), so slot 3's perception and mode 32 timer come out one pool entry off for that frame.
    The C's draws that frame are all AI (perception, ai_tag_help_check, ai_wants_tag_out,
    ai_decide_apron_side, ai_decide_special / ai_decide_move_list for slot 2's ground attack); the asm of
    ai_decide_special and ai_decide_move_list was checked and matches. A logger hooked into
    rand_pool_next's call of rng_main (tried 2026-11-03, reverted) changed the run itself (a different
    seed from the first frame), so it could not say which call the extra draw is.
- Andrew's nine input recordings of 2026-11-03 (scripts `nm_kane_*`, `nm_p1_striking_*`, `nm_tall_*`,
  `nm_steps_down_*`, `nm_step_onto_table_*`, `nm_hit_light_and_hard_*`, `nm_p2_run_along_apron_*`): all play
  back RAM-identical in akiemu, and with the states and paths ported from them every record the C computes
  matches: eight replay with no frame skipped. Decompiled from the asm for them: `input_c_up` (all
  branches; `climb_place_free`, `illegal_man_leave`, `partner_in_move`), states 0x6F, 0x9A / 0x9B, 0x26 /
  0x27, 0x17, 0x70, 0x06, the L side-step in 0x03, `climb_check`, `input_apron_moves`,
  `apron_grapple_start`, the ladder test of `input_ladder_climb`, and the `pair_scripted_place` cases for
  0x17, 0x6F, 0x70, 0x9A / 0x9B. Open from them:
  - ~~The outward elbow drop needs the aerial code~~ Done the same day (`src/nomercy/nm_aerial.cpp`):
    `dive_aim`, `aerial_step`, `aerial_height`, `special_move_kind`, `dive_counter`, `dive_land_hit`,
    `special_move_land` and states 0x75 / 0x76 / 0xC0; the elbow-drop recording replays identical with no
    frame skipped. Wrestler +0x2A0 is named `dive_target` (types.h). Still unported in it: a dive from a
    ladder (`dive_aim_target`), and a dive onto an object (`object_set_motion`). The elbow drop missed
    in the recording, so `dive_counter` / `dive_land_hit` ran their tests but not a landing.
  - `pair_scripted_place` still stops for some state on 2 frames of the tag-in recording.
- Andrew's cage and whip recordings (2026-11-03: `nm_cage_match_irish_whip_and_strike`,
  `nm_irish_whip_and_throw`, `nm_irish_whip_and_big_throw`, `nm_climb_onto_cage_and_climb_back_down`,
  `nm_lose_stamina_on_cage`, `nm_cage_move_and_strikes`): all six replay with no frame skipped and every
  record identical. Decompiled from the asm for them: the whipped runner at a wall in `run_step` (the
  steps 0x15 / 0x16, a barrier 0x2D / 0x35, 0xC1, the ropes and a charged whip over them 0x70),
  `lockup_place_move`, `grapple_attempt_double`, states 0x24, 0x5B, 0x5C; on the cage wall `walk_move`,
  `walk_vector`, `turnbuckle_check`, `turnbuckle_anim`, the cage case of `lying_body_axis`,
  `cage_climb_check`, `place_from_zone_pair`, states 0xC6, 0xC7, 0xCC, 0xCF; and the `pair_scripted_place`
  cases for 0x5C and the cage drop. Still unported on the way: a whipped runner through a cage door
  (`area_change`), the states the climb starts (0xC8 when reached by `cage_climb_check` from the ring,
  198, 199 via `turnbuckle_check` and 202 were not entered in these recordings beyond what replayed).
- Andrew's cage victory and turnbuckle recordings (2026-11-03, 11 of them): 9 replay with no frame skipped and
  every wrestler, object and AI record identical (`nm_cage_victory_failure`,
  `nm_cage_activate_special_and_trigger_win_condition_by_climbing_over`, `nm_cage_high_succesful_elbow_drop`,
  `nm_P2_yanked_off_of_cage`, `nm_cannot_kick_down_on_cage_because_no_one_is_ground_level`,
  `nm_turnbuckle_climb_cancel`, `nm_turnbucke_climb_and_jump_strike`, `nm_turnbuckle_climb_hold_taunt_strike`,
  `nm_special_into_finisher_move_and_lift_opponent_to_seating`). Decompiled from the asm for them: states
  0x0A, 0x38, 0x39, 0x75 / 0x76 / 0xC0, 0x79-0x7D, 0xC4, 0xC5, 0xCD, 0xCE, `input_climb_turnbuckle` and
  the `pair_scripted_place` cases for 0x0A and 0xCD. Answers and open points:
  - **The cage win (settled from code):** on top of the cage (0xC4 step 2) each press of A, B, L or R
    (`pressed & 0xC030`) takes one off a timer, which must go below 0: 26-35 presses, 16-25 in finisher
    mode (flags_ee 0x04). The first go restarts 0xC4 with the next set of animations, the second goes over
    (0xC5), so two mashes in all. **Differs from what Andrew saw:** neither setter of 0xC4 (`input_c_up`'s
    cage section, 0xC4 itself) tests the meter, so SPECIAL shortens the mash rather than being required;
    a low meter can still throw the climber off (flags_ee 0x01 goes to 0xC6, and 5 spirit drains each
    loop). Not tested: a win from the top without SPECIAL. A recording of that would settle it.
  - **Port bugs the replays caught, both my misreads of the asm:** `input_c_up` returned early on no
    press and skipped the cage hold counter; state 0x63 cleared `down_hit_count` every time, when the
    asm only clears it (and `down_hit_kind`) when the hit does not stay. Static reading of branch
    structure stays the main source of error, not formulas.
  - Wrestler +0x50 is `dive_hit` (what a dive landed on), +0xC4 is `pickup_anims`, an N64 pointer into
    the row tables 0x8014DC50 / 0x8014DC78 (it was named `pickup_timer`; it is no counter).
  - Renamed: `state_CD_cage_yank`, `state_CE_cage_yanked` (seen in `nm_P2_yanked_off_of_cage`; were
    turnbuckle_grapple / grappled), `state_75_apron_dive`.
  - Not exercised by these recordings, so read but unchecked: the 0x79 grapple-pair branch (0x44 / 0x48 /
    0x4A), the pin pair at the end of a dive (0x44 / 0x45 step 3), a 0x79 dive onto an object
    (`object_set_motion`, stops as unported), 0x39's reversal (chance_roll kind 11) and its 0x55 path.
  - `nm_submission` frame 410, slot 1 (open): region[3] drops from 46.6 to 0.5 in the recording, with no health
    change, in state 0x49 step 1 under the attacker's 0x48. The C does not do it; every other record matches. Not the
    skipped frames, as first guessed: 0x46 is ported and none are skipped now. The asm's only writers of the array are
    its setup, `region_add` (only from `apply_damage`) and 0x49's own step 2, so something else is writing there.
  - **Recorder limit:** `nm_cage_match_being_paused_and_unpaused_in_game` and
    `nm_cage_match_being_fully_quit_in_game` do not play back (they differ from records 14 and 0, and in
    the C from frames 15 / 16). Input during the pause menu is not captured. Pausing and quitting are
    out of scope for the match logic anyway, but those two cannot serve as traces.
- State enum (2026-11-03): states are `state::Name` (`src/nomercy/nm_state_ids.h`, generated by `tools/decomp/gen_states.py`
  from `_re_docs/symbols/nomercy/state_enum.toml`). The raw numbers in the code were rewritten by script; names are as
  sure as each handler's notes. Open: 0x75 / 0x76 and 0x79-0x7D are named by their moves until someone says what those are.
- AI mode enum (2026-11-03): modes are `ai_mode::Name` (`src/nomercy/nm_ai_mode_ids.h`, generated by
  `tools/decomp/gen_states.py` from `_re_docs/symbols/nomercy/ai_mode_enum.toml`). Only 32 of the 145 modes appear in any
  recording (`[recorded]` in that file); every other entry says so on hover and is named from the code alone. A few
  numbers that aren't modes were mislabelled by the rewrite and fixed by hand; more would be wrong labels, not behaviour.
  Vague mode functions and `ai_try_mode_67` / `_89` were renamed; "Renamed from" is on each in the TOML.
- The running game (2026-11-03, `src/nomercy/nm_run.cpp`, from IDA drafts, **no recording reaches most of it yet**):
  0x08, 0x0D-0x10, 0x15, 0x16, 0x19, 0x1D, 0x1E, 0x71, 0x78, 0x99, 0x9E, 0x9F, 0xA0, 0xA1. Still stopping as unported:
  `run_corner_stop` (0x78's take-off) and `pair_scripted_place`
  for 0xA1 and 0x89. The kick-catch recordings (`nm_catch_and_counter_kick`, `nm_catch_kick_press_b_...`) replay
  identically, the first check of a draft-ported state (0x69 / 0x6A).
- The ground game and counters (2026-11-03). Ported: 0x25, 0x32, 0x36, 0x37, 0x46, 0x47, 0x4D, 0x97, 0x98,
  `ground_grapple_start`, `ground_grapple_defend`, `recover_tick_ground`, `input_stir`, `input_c_left`. Andrew's 9 ground
  recordings replay with every wrestler and AI record identical; only `nm_stand_P2_...` skips frames (30, in
  `match_frame_extras`, paused play, unported). `nm_ground_hold` and the rest of `nm_submission` are clean now too.
  - **Port bug:** 0x4E step 1 returned where the asm falls into the shared slide and bounds at the end of the switch.
  - Renamed: `state_32_dazed` (was `state_32_ropes_lean`; the ropes are only its apron case), `state_97_spin_dazed` (was
    `state_97_l_on_ropes`), `state_98_spun` (was `state_98_on_ropes_taken`).
  - Counters, `src/nomercy/nm_counter.cpp`: 0x64-0x69, 0x6A, 0x6C, 0x6D, 0x88, 0x89, `finisher_full_spirit_roll`,
    `object_hand_over`. **Most have no recording that reaches them yet**, and 0x89's `pair_scripted_place` case still
    stops as unported.
  - **Method:** those were the first states drafted from IDA's pseudocode (`tools/decomp/hr_draft.py`, from
    export.json, types.h applied) instead of read from the asm. Hex-Rays on MIPS drops f32 arguments (uninitialised
    v-variables), which the tool's asm-order list fills in, and it can't help with `pair_scripted_place`'s shared tails.
    It also hid `reversal_check`'s return value, which the callers branch on, so the drafts still need the asm for returns.
    All 5 counter recordings then matched on every record: `nm_catch_and_counter_kick`, `nm_catch_kick_press_b_...`,
    `nm_kick_caught_and_pressed_a`, `nm_grapple_deflected`, `nm_p2_is_dazed_then_has_punch_countered_...` (with the
    follow-up path of `move_chain_check`, `move_anim_offset`, `reversal_check`, `move_follow_up` added).
- The ladder match (2026-11-03, `src/nomercy/nm_ladder.cpp`), ported from the asm ahead of any recording, then
  checked by Andrew's 12 ladder recordings (`nm_ladder_*`, `nm_climb_*`, `nm_P1_*`, `nm_p1_*`, `nm_p2_*`,
  `nm_run_into_*`, `nm_strike_ladder_*`, including the win): all 12 replay with no frame skipped and every wrestler
  record and AI block identical. Ported: states 0x2F, 0x8B-0x8E, 0x91, 0xAA-0xBD, `ladder_pose`,
  `ladder_object_update`, `object_set_motion`, `object_at_point`, `object_contact_faces`,
  `objects_wrestler_contact`, `input_ladder_climb`, `input_weapon_grab_zone`, `input_weapon_use`, `input_c_left_held`,
  `ladder_camera`, the dive aim from a ladder, 0x4C on the ladder, `grapple_attempt_slide`, `arena_zones_swap`, and the
  object branches of `run_collide`, `strike_land`, `object_hit_check`, 0x79 and `special_move_kind`.
  - **The state code was right first time; the one logic bug was older.** `run_exit_check` had a test backwards, so
    C-down at the ropes while running left the ring when the opponent was inside; the asm wants it outside
    (location 0x66). Only a run with C-down near the ropes showed it. Bugs here keep being branch senses.
  - **Open: a held ladder's probe points** (object model +0x2C on) differ on a few frames (`nm_p2_struck_...`
    frame 30, `nm_ladder_while_hold_...` frames 121-125, some matching the previous frame's value), while every
    wrestler record matches. Probably the known limit that the render tree (g_model_nodes) is not recorded, so the
    hand point is stale in a step replay. That is a guess; not ruled out as a real difference.
  - Bleeding and first blood are written up in `match_rules.md`. **First blood itself is not yet seen in a recording**
    (rule 0x1000: the bleeding recording has rules 0x833), nor the guest referee's C-left calls.
  - Read but not exercised: lifting a table (0x8C with `arena_zones_swap`, `crowd_group_show`,
    `effect_group_show`), a dive from a ladder at a table (`dive_aim_target`), a whipped runner into a ladder.
  - More renames: `state_2F_bleeding` (was `state_2F_ropes_step_through`, a call-graph match that was wrong),
    `state_8C_lift_object` (was `state_8C_steps_grab`), `input_c_left_held` (was `guest_referee_call`, which is only
    part of what it does).
  - **Winning:** the prize needs the ladder within 30 of the ring's centre, otherwise the reach (0xBD) just
    ends in 13001 (step 2). Up held for 16 frames at the top starts it. Then each press of A, B, L or R
    (the same `pressed & 0xC030` as the cage) takes one off 40 - spirit / 5, which must go below 0: 41 presses
    at 0 spirit, 21 at 100. 16 frames of down, or anyone losing, cancels the reach.
  - A set-up ladder topples by itself 23 frames on, unless its owner is still in 0x8D (the camera shake instead).
  - Renamed: `object_contact_faces` (was `grapple_contact`, a call-graph match from World Tour that was wrong:
    it builds an object's faces from its probe points), `state_BC_ladder_thrown_off` (was `state_BC_ladder_c_up`:
    `ladder_pose` sets it when the ladder flies; the C-up note in the states review above doesn't hold).
  - The N64 clears one byte too few in `object_contact_faces` (its second mem_zero is at out + 1, not out +
    48). Harmless, since only faces that were written get read, and kept as is.
  - The port keeps the faces in a scratch area of the RAM image (0x807FFE00), because `segment_hit` reads
    walls from RDRAM. The N64 has them on its stack.
  - Still unported nearby: 0x90 and the double-team states. The ladder AI modes (133-139) have not run on any
    recording that drives a CPU on the ladder.
- Weapons and loose objects (2026-11-03, `src/nomercy/nm_object.cpp`, from the asm; traced with Andrew's
  input recording of Kane vs Stephanie: steps, a crowd weapon, a throw, a count-out). Decompiled:
  `object_model_update` (main segment; the render binding is left to the host, the debris pick's
  rng_main draw kept), `object_pose_in_place`, `weapon_release`, `objects_update`, `object_fly`,
  `arena_area_lookup`, `segment_hit` kinds 1-3, `object_hit_check`, `weapon_tip_point`,
  `weapon_pick_type`, `object_free_slot`, `weapon_type_allowed`, states 0x6E, 0x8A, 0x8F,
  `may_leave_ring`, the crowd / steps branch of `input_weapon_grab`. On that recording every wrestler
  record (4,032 slot-frames), object record (7,049 object-frames) and AI block (1,014 frames) the C
  computes is identical. Findings and open points:
  - **The logic reads draw output.** An object's probe points (its model record +0x2C, x negated) and
    origin (+0x20) are written by `objects_draw` (main segment, the draw after each tick) from the
    object's matrix under its holder's hand node, and `object_fly` / `weapon_tip_point` /
    `weapon_release` read them on the next tick. That part is decompiled as `objects_probe_points`
    (run at the start of `match_tick`); the library has to keep it on the logic side. The trace now
    records the object model records (`object_models`, `object_debris` in `nomercy_regions.toml`).
  - In `--step` replays the hand point / probes differ for a few frames right after a skipped frame,
    because the wrestlers' render tree (g_model_nodes) is not recorded; not a C difference.
  - The wrestler model poses (`models`) differ on 11 of 1,014 frames of that recording (slots 0, 1 and
    3, in threes, e.g. records 34-36, 52-54, 140-142, 547-548), with every wrestler record identical:
    something in the animation posing (`nm_model.cpp`) at those frames, not looked at yet.
  - Still unported on it: `input_c_left_held` (C-left held: the guest referee, `input_weapon_use`,
    `input_c_left` and the holds it starts; 4 frames) and `input_c_up` with a direction (2 frames).
    `place_from_zone_pair` (dropping off the cage wall), the debris objects' matrix (type 31+), a
    carried object (flags 0x03), a thrown object hitting another (`object_set_motion`) and the
    ladder paths stop as unported.
- Count-outs and the time limit (2026-11-03): `countout_tick` and `match_clock_tick` are decompiled from the
  asm (`src/nomercy/nm_frame.cpp`, `match_rules.md`); DQs were already in (`strike_dq_check`: a weapon strike,
  strike flags 0x100600, on the other legal man under rule 0x2000). Still open:
  - ~~`match_result` is a stub~~ (decompiled from the asm 2026-11-03, with match_end's over-the-top, guest
    referee and falls-counted paths and the elimination tallies `result_stats_record`; not trace-checked).
  - ~~**[RAM]** a count-out has not been seen in a trace~~ Seen 2026-11-03 in Andrew's input recording
    `kane_vs_steph_picks_weapon_throws_it_gets_counted_out_loses` (10-second count-out, Kane counted out
    on the floor): the match records (match_b: counts, clock, flags) replay identical on every frame the
    C runs. Not yet checked field by field against the 2 / 3 / 4-second rhythm in `match_rules.md`.
- Tag matches (code 2026-11-03, `src/nomercy/nm_tag.cpp`, `match_rules.md` "Tag matches"): the tag
  conditions, the exchange (0x93 / 0x94, `tag_in`), the apron partner (0x95), the illegal man's walk back
  (0x92) and when he may stay (`idle_check`, corrected from the asm, `may_stay_inside`) are written out;
  Wrestler +0xDE is named `tag_health`. Still open:
  - ~~no tag seen yet~~ Human tags seen 2026-11-03 (d-pad toward the partner's apron side plus C-up, with
    the partner's hand out, idle_flags 0x08; `nm_tag_tagin.json`): 0x93 / 0x94 replay identical. Still not
    seen: a CPU tagging out on its own (the 4-CPU save did not tag in 1,500 frames).
  - The CPU's tag-out wish (`ai_wants_tag_out`, sub_800FB4C0's health drop of 51 against tag_health) and the
    tag help time option (0x800AA04E) are not read yet.
  - `pair_scripted_place` (0xDC4 bytes) is not decompiled; 0x94 and about 30 paired states use it.

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

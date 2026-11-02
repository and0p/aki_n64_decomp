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
  0x8A); fights going up the entrance aisle (check against 0x8D, read so far as fetching a
  weapon); instant replays after a big move in singles; multi-hit combos (chained strikes); 6
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
- Match types in the guide that the earlier games lack or differ in: triple threat, cage
  (escape to win), Royal Rumble (four in the ring, over the top rope), guest referee, ladder,
  ironman (most falls in 5 to 60 minutes), survival. Where each is set up and checked.
- `g_state_handlers` entry 0xD2 holds `stance_turn_anim` (a carried-over name). No state setter
  call with a constant goes above 0xCD; is 0xD2 a real state, reached with a computed number?

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

States whose code was read but whose meaning is still a guess. Most of them only play an
animation and hand on, so watching the animation answers what they are. Open
`_tools/viewer/nomercy_viewer.html` (built by `tools/rom/nomercy_models.py ROM viewer`), type the
animation id (= archive file number) into the filter, and note what the move looks like. The ids
are the constants each handler passes to `wrestler_set_anim` (2026-11-02). Where none is listed, the
handler picks its animation from a table or the move record, so watch the state in a match instead.

- **Stand-up and neutral:** 0x00 (12959), 0x02 (11795 / 11796, from the apron d-pad), 0x09
  (11876), 0x15 (12003 / 12005), 0x16 (12625), 0x19 (12055), 0x1E (11978, a hit taken with weapon
  release), 0xD2 (12506, carried over as a stance turn), 0x66 (special pose?).
- **Taunt / apron:** 0x1B (11745, 12168: taunt, or the partner's apron wait), 0x0A / 0x93 / 0x95
  (12092, 12921 / 12922: tag reach and apron partner), 0x94 (12924).
- **Hit reactions and falls:** 0x17 (12147-12150), 0x2E TKO check (12611-12616), 0x32 dazed
  (12557-12559), 0x33 off balance (12562 / 12563), 0x4D hit while blocking (12990 / 12991),
  0x18 corner slump (12060), 0x05 / 0x35 run collision and knocked back (table-picked), 0x2D
  knockdown (12565 / 12579 seen in RAM: what picks the slouched sit-down fall?).
- **Lifts and carries:** 0x34 (12076 / 12081, being lifted?), 0x3A (12066 / 12068), 0x3C (12070 /
  12072), 0x3E (12074 / 12083), 0x3F (12082), 0x40 (12077), 0x38 / 0x39 pick-up (11909 / 11910),
  0x80 carried escape (12642 / 12646).
- **Getting up / down:** 0x4E / 0x4F (12157, 12171, 12172), 0xA7 stay down (12943, 12950).
- **Grapple and counters:** 0x68 (11887, from `dive_counter`), 0x6B grapple evade (11877 /
  11878), 0x6E cheap shot (12128, 13017), 0x6F (11824, 12805, 13016: also climbing onto a raised
  place), 0x72 (12104), 0xD0 / 0xD1 (12041-12043: the lock-up's second whip kind).
- **Turnbuckle / cage family:** 0xC2 (12827 / 12833), 0xC3 (12828 / 12830), 0xC6 (12862 / 12863),
  0xCA (12860 / 12862), 0xCB (12855), 0xCC (12804), 0xCF (12843), 0xC8 cage climb start (12856 /
  12858). Which are the cage and which the turnbuckle?
- **Ladders:** 0xAA-0xBD (12967-13001; e.g. 0xB6 tipping 12972-12974, 0xB4 grapple on the ladder
  12982 / 12983, 0xBA / 0xBB 12993 / 12994).
- **Weapons:** 0x8B drop (12390), 0x8C ring steps (12379, 12384, 12962, 12964), 0x90 (12458-12460).
- **Picked from tables (watch in a match):** 0x06, 0x07, 0x1A, 0x1C, 0x1D, 0x5A-0x60, 0x67, 0x71,
  0x73-0x78, 0x7E, 0x7F, 0x88, 0x89, 0x8E, 0x8F, 0x92, 0x96-0x99, 0x9C / 0x9D, 0xA4, 0xBC, 0xC1, 0xC7.

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
- `move_info` (+0x2E4), `info` (+0x310), `zone_info` (+0x348): what the pointed-to records are
  (the area data with a forced weapon type at +8, trait bits at info +0x20).
- `wrestler_start_move` and the move table it reads: move numbers 172-190 so far; tie them to the
  per-wrestler move sets.
- States: a profile pass (2026-11-02) named about 50 more from setters, animations and calls (many `guess`).
  Still unnamed or only paired: 0x1B (apron partners alternate with 0x01; `guest_referee_call`
  sets it), 0x5B /
  0x5C, 0x4E, 0x6F, 0x83-0x85 (double team), 0xAE / 0xB1 (after a move on a ladder), 0xA6 (a
  count). Read each handler's animation and setter.
- `end_pose` (+0xE2) values that `state_30_settle` switches on.
- Start table 0x80146350 (`match_init_wrestlers`): layout per match type.
- New features still to find among the states: apron walking round the corners, spinning a
  groggy opponent, C-right focus change (probably writes `opponent`), the cage, Z (CPU /
  manual control). Look for their inputs in the stand / apron input functions.
- `g_state_handlers` entry 0xD2: no setter uses a constant above 0xCD.
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
    call-graph spot may be worth rechecking in that game.
  - The CPU: the situation classes are mapped (`AiSituation` in `symbols/nomercy/ai.toml`); the deciders
    are named by where-class (`bulk.toml`); next is what each personality trait means (which chance
    table it picks in which decision), the remaining mode bodies, and a converter-side dump of
    the weighted mode tables (`ai_pick_mode_weighted`; 14 table addresses found directly, more
    passed as computed pointers); every function in the AI area (0x800F0000-0x8010A000) now has a name, most `guess` from one
    read; the trait readings in `ai_update`'s desc are guesses from where each trait is used; a profile pass (2026-11-02) gave about 40 modes `guess` names by the buttons
    they press. No RAM needed: the modes write the buttons to AI block +0x0C, so the code shows
    what each one does; RAM would only show how often each comes up.
  - The damage history (+0x344) entries: who writes them and what (a, b) are.
  - `apply_damage`'s three summed parts (only the base damage is visible in the pseudocode).
  - The move record's remaining bytes, and how a wrestler's 548 moves are laid out (which
    numbers are strikes, grapples per direction, running, ground, turnbuckle, finishers).
  - Match types: which bit of `g_match_type` is which match. Reading the menu overlay that sets
    it would settle it from code (not in the IDA build yet); otherwise **[RAM]** at the start of
    each match type.
- **[RAM]**: which weapon each type number is (17 = stop sign, 5 = the next crowd weapon) and
  which 12 types the unlock mask (`weapon_type_allowed`, 0x8009FDEA) covers; who a wrestler's
  ally is (ally_id, set outside the match); the option words 0x80097EC0 / 0x800A9950 under the
  menu settings; what `g_arena` (0x80159628) is per arena.

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

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
  Revenge's 11 named types (README) suggest what to look for. (`outside_ring.md`)

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

- **Battle royal mode**: mostly answered from code (README "Battle royal": eliminations are
  falls, instant ring-out, entrant list `0x8007FBC0`, entry through 0x8F). Still open: the
  end-of-match check once every entrant is used, how losers leave (0x82 / 0x90), and whether
  over-the-top-rope has any special handling (none found).
- Leads from a feature list in an online guide (Andrew, 2026-11-01), to match against code:
  the run-in when one wrestler keeps the advantage (probably state 0x8F, `run_in_check`); the
  stance only near the focused opponent; runners always hop over a fallen opponent (maybe 0x89 /
  0x8A); fights going up the entrance aisle (check against 0x8D, read so far as fetching a
  weapon); instant replays after a big move in singles; multi-hit combos (chained strikes); 6
  arena setups. (`wcw_revenge/README.md`)
- Revenge move slots marked `weak` in `wcw_revenge/moves.md` (guide fit): settle them from
  Revenge's own grapple slot table (World Tour's `g_grapple_slots`). Hidden wrestlers are done; alternate entries, managers and THQ Man (reads as AKI Man) are not. (`wcw_revenge/moves.md`)
- Identify the new Revenge states without a World Tour twin (list in `wcw_revenge/README.md`,
  "New states"): needs RAM, i.e. the aki_debug tool ported to the Revenge recomp.
- Hand-check the 73 `guess` pairs in `symbols/revenge/from_wcw.toml` (call-graph matches with
  a score below 1.0), and re-run `tools/sym/match_calls.py` as more World Tour functions get
  names. (`wcw_revenge/README.md`)
- RAM (paused CPU match): check the 16 new AI modes 46–61 and the AI block (`0x800F9B88` +
  96 × slot) against the first reading in the notes. (`wcw_revenge/README.md`)
- Where Revenge sets CPU difficulty (World Tour's 50 / 250 / 900 skill table is not there in
  that form). Lead (2026-11-01): `ai_state_init` (`0x800A97A0`) copies a per-slot level from
  `0x8007E2A8` (values 0-2, 3+ becomes 1) into the AI block +0x10. (`wcw_revenge/README.md`)

- Each Revenge wrestler's default attire (attires are shared per body build), and the roster
  names for the 10 groups. (`wcw_revenge/README.md`)

- Revenge arenas: how the untextured turnbuckle pads (mesh 755) are coloured, the objects
  loaded by `0x80011BB4` from `0x80036D18`, and the transforms of the shared stands at
  `0x80036750`. (`wcw_revenge/README.md`)

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

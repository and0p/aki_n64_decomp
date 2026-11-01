Reverse engineering docs will live in this folder.

Subfolders TBD, some ideas:

individual_games/
    one folder for each game for things we find that are specific to them
file_formats/
    one markdown for each file type we derive from ROMs

other findings go in MD files right in this folder?

## Index

- `state_comparison.md`: ideas for checking the library against the original frame by frame
  (golden traces from the emulator, a shared field list, diffing, the recompiled code as oracle).
- `QUESTIONS.md`: the tracker for open questions, grouped by how to settle them (viewer, RAM,
  code). Add new ones there; when one is answered, record it in its doc and remove it.
- `individual_games/wcw_world_tour/match_state.md`: World Tour in-match RAM layout (clock,
  wrestler array, spirit, positions, joints, action state and animation ids), and how to read
  RDRAM out of the recomp from IDA (helper: `tools/ida/n64mem.py`).
- `individual_games/wcw_world_tour/moves.md`: move sets (222 slots per wrestler), the packed
  24-byte move record, the animation lookup, and the asset archive file table and loader.
- `individual_games/wcw_world_tour/animation_states.md`: which states and pickers start the
  generic animations 2901–3027, checked against Andrew's hand tags (`anim_tags.json`).
- `individual_games/wcw_world_tour/grapples.md`: the grapple flow: lock-up, input window,
  what each input does, the defender's reversal roll, breaks.
- `individual_games/wcw_world_tour/match_options.md`: the match modes and options from the
  menus, and the option globals seen in code so far.
- `individual_games/wcw_world_tour/movement.md`: directions, walking and walk speed, running,
  ring/arena bounds and wrestler-to-wrestler push-apart.
- `individual_games/wcw_world_tour/arena.md`: ring, ropes, barrier, crowd and entrance
  geometry, and how it lines up with the collision boxes.
- `individual_games/wcw_world_tour/frame_loop.md`: the 30 Hz frame loop, input and button
  remapping, and the random number generators.
- `individual_games/wcw_world_tour/strikes.md`: starting a strike, the hit test and joint radii, hit reactions and their odds, blocking and just-frame block counters.
- `individual_games/wcw_world_tour/knockdown.md`: where a wrestler goes after a move, time spent down, get-up options, corner slump and running into the post.
- `individual_games/wcw_world_tour/ai.md`: the CPU opponent: AI block, difficulty and perception noise, situation classes, how modes are chosen, mode list (first pass).
- `individual_games/wcw_world_tour/tag_team.md`: legal man, tags and the corners, tag-in bookkeeping, saves.
- `individual_games/wcw_world_tour/outside_ring.md`: climbing in and out, crowd weapons, the turnbuckle.
- `individual_games/wcw_world_tour/body_parts.md`: body-part condition: how parts get hurt, the four effects (weaker attacks, slower legs, injured idle, submissions), carry-over between matches.
- `individual_games/wcw_world_tour/match_end.md`: pin count and kick-out formula, submissions, TKO, count-out and time-limit decisions, the rule switches and how results are recorded.
- `individual_games/wcw_revenge/README.md`: Revenge compared with World Tour: tooling (IDA build,
  symbols), memory layout, first landmarks (146 states, 62 AI modes, 944-byte wrestler).
- `file_formats/asset_archive.md`: the ROM's numbered file archive, its LZSS compression and a
  map of which files hold what (tool: `tools/rom/aki_archive.py`, with `survey` for the map).
- `file_formats/wrestler_models.md`: wrestler mesh parts, seam skinning, CI4 textures, and
  the roster → costume → body type / head variant tables that pick the files.
- `file_formats/animations.md`: the 19-joint skeleton, the posing maths, the streamed
  animation file format, and the animation previewer (`tools/rom/wcw_models.py viewer`).
- `individual_games/wcw_world_tour/ida_database.md`: building the static MIPS IDA database
  with Hex-Rays (`tools/ida/wcw_build_idb.py`).
- `symbols/wcw/`: **the source of truth for named functions, globals, enums and structs**
  (TOML plus `types.h`). Query it with `py tools/sym/sym.py`; see `tools/sym/README.md`.
  Markdown notes should point at symbol names rather than repeat these tables.
- `gamefaq/`: two public FAQs (move lists per wrestler, finisher rules). Treated as accurate;
  they were used to name move slots in `moves.md`.

# World Tour: tag team

First pass (2026-10-30), read from the code: `tag_try` (`0x800DDF9C`), `apron_enter_try`
(`0x800DD970`), `tag_in` (`0x800DEC6C`), the tag states 0x71–0x73 and the AI's tag checks.
Not checked against a live match. Match options and elimination are in `match_end.md`.

## Who is legal

- `LEGAL` (`flags_lo` 2) marks the wrestler in the match for each team; `TEAM_B` (0x20000000)
  marks the second team; `teammate_idx` (+0x344) is the partner (0↔1, 2↔3).
- The **human always controls the legal man**: tagging swaps the CPU flags of the pair
  (`tag_in`), the same as `tag_partner_takes_over` does on an elimination.
- **Rule 0x40 (no tagging needed)**: everyone may fight. Otherwise `wrestler_may_act` keeps an
  illegal partner out unless a legal man is on the floor or its partner needs help (below).
- `tag_time_tick` (`0x800DD6C4`, every update) counts the legal man's frames in the ring in
  `0x800F1D38[slot]`; `tag_in` resets it and stores the health at tag-in in
  `0x800F1D30[slot]` (the AI uses both to decide to tag out).

## Making a tag (C-up)

Through `input_cup_ropes` (C-up with the d-pad toward the ropes) and the turnbuckle state:

- **Each team has a corner:** `on_own_corner` (`0x800DDE78`) is true on the apron with x and z
  both negative for team A, both positive for team B. The tag needs the legal man in the ring,
  standing (or the partner in state 0x73 early), in the **same ring zone** as the partner on
  the apron at that corner, and within reach (`sub_800DE418`).
- Then `tag_exchange` (`0x800DE564`): the legal man goes to **0x72** (tags out: 3347, steps back)
  and the partner to **0x71** (tags in: 3346, then `tag_in` and in through the ropes). The
  variant from the top rope or from the partner's reach (state 0x73 / turnbuckle step 4) skips
  ahead: 3370/3371, `tag_in`, then the turnbuckle state (3020), so the new man comes in **off the
  top rope**.
- If no tag is possible, C-up just goes through the ropes (state 0x54).

### Reaching for the tag (state 0x73)

The partner on the apron at its corner can press C-up to reach: 3366 (3367 when the legal man is
lying), then 3368/3369 by facing; R during it cancels (0x80000); otherwise it climbs the
turnbuckle (state 0x09 step 4). Either way the tag happens when the legal man comes close.

## The partner coming in

`apron_enter_try` (C-up on the apron toward the ring, or from the turnbuckle):

- If `wrestler_may_act`: climb in (state 0x55). With tagging required and the partner not
  legal, it may only come in when **its partner's `flags_hi & 0x800`** is set, which pins and
  holds set on both wrestlers (`match_end.md`): **the save**. It then targets the opponent:
  the one pinning or holding (states 0x31, 0x35, 0x37, 0x64, 0x66) or being held by the partner
  (0x32, 0x36, 0x38, 0x65, 0x67), so a strike breaks the pin or hold (`strikes.md`).
- Otherwise, standing at its own corner → state 0x73 (reach for the tag); from the turnbuckle
  facing its own corner → straight to 0x73 step 2.

Rear-L double teams (0x64/0x65, `grapples.md`) are the legal partner holding an opponent so the
other can strike.

## AI

The CPU tags out when it has lost 51+ health since tagging in or has been in for 90 s
(`ai_wants_tag_out`), walks to the partner (mode 45) and presses C-up; on the apron it reaches
or climbs in by the same rules (`ai.md`).

## Open questions

- ~~The thresholds in `sub_800DE418`~~ Read from the disassembly (2026-11-01): within reach when the
  distance across the ring side is **under 90** and along it **under 80** (pelvis positions;
  for sides 1 / 3 the axes swap). (Old note: the tag reach compares |dx| and |dz| between the two, swapped
  for ring sides 1 and 3; the constants are lost in the pseudocode).
- Whether the illegal man is counted out or pinned (the count code only counts LEGAL wrestlers in
  tag, `countout_tick`).

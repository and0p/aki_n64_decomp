# WCW/nWo Revenge: match types, falls and run-ins

Part of the WCW/nWo Revenge notes (index: `README.md`). Written as differences from World Tour
(`../wcw_world_tour/`); anything not mentioned is not checked yet, not known to be the same.

## Match type and rule words

`0x8007E280` is the match type, `0x8007F0A0` the rules (World Tour `g_match_type` /
`g_match_rules`). Bits seen in the code:

| Word | Bit | Meaning |
|------|-----|---------|
| type | 0x100 | run-ins possible (`entrants_enter_check`); `match_end` also compares the two sides' health + spirit with it |
| type | 0x200 | tag |
| type | 0x400 | battle royal (everyone for themselves) |
| type | 0x800, 0x1000 | read by `match_end` and `match_clock_tick`; probably the health decision and time-limit variants (as World Tour's 0x800), not checked |
| rules | 0x200 | interference (run-ins) on |
| rules | 0x40 | tag rules for the illegal man (`illegal_man_check` and friends) |
| rules | 0x400 | read by `down_count_allowed`, `state_28_down`, the run-in state and the whip input; not identified |

## Falls

`match_record_fall` (`0x800B9730`, World Tour `match_record_result`) records each decision: the
result word goes to `0x8007E848`, winners get lo 0x4000, losers lo 0x8000, and the per-wrestler
win / loss tallies at `0x8007E850` are updated (`result_stats_record`). A wrestler who has lost
a fall stays down in 0x85 or goes to 0x82 / 0x90 when standing (`states.md`).

## Run-ins

With match type 0x100 and rule 0x200, `entrants_enter_check` (from `match_tick`) may bring a
run-in into an empty slot: `run_in_try` takes the roster word stored for that slot at
`0x800FACEA`, when the side mask `0x800FADD0` allows it (it tracks who has had the advantage)
and the wrestler being helped stands or is down, and enters state **0x8F**, which walks the
newcomer into the ring.

## Battle royal (2026-11-01, code + emulator)

- **Match type** `0x8007E280` bit **0x400** (World Tour's "everyone for themselves"). The menu
  path is Special Match > Battle Royal; the entrant count goes 4, 8 ... 20.
- **Entrants:** the full list is u16 roster words at `0x8007FBC0` in entrance order (the 8-man
  test: Nash, Raven, DDP, Saturn, Hogan, Sting, Lodi, Booker T); `0x800EB474` total, `0x800EB476`
  still waiting, `0x800EB478` index of the next. The first four fill the four match slots.
- **Eliminations are falls.** Any decision (pin, submission, TKO, count-out) goes through
  `match_record_fall` (`0x800B9730`): the loser gets lo 0x8000; if entrants are left,
  `br_next_entrant` writes the next roster word into the loser's +0x48 and sets lo 0x10, and
  `entrants_enter_check` (from `match_tick`) then puts that slot into **state 0x8F**, during which
  the slot switches to the new wrestler (roster +0x02) and it walks into the ring. With nobody
  left the loser just loses LEGAL (lo 0x200) and stays out.
- **Ring-out is instant**: with the default "ring out: lose", a wrestler on the floor is counted
  out at once. In the emulator, moving Raven to the floor gave result word 0x00010021
  (count-out 0x10000), Raven out and Hollywood Hogan in through 0x8F within about 4 seconds.
  `match_end` with 0x400 eliminates every legal wrestler on the floor together (unless someone
  is in 0x8F), and if only one is left inside, that wrestler is the winner (result 0x100000).
- In 5 minutes of 8-man CPU play nobody was eliminated: the CPUs rarely pin to a finish or leave
  the ring. The roll-out state 0x71 only reaches the apron.
- **Saves:** `rv_battle_royal8_cpu.state` (8 entrants, start).
- Open: what ends the match when all entrants are used (the last one standing check), how the
  0x82 / 0x90 loser states leave the ring, and the replay/HUD side.

## Tag matches

CPU vs CPU **tag** match (`_tools/libretro/states/rv_tag_cpu.state`, Hogan & Giant vs Hall &
Bischoff at Nitro, the menu's CPU vs CPU tag option), 30 seconds logged: states 0x01–0x03,
0x0B, 0x0C, 0x15, 0x17, 0x1B, 0x1E, 0x2B, 0x2C, 0x3C, 0x3D, 0x43–0x46, 0x49, 0x4A, 0x53, 0x54 and
**0x81** (the tag reach); AI modes 0, 1, 3, 7, 8, 14, 16–19, 21, 25–27, 30, 34, 38, 39, 44. Again
no new state and no AI mode above 45. Flags lo at the start: the legal men 0x20000244, the
partners on the apron 0x20000084 / 0x2000008C, so **0x200 marks the legal man** (World Tour
LEGAL 0x2), 0x80 the apron, 0x4 CPU; 0x8 on one partner only (not identified).
A further minute (frames 900–2700 after the save) added a full tag exchange (0x81 reach, 0x7F
tag in, 0x80 tag out), 0x09, 0x14, 0x17, 0x28, 0x2E, 0x2F, 0x40 / 0x41, 0x5F, 0x61 and AI modes 2,
4, 6, 11, 31, 35, 37, 45; still no new state and no mode above 45. Lo 0x8 stayed on Bischoff
(slot 3) the whole time, in every state including the tag, so it is a per-wrestler property
(perhaps "has a manager"), not a match state.

## Pins and mashing

Revenge's `mash_count` (`0x800D7690`) ignores presses for
two frames after each counted one (counter `0x800F9820[slot]`), so mashing tops out at 10
counted presses a second; World Tour counts every press.

The pin and hold rules are otherwise World Tour's (`../wcw_world_tour/match_end.md`) as far as
checked: `recover_difficulty`, `mash_count` and the referee count were matched by the call graph.

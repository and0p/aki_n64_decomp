# WCW/nWo Revenge: comparison with World Tour

Other pages: `moves.md` (move sets, roster, input slots), `controls.md` (controls and unlocks).

Started 2026-10-31. Written as differences from World Tour (`../wcw_world_tour/`); anything
not mentioned here is not checked yet, not known to be the same.

## Tooling

- IDA database: `py tools/sym/build.py --game revenge --ida` (runs
  `tools/ida/revenge_build_idb.py`, which reuses `tools/ida/wcw_idb_setup.py` with
  `AKI_GAME=revenge`). Output in `_tools/ida_revenge/` (git-ignored). Function starts come
  from the Revenge recomp's `syms/dump.toml`; 1,336 functions, all decompile.
- Symbols: `_re_docs/symbols/revenge/` (same format as World Tour's), queried with
  `py tools/sym/sym.py --game revenge ...`. `types.h` is a copy of World Tour's and is
  **not** valid for Revenge yet (the wrestler struct is laid out differently, below).
- `tools/sym/match_games.py` matches functions with identical normalised machine code. Between
  World Tour and Revenge it finds only 12, because **Revenge is compiled with optimisation**
  (World Tour's functions spill every value to the stack; Revenge keeps them in registers,
  e.g. `rng_match` is 136 bytes in World Tour and 100 in Revenge). So matching has to go by
  behaviour: constants, tables, call shape. The script may work better between Revenge and the
  later games.

## Memory layout

From the recomp's splat config (`_recomps/WCWnWoRevengeRecomp/disasm/revenge.yaml`):

| Piece | ROM | VRAM | World Tour |
|-------|-----|------|------------|
| main (code with data pockets) | 0x1000–0x3C770 | 0x80000400–0x8003BB70 | 0x80000400–0x80062090 incl. bss |
| ovl_a (menus?) | 0x3C770–0x834A0 | 0x80090000 | |
| **ovl_b (the match)** | 0x834A0–0xDAC50 | text 0x80090000–0x800E2B10, data –0x800E77B0, bss –0x800FAFA0 | text –0x800E5F70, data –0x800E9400, bss –0x800F8AC0 |

The two overlays share 0x80090000, as in World Tour (the recomp's header comment saying "no
overlays" is out of date; its segment list has both). The IDA build loads only main + ovl_b.
`ovl_b` is the match overlay: World Tour's `rng_match` multiplier is there.

## First landmarks

| What | Revenge | World Tour | Difference |
|------|---------|------------|------------|
| `rng_match` (LCG ×0x5D588B65) | 0x800D4204 | 0x800A0468 | same algorithm |
| `wrestler_update` | 0x8009D8D0 | 0x800A7C80 | |
| `g_state_handlers` | 0x800E2FF0, **146 states** | 0x800E723C, 120 | 26 more action states |
| `ai_think` | 0x800A9924 | 0x800B5780 | |
| AI mode table | 0x800E3660, **62 modes** | 0x800E7AE0, 48 | 14 more AI modes |
| `g_wrestlers` | 0x800F9D10, **944 bytes each** | 0x800F07F0, 928 | 16 bytes more per wrestler |
| model joints per wrestler | 2,852 bytes | 2,852 | same |

The wrestler struct is laid out differently; see below.

## Wrestler struct (first fields mapped)

From `wrestler_update`, `wrestler_set_state` (`0x800A6C48`), `wrestler_set_move`
(`0x800A7C40`), `wrestler_set_move_from` (`0x800A7D1C`), `move_count_stats` (`0x800A7F6C`)
and the facing update `0x800D4AD0`, compared with World Tour. The layout is **reordered**, not
just shifted:

| Field | Revenge | World Tour |
|-------|---------|------------|
| state | +0x0A | +0x0A |
| state_step | +0x0C | +0x12 |
| (cleared with the step; state_timer?) | +0x0E | +0x14 |
| flags (two words) | +0x20, +0x24 | +0x18, +0x1C |
| ring_zone | +0x3C | +0x32 |
| pos | +0x4C | +0x3C |
| facing | +0x58 | +0x48 |
| partner_idx / other_idx | +0x351 / +0x352 | +0x341 / +0x342 |
| teammate_idx | +0x355 | +0x344 |
| teammate flags copy | +0x358 / +0x35C | +0x348 / +0x34C |
| per-state scratch (`_unk350`) | +0x360 | +0x350 |
| turn_step | +0x368 | +0x338 |
| moveset / move_slot / stat_category | +0x384 / +0x386 / +0x388 | +0x370 / +0x372 / +0x374 |
| move (MoveParams), its flags | +0x38C, +0x3AC | +0x378, +0x398 |

Move data: **279 move slots per wrestler** (6,704-byte blocks at `0x80075478`; World Tour 222),
generic moves at `0x8007BD78` (24-byte records as in World Tour's file 221), move statistics
87 categories × 8 bytes at `0x8007F0E4`. Revenge also keeps one more "current" wrestler pointer
than World Tour (five instead of four).

## Assets: same archive format, reordered files

- Same archive: u32 offset table with bit 0 = LZSS, files relative to a data base. Revenge:
  **7,657 files**, table at ROM `0xCE2752` (copied by `func_800034A0`), data at `0xDAC50`
  (loader `func_80003684`). `tools/rom/aki_archive.py` now detects the game from the ROM hash;
  `verify` passes for all 7,657.
- Animations use the same 98-byte keyframe layout (4,017 files from 3,641 on parse as
  animations). **661 of World Tour's 2,598 animations are byte-identical** in Revenge, but
  renumbered (no single offset; +854, +896, +1883 … in places), so animation ids differ between
  the games and the asset converter needs a per-game id table.
- Texture and palette runs look the same as World Tour's; models and move sets not checked.

## State table alignment

Matching each World Tour state handler with Revenge's by shared animations (through the 661
identical files) and constants, with the order kept, gives a consistent alignment: Revenge
kept World Tour's states in order and **inserted new ones**. Revenge number = World Tour number
plus:

| World Tour states | Revenge offset |
|-------------------|----------------|
| 0x01–0x07 | +0 |
| 0x08–0x23 (0x12, 0x19 unmatched) | +1 |
| 0x24–0x2C | +4 |
| 0x2D–0x2E | +5 |
| 0x31–0x51 (0x2F +7, 0x30 unmatched) | +7 |
| 0x52 | +9 |
| 0x53–0x58 | +12 |
| 0x59, 0x5D–0x6C, 0x6F–0x73 | +14 (0x6D +15) |
| 0x74–0x76 | +18 |

Unmatched or new Revenge states: 0x00, 0x08, 0x13, 0x1A, 0x25–0x27, 0x31, 0x34, 0x35, 0x37,
0x59, 0x5A, 0x5C–0x5E, 0x65, 0x66, 0x68–0x6A, 0x7B, 0x82–0x85, 0x89–0x91. Where several matches
were weak the exact boundary of an insertion may be off by one; 25 handlers with strong matches
are named in `symbols/revenge/states.toml` (e.g. turnbuckle 0x0A, whips 0x0B/0x0C, corner slump
0x14, hit reaction 0x28, recover 0x2B, dazed 0x2D, pick-up 0x32, carries 0x74/0x75, crowd
weapon 0x7D).

Tools: `tools/sym/match_consts.py` pairs functions by their constants and propagates through
calls (162 pairs, mostly rendering and loading code); the state alignment was done with the
same constant sets and an order-keeping alignment (not yet a script).

`tools/sym/match_calls.py` (2026-11-01) spreads names from the matched functions along the
call graph: each unpaired caller / callee of a pair is scored against the neighbours of its
partner by shared constants, size, the sizes of its callees, and how many of its already-paired
callers and callees agree; mutual best pairs with a clear margin are taken, round after round.
Results in `symbols/revenge/from_wcw.toml` (272 names; score >= 1.0 `likely`, below `guess`).

- **Accuracy.** A first, looser version (constant-free functions scored as similar, size ratio
  down to 0.4) got about 1 in 25 wrong on small functions, mainly where the optimised build
  inlined a helper (`angle_diff` landed on a function that inlines it; `recover_tick`, which
  Revenge has twice, was taken for `wrestler_set_move_from`). The current scoring fixes those;
  the few wrong ones found by hand since (`wrestler_add_health`, `chance_check`) are corrected
  in `bulk.toml`. Treat `guess` entries as unchecked.
- **Independent agreement.** 36 state bodies paired by the call graph land exactly on the
  handlers the state-table alignment predicts, so those handlers are marked `likely` in
  `symbols/revenge/states_aligned.toml` (all 146 handlers and their bodies are named there from
  the alignment: 128 new entries, the rest `guess`).

Totals: 537 of 1336 functions documented, **473 of the 653 in the match overlay**. The other
150 or so call-graph pairs are World Tour main-segment functions with no name yet.

### New states (first pass, 2026-11-01)

The state setters are `wrestler_set_state` (0x800A6BE0), `_now` (0x800A6C48, RERUN is
0x10000000 at +0x24), `_free` (0x800A6CDC), `_free_now` (0x800A6D80) and `pair_set_state`
(0x800A6E44). From who sets them and the animations they play (759 Revenge animation files
are byte-identical to World Tour ones; table in the scratch `anim_r2w.json`, worth making a
tool):

- **0x83 / 0x84, turning a dazed opponent around**: L next to a dazed opponent (0x2D),
  animation = World Tour 3091 / 3087. World Tour did this with C-up inside the dazed state.
- **0x8D**: started from the run (`run_step`) with a d-pad direction toward one end of the
  ring at a set x range, depending on `0x8007FC10`; plays the plain run (World Tour 2924) for
  30 frames, an A/B/L/R press with `chance_roll` kind 3 sets a success bit, then runs on. Not
  identified.
- **0x13**: from the run when a 4-point probe ahead hits something (otherwise the rope / crash
  0x11); generic move 10 and a hit on the opponent: a running collision of some kind.
- The others (0x00, 0x08, 0x1A, 0x25, 0x27, 0x31, 0x34, 0x35, 0x37, 0x59–0x5E, 0x65,
  0x68–0x6A, 0x82, 0x85, 0x89–0x91) play animations with no World Tour twin; naming them needs
  RAM (watching which state a known action enters).

Tried in the emulator (`akiemu --game revenge`, 2026-11-01): 6,000 frames of random P1 input
against the CPU reached only states World Tour also has (0x01–0x07, 0x0B, 0x0C, 0x14, 0x1E,
0x1F, 0x28, 0x2B, 0x2C, 0x2E, 0x2F, 0x3A, 0x3B, 0x40–0x46, 0x49, 0x4A, 0x5F). Forcing each new
state onto a standing wrestler shows its first animation (e.g. 0x08 4141, 0x13 4199, 0x1A 3697,
0x25 7289, 0x27 4376 with the opponent into 0x28, 0x59 4391, 0x5C–0x5E 3773–3775, 0x89 3706,
0x8A 3707) but most wait for a partner or context and stay in step 1. Run + Z + up (the 0x8D
branch of `run_step`) did not trigger it from the middle of the ring. Identifying these is
best done by playing in the emulator window and marking (F4) when a Revenge-only move happens.

### New states traced from their setters (2026-11-01)

Every call to the five state setters was searched, with the values of variables passed to them
(`who.py`-style scan of the pseudocode, then reading each site). Flags in Revenge sit at +0x20
(hi) and +0x24 (lo); location bits are lo 0x40 ring, 0x80 apron, 0x100 floor (World Tour's
4 / 8 / 0x10 shifted by 4). Several new moves are gated by **wrestler header traits** (the
header word at `*(w + 0x364) + 0x3C`), like World Tour's ring-entry traits.

| State | Entered from | Condition | Reading |
|-------|--------------|-----------|---------|
| 0x1A | `state_19_trip_runner` | the opponent of a successful trip | tripped runner (receiver of 0x19) |
| 0x25 | `input_cup_opponent` | C-up, in the ring or on the floor, opponent with hi 0x2000 (not 0x4000000) within reach and in front (angle < 512) | **breaking up a pin or hold**: the target's hi 0x2000 is set by the pin, hold and carry attacker states (0x38, 0x35, 0x66), so this is C-up on a wrestler who is pinning or holding someone (move slots 185 / 186). Tagging is 0x7F–0x81 (`tag_try`) |
| 0x27 (seen: Nash in the 8-man battle royal, AI strike mode 3) | `state_1D_strike_body` | the strike connects while `sub_800C7870` holds (both in the ring, opponent byte +0x37 bit 0, distance under a constant) | a strike variant; the opponent goes to 0x28 (hit reaction) |
| 0x31 | `sub_800E0590` (from `state_01_stand`) | pair 0x30 / 0x31 next to a downed opponent | **flip the opponent over** (the new C-Left), 0x31 the one flipped |
| 0x34 / 0x35 | `input_strike` | R pressed in the ring next to an opponent with byte +0x37 bit 0 (closer than `0x800E76A8`) | **R counter** of an attack (the guide's "R: counter moves"); 0x34 counters, 0x35 is countered. R next to a lying opponent instead starts the pick-up 0x32 / 0x33, otherwise R is the block 0x40 |
| 0x37 | `ground_grapple_try` | pair 0x36 / 0x37 on a downed opponent in reach | receiver of the ground grapple 0x36 |
| 0x58 / 0x59, 0x5A / 0x5B, 0x5D / 0x5E | `sub_800C41F0` (from the strike landing `0x800C3A78`) | the caught-strike record (+0x3A bits, World Tour's `state51_flags`): slot bits 0x7E00 → 0x5A / 0x5B; bit 1 (far strike) → 0x5D / 0x5E; otherwise 0x58 / 0x59 | **strike counters, now three kinds**, each with its own receiver state (World Tour had 0x51 / 0x52 only) |
| 0x5C | `sub_800DB72C`, `sub_800C41F0` | lo 0x8000000 and `sub_800D7B54(w, 0, 0, 2)`, with sound 300 | not identified (animation 3773 = World Tour 4537) |
| 0x65, 0x67, 0x69 | `input_apron_moves` | on the apron: d-pad toward a side with trait 0x80000 → 0x65; opponent on the floor with trait 0x100000 → 0x67; trait 0x2000000 and allowed to act → 0x69 | **trait-gated apron moves** |
| 0x66 | `input_climb_turnbuckle` (`0x800DF5C4`, from `state_01_stand`) | opponent lying in the ring, trait 0x400000 | a trait-gated climb / springboard onto a downed opponent (it sits in the turnbuckle-climb input) |
| 0x68 | `run_inputs` | running on the apron (lo 0x80), A, trait 0x200000 | **trait-gated apron running attack** |
| 0x82, 0x85, 0x90 | `state_01_stand` (0x82, 0x90), `state_28_down` (0x85 instead of getting up) | lo 0x8000 in the ring | **lo 0x8000 = lost a fall** (set by `match_record_fall`): a standing loser goes to 0x82 / 0x90, a downed one stays down in 0x85 (getting up through 0x8E). Not the analog "stay down" trick as first guessed |
| 0x83 / 0x84 | `input_l` | L next to a dazed opponent | turning a dazed opponent around (see above) |
| 0x89 | `sub_800DF214` (from stand / taunt) | C-down with a d-pad direction while standing, facing a wrestler (slot stored at +0x354) within 512 | probably the **leapfrog / drop to the mat** over a runner (animation 3706, next to the run animations); the guide notes these got easier in Revenge |
| 0x8A | `run_collide` | the run meets something (`sub_800B678C` = 1) | a running collision (animation 3707) |
| 0x8B / 0x8C | `sub_800CD3F8` (grapple start, from `state_3D_grapple_attempt_body` via `sub_800CCE50`) | on the apron (lo 0x80, not 1 / 8) when none of grapple context 0x40384 applies | **grapple between the apron and the ring**, a new pair (the ring version is 0x45 / 0x46) |
| 0x8D | `run_step` | running toward one end, Z + up, x in a set range (`0x8007FC10` 0 or 2) | **run off to grab a weapon**: 30 frames of the run animation away from the ring; an A/B/L/R press wins `chance_roll` kind 3, and then `weapon_give` (`0x800E1920`) hands a weapon and the wrestler runs back (state 0x03) |
| 0x8F | `sub_800C0004` / `sub_800C01A8` from `match_tick` | for wrestlers standing (1) or down (0x2C) with lo 0x10, with `chance_check` | **a new wrestler coming in**: the run-in (match type 0x100 with interference) and the next battle-royal entrant (match type 0x400) both enter through 0x8F; in the battle royal the eliminated wrestler's slot switches to the entrant (+0x48 -> roster +0x02) during it (see "Battle royal") |
| 0x13 | `run_step` | a 4-point probe ahead hits something | a running collision with the opponent (generic move 10) |
| 0x08 | `run_inputs` | running + B while holding a weapon (hi 0x10000) | **running weapon attack** (generic move 41 / 42 and animation 4141 / 4137 by hi 0x20000); without a weapon it is the plain running attack 0x07 |
| 0x8E | not set by code found so far | | uses the same get-up animations as 0x85 (3869–3874, turning ±1024): the getting-up end of the "stay down" pair, reached from 0x85's own code |
| 0x91 | not set by the match code | | zeroes the facing and plays one of five poses chosen by the d-pad (6223, or 4932 / 4933 / 4191 / 4192), then stands: most likely the **pre-match pose** set by the entrance code outside the match overlay |
| 0x00 | | | the zero state a slot starts in |

**Weapons:** hi 0x10000 means "holding a weapon" (set by `weapon_give`, `0x800E1920`, which picks
a weapon object and a random type 3–10; called by the crowd-weapon state 0x7D, the run-in 0x8F
and 0x8D). With a weapon, C-up next to a downed opponent gives 0x24 instead of 0x55.

Weapon types and objects, confirmed against a set of GameShark "have weapon" codes (Andrew,
2026-11-01) and `weapon_give`:

- **Weapon objects** are 48-byte records from `0x800F98E0` (byte +0 and +1 the type, +2 a state).
  A wrestler's object index is `0x800F99A0[slot]` (a byte per slot).
- **Types:** 0 mallet, 1 blue microphone, 2 black microphone, 3 trophy, 4 black baseball bat,
  5 spotted baseball bat, 6 steel chair, 7 table piece, 8 trash can, 9 suitcase, 10 stop sign.
- **Holding flags** (hi word, byte `+0x21` of the wrestler): 0x10000 = holding a weapon, plus
  0x20000 for the **one-handed types 0–5**; the codes write 3 (both bits) for those and 1 for the
  two-handed 6–10. `weapon_give` sets 0x20000 for types below 6, and the running weapon attack
  0x08 picks its swing by it (animation 4137 / generic move 42 one-handed, 4141 / 41 two-handed).
- **Which type:** crowd weapons are random 3–10 (trophy to stop sign). Roster `0x703` (Dake Ken)
  always gets the mallet (0); with header +0x38 bits 0xC0 a wrestler gets their own weapon
  (type 4 or 6, from `weapon_give`'s flag argument 4).

`0x8007F07C` (u16) is the unlock mask: 0xFFFF unlocks everyone (moves.md).

### CPU vs CPU run (2026-11-01)

`_tools/libretro/states/rv_cpu_cpu.state`: the 1P vs CPU save with P1's flags lo |= **0x4, the
CPU bit in Revenge** (World Tour 0x1); both wrestlers then run on AI. 16,000 frames (about 9
minutes) of Hogan vs Hogan logged every second frame:

- States seen: 0x01, 03, 04, 06, 07, 0B, 0C, 0F, 10, 14, 1B, 1E, 1F, 20, 21, 28, 2A–2F, 32, 33,
  3A–46, 49, 4A, 4F, 50, 53, 54, 5F, 61, 62. The common transitions fit the alignment (stand →
  grapple attempt 0x44 → grapple 0x45 / 0x46 → move 0x49 / 0x4A → down 0x2C → get up 0x43;
  strike 0x1E → hit 0x28 → recover 0x2B; whips 0x0B / 0x0C; pick-up 0x32 / 0x33; ground holds
  0x3A–0x3D; block 0x40 / 0x41; chain move 0x53 / 0x54; evade 0x5F; ropes 0x61 / 0x62). Moves in
  0x49 / 0x4A came from slots 0–13, 61–63, 125, 147–150, 159, 165, 172, 181, as in moves.md.
- None of the new states appeared: they need tag partners, weapons, the apron or trait holders.
- **0x2F** follows the stand and the run and leads to 0x2E or 0x62, which does not fit "flip over";
  it is more likely a stagger or knock-back (to check).
- AI modes seen: 0–3, 6–14, 16–19, 21, 25–28, 30, 31, 33, 35, 37–39, 43; 27 (circling) is most of
  the time. None of the new modes 46–61 ran in singles.

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

### Battle royal (2026-11-01, code + emulator)

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

### Strike combos ("multi-hit combos", 2026-11-01)

The same mechanic as World Tour's state 0x44 (`ai.md`, `grapples.md`), now states 0x4B / 0x4C
(`state_4B_strike_combo_body`), with more to it:

- **Who:** wrestlers with header trait bit 0 (`*(w + 0x364) + 0x3C & 1`): Goldberg, Glacier,
  Yugi Nagata, Ultimo Dragon, AKI Man, Shogun, Kim Chee, Han Zo Mon and the hidden Kanyon. The
  guide lists a "Combo Meter" move list for the same eight (Kanyon is not in it).
- **Start:** B from a strong front grapple (grapple context 0x42 == STRONG), `sub_800D00FC`.
- **Length:** 6 hits, or **10** with SPECIAL ready (World Tour 6 / 9). The combo's own animations
  come from slots 236–238 (World Tour 187–189).
- **Each hit** is picked by `strike_combo_finish` (`0x800AD2C4`) from the strong-grapple slots:
  B, B+Up, B+Down, **B+Left, B+Right** → 33, 38, 43, 48, 53 (left and right are new; World Tour
  had three), and the last hit uses that slot + 2. **A** (+Up / Down) ends the combo with the
  strong-grapple move 18 / 23 / 28; A+B → slot 60; SPECIAL ready → the special 58.
- Timing: the next input is read during the hit (`strike_combo_input`, as in World Tour); a
  per-slot meter (`strike_combo_meter`) is set from the hits left.

### Wrestler header trait bits (2026-11-01)

Every test of the header words `*(w + 0x364) + 0x38` and `+ 0x3C` found in the match code (constant
masks only; bits read through a variable, such as the stance style, are not in this list):

| Word | Bit | Read by | Meaning so far |
|------|-----|---------|----------------|
| +0x3C | 0x1 | `sub_800D00FC`, AI mode 14, `state_47_body` | strike combo from a strong grapple (above) |
| +0x3C | 0x200 | `state_05` | picks animation 4167 instead of 4166 in state 0x05 (a per-wrestler animation variant) |
| +0x3C | 0x400 | `state_0A_whip_attacker_body` | a whip variant |
| +0x3C | 0x800, 0x1800 | `ai_vs_floor` | AI choices outside the ring (probably dive styles) |
| +0x3C | 0x2000, 0x8000, 0x10000 | `state_55_ropes_b_body` | ring-entry styles (World Tour used 0x400–0x2000) |
| +0x3C | 0x4000, 0xE000 | `state_60_ropes_a_body` | ring-exit styles |
| +0x3C | 0x20000 | `sub_800DF924`, `sub_800AB43C` | a turnbuckle / springboard ability (run and climb inputs) |
| +0x3C | 0x40000 | `state_64_body` | its own animation (3832) and an extra step in state 0x64 instead of the stance animation |
| +0x3C | 0x80000, 0x100000, 0x2000000 | `input_apron_moves` | apron moves 0x65, 0x67, 0x69 |
| +0x3C | 0x200000 | `run_inputs` | running attack on the apron 0x68 |
| +0x3C | 0x1000000 | `run_inputs` | a running move in the ring, gated by `run_flags` (+0x28) bit 1 and not bit 8; leads to `sub_800B5CA0` (not identified) |
| +0x3C | 0x400000 | `input_climb_turnbuckle` | move on a downed opponent 0x66 |
| +0x3C | 0x800000 | `get_up_anim` | get-up style |
| +0x3C | 0x4000000, 0x8000000 | `state_2F_cover_attacker_body` | cover (pin) styles |
| +0x3C | 0x80000000 | `state_63_dive_outside_body` | dive-to-the-floor style |
| +0x38 | 0x4 | `grapple_attempt_resolve` | **test of strength**: when both wrestlers have it, both have health >= 151 and grapple at the same moment (opponent in 0x44 step 4, both in the ring) |
| +0x38 | 0x40, 0xC0 | `weapon_give` | the wrestler's own weapon type |
| +0x38 | 0x1000 | AI mode 9 (meet a runner) | AI preference |
| +0x38 | 0x4000 | AI mode 14 (grapple) | AI preference |

Other header bytes read by the match code, compared with World Tour's (whose bytes are spread
over `moves.md`, `frame_loop.md`, `movement.md`, `strikes.md` and `outside_ring.md`). From +0x11
on, Revenge has one more climb byte, so the bytes after it sit one higher:

| Revenge | Read by | Meaning | World Tour |
|---------|---------|---------|------------|
| +0x00 | not seen by constant offset | blood resistance in World Tour | +0x00 |
| +0x01 / +0x02 | `health_regen` | health regained every 60 / 90 frames | +0x01 / +0x02 |
| +0x03 | `move_apply_hit` | extra spirit on hits | +0x03 |
| +0x04 | `walk_speed` | walk class 1–3 | +0x04 |
| +0x05–0x0E | `move_apply_hit` (through a pointer) | attack per body part / defence per region, presumably as in World Tour | +0x05–0x0E |
| +0x10 | `chance_roll` | crowd-weapon chance | +0x10 |
| +0x11–0x17 | `state_09_turnbuckle_body` | climb kinds per turnbuckle move (7; World Tour 6) | +0x11–0x16 |
| +0x18 | `chance_roll`, AI modes | **weight class** (lift chance by difference / 3) | +0x17 |
| +0x1B | `dive_aim` | dive reach | +0x1A |
| +0x1C | `chance_roll` kind 6 | TKO-save table index | |
| +0x1E | `sub_800C778C` | strike animation style (from 4169) | |
| +0x20 | `reversal_slot` | which 400-byte reversal table (`0x80064AE0` + 400 x n); World Tour had a per-wrestler file instead | |
| +0x24 / +0x26 | `sub_800AC208` (AI) | two move slots + 1 that the CPU picks with 50 % each (one when its health is below 65): favourite moves | |
| +0x38 / +0x3C | many | trait words (table above) | +0x38? / +0x3C? |

Bytes +0x05–0x0F, +0x19–0x1A, +0x1D, +0x1F, +0x21–0x23, +0x28–0x37 are not read by constant
offset; World Tour's block-counter bytes (+0x18 / +0x19 there) have not been located yet.

First difference seen through it: Revenge's `mash_count` (`0x800D7690`) ignores presses for
two frames after each counted one (counter `0x800F9820[slot]`), so mashing tops out at 10
counted presses a second; World Tour counts every press.

## CPU AI

Read from `ai_think` (`0x800A9924`) and the mode table (`0x800E3660`), compared with World
Tour's `ai.md`. Names in `symbols/revenge/ai.toml`.

- **Same design:** the AI still presses buttons into a per-slot block, picks a mode, and runs
  the mode's handler each frame. Mode changes reset the step, remember the opponent's state and
  clear the low flag bits, as in World Tour.
- **AI block: 96 bytes per slot at `0x800F9B88`** (World Tour 84 at `0x800F1710`). Same at the
  start (mode +2, step +4, held +6, pressed +8, stick +0xC, skill +0x12, own situation +0x14);
  then moved: opponent situation +0x22 (WT +0x1C), opponent's state at mode start +0x2E (WT
  +0x24), distance and angle +0x38, corner distances +0x48 (WT +0x40), flags +0x5C (WT +0x50).
  Revenge also stores both wrestlers' ring quarter (+0x1C, +0x2A) and ring zone (+0x1E, +0x2C).
- **Modes 0–45 are World Tour's modes 0–45, in the same order**: aligning the two tables by
  the buttons each mode presses and the constants it uses gives index = index for all 46, with
  near-identical button sets (grapple, strike, block, run, the timed counters, tag…). Revenge
  adds **16 modes, 46–61** (World Tour's 46 and 47, the whip-runner hand-off and the dive to the
  floor, became part of the reflexes and mode 46).
- **Reflexes grew**: the situation classifier has more classes (up to 23). Besides World Tour's
  reflexes (grappled 16, pinned/down 18, whipped 17, dazed 37), Revenge sends classes 12 and 22
  to 37, getting up (15) to 38, grappling as attacker (16) to 14 and the whip runner (19) to 8
  directly, and two new situations (20, 21) to the new modes 50 and 51.
- New in `ai_think`: when the opponent is in state 0x29 (World Tour's TKO stagger) the CPU
  taunts 30 % of the time (mode 34) or backs off (39); and a guard resets the AI to mode 0 if a
  wrestler stays 150 frames with flags 0x280 set.

### The new modes (first reading)

| Mode | What it seems to do |
|------|---------------------|
| 46 | hold A while running, ending on states 4 / 99 (World Tour's run-and-dive, adapted) |
| 47 | run toward a corner and climb the turnbuckle (state 0x0A), holding C-down |
| 48 | like 47 from the apron: climbing in or up (states 0x2B, 0x86, 0x89–0x8A) |
| 50, 51 | in the new states 0x5D / 0x5E: wait a per-wrestler delay, then A or B |
| 52 | hold Down while below a height |
| 53 | steer on the floor by x and z separately |
| 54, 55 | wait for state 0x30, then C-right and B |
| 56 | circle the opponent (like mode 27) |
| 57–60 | wait for state 0x30, then the ground hold (mode 13) |
| 61 | close to the opponent: a skill roll gives the timed block (mode 32) |

The difficulty table (World Tour's skill 50 / 250 / 900 at `0x800E7560`) is not found yet in
Revenge.

## Wrestler models compared with World Tour

From the archive (both games scanned with World Tour's mesh rule) and Revenge's `model_load`
(`0x8000C91C`) and node builder (`0x8000BDD0`) next to World Tour's `model_load` /
`model_build_nodes`.

**Same formats and the same model structure:**

- The **mesh part format is unchanged** (8-byte header, 8-byte vertices, index triples; 666
  archive files match it exactly, World Tour 395), and so is the texture format (CI4 / CI8 with
  the 8-byte header, RGBA5551 palettes).
- A wrestler is still **23 part slots on the 19-joint skeleton**, built per part and drawn
  with the **same 7 seams** (pelvis, waist, neck, both thighs, both forearms). The part record
  grew from 40 to 44 bytes; the node records are the same 220 bytes (5,060 per wrestler).
- Body parts come per body type and bone, head parts from a per-wrestler head list (neck, head,
  optional extra on the head such as hair), as in World Tour. Revenge has **no foot extras**
  (World Tour's nodes 21/22).

**Not more detailed per part.** The meshes are about the same size or slightly smaller:

| | World Tour | Revenge |
|--|-----------|---------|
| mesh part files | 395 | 666 |
| average vertices / triangles per part (all) | 28.2 / 33.0 | 23.8 / 27.1 |
| body parts (one run) | 209 files, 34.0 / 39.5 | 278 files, 31.0 / 34.8 |
| largest part | 64 vertices, 86 triangles | 56 vertices, 75 triangles |

What grows is **variety**: 70 % more mesh parts (more body types and heads) and more textures
(e.g. 64×32 CI4: 177 vs 32; 64×64: 116 vs 63; the 128×128 CI4 textures at 677–713 are probably
arena pieces, not wrestlers). Attires such as a long coat are built from their own body-part
meshes (body type 36 for the first wrestler).

New in the loader: each wrestler gets two heap buffers (700 × 16 bytes and 1,200 × 8 bytes,
`sub_80007A74`), which look like room for built vertices and display-list commands; small fixed
offsets are applied to a few joints after building (shoulders ±5, ±7). Neither is followed yet.

### Tables and the previewer

`tools/rom/revenge_models.py ROM viewer _tools/viewer/revenge_viewer.html` builds the same
previewer as World Tour's (86 roster slots, first 8 attires each, all 4,017 animations; no
labels or arenas). Renders check out: the bodies assemble correctly and animation 3685 (World
Tour's 2919, byte-identical) plays the same pose.

| Table | Address | Contents |
|-------|---------|----------|
| roster descriptors | `0x80031998` | ptr per group (10 groups) → u16 per wrestler; low 7 bits = **body build** |
| head lists | `0x80032B44` | ptr per group → ptr per wrestler → 0-ended head variants; each 12 u16: neck / head / extra mesh, their palette and texture, face palette / texture, [11] texture-record index |
| attires per build | `0x800360F0` | ptr per build → ptr per attire → texture records (picked by head [11]); the first record's [0] & 0x7F = body type |
| attire count per build | `0x8003673C` | u16 per build (24, 63, 33, 20, 7, 1 …) |
| body parts | `0x800317F0` | ptr per body type → 17 u16 mesh files |
| bone → part | `0x80031089` | as World Tour's `g_bone_part` |
| skeleton | `0x800307C0` (bind), `0x800307E9` (order), `0x800307FC` (parents) | identical data to World Tour's |

**Attires are shared per body build**, not per wrestler as in World Tour: build 1 has 63
attires that every wrestler of that build can wear, which matches Revenge letting you pick
outfits. The per-wrestler default is not found yet (the previewer shows the first 8).

**New texture format 0x0020**: CI4 pixels with a **32-colour palette** (two 16-colour banks; the
second looks like a darker copy of the first). 357 textures use it, many of them body parts.
The previewer uses bank 0; when the game uses bank 1 is not known (lighting or a dirty /
bloodied version?). World Tour has only 0x0010 (CI4, 16 colours) and 0x0100 (CI8).

### Attires and colours (2026-11-01, from code; Andrew's viewer notes)

Two things the first previewer got wrong:

**Which costumes a wrestler wears.** The costume list of a body build (`0x800360F0`, up to 63
for build 1) is shared by everyone with that build. Each wrestler's four attires are defaults
in `0x80036710` (pointer per roster group, then per wrestler, then 4 x 4 bytes), unpacked by
`sub_8000B630`: byte 0 = head variant + 1, byte 1 = **costume number + 1** in the build's list,
bytes 3 / 2 = **colour 1 / colour 2** + 1 (0 = keep the texture's own colours). Examples: Sting
(0x001) wears costumes 0, 0, 1, 2; Giant (0x002) 0, 19, 0, 0; DDP (0x004) 6, 7, 6, 8; Hogan
(0x201) 19, 19, 21, 20. At boot `sub_8002BAB8` copies these into a 42-byte-per-attire RAM table
(`0x8003A594`, 80 profiles x 4 attires, with the names); the costume editor edits that copy and
`match_setup` reads attire bytes +36 (head), +37 (costume), +39 / +40 (colours) from it
(`sub_8002B7E0`) for `model_load`. Attires 1 and 3 of Kanyon (0x404) use the name "Mortis" and of
AKI Man (0x601) "THQ man", which is why THQ Man reads as roster 0x601.

**Why skin (and other parts) are red.** The 32-entry palettes (format 0x0020) are 16 colours
plus 16 **recolour keys**; this is the "second bank". `sub_8000B884` rewrites each colour whose key
is set:

- key bits only in 0x07C1 (green field): colour 1 — shade `(key & 0x7C1) >> 6 >> 1` of row
  `colour 1` in the ramp file **2641** (512 bytes = 16 colours x 16 shades, RGBA5551), counted from
  the top (`16 x colour + 15 - shade`); colour 15 or more keeps the original.
- key bits only in 0x003F (blue field): colour 2, the same way.
- **skin**: for bones with a skin file (`0x8003086C[bone]`, files 2642–2657, 96 bytes = 3 tones x
  16), every palette *colour* that is itself a pure red value (only 0xF801 bits) is a skin
  placeholder and becomes shade `(c & 0xF801) >> 11 >> 1` of tone `head[11]` (head record
  +0x16, which also picks the costume's texture record). Bare-skin parts have 16-colour palettes
  that are entirely such placeholders.

The previewer (`tools/rom/revenge_models.py`) now does all of this (2026-11-01): each wrestler
shows its 4 default attires with their head, skin tone and colours, and is named from the
select-screen lists (`0x8003A4F8`: 11 groups of records with the roster word at +0 and the
name string pointer at +8).

So the red skin in the viewer is the untouched placeholder colours. The editor's two colour
pickers are colour 1 / colour 2.

## Arenas

From `arena_build` (`0x8000E920`, called with the arena number, 0–5) and the crowd builder
`0x8000EF90`, both in main, with World Tour's `arena.md` for comparison. Shown in the Revenge
previewer (arena menu); the names come from the renders.

| # | Arena | Mat logo, entrance |
|---|-------|--------------------|
| 0 | Nitro | Nitro apron, stage with the Nitro towers |
| 1 | Souled Out | Souled Out logo, banner stage with stairs |
| 2 | SuperBrawl | red carpet ramp under a lit arch |
| 3 | Bash at the Beach | Bash logo, beach stage with a sky backdrop |
| 4 | Halloween Havoc | Havoc logo, skull and haunted-house stage |
| 5 | Starrcade | Starrcade logo, star arch |

Compared with World Tour (4 arenas by federation, WCW / nWo / DOA / IU):

- **6 arenas, all WCW events.** Same building method: static meshes in world space loaded by a
  `scenery_object`-style loader (`0x80007020`: palette p, texture p + 1), side pieces repeated
  per ring side.
- **Ring:** 6 meshes (`0x80036C68` [0–5]): apron + canvas in one mesh (758), the centre logo as
  one piece **10 units above the mat** (847, World Tour two halves level with it), and the four
  skirts as separate meshes (760–763) instead of one. Palettes per arena from the pointer at
  [6 + 2·arena]; arenas without a logo have palette 0 for it.
- **Corners:** post, plate and pads (749, 752, 755) are modelled in place (y 90–240), so the
  per-piece lifts of World Tour are gone; the pads (755) have no texture (probably vertex
  colours or per-arena colours; not drawn by the previewer yet).
- **Floor and barrier:** floor strip 757 per side; the barrier is split into two halves (785,
  787) with a gap in the middle, filled by 771 on three sides and **left open on the entrance
  side** for the aisle (788, a ramp from the ring to the stage). In World Tour the aisle was
  walled off.
- **Crowd and entrance:** 24–29 pieces per arena (`0x80036C68` [38 + 2·arena] meshes, [50…] and
  [62…] two palette sets, [34] counts), on the **+Z side** (World Tour −Z): an arena-specific
  stage and banners (e.g. 864–872 for Nitro) plus shared crowd stands (793–839). A second list
  at `0x80036750` (19 + 3 meshes, two palette sets) looks like the stands for the other sides.
- `0x80011BB4` loads more objects from `0x80036D18` (11 lists of 1–7 meshes and a 6-piece
  sequence), not identified (entrance effects, ring steps?).
- The ropes are still generated in code, not meshes (the previewer draws World Tour's).

## Next steps

1. Finish the wrestler struct and write a Revenge `types.h`.
2. Confirm the weak parts of the state alignment and name the remaining matched handlers;
   look at what the new states do.
3. Map the other ~1,940 Revenge animations to World Tour's (many are probably re-exported
   versions of the same motion: compare decoded poses, not bytes).
4. The AI: confirm the new modes in RAM, find the difficulty table, read the decision tables.
5. Decode the model tables and render Revenge wrestlers in the viewer; then the move-set files.
